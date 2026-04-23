# 从 PhotonLibOS 工具链迁移到 fox 生态

把基于 **`photon_http_server` + `route`（`crdl_compiler`）+ `weave++` + `http_common`**
的项目迁移到 **`fox-http` + `fox-route` + `fox-page`**。

---

## 工具链对照

| 旧（Photon 栈） | 新（fox 生态） |
|---|---|
| `photon_http_server` 静态库（`-lphoton_http_server`） | `fox-http::fox-http` CMake target（`libfox-http.a`） |
| `PhotonLibOS`（`-lphoton`，fiber 调度器） | Boost.Asio（header-only 为主） |
| `http_common` 静态库 | 合入 `fox-http` 的 `fox::http::util` 命名空间 |
| `route/crdl_compiler`（CRDL → Router 生成器） | `fox-route`（二进制名即 `fox-route`） |
| `route/crdl_func`（抽签名给 weave++ 用） | `fox-route-func` |
| `weave++`（HTML → C++ 生成器） | `fox-page` |

**端到端已验证的迁移样板：**`/home/yelin/code/fox-http-example`（从原
`simple_http_template` 拷贝后改造而来，原名 `simple_http_template_hs`，
2026-04-23 重命名）。本文所有示例都对得上那里的实际代码。

---

## 为什么迁移

- **去 Photon 依赖**：PhotonLibOS 是重量级 fiber runtime，需要系统级安装；新
  栈只需 Boost headers + pthread。
- **性能**：同机头对头压测（`ab -n 100000 -c 1000 -k`，hello handler）：
  - PhotonLibOS：**86k req/s**，p99 23-31ms，CPU 52%
  - fox-http：**283k req/s**，p99 10-13ms，CPU 25%
  - 吞吐 ~3.3×，CPU 效率 ~6×。详见 `DESIGN.md §10.2`。
- **响应模型更表达**：fox-http 三种模式（Buffered / Immediate writev / Stream
  lambda）互斥，按场景选，短 API 零切换，weave++ 模板继续零拷贝，LLM 长流
  不阻塞 io。
- **异常安全**：handler 抛异常 → Connection catch → 500，io 线程不崩。Photon
  下未捕获异常会传到 fiber scheduler，常见处理是整个 vcpu 卡住。
- **纯标准 C++ 心智模型**：不再需要理解 photon fiber、photon::init、vcpu 数
  这些概念；线程池 + `async_read`/`async_write`，Boost.Asio 用户秒懂。

---

## 改动总览

| 维度 | 旧 | 新 |
|---|---|---|
| 服务器类 | `PhotonHttpServer server(port, vcpu_num);` | `fox::http::HttpServer server(port, io_threads);` |
| 注册 handler | `server.add_handler(h);` | `server.set_handler(h);` |
| Handler 基类 | `photon::net::http::HTTPHandler` | `fox::http::HttpHandler` |
| Handler 方法 | `int handle_request(Request&, Response&, string_view) override;` | `void handle(HttpRequest&, HttpResponse&) override;` |
| 请求类 | `photon::net::http::Request` | `fox::http::HttpRequest` |
| 响应类 | `photon::net::http::Response` | `fox::http::HttpResponse` |
| HTTP 方法枚举 | `photon::net::http::Verb`（`Verb::GET` 等） | `fox::http::HttpRequest::Method`（`Method::GET` 等） |
| 请求路径 | `req.target()` | `req.path()` |
| 请求方法 | `req.verb()` | `req.method()` |
| 请求 header | `req.headers["Content-Length"]` | `req.header("Content-Length")`（大小写不敏感） |
| 请求 body（定长） | `req.headers.content_length()` + `req.read(buf, n)` | `req.body()`（Connection 预读） |
| 请求 body（chunked） | 需手动解 | `req.body()` 同上（Connection 自动解 chunked） |
| 响应状态 | `resp.set_result(200)` | `resp.set_status(200)` |
| 响应 header 插入 | `resp.headers.insert(k, v)` | `resp.headers().insert(k, v)`（注意括号） |
| 响应 Content-Length | `resp.headers.content_length(n)` | `resp.headers().content_length(n)` |
| 响应 body（buffered） | `resp.write(data, n)` 或多次 | `resp.set_body(std::string)` |
| 响应 writev 零拷贝 | `resp.writev(iov, n)` | `resp.writev(iov, n)` —— **签名兼容** |
| 响应流式（chunked/SSE） | 手动写 `Transfer-Encoding` + 多次 `write` | `resp.stream([](HttpResponse&){ ... })` + `write_chunk` / `write_sse_event` |
| 表单解析 | `http_common::parse_form_urlencoded(body)` | `fox::http::util::parse_form_urlencoded(body)` |
| URL 解码 | `http_common::url_decode(s)` | `fox::http::util::url_decode(s)` |
| 400 响应 | `http_common::http_400_bad_request(resp, msg)` | `fox::http::util::bad_request(resp, msg)` |
| FILESYSTEM 路由 | Photon `new_fs_handler` + `new_localfs_adaptor` | fox-route 生成的内联简单文件服务器（读文件 + 扩展名 → MIME） |

---

## 1. `main.cpp` — 服务器启动

```cpp
// 旧
#include "photon_http_server.h"
#include "router.generated.h"

int main() {
    set_log_output_level(ALOG_INFO);
    PhotonHttpServer server(19876, 0);       // 0 = hardware_concurrency
    Router router;
    server.add_handler(&router);
    return server.run();
}
```

```cpp
// 新
#include "fox-http/http_server.h"
#include "router.generated.h"

int main() {
    fox::http::HttpServer server(19876, 0);  // 0 = hardware_concurrency
    Router router;
    server.set_handler(&router);
    return server.run();
}
```

两个关键点：

- `set_log_output_level` / `ALOG_*` 是 Photon 的东西，直接删。fox-http 用
  `FOX_HTTP_LOG` 宏，默认关闭；开启方式：CMake `-DFOX_HTTP_DEBUG_LOG=ON`。
- 信号处理（SIGINT/SIGTERM/SIGTSTP）`server.run()` 内置，老代码里手挂信号的
  部分可以删。

---

## 2. Handler 函数 — `handlers.cpp`

### 基本的 `resp` 写法

```cpp
// 旧 Photon
void hello(photon::net::http::Response& resp) {
    static const char body[] = "Hello";
    resp.set_result(200);
    resp.headers.insert("Content-Type", "text/plain; charset=utf-8");
    resp.headers.content_length(sizeof(body) - 1);
    resp.write(body, sizeof(body) - 1);
}
```

```cpp
// 新 fox-http —— 最省事的 buffered 模式
void hello(fox::http::HttpResponse& resp) {
    resp.set_status(200);
    resp.headers().content_type("text/plain; charset=utf-8");
    resp.set_body("Hello");        // 自动计算 Content-Length
}
```

### 读文件（Photon 的 `new_localfs_adaptor` → 标准 `<fstream>`）

```cpp
// 旧
#include <photon/fs/localfs.h>
void favicon(photon::net::http::Response& resp) {
    auto fs = photon::fs::new_localfs_adaptor();
    DEFER(delete fs);
    auto file = fs->open("../pages/favicon.ico", O_RDONLY);
    if (!file) { resp.set_result(404); resp.headers.content_length(0); return; }
    DEFER(delete file);
    struct stat st;
    file->fstat(&st);
    resp.set_result(200);
    resp.headers.insert("Content-Type", "image/x-icon");
    resp.headers.content_length(st.st_size);
    resp.write_stream(file, st.st_size);
}
```

```cpp
// 新
#include <fstream>
#include <sstream>
void favicon(fox::http::HttpResponse& resp) {
    std::ifstream ifs("../pages/favicon.ico", std::ios::binary);
    if (!ifs) { resp.set_status(404); return; }
    std::stringstream buf; buf << ifs.rdbuf();
    resp.set_status(200);
    resp.headers().content_type("image/x-icon");
    resp.set_body(buf.str());
}
```

> 如果这是个热路径（大量静态文件），改走 fox-route 的 `FILESYSTEM` 路由，
> 不用自己写 handler；见下面"FILESYSTEM"小节。

### 表单 / JSON handler

签名里的框架类型跟着换即可，参数名不变：

```cpp
// 旧
void login(std::unordered_map<std::string, std::string> form,
           photon::net::http::Response& resp);
Json::Value api_user(int64_t id);

// 新
void login(std::unordered_map<std::string, std::string> form,
           fox::http::HttpResponse& resp);
Json::Value api_user(int64_t id);   // 无 resp 的不用改
```

### writev 零拷贝（weave++/fox-page 生成的代码）

`writev(iov, n)` 签名两边完全一致，**fox-page 生成的代码直接链接 fox-http
即可**，老 weave++ 生成的 `.cpp` 只要改 include + 命名空间即可：

```cpp
// 旧 include
#include <photon/net/http/server.h>
using namespace photon::net::http;
void page(photon::net::http::Response& resp, ...) { ... resp.writev(iov, n); }

// 新 include
#include "fox-http/http_response.h"
using fox::http::HttpResponse;
void page(fox::http::HttpResponse& resp, ...) { ... resp.writev(iov, n); }
```

建议直接用 `fox-page` 重新生成，不要手改产物。

### 流式（新增能力）

LLM token 流 / SSE / 大下载——Photon 需要手写 fiber 并小心不要阻塞 vcpu。
fox-http 用 `resp.stream(lambda)`：

```cpp
void sse(fox::http::HttpResponse& resp) {
    resp.headers().content_type("text/event-stream");
    resp.headers().insert("Cache-Control", "no-cache");
    resp.stream([](fox::http::HttpResponse& r) {
        // 这里可以随意同步阻塞（等 LLM、等 DB、sleep），io 线程已经放走
        for (int i = 0; i < 10; ++i) {
            r.write_sse_event("message", "tick " + std::to_string(i));
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    });
}
```

---

## 3. 代码生成器 — CRDL 和 HTML

### CRDL 语法

**`.crdl` 文件本身不用改。** `fox-route` 对 `GET /user/{int:id} -> text show(id)`
这类语法完全兼容。

> 唯一例外：原先 route 的早期版本支持 `-> string foo()` 作为返回类型，新
> 版本统一为 `text`/`html`/`json`。如果你的 `.crdl` 用了 `string`，改成
> `text` 即可。`fox-http-example` 就做了这个调整。

### 生成器调用

```bash
# 旧
crdl_compiler -o build/gen routes.crdl
crdl_func routes.crdl show_user    # 输出 "void show_user(photon::net::http::Response&, int64_t)"
weave++ page.html -o build/gen --func "$(crdl_func routes.crdl page)"

# 新
fox-route      -o build/gen routes.crdl
fox-route-func routes.crdl show_user  # 输出 "void show_user(fox::http::HttpResponse&, int64_t)"
fox-page page.html -o build/gen --func "$(fox-route-func routes.crdl page)"
```

参数意义完全一致，只是二进制名换了。

### 生成产物的差异

`fox-route` 生成的 `Router` 类：

- 继承 `fox::http::HttpHandler`（原先是 `photon::net::http::HTTPHandler`）
- 实现 `void handle(HttpRequest&, HttpResponse&)`（原先是 `int handle_request(..., string_view)`）
- 静态 + 动态 + catchall 三类路由不变
- `ANY` 方法现在展开成 7 种 HTTP 方法的重复注册（原 Photon 有 `Verb::ANY` 枚举，新 API 没有，生成器代为处理）
- FILESYSTEM 路由：旧版调 Photon 的 `new_fs_handler`；新版在生成代码里直接
  内嵌简单文件服务器（读文件 + 扩展名 → MIME，拒绝 `..` 穿越）。**缺** Last-Modified /
  Range；生产大文件建议前置 nginx。

`fox-page` 生成的渲染函数：

- 签名改成 `void fn(fox::http::HttpResponse& resp, ...)`
- 包含头从 `<photon/net/http/server.h>` 改为 `"fox-http/http_response.h"` +
  `"fox-http/http_util.h"`
- `resp.set_result` → `resp.set_status`；`resp.headers.insert` → `resp.headers().insert`
- `resp.writev(iov, n)` **完全不变**，iov zero-copy 语义保留
- `LOG_ERROR(...)` 日志调用改为 `std::cerr`

---

## 4. CMakeLists.txt

典型下游项目需要下列改动：

### 依赖查找

```cmake
# 旧 —— 拉 Photon / photon_http_server / http_common 三个库
find_library(PHOTON_LIBRARY NAMES photon ...)
find_library(PHOTON_HTTP_SERVER_LIBRARY NAMES photon_http_server ...)
find_library(HTTP_COMMON_LIBRARY NAMES http_common ...)
# 注意链接顺序 photon_http_server 在 photon 前面
```

```cmake
# 新 —— 一个 fox-http 搞定；http_common 已合入
if(NOT TARGET fox-http::fox-http)
    set(FOX_HTTP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../fox-http
                     ${CMAKE_BINARY_DIR}/fox-http_build)
endif()
# 或者装到 /usr/local 后用 find_package(fox-http REQUIRED)
```

### 代码生成器位置

```cmake
# 旧
find_program(CRDL_COMPILER NAMES crdl_compiler HINTS ../route/build)
find_program(CRDL_FUNC     NAMES crdl_func     HINTS ../route/build)
find_program(WEAVEPP       NAMES weave++       HINTS ../weave++/build)
```

```cmake
# 新
find_program(FOX_ROUTE      NAMES fox-route      HINTS ../fox-route/build)
find_program(FOX_ROUTE_FUNC NAMES fox-route-func HINTS ../fox-route/build)
find_program(FOX_PAGE       NAMES fox-page       HINTS ../fox-page/build)
```

### 链接

```cmake
# 旧
target_link_libraries(my_app PRIVATE
    ${PHOTON_HTTP_SERVER_LIBRARY}
    ${HTTP_COMMON_LIBRARY}
    ${PHOTON_LIBRARY}
    gflags ${JSONCPP_LIBRARY} ${YXMYSQL_LIBRARY} ${MYSQLCLIENT_LIBRARY})
```

```cmake
# 新
target_link_libraries(my_app PRIVATE
    fox-http::fox-http
    gflags ${JSONCPP_LIBRARY} ${YXMYSQL_LIBRARY} ${MYSQLCLIENT_LIBRARY})
```

`fox-http::fox-http` 是 CMake alias，会自动带上 `include/` 路径，不需要手工
指定。

---

## 5. 机械替换清单（sed 脚本）

在项目根执行（跳过 `build/` 和 `.git/`）：

```bash
# C++ 源码
find . -type f \( -name "*.cpp" -o -name "*.cc" -o -name "*.h" -o -name "*.hpp" \) \
  ! -path "*/build/*" ! -path "*/.git/*" -exec sed -i \
    -e 's|#include "photon_http_server.h"|#include "fox-http/http_server.h"|g' \
    -e 's|#include <photon/net/http/server.h>|#include "fox-http/http_request.h"\n#include "fox-http/http_response.h"|g' \
    -e 's|#include "http_common.h"|#include "fox-http/http_util.h"|g' \
    -e 's|PhotonHttpServer|fox::http::HttpServer|g' \
    -e 's|photon::net::http::HTTPHandler|fox::http::HttpHandler|g' \
    -e 's|photon::net::http::Request|fox::http::HttpRequest|g' \
    -e 's|photon::net::http::Response|fox::http::HttpResponse|g' \
    -e 's|photon::net::http::Verb|fox::http::HttpRequest::Method|g' \
    -e 's|Verb::|fox::http::HttpRequest::Method::|g' \
    -e 's|http_common::|fox::http::util::|g' \
    -e 's|\.set_result(|.set_status(|g' \
    -e 's|\.headers\.insert(|.headers().insert(|g' \
    -e 's|\.headers\.content_length(|.headers().content_length(|g' \
    -e 's|\.headers\[\(.*\)\]|.header(\1)|g' \
    -e 's|\.target()|.path()|g' \
    -e 's|\.verb()|.method()|g' \
    -e 's|add_handler(|set_handler(|g' \
    {} +
```

跑完后：

1. 全局 `grep -rn "photon\|PhotonHttp\|Verb::\|set_result\|headers\.insert\|\.target()\|\.verb()"` 看残留
2. `req.read(buf, n)` 不能 sed 一键换（语义变了：`body()` 返回整个 body），
   需手工调整
3. `write_stream(file, n)` 没有直接对应，改用 `set_body` 读入文件或自己按需实现
4. 老的 `resp.write(buf, n)` 如果是配合 `content_length` 的定长 buffered 响应，
   直接改成 `resp.set_body(std::string(buf, n))`；如果是 streaming 多次写，
   改用 `resp.stream(...)` + `write_chunk`

### CMakeLists.txt 替换

```bash
find . -type f -name "CMakeLists.txt" ! -path "*/build/*" ! -path "*/.git/*" \
  -exec sed -i \
    -e 's|NAMES crdl_compiler|NAMES fox-route|g' \
    -e 's|NAMES crdl_func|NAMES fox-route-func|g' \
    -e 's|NAMES weave++|NAMES fox-page|g' \
    -e 's|crdl_compiler |fox-route |g' \
    -e 's|crdl_func |fox-route-func |g' \
    -e 's|weave++ |fox-page |g' \
    {} +
```

---

## 6. `.crdl` 文件

几乎无需改。检查两点：

- 返回类型 `-> string foo()` → `-> text foo()`（如果有）
- `ANY` 方法继续能用，但注意 fox-route 会把它**展开成 7 个方法的重复注册**
  （static + dynamic 都会多出若干条），对性能没影响但 router 内存占用略增。

---

## 7. HTML 模板

语法 **完全不变**。`x-cpp-head` / `x-cpp-source` / `x-cpp-tail` script 标签、
`{{expr}}` 插值、`cpp-*` 指令、`<% %>` 旧式代码块——fox-page 全盘继承。

**只需重新跑 fox-page** 生成新 `.cpp` 文件，它们会自动使用新 API。

---

## 8. 验证清单

一步步走：

1. **装好 fox-http、fox-route、fox-page**（`cmake --build build -j` 三次）
2. **跑 sed 脚本** 机械替换上述模式
3. **删掉老的 Photon 依赖**（CMakeLists 里的 `find_library(PHOTON_*)` 段全删）
4. **重配 + 构建** `rm -rf build && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j`
5. **起服务 + curl 冒烟**：每条路由 curl 一次，确认状态码和 body
6. **端到端对比**：
   - 老 Photon 版本和新版本各起一份，ab 两边跑（不同端口），对比吞吐和延迟
   - 功能路径（表单、JSON、文件上传）用同样的 payload 过一遍
7. **压测**：`ab -n 100000 -c 1000 -k` 确认没有 failed、吞吐在预期区间
8. **异常路径**：故意在 handler 里 `throw std::runtime_error(...)`，新版应返回
   500 + 错误消息，io 线程不崩；老 Photon 版本通常会整个 fiber 卡住或进程挂

---

## 9. 完整迁移样板

`/home/yelin/code/fox-http-example/` 是从旧 `simple_http_template`（基于
Photon 工具链）迁移过来的完整工作项目：

- 12 条路由（static / dynamic param / filesystem / JSON / text / HTML）
- yxmysql + jsoncpp 依赖保留
- weave++ 生成的 index.html / user.html 页面
- POST 表单处理（login）
- 文件读取（favicon）

它的 git 历史里可以看到每个文件的 before/after，可作为迁移过程中的参考样板。

---

## 10. 常见问题

**Q: Photon 的 `set_log_output_level(ALOG_INFO)` 在哪？**
去掉。fox-http 用 `FOX_HTTP_LOG` 宏，CMake `-DFOX_HTTP_DEBUG_LOG=ON` 开启。

**Q: `DEFER(delete fs)` / `DEFER(delete file)` 这种 Photon 宏？**
删掉，改用 RAII（`std::unique_ptr` / `std::ifstream`）。

**Q: `LOG_ERROR(0, "...", value)` 这类 Photon 日志宏？**
改 `std::cerr << "..." << value << std::endl;`。

**Q: `Json::Value` / `jsoncpp` 要改吗？**
不用。fox-route 生成的代码对 `json` 返回类型继续用 jsoncpp 序列化。

**Q: yxmysql 这种第三方库？**
不受影响。Handler 内部怎么访问 DB 完全没变。

**Q: 我的 handler 直接用了 `photon::fs::*` 文件系统 API？**
改标准库（`<fstream>` / `<filesystem>`）或你现有的文件 I/O 库。

**Q: Photon 下我用了 `photon::thread_create` / `photon::thread_sleep` 做异步？**
重新设计成 fox-http 的 `resp.stream(lambda)` —— lambda 跑在 handler 线程池
上，可以随意 `std::this_thread::sleep_for` / 阻塞读 DB / 等 LLM token。

**Q: Photon 的 vcpu 数怎么映射到 fox-http？**
`HttpServer` 构造函数的第二个参数是 io 线程数（默认 `hardware_concurrency`）。
流式场景下的 handler 线程池是独立的，`server.set_stream_pool_size(N)` 可调，
默认 `4 × hw_concurrency`，懒加载。

---

## 变更历史

- 本文档：2026-04-20 重写为真正的 Photon → fox 生态迁移指南（先前版本
  是 httpserver → fox-http 的改名指南，已取代）
- `CHANGELOG.md` 的 Phase 4 条目（2026-04-18）记录了第一次完整迁移的过程
  和踩坑（`string` 返回类型、CRDL `ANY`、CMakeLists `handlers.cpp` 路径歧义等）
