# HTTP 服务器架构重构设计文档

> 本文档记录从 PhotonLibOS 生态迁移到基于当前 Boost.Asio httpserver 的架构重构决策与实施计划。
> 长任务的防遗忘备忘录；一切关键讨论结论以本文件为准。
> **写作约定：每个设计决策都要解释"为什么"而不仅是"做什么"。**

---

## 1. 背景

### 1.1 当前生态

- **`/home/yelin/code/httpserver`**（本项目）：基于 Boost.Asio 的轻量级 HTTP 服务器，多线程 `io_context`，支持 keep-alive。最近刚修完多线程压测下的 `async_read_until` / `consume` 竞态（CHANGELOG 2026-02-18），ab 压测 100,000 次请求 0 失败，313k req/s。
- **`/home/yelin/code/photon_http_server`**：PhotonLibOS HTTP 服务器的薄封装，对外暴露 `PhotonHttpServer(port, vcpu_num)` + `add_handler(HTTPHandler*)` + `run()`。
- **`/home/yelin/code/http_common`**：PhotonLibOS 生态的工具函数（`parse_form_urlencoded`、`url_decode`、`http_400_bad_request` 等）。
- **`/home/yelin/code/route`**：CRDL → Router 代码生成器。生成的 `Router` 继承 `photon::net::http::HTTPHandler`，实现静态 + 动态路由（`/user/:id`）。
- **`/home/yelin/code/weave++`**：HTML 模板 → C++ 代码生成器。生成代码重度依赖 `resp.writev(iov_list.data(), iov_list.size())` 零拷贝发送静态字符串片段 + 动态值。
- **`/home/yelin/code/simple_http_template`**：端到端示例项目，依赖上面全部组件。
- **`/home/yelin/code/yxmysql`**：MySQL 客户端，独立，不依赖 HTTP。

### 1.2 为什么要迁移

**推力（痛点）：**
1. **PhotonLibOS 依赖重**：需要系统级安装，fiber-based 并发模型与标准 C++ 心智模型差距大，调试链路长，新人/外部用户上手成本高。
2. **当前 httpserver 的 API 无法承载周边工具链**：
   - `RoutingModule::register_handler` + `BaseRequestHandler::operator()` 是面向"每条路由单独注册"的模型；`route` 项目生成的是一个大 Router 类，需要统一的 handler 基类接管所有请求——当前没有。
   - 不支持动态路由（`/user/:id`），`route` 生成器依赖这个能力。
   - `HttpResponse` 只能 `set_body(string)` 一次性构造完整响应——`weave++` 依赖的 `writev` 零拷贝写不了，LLM 的 SSE/chunk 流式也做不了。

**拉力（机会）：**
1. 本项目的 Boost.Asio 内核已被压测打磨过（刚修完多线程 keep-alive 竞态），性能和稳定性都不差。
2. 周边项目都是自己的代码，可以无成本改动——不必背向后兼容包袱，可以一次性把新架构做干净。

### 1.3 迁移目标

- 去除 PhotonLibOS 依赖，周边工具全部迁移到基于当前 httpserver 项目。
- 当前 httpserver 的对外 API 重新设计为干净的新架构（参考但不抄 Photon）。
- 不保留对 Photon API 形状的兼容层。
  - **为什么不做兼容层**：兼容层短期省迁移工夫，长期是技术债。Photon 的一些形状（fiber 假设、`Verb` 枚举、`headers.insert` 直接暴露成员变量）放到 Asio 环境里并不自然，强行模仿反而让新代码读起来怪。既然周边都可以改，那就一步到位。

---

## 2. 设计原则

### 2.1 保留 Boost.Asio 内核

**不重造。** 多线程 `io_context`、`async_read_until`、`async_write`、keep-alive、空闲连接定时器——这些已经调优过。

**为什么：**
- 刚修完一个不直观的多线程竞态（`consume` 在 `write` 之后触发 race），重写一遍大概率会再踩一遍同样的坑。
- 性能基线 313k req/s 是打过分的成绩，没有理由推倒。
- Boost.Asio 是 C++ 网络库的事实标准，招人/接手/调试都有社区支持。

### 2.2 优化三种典型场景（按权重）

我们要让新架构**显式**照顾这三类 handler：

1. **短 API（JSON 返回）**——追求吞吐、零多余开销
   - 最常见的使用模式，任何退步都会放大成运行成本。
2. **weave++ 模板渲染**（`writev` 拼接静态 + 动态片段）——追求零拷贝
   - 为什么重要：weave++ 生成的代码里 iov_list 大部分段都是编译期常量字符串。若走"拼接成 string 再写"就浪费了这个编译期信息。Photon 的 writev 给了这个能力，我们不能退步。
3. **LLM SSE / chunk 流式**——追求长连接不阻塞 io
   - 为什么重要：接下来 LLM 工具化场景会越来越多，每个请求动辄 30 秒以上。如果服务器线程被一个慢流占住，就跟没有一样。

**为什么不做通用化（试图兼顾所有场景的统一模型）：**
三个场景的需求冲突——短 API 要零切换、writev 要同步、LLM 要长期不阻塞 io。一刀切只能牺牲其中两个。不如明确分三种模式，handler 按需选择。

### 2.3 单一 handler 入口

服务器只认一个 `HttpHandler`，路由由用户的 handler 自己做（可以用内置 `HttpRouter`，也可以自己写）。

**为什么：**
- `route` 项目生成的是"一整个 Router 类"，天然就是单一入口模型。当前 httpserver 的"每条路由单独注册"反而把这个 Router 拆散了。
- 单一入口让中间件、鉴权、全局错误处理变得简单——一个 handler 外面套一层就行。
- 并不排除"静态路由注册"的便利——内置 `HttpRouter` 本身就是一个这样的 handler。

### 2.4 快路径零切换 + 可选 opt-in 流式

默认 handler 跑在 io 线程不切换；只有显式声明流式的 handler 才涉及 handler 线程池。

**为什么：**
- 大多数 handler 是 CPU 密集短路径，不需要切出去。强行切线程在 313k req/s 压力下会掉 20-40% 吞吐。
- 长流场景一旦发生就是数十秒级，必须切出去，否则阻塞整个 io 线程池。
- 让用户显式选择而不是"自动检测"，是因为引擎猜不准——同一个 handler 有的请求走快路径、有的请求走慢路径很常见（比如普通 API vs SSE）。

---

## 3. 架构总览

```
┌─────────────────────────────────────────────────────────────┐
│                        HttpServer                            │
│  port / signal handling / run() / stop()                     │
└─────────────────────────────────────────────────────────────┘
                              │
                 ┌────────────┴────────────┐
                 │                          │
         ┌───────▼─────────┐       ┌───────▼──────────┐
         │  io thread pool │       │ handler thread   │
         │  (N = hw conc.) │       │ pool (lazy, 仅   │
         │  accept/read/   │       │ streaming 用)    │
         │  fast handler   │       │                  │
         └───────┬─────────┘       └─────────┬────────┘
                 │                            │
         ┌───────▼─────────────────┐          │
         │      Connection         │          │
         │  async_read_until →     │          │
         │  parse → HttpHandler    │          │
         │  ::handle(req, resp) →  │          │
         │  async_write            │          │
         └───────┬─────────────────┘          │
                 │                            │
         ┌───────▼─────────┐                  │
         │   HttpHandler   │◀─────────────────┘
         │   (user impl,   │   stream(lambda) post here
         │   内置 Router)   │
         └─────────────────┘
```

---

## 4. 线程模型：混合模型（Hybrid）

### 4.1 核心决策

- **io 线程池**：默认 `std::thread::hardware_concurrency()` 个。负责 accept、`async_read_until`、parse、调用 `HttpHandler::handle`、发回复、keep-alive 续读。
- **handler 线程池**：懒加载。仅当有 handler 调用 `resp.stream(lambda)` 进入流式模式时才派上用场。默认大小可配（建议 `4 × hw_conc` 或上百，按 LLM 并发流数目决定）。
- **默认路径**：handler 跑在 io 线程，handler 填完 `HttpResponse`（或调 `writev`）后返回，io 线程立即处理下一个任务。**零线程切换**。
- **流式路径**：handler 内调 `resp.stream([](HttpResponse& r){ ... })`。lambda 被 post 到 handler 线程池异步执行；原 io 线程释放。Lambda 内可随意同步阻塞（写 chunk、等 LLM token）。

### 4.2 为什么不采用纯 C 模型（所有 handler 都切 handler 线程）

曾讨论过"所有 handler 都跑 handler 线程池"的纯 C 模型，最终否决。**原因：**

- **开销可量化**：每请求多 2 次线程上下文切换，Linux 上每次 1-3μs。当前 hello-world 压测每请求约 3μs，叠加后吞吐预计掉 20-40%。
- **大多数 handler 不需要**：纯 CPU 的短路径 handler 从 io 线程切出去再切回来，没有任何好处。
- **已有等价方案**：opt-in `resp.stream(lambda)` 可以在需要时切出去，覆盖所有会从切换中收益的场景。

### 4.3 为什么不采用纯 A 模型（handler 内部 async write 回调）

曾讨论过"handler 永远跑 io 线程，但 response 写入是 async + 回调"的模型。**否决原因：**

- **weave++ 会严重受损**：`writev` 传入的 iov 指向 handler 栈上的局部字符串，异步下 handler 返回后栈失效，必须把所有动态段拷到堆上才能让异步 write 持有。零拷贝这个核心收益就丢了。
- **心智负担**：用户代码变回调链，代码生成器模板重写工作量大，可读性差。
- **不解决 LLM 阻塞问题**：LLM 调用本身（等 token）就是同步阻塞的，无论写入异步不异步，io 线程都被 handler 占住。

### 4.4 为什么 handler 线程池是懒加载

默认不创建 handler 线程池（或只创建最小 worker），首次 `stream()` 调用时才启动。

**为什么：**
- 大多数部署场景（纯 API、静态模板）根本用不到流式，给每个进程白白起 N 个空闲线程浪费内存。
- 懒加载让"只做短 API"的用户零成本——既不用配置，也不产生线程。

---

## 5. 组件接口设计

### 5.1 `HttpServer`

```cpp
class HttpServer {
public:
    // port: 监听端口
    // io_threads: io_context 线程数，0 表示 hardware_concurrency()
    explicit HttpServer(unsigned short port, size_t io_threads = 0);
    ~HttpServer();

    // 注册唯一的 handler（通常是一个 HttpRouter）
    void set_handler(HttpHandler* handler);

    // 流式 handler 线程池大小，0 表示懒创建默认值
    void set_stream_pool_size(size_t n);

    // 阻塞直到收到 SIGINT / SIGTERM / SIGTSTP，返回 0 正常关闭
    int run();

    // 显式停止（另一个线程调用）
    void stop();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;
};
```

**为什么这样设计：**
- **构造函数接收 port 而非"先构造后 bind"**：避免"构造了但没 bind"的半初始化状态，bind 失败直接抛异常。
- **`set_handler(HttpHandler*)` 而非 `std::shared_ptr`**：让 handler 生命周期由用户管理，通常 handler 是栈对象或静态对象（如 Router），无需共享指针的开销和复杂性。
- **`run()` 内置信号处理**：Photon 版本就有这个便利，用户 `main()` 一行 `server.run()` 就能跑，避免每个项目重复写信号处理。
- **`run()` 返回 `int`**：便于 `main()` 直接 `return server.run();`，符合 Unix 约定。

### 5.2 `HttpHandler`

```cpp
class HttpHandler {
public:
    virtual ~HttpHandler() = default;
    virtual void handle(HttpRequest& req, HttpResponse& resp) = 0;
};
```

**为什么这样设计：**
- **单方法接口**：最小化抽象成本，后续扩展（比如中间件）可以通过组合 handler 实现，不需要多方法基类。
- **无返回值**：错误状态通过 `resp.set_status()` 表达。返回 int 只会让 handler 作者纠结"return 多少"——让 response 自己说话更直接。
- **去掉 Photon 的 `string_view` prefix 参数**：Photon 那个参数用于路径前缀剥离，我们把路径处理完全交给用户/Router，handler 只面对完整 path，心智模型更简单。

### 5.3 `HttpRequest`

```cpp
class HttpRequest {
public:
    enum class Method {
        GET, POST, PUT, DELETE, PATCH, HEAD, OPTIONS, UNKNOWN
    };

    Method method() const;
    std::string_view method_str() const;    // 原始字符串

    std::string_view path() const;           // 不含 query
    std::string_view full_target() const;    // 含 query

    // Query 参数（已 URL-decode）
    std::string_view query(std::string_view name) const;   // 不存在返回空
    bool has_query(std::string_view name) const;
    const std::map<std::string, std::string>& queries() const;

    // Header
    std::string_view header(std::string_view name) const;  // 大小写不敏感
    bool has_header(std::string_view name) const;
    const std::multimap<std::string, std::string>& headers() const;

    // Body（已按 Content-Length 完整读取）
    std::string_view body() const;

    std::string_view version() const;       // "HTTP/1.1"
};
```

**为什么这样设计：**
- **补齐 PATCH / HEAD**：当前 `Method` 枚举缺这两个，Photon 有，REST API 也常用。一步补齐避免日后再加。
- **返回 `string_view` 而非 `const string&`**：body 和 header 值经常被用户 substr/parse，string_view 避免构造 string 的分配开销。头文件也不需要暴露 std::string。
- **header 查询大小写不敏感**：RFC 7230 规定 header 名大小写不敏感。当前实现用普通 `std::map` 是 bug 级别的 corner case——`Content-Length` 和 `content-length` 会被当成两个 header。必须修。
- **body eager 读取（按 Content-Length）**：**暂不**支持流式上传和 chunked 请求体。
  - **为什么**：实现简单（一次 async_read_until 头 + async_read_exactly body），覆盖 99% 的 API 场景。流式上传需要改 Connection 状态机、暴露异步 read 接口，复杂度大幅上升且周边工具都不需要。留给未来。
- **Query 暴露两套 API**：`query(name)` 单值便利接口 + `queries()` 全集 map 用于遍历。同时满足"我就想查一个"和"我要处理所有"两种需求。

### 5.4 `HttpResponse`

```cpp
class HttpResponse {
public:
    // 状态
    void set_status(int code);                   // 200, 404, ...
    // 不再需要 set_reason_phrase，根据 code 自动填

    // Headers
    class Headers {
    public:
        Headers& insert(std::string_view k, std::string_view v);
        Headers& content_length(size_t n);
        Headers& content_type(std::string_view ct);
        // ...
    };
    Headers& headers();

    // ─── 三种写入模式（互斥，只能选其一） ──────────────

    // 模式 1：Buffered —— 短 API 常用
    // handler 返回后 Connection 统一组装 response 并 async_write
    void set_body(std::string body);
    void set_body(std::vector<uint8_t> body);

    // 模式 2：Immediate —— weave++ 场景，同步 writev 零拷贝
    // 可在 handler 里多次调用；第一次调用时先发 headers
    // 必须在调用前通过 headers().content_length() 或 Transfer-Encoding 告知长度
    ssize_t write(const void* data, size_t n);
    ssize_t writev(const struct iovec* iov, int iovcnt);

    // 模式 3：Stream —— LLM/SSE 场景，切到 handler 线程池
    // lambda 在 handler 线程执行，可以自由同步阻塞
    // 典型用于 chunked transfer 或 SSE
    void stream(std::function<void(HttpResponse&)> fn);

    // 流式辅助：发送一个 chunk（Transfer-Encoding: chunked 自动管理）
    ssize_t write_chunk(const void* data, size_t n);
    void end_chunks();                              // 发送结束 chunk

    // SSE 辅助
    ssize_t write_sse_event(std::string_view event, std::string_view data);
};
```

**为什么采用"三模式互斥"设计：**

三种场景的最优写入策略不同，强行统一反而让每种都别扭：

| 模式 | 适用 | 写入路径 | 关键收益 |
|---|---|---|---|
| Buffered | 短 API | handler 填 body → Connection async_write | 和当前吞吐一致，handler 最简单 |
| Immediate | writev 模板 | handler 同步 writev 到 socket | 零拷贝、零额外分配 |
| Stream | LLM/SSE | handler 返回后，lambda 到 handler 线程执行 | io 线程不阻塞，支持长连接 |

**为什么不合并成"一个 write 接口自动路由"**：运行期自动判断会带来开销和不可预测性——用户希望明确知道"我这里会不会切线程"。让用户选模式是有价值的控制。

**为什么互斥（混用会 assert）**：比如已经 `set_body` 又 `writev` 会导致 body 被写两遍或丢失。与其默默错，不如 fail fast。

**为什么 `set_reason_phrase` 被去掉**：HTTP 状态码到 reason phrase 是一对一标准映射（200→OK），用户手工设置只会引入 typo。内部查表即可。

**为什么 headers 是函数返回的 Headers 对象而非直接成员**：Photon 是 `resp.headers.insert(k,v)`（直接暴露成员）。我们用 `resp.headers().insert(k,v)`（函数返回引用）的好处是封装——未来 headers 可能要做懒序列化或分 pending/sent 两套，不暴露成员给重构留空间。代价是多一对括号，可接受。

### 5.5 内置 `HttpRouter`

```cpp
class HttpRouter : public HttpHandler {
public:
    using HandlerFn = std::function<void(HttpRequest&, HttpResponse&)>;

    // 静态路由
    void add(HttpRequest::Method m, std::string_view path, HandlerFn fn);

    // 动态路由，path 中 ":name" 为参数段，"*name" 为通配后缀
    // 例如 "/user/:id"、"/files/*path"
    void add(HttpRequest::Method m, std::string_view path_pattern, HandlerFn fn);

    // 便捷别名
    void get(std::string_view path, HandlerFn fn);
    void post(std::string_view path, HandlerFn fn);
    // put / del / patch / head / options

    // 404 / 405 回调（可选）
    void set_not_found_handler(HandlerFn fn);
    void set_method_not_allowed_handler(HandlerFn fn);

    void handle(HttpRequest& req, HttpResponse& resp) override;
};
```

**为什么要内置 Router 而不是完全交给 `route` 代码生成器：**
- 代码生成器对简单场景（临时项目、几条路由的 demo）太重——手写两行 `router.get("/hello", fn)` 更快。
- 没有内置 Router 意味着用户必须会用 `route` 才能构建任何 HTTP 应用，入门曲线陡。
- 两者定位不冲突：内置 Router 覆盖简单场景，`route` 覆盖从 CRDL 生成复杂 Router 的场景。

**路由匹配策略（为什么）：**
- 静态路由用 `unordered_map<(method, path)>` 查找——O(1) 最常见场景。
- 动态路由按段数分桶线性匹配——段数通常 ≤5，线性扫描快于构建 trie，实现简单。
- 静态优先动态兜底——用户直觉符合（精确匹配优先）。

### 5.6 `http_common` 合入主项目

吸收到 `http_util.h` / `http_util.cpp`：
- `parse_form_urlencoded(body) -> map<string,string>`
- `url_decode(in) -> string`
- `bad_request(resp, msg)` 等便捷错误响应

**为什么合并：**
- 这些函数本来就是 HTTP 标准的基础操作，放一个独立项目里对使用者是多一个依赖。
- 独立项目维护成本 > 收益（没人会单独用 `url_decode` 而不要 HTTP server）。

---

## 6. 流式模式实现细节

### 6.1 Buffered（默认）

1. handler 调用 `resp.set_status()` / `resp.headers().insert()` / `resp.set_body()`
2. handler 返回
3. Connection 将 resp 序列化为 shared_ptr<string>
4. `async_write` 发送，完成后若 keep-alive 继续 `read()`

**等同于当前模型**，仅 API 命名变化。**为什么保持这个路径不变**：这是当前 313k req/s 吞吐的来源，换路径就得重新压测调优。

### 6.2 Immediate（writev 直发）

1. handler 调用 `resp.set_status()` / `resp.headers().content_length(n)`
2. handler 调用 `resp.writev(iov, cnt)`（可多次）
   - 首次调用：内部同步 `boost::asio::write` 发送 `status_line + headers + \r\n\r\n + iov[0..cnt-1]`
   - 后续调用：同步 `boost::asio::write(iov)`
3. handler 返回 → Connection 不再发任何东西，直接判定 keep-alive 续读或关闭

**线程**：全部发生在 io 线程，同步阻塞 io 线程直到 send 完成。适用于短响应（几 KB ~ 几 MB）。

**为什么接受 io 线程被短暂阻塞：**
- 本地/局域网 TCP send 对几 KB 级响应是微秒级操作，阻塞忽略不计。
- 换取零拷贝、零堆分配、单 syscall——weave++ 生成代码的吞吐热路径依赖这个。
- 对于跨公网的大响应（几 MB 以上），确实会阻塞一段时间——但这是极少数场景，且用户可以选 Stream 模式避免。

### 6.3 Stream（lambda off-thread）

1. handler 配置好 headers（通常 `Transfer-Encoding: chunked` 或 `text/event-stream`）
2. handler 调用 `resp.stream([captures](HttpResponse& r){ ... 长循环 ... })`
   - Connection 标记该请求进入流式模式
   - handler 返回后，Connection 不发默认响应，而是把 lambda `post` 到 handler 线程池
3. Lambda 在 handler 线程执行，内部调用 `r.write_chunk(...)`——同步 `boost::asio::write`
4. Lambda 返回后，Connection 发送结束 chunk（若用 chunked），然后 keep-alive 续读或关闭

**为什么是 lambda 而不是 coroutine / 单独注册流式 handler 类：**
- **Coroutine（C++20）**：心智模型最干净，但要求 C++20、协程 runtime、调度器——实现成本和依赖都重，本次不做。
- **流式 handler 类**：强制用户拆类，同一个 handler 不能同时支持非流式和流式（比如根据 Accept 头决定返回 JSON 还是 SSE）。
- **Lambda** 方案：用户写普通 handler，判断要流式就 `resp.stream([...]{...})`，控制流清晰，捕获变量由 lambda 语法处理（注意不能捕获栈引用逃逸）。

**并发安全**：单个 Connection 同时只有一个流式 lambda 在执行，不存在多线程写同一 socket 的问题。

**handler 线程池**：使用 `boost::asio::thread_pool` 或独立的 `io_context` + 线程池均可，倾向于前者，简单。

---

## 7. 目录结构与构建

### 7.1 新目录结构（重命名/重组后）

```
httpserver/
├── CMakeLists.txt
├── DESIGN.md                   # 本文件
├── CHANGELOG.md
├── include/
│   └── httpserver/
│       ├── http_server.h
│       ├── http_handler.h
│       ├── http_request.h
│       ├── http_response.h
│       ├── http_router.h
│       └── http_util.h         # 原 http_common
├── src/
│   ├── http_server.cpp
│   ├── connection.cpp / .h     # 内部实现
│   ├── http_request.cpp
│   ├── http_response.cpp
│   ├── http_router.cpp
│   ├── http_util.cpp
│   ├── server_status.cpp / .h  # 内部，可选保留
│   └── timer_manager.cpp / .h  # 内部
└── test/
    ├── hello_world_test.cpp
    ├── router_test.cpp
    ├── streaming_test.cpp
    └── bench/
        └── ab_bench.sh
```

**为什么引入 `include/httpserver/` 和 `src/` 分离：**
- 当前所有头文件和 .cpp 平铺在根目录，周边项目 `#include "http_request.h"` 会污染用户的 include 名字空间（撞名风险）。
- 改为 `#include <httpserver/http_request.h>` 明确归属，也让 CMake install 后的路径层级清晰。

### 7.2 CMake

替换 `makefile` 为 `CMakeLists.txt`，产出：
- `libhttpserver.a`（静态库，方便周边项目链接）
- 可选 `libhttpserver.so`
- 安装 headers 到 `include/httpserver/`
- 导出 CMake target `httpserver::httpserver`，周边项目通过 `find_package(httpserver)` 或 `add_subdirectory` 使用

依赖：
- Boost.Asio（header-only 部分或 `-lboost_system`）
- pthread
- 测试：gtest

**为什么换 CMake：**
- 周边项目（`photon_http_server`、`simple_http_template` 等）已经是 CMake，集成一致。
- `find_package` / `target_link_libraries` 的依赖传播比 make 的 `-I/-L` 手工管理可靠得多，尤其跨项目。
- IDE（CLion、VSCode）对 CMake 项目的索引/调试支持更好。
- 代价：多了 `CMakeLists.txt` 的学习成本——但既然周边都已经是 CMake，这个成本是"回本"而不是新加。

### 7.3 项目名

**暂不改动**。以后想好再说。文档、CMake target、头文件命名空间暂用 `httpserver`。

---

## 8. 周边项目迁移计划

### 8.1 `http_common` → **合并进主项目**

- 把 `parse_form_urlencoded` / `url_decode` 等函数搬到 `include/httpserver/http_util.h`
- 原仓库归档

### 8.2 `route`（代码生成器）→ **保留独立项目**

- 生成的 Router 代码改为继承 `httpserver::HttpHandler`，实现 `handle(HttpRequest&, HttpResponse&)`
- 动态路由参数暴露方式：生成代码里直接从匹配到的段提取，或依赖内置 `HttpRouter`（二选一，倾向前者以保持生成代码自包含）
- `photon::net::http::Verb` → `HttpRequest::Method`
- 需要更新 `code_generator.cpp` 的模板

**为什么保留独立：** 它是构建期代码生成工具，和运行时库是两个物种。合并反而让运行时库多了一个 CLI 入口和 CRDL parser 依赖。

### 8.3 `weave++`（代码生成器）→ **保留独立项目**

- 生成代码里的 `resp.writev(iov_list.data(), iov_list.size())` 迁移到新 API，签名兼容（参数一致），仅头文件和类型变更
- `resp.set_result(200)` → `resp.set_status(200)`
- `resp.headers.insert(k, v)` → `resp.headers().insert(k, v)`

### 8.4 `simple_http_template` → **保留作为端到端验证**

- 更新 `test.cpp`：`PhotonHttpServer` → `HttpServer`
- Handler 函数签名全部更新（参考本文 5.3/5.4）
- 保持 Json、DB 等外部依赖不变

**为什么保留：** 它是所有组件整合的回归测试床。迁移完就是"是否成功"的判据。

### 8.5 `yxmysql` → **不动**

本来就不依赖 HTTP。

---

## 9. 工作分解（按实施顺序）

**为什么这个顺序：** 每一步都要能独立跑通、独立回归，避免"一口气改完再调试"的长时间无反馈状态。

### Phase 1：核心骨架
1. **清理**：删除 `routing_module.*`、`request_handler.*`、旧 `RoutingModule` 使用点
2. **CMake 替换 makefile**
3. **`HttpHandler` + `HttpServer`** 外观类 + 信号处理
4. **重构 `HttpRequest`**：`std::string_view` 接口、补齐方法枚举、大小写不敏感 header 查询
5. **重构 `HttpResponse`（Buffered 模式）**：新 API 形状，保持 set_body → async_write 现有行为
6. **Connection 适配**：调用 `HttpHandler::handle` 而非 `RoutingModule::route_request`
7. **Hello world + ab 回归测试**：确认吞吐不掉

**里程碑判据**：能跑 `GET /hello` 返回 200，ab 压测吞吐不低于当前 313k × 90%。

### Phase 2：内置 Router + 工具函数
8. **`HttpRouter`**：静态 + 动态路由，自测
9. **合并 `http_common`**：搬入 `http_util.*`
10. **单元测试**：router、util

**里程碑判据**：`router.get("/user/:id", fn)` 能命中动态路由并取到参数。

### Phase 3：流式能力
11. **`HttpResponse::writev`（Immediate 模式）**：io 线程同步 writev
12. **`HttpResponse::stream(lambda)`（Stream 模式）**：handler 线程池 + chunked/SSE 辅助
13. **流式集成测试**：LLM mock、SSE、大文件 writev

**里程碑判据**：mock 一个每秒吐 10 个 token 的 LLM，连续 30 秒输出不丢 token、不阻塞其他连接。

### Phase 4：周边项目迁移
14. **`route`**：更新生成器模板
15. **`weave++`**：更新生成器模板
16. **`simple_http_template`**：端到端迁移 + 回归测试

### Phase 5：性能验证与文档
17. **压测**：ab / wrk，对比迁移前后
18. **README / API 文档**
19. **CHANGELOG 记录**

---

## 10. 性能目标与压测

### 10.1 设计期目标

| 指标 | Phase 0 基线 | 目标（迁移后） |
|---|---|---|
| ab hello-world 吞吐 | 313k req/s | ≥ 280k req/s（允许 10% 内下滑） |
| ab 失败数 | 0 | 0 |
| writev 零拷贝 | 不支持 | 首次支持 |
| 并发 SSE/LLM 流 | 不支持 | ≥ 100 并发 |

压测命令基线：`ab -n 100000 -c 1000 -k http://127.0.0.1:8080/hello`

**为什么允许 10% 内下滑**：新增的虚函数调用、Headers 对象包装、CMake 编译配置差异等都会带来微小开销。强求零退步可能逼着做过度优化（比如 inline 一切、裸指针暴露内部结构），得不偿失。10% 是"用户感知不到、但开发保有余地"的平衡。

### 10.2 与 PhotonLibOS 头对头对比（2026-04-20）

同机器、同工作负载下对比 PhotonLibOS（原项目栈）和 fox-http。

**环境**：
- 硬件：12th Gen Intel Core i9-12900（12 cores），Linux 6.8
- 工作负载：响应 `HTTP/1.1 200 OK` + 6 字节 body
- 命令：`ab -n 100000 -c 1000 -k http://127.0.0.1:<port>/<path>`
- 两边均 Release（`-O3 -DNDEBUG`），默认线程数 = hardware_concurrency = 12
- Photon 端：`PhotonHttpServer` + `HTTPHandler`（最小 hello，完全对应 fox-http 的 hello_world）

**结果**（3 次运行均值）：

| 实现 | req/s | p50 | p95 | p99 | CPU 占用 |
|---|---|---|---|---|---|
| PhotonLibOS | **86k** | 11-13 ms | 13-14 ms | 23-31 ms | ~52% |
| fox-http Buffered | **283k** | 3 ms | 4-5 ms | 10-13 ms | ~25% |
| fox-http Immediate writev | **285k** | 3 ms | 4 ms | 11-12 ms | ~25% |

两边均 0 失败、100% keep-alive。Photon 多次运行间吞吐逐步下降（96k → 86k → 76k），fox-http 保持稳定（276-291k）。

**结论**：
- 吞吐：fox-http 是 Photon 的 **~3.3×**
- 延迟 p50：fox-http 低 ~4×；p99 低 ~2-3×
- CPU 效率：fox-http 在半负载 CPU 下达到 3× 吞吐，每请求 CPU 代价约为 Photon 的 **1/6**

**归因（推测）**：
- fox-http 内核是 Boost.Asio epoll + 普通线程池；Photon 是 fiber + 用户态调度器，每请求多一次协程切换
- fox-http Immediate 模式的 headers coalesce + TCP_NODELAY 把每响应压缩到 1 次 writev（详见 CHANGELOG 2026-04-17 Phase 3）
- Photon 的 fiber 栈在高并发下可能造成内存与缓存压力

**对迁移的意义**：DESIGN.md 最初预期"换架构会掉一些性能，允许 10% 下滑"。实际迁移后不仅没掉，反而性能提升 3×——10% 下滑预算始终未触底。

### 10.3 阶段内演进

同基线代码路径（Asio-based，非 Photon）在各 Phase 下的成绩：

| Phase | req/s | 备注 |
|---|---|---|
| Phase 0 基线 | 313k | RoutingModule 直接分发，Release |
| Phase 1 骨架 | ~284k | 虚函数分发 + HttpHandler 抽象 + Headers 线性查找 |
| Phase 2 Router | ~287k | 加了 Router，几乎无额外开销 |
| Phase 3 流式 | ~285k (buf) / ~276k (writev) | 首次引入 Immediate/Stream 两模式 |
| Phase 4-5 改名 + 异常安全 | ~285k | 无功能开销 |

期间踩过的大坑：Phase 3 writev 首版只有 24k req/s，两次 sync write（headers + body）触发 Nagle + delayed-ACK 40ms 停顿。修复：coalesce headers 到首次 iov + TCP_NODELAY。详见 CHANGELOG。

---

## 11. 未决事项（开坑区）

**当前状态（2026-04-19）：核心未决项已解决或明确搁置。**

已解决（实现在各 Phase，条目保留供追溯）：
- ~~动态路由参数暴露~~ → Phase 2：`req.param("id")` 接口
- ~~handler 线程池默认大小~~ → Phase 3：`4 × hw_concurrency`，可通过 `set_stream_pool_size()` 配置
- ~~handler 抛异常~~ → Phase 5：Connection 在 dispatch 和 stream lambda 处都
  加了 try/catch。头未发时改写为 500 + 异常消息；头已发时 log + 关连接。
  io 线程不会因 handler 崩溃。

明确搁置，不计划短期实现：
- **chunked 请求体**：当前 Connection 只按 Content-Length eager 读 body，不支持
  `Transfer-Encoding: chunked` 请求体。周边工具无此需求；实现需改 Connection
  状态机（多段循环读）。如有需求再开。
- **HTTP/2 / HTTPS**：HTTP/1.1 覆盖主要内部场景；HTTPS 建议由反向代理
  （nginx / caddy）承担，避免 OpenSSL 依赖。
- **日志系统**：当前 `HTTPSERVER_LOG` 宏足够；接 spdlog 会引入依赖，暂不做。
- **项目名**：仍叫 `httpserver`，待命名灵感。

---

## 12. 决策记录（Changelog of decisions）

| 日期 | 决策 | 理由 |
|---|---|---|
| 2026-04-16 | 不做 Photon 兼容层，完全重构 | 周边都可改，兼容层长期是债；Photon 形状（fiber 假设、headers 直接成员）在 Asio 里不自然 |
| 2026-04-16 | 保留 Boost.Asio 内核 | 已调优、刚修竞态、313k req/s 基线稳定；重写大概率再踩同样的坑 |
| 2026-04-16 | 混合线程模型（io + 懒加载 handler 池） | 纯切换模型掉吞吐 20-40%；大多数 handler 不需要切；lazy 初始化对纯 API 用户零成本 |
| 2026-04-16 | 删除 `RoutingModule` | 新 `HttpRouter` 作为 HttpHandler 更一致；`route` 生成器也是这个模型 |
| 2026-04-16 | 合并 `http_common` 入主项目 | url_decode/form_parse 是 HTTP 库基本功能；独立维护成本 > 收益 |
| 2026-04-16 | 保留 `route`、`weave++` 为独立代码生成器 | 构建期工具和运行时库物种不同；合并会引入 CRDL parser 等非 HTTP 依赖 |
| 2026-04-16 | 换用 CMake | 周边项目已是 CMake；find_package 依赖传播比 make 可靠 |
| 2026-04-16 | 项目名暂不改 | 未想好；重构中途改名是无意义扰动 |
| 2026-04-16 | `HttpResponse` 三模式：Buffered / Immediate(writev) / Stream(lambda) | 三种场景需求冲突，统一模型只能牺牲两个；三模式互斥 fail fast 避免误用 |
| 2026-04-19 | Handler 抛异常 → Connection catch 后回 500 | io 线程异常传播会崩溃进程；头未发时 500 可清洁替换，头已发时只能 close |
| 2026-04-16 | Stream 用 lambda 而非 coroutine | C++20 协程依赖太重；lambda 方案同 handler 内可混用流式/非流式 |
| 2026-04-16 | Request body eager 读取，暂不支持流式上传 | 周边工具无此需求；流式上传需重构 Connection 状态机 |
| 2026-04-16 | Header 查询改为大小写不敏感 | RFC 7230 规定；当前 `std::map` 实现是 bug 级别 corner case |
| 2026-04-16 | 引入 `include/httpserver/` 命名空间目录 | 避免头文件平铺污染周边项目 include 名空间 |
