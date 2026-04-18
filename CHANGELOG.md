# 更改日志

记录每次修改的问题背景、分析思路和解决方案。

---

## 2026-04-18 Phase 4：周边项目迁移

从 PhotonLibOS 栈迁移到 httpserver。原项目保持不动；建立了三个平行的 `_hs` 副本：

- `/home/yelin/code/route_hs`：CRDL 路由代码生成器。`src/code_generator.cpp`
  重写了模板字符串：生成的 `Router` 继承 `httpserver::HttpHandler`，`handle()`
  接受 `HttpRequest&` / `HttpResponse&`；`Verb` → `HttpRequest::Method`；
  `resp.set_result` → `resp.set_status`；`resp.headers.insert` → `resp.headers().insert`；
  `req.verb()/req.target()/req.read()` → `req.method()/req.path()/req.body()`；
  `ANY` 方法展开为 7 种 HTTP 方法的重复注册；
  FILESYSTEM 路由改为生成内联简单文件服务器（`std::filesystem` + `std::ifstream`
  + 扩展名到 Content-Type 映射），不再依赖 Photon 文件系统。
- `/home/yelin/code/weave++_hs`：HTML 模板生成器。三个 wrapper 方法的 includes
  和 using 全部切到 httpserver；`ResponseObject&` → `HttpResponse&`；
  `resp.set_result` → `resp.set_status`；`resp.headers.*` → `resp.headers().*`；
  `LOG_ERROR` → `std::cerr`。`writev` / iovec 语义不变，weave++ 的零拷贝设计
  完整保留。
- `/home/yelin/code/simple_http_template_hs`：端到端示例。CMakeLists
  `add_subdirectory(../httpserver)`，`find_program` 定位 `crdl_compiler`/
  `crdl_func`/`weave++` 于相邻 `_hs` 项目的 build 目录。`test.cpp` 用
  `httpserver::HttpServer` 替换 `PhotonHttpServer`，DB init 改为非致命
  以便无 MySQL 环境下仍可跑起来。`handlers.cpp` 全部用新 Response API；
  `favicon` 改用 `std::ifstream` 读文件。`routes.crdl` 把 `string` 返回类型
  改为 `text`（CRDL 语法只支持 `text`/`html`/`json`）。

### 端到端验证

`simple_http_template_hs` 起服务后 curl 通过：

| 路由 | 结果 |
|---|---|
| `GET /hello` | 200，writev 模板字符串 |
| `GET /` | 200，weave++ 生成的 index.html 渲染 |
| `GET /user/42` | 400（DB 未初始化，环境问题；代码路径正确） |
| `POST /login` | 200 + Set-Cookie（好凭证）/ 401（坏凭证） |
| `GET /test/string` | 200，text return-type 路由 |
| `GET /test/string/42` | `int:id` 参数注入 |
| `GET /test/alpha/string/7` | 多参数（string + int）注入 |
| `GET /css/styles.css` | 200，FILESYSTEM 路由读本地文件 |
| `GET /nonexistent` | 404 |

### 设计原则回顾（与 DESIGN.md 对齐）

周边项目改动只发生在副本中，原 Photon 链路完整保留；httpserver 本身未
因迁移产生新代码改动——新 API 已在 Phase 1-3 完成。

---

## 2026-04-17 Phase 3：流式响应（writev 零拷贝 + stream lambda + SSE/chunked）

### 本次改动（Phase 3）

- `HttpResponse` 引入三种互斥写入模式 `Mode { Buffered, Immediate, Stream }`，
  首次调用 `set_body` / `write` / `writev` / `stream` 时锁定。模式冲突触发 assert。
- **Immediate 模式**：`write(data, n)` / `writev(iov, n)` 在 io 线程上同步 writev
  落地。首次调用把 headers 合并到 iov 头部，单次 syscall 发完（见下"coalesce"）。
- **Stream 模式**：`resp.stream(fn)` 把 lambda 移到 handler 线程池执行，io 线程
  立即放回继续 accept/read 其他请求。配套 `write_chunk(data, n)` /
  `end_chunks()`（Transfer-Encoding: chunked 自动管理）与
  `write_sse_event(event, data)`（SSE 格式，自动 data 多行前缀）。
- 内部新增 `ResponseStream` / `StreamDispatcher` 接口（`src/response_stream.h`），
  Connection 实现 `SocketStream`（同步 boost::asio::write over tcp::socket），
  `HttpServer::Impl` 实现 `StreamDispatcher`（懒加载 `boost::asio::thread_pool`，
  默认 `4 × hw_concurrency`，可通过 `set_stream_pool_size()` 配置）。
- `Connection::dispatch()` 按 Response 模式分流：Buffered → 原 `async_write`；
  Immediate → 直接 finish（handler 已经写完）；Stream → 把 lambda post 到 handler
  pool，完成后 post 回 io 线程续 keep-alive。
- Buffered/Immediate 路径的 `HttpResponse` 保留栈对象（move 到 shared_ptr 仅
  发生在 Stream 模式下），避免快路径每请求一次堆分配。

### 性能优化过程

writev 首版 ab 测试只有 24k req/s（buffered 285k 的 1/10）。根因：
每次响应发两次 sync write（headers、body），触发 TCP Nagle + delayed-ACK，
导致每个请求多出 40ms 停顿。

两个修复合并落地：
1. **headers coalesce**：首次 write/writev/write_chunk 把 headers 合并进 iov
   头部，一次 writev 发完。
2. **TCP_NODELAY**：accept 后在 socket 上开 NODELAY。HTTP 响应本就无需 Nagle
   batching，关掉避免 delayed-ACK 陷阱。

### 性能回归

ab `-n 100000 -c 1000 -k` 三次均值（相对 Phase 2 ~287k）：

| 路径 | req/s | vs 基线 |
|---|---|---|
| Buffered `/hello` | 285k（287/285/282） | 持平 |
| Immediate writev `/weave/:name` | 276k（271/280/278） | 首次支持，仅低 3% |

流式场景验证：2 个 io 线程 + 5 个并发 SSE 流（每个 2 秒），100 个快 `/hello`
请求 0.292s 全部完成（~3ms/req）。io 线程未被流式阻塞。

### 新增测试

- `test/response_test.cpp`：8 个用例，FakeStream 验证 headers coalesce、
  writev 零拷贝、chunked、SSE 格式。
- 全部 35 个测试通过。

### API 变化（Photon 迁移侧需对齐）

- `resp.set_result(int)` → `resp.set_status(int)`
- `resp.headers.insert(k, v)` → `resp.headers().insert(k, v)`（函数括号）
- `resp.headers.content_length(n)` → `resp.headers().content_length(n)`
- `resp.write(buf, n)` / `resp.writev(iov, n)` — 签名兼容
- 流式场景：Photon 的 `photon::thread_create` → `resp.stream([](HttpResponse&){...})`

---

## 2026-04-17 Phase 2：内置 Router、合并 http_common、完善 body 读取

### 本次改动（Phase 2）

- 新增 `HttpRouter`（`include/httpserver/http_router.h` + `src/http_router.cpp`）
  - 静态路由（unordered_map 精确匹配）
  - 动态 `:name` 参数段（按 segment_count 分桶）
  - 尾部 `*name` 通配（按 literal 前缀长度排序，长前缀优先）
  - 默认 404 / 405（Allow header 自动填），可覆盖
- `HttpRequest` 新增 path param 访问：`param(name)` / `has_param` / `set_param` /
  `clear_params`。Router 在匹配后写入。
- 合并 `http_common` 工具函数到 `include/httpserver/http_util.h`
  （`httpserver::util::url_decode` / `parse_form_urlencoded` / `stringify<T>` /
  `bad_request` / `not_found` / `internal_error`）。
- **补：`Connection` 现在按 Content-Length eager 读 body**。Phase 1 漏了这块，
  导致 POST 处理器拿不到 body——迁移 `route` / `weave++` 需要这个能力。实现上
  先 drain streambuf 中已缓存的溢出字节（async_read_until 通常会超读），不足部分
  再 `async_read(transfer_exactly)` 补齐。
- 新增单元测试：`test/router_test.cpp`（10 个用例覆盖静态/动态/catchall/优先级/
  405/自定义 404）、`test/util_test.cpp`（11 个）、`test/request_test.cpp`
  （6 个）。27 个测试全部通过。
- `hello_world` demo 改用 `HttpRouter`，演示静态 / `:id` / `*path` / POST 各路径。

### 性能回归

ab `-n 100000 -c 1000 -k` 三次运行：289k / 292k / 280k req/s。与 Phase 1
（274k / 285k / 293k）基本持平，Router 分发开销可忽略。

---

## 2026-04-17 Phase 1：核心骨架重构（PhotonLibOS 迁移）

### 背景

迁移周边项目（`http_common` / `route` / `weave++` / `simple_http_template`）
从 PhotonLibOS 到本项目。详见 `DESIGN.md`。

### 本次改动（Phase 1）

- 删除 `RoutingModule` / `BaseRequestHandler` 旧路由体系
- 重新布局：`include/httpserver/` 为公共头文件，`src/` 为实现
- 全部代码置于 `httpserver` 命名空间
- 新增 `HttpHandler` 抽象基类（单方法 `handle(req, resp)`）
- 新增 `HttpServer` 外观类（pimpl；构造 `(port, io_threads=0)`；`run()` 内置
  SIGINT/SIGTERM/SIGTSTP 信号处理）
- 重构 `HttpRequest`：`method()` / `path()` / `header()` / `query()` / `body()`
  返回 `string_view`；header 查询大小写不敏感；补齐 PATCH/HEAD 方法
- 重构 `HttpResponse`（Buffered 模式）：`set_status(int)` 自动映射 reason phrase；
  `headers().insert/content_length/content_type` 链式 API
- `Connection` 改为接 `HttpHandler&` 直接分发
- `makefile` → `CMakeLists.txt`，导出 `httpserver::httpserver` target
- `DEBUG_LOG` → `HTTPSERVER_LOG`（避免宏名与用户冲突）
- 新增 `test/hello_world.cpp` 作为冒烟测试与压测目标

### 压测回归

`ab -n 100000 -c 1000 -k`，3 次运行：

| 指标 | 2026-02-18 基线 | Phase 1 结果 |
|---|---|---|
| Requests/sec | 313,234 | 274k / 285k / 293k（均 ~284k） |
| Failed requests | 0 | 0 |
| Keep-Alive requests | 100,000 | 100,000 |

约 9% 吞吐下滑，在设计文档允许的 10% 范围内。下滑来自新抽象
（虚函数分发、Headers 线性查找等）。留待 Phase 3 评估是否值得优化。

---

## 2026-02-18 修复多线程 keep-alive 压测下请求解析失败

### 问题现象

单次 `curl http://127.0.0.1:8080/hello` 访问正常，但使用
`ab -n 100000 -c 1000 -k http://127.0.0.1:8080/hello` 压测时：

- 服务端大量输出 `Connection(...)::read() - header delimiter not found`
- 100000 次请求中有 6411 次失败（Length: 4469, Exceptions: 1942）
- Keep-Alive 只成功 98058 次（应为 100000）
- 吞吐量 207,325 req/s

### 根因分析

问题出在 `connection.cpp` 的 `read()` 回调中，`request_buffer_.consume()` 放在了
`write()` 之后：

```cpp
write(response_string);                    // 1. 启动异步写
request_buffer_.consume(header_length);    // 2. 消费 buffer  ← 太晚了
```

服务器通过多线程运行 `io_context`（`server.cpp` 中创建 `hardware_concurrency()-1`
个线程）。在 localhost 上 async_write 可能极快完成，导致如下竞态：

1. **线程 A**（当前 read 回调）：调用 `write()` 启动 `async_write`
2. **线程 B**：`async_write` 完成，write 回调触发，调用 `read()` →
   `async_read_until` 检查 buffer，发现旧的未消费的 `\r\n\r\n`，标记为成功
3. **线程 A**：此时才执行 `consume()`，将 buffer 中的旧数据清除
4. **线程 B/C**：新的 read 回调触发，用 `std::search` 搜索 buffer 中的
   `\r\n\r\n` → 已被 consume 清掉，搜索失败

本质上是多线程 `io_context` 下缺乏 strand 序列化，`consume()` 和下一次
`async_read_until` 之间产生了竞态。

此外，代码手动用 `std::search` 搜索分隔符是多余的——`async_read_until` 成功时
已保证前 `bytes_transferred` 字节包含完整的头部（含分隔符 `\r\n\r\n`），可以直接使用。

### 修改方案

修改文件：`connection.cpp` — `Connection::read()` 方法

1. **直接使用 `bytes_transferred`**：移除手动 `std::search` 搜索分隔符的逻辑，
   直接从 buffer 前 `bytes_transferred` 字节提取头部数据
2. **将 `consume()` 移到 `write()` 之前**：确保 buffer 在启动新的异步操作前已被
   正确消费，消除竞态窗口

修改前：
```cpp
auto buffers = request_buffer_.data();
auto begin = boost::asio::buffers_begin(buffers);
auto end = boost::asio::buffers_end(buffers);
auto delimiter_it = std::search(begin, end, HEADER_DELIMITER.begin(), HEADER_DELIMITER.end());
if (delimiter_it == end) {
    DEBUG_LOG("header delimiter not found");
    socket_.close();
    return;
}
auto header_length = std::distance(begin, delimiter_it) + HEADER_DELIMITER.size();
std::string header_data(begin, begin + header_length);
// ... parse, handle ...
write(response_string);
request_buffer_.consume(header_length);      // BUG: consume 在 write 之后
```

修改后：
```cpp
auto begin = boost::asio::buffers_begin(request_buffer_.data());
std::string header_data(begin, begin + bytes_transferred);
// ... parse, handle ...
request_buffer_.consume(bytes_transferred);  // FIX: consume 在 write 之前
write(response_string);
```

### 修复效果

| 指标 | 修复前 | 修复后 |
|---|---|---|
| Failed requests | 6,411 | **0** |
| Keep-Alive requests | 98,058 | **100,000** |
| Requests/sec | 207,325 | **313,234** (+51%) |
| Transfer rate (KB/s) | 81,623 | **122,357** (+50%) |
