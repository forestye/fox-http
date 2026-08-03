# fox-http

一个基于 Boost.Asio 的轻量 C++17 HTTP/1.1 服务器库。面向三类典型场景做了显式
优化：短 API（JSON/HTML）、模板渲染（`writev` 零拷贝）、LLM/SSE 流式长连接。

- **单一 handler 入口**：`fox::http::HttpHandler::handle(req, resp)`。
- **三种响应模式**，handler 按需选择：
  - `Buffered` — 填好 body 返回，库负责序列化 + `async_write`
  - `Immediate` — `resp.writev(iov, n)` 同步一次 `writev` 发到 socket，零拷贝
  - `Stream` — `resp.stream([](auto& r){ ... })` 把后续工作推到 handler
    线程池，原本处理请求的 io 线程立即空出来接其他连接
- **内置 Router**：静态、`:param`、`*catchall` 路径模式，默认 404/405 处理。
- **压测基线**：`ab -n 100000 -c 1000 -k` 于 12 核机器稳定 ~285k req/s，
  keep-alive 100%，0 失败；`writev` 模式与之相当（~275k req/s）。
- **异常安全**：handler 抛异常时进程不崩。如果响应头还没发送，自动替换成
  500 Internal Server Error 并关闭连接；如果已经在发送中，记录日志后关闭连接。

> 如果你要从基于 PhotonLibOS 的 `photon_http_server` + `route` + `weave++`
> 工具链迁移过来，见 [`MIGRATION.md`](MIGRATION.md)。
> 架构决策记录在 [`DESIGN.md`](DESIGN.md)，阶段变更历史在
> [`CHANGELOG.md`](CHANGELOG.md)。

---

## 依赖

- **C++17** 编译器（GCC 9+ / Clang 10+）
- **Boost**（仅 Asio 头文件，1.87 以上最方便——那之后 `boost::system` 变为
  header-only；更早的 Boost 也行，只需手动链接 `boost_system`）
- **CMake** 3.14+
- **pthread**
- （可选）**GoogleTest** 用于单元测试

---

## 5 分钟 Quickstart

```bash
git clone <this-repo> fox-http
cd fox-http
cmake -S . -B build -DFOX_HTTP_BUILD_TESTS=ON   # 打开后才会编译 hello_world / unit_tests
cmake --build build -j
./build/hello_world 8080 &
curl -s http://127.0.0.1:8080/hello   # → "hello"
```

> 库默认只编译 `fox-http` 静态库本身，不构建 demo 与测试目标。
> 想跑测试或体验 demo，请配置时显式 `-DFOX_HTTP_BUILD_TESTS=ON`。

最小 main：

```cpp
#include "fox-http/http_handler.h"
#include "fox-http/http_request.h"
#include "fox-http/http_response.h"
#include "fox-http/http_server.h"

using namespace fox::http;

class Hello : public HttpHandler {
public:
    void handle(HttpRequest& req, HttpResponse& resp) override {
        resp.set_status(200);
        resp.headers().content_type("text/plain; charset=utf-8");
        resp.set_body("hello " + std::string(req.path()) + "\n");
    }
};

int main() {
    Hello h;
    HttpServer server(8080);      // 第 2 个参数是 io 线程数，省略即 hardware_concurrency
    server.set_handler(&h);
    return server.run();          // blocks until SIGINT / SIGTERM
}
```

---

## 三种响应模式对比

### 1. Buffered（默认，适合短 API）

handler 填 body，库负责序列化 + 异步发送。

```cpp
void handle(HttpRequest&, HttpResponse& resp) override {
    resp.set_status(200);
    resp.headers().content_type("application/json");
    resp.set_body(R"({"ok":true})");
}
```

### 2. Immediate（零拷贝 `writev`，适合模板渲染）

静态片段（编译期字面量）和动态片段拼成 iov 数组，一次 `writev` 发完。
`fox-page` 代码生成器就是这么用的。

```cpp
void handle(HttpRequest& req, HttpResponse& resp) override {
    static const char part1[] = "<html><body><h1>hello ";
    static const char part2[] = "</h1></body></html>";
    std::string name(req.param("name"));

    resp.headers().content_type("text/html; charset=utf-8");
    resp.headers().content_length(
        (sizeof(part1) - 1) + name.size() + (sizeof(part2) - 1));

    struct iovec iov[3];
    iov[0] = {(void*)part1,        sizeof(part1) - 1};
    iov[1] = {(void*)name.data(),  name.size()};
    iov[2] = {(void*)part2,        sizeof(part2) - 1};
    resp.writev(iov, 3);
}
```

首次 `writev` 会自动把状态行 + headers 合并到 iov 头部，保证单 syscall
发送（避免 TCP Nagle + delayed-ACK 的 40ms 停顿）。

### 3. Stream（handler 线程池，适合 LLM/SSE）

handler 返回后，传入的 lambda 被 post 到懒加载的线程池执行，io 线程随即
空出来接其他连接。lambda 内可以阻塞（等 LLM token、DB 查询、sleep）。

```cpp
void handle(HttpRequest&, HttpResponse& resp) override {
    resp.headers().content_type("text/event-stream");
    resp.headers().insert("Cache-Control", "no-cache");
    resp.stream([](HttpResponse& r) {
        for (int i = 0; i < 10; ++i) {
            r.write_sse_event("message", "tick " + std::to_string(i));
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    });
}
```

线程池默认 `4 × hardware_concurrency()`，可通过
`server.set_stream_pool_size(N)` 配置；完全没有流式 handler 时不会创建
任何线程。

还支持 chunked 编码的低层 API：`resp.write_chunk(data, n)` +
`resp.end_chunks()`。

---

## 路由：`HttpRouter`

`HttpRouter` 本身就是 `HttpHandler`，把它 `set_handler` 给 server 就行。

```cpp
#include "fox-http/http_router.h"

HttpRouter router;

// 静态路径
router.get("/hello", [](HttpRequest&, HttpResponse& r) {
    r.set_body("hello");
});

// 参数段 :name → req.param("name")
router.get("/user/:id", [](HttpRequest& req, HttpResponse& r) {
    r.set_body("user id = " + std::string(req.param("id")));
});

// 尾部通配 *path → 捕获剩余完整后缀
router.get("/files/*path", [](HttpRequest& req, HttpResponse& r) {
    r.set_body("got: " + std::string(req.param("path")));
});

// POST，读 body
router.post("/echo", [](HttpRequest& req, HttpResponse& r) {
    r.set_body(std::string(req.body()));
});

// 自定义 404 / 405
router.set_not_found_handler([](HttpRequest&, HttpResponse& r) {
    r.set_status(418);  // I'm a teapot
});
```

匹配优先级：**静态精确 > 动态路由（段数相同的优先）> catchall（字面前缀
最长的优先）**。若某 path 在其它 HTTP 方法有注册但当前请求方法不匹配，
返回 405 + 自动 `Allow` 头。

---

## CMake 集成

### 作为子目录（本地开发，无需安装）

```cmake
if(NOT TARGET fox-http::fox-http)
    add_subdirectory(path/to/fox-http ${CMAKE_BINARY_DIR}/fox-http_build)
endif()

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE fox-http::fox-http)
```

`FOX_HTTP_BUILD_TESTS` 默认 OFF，因此子目录引入不会带入 fox-http 的 demo
和测试目标，无需在父项目里再 `set(... OFF FORCE)`。

### 通过安装使用（`find_package`）

```bash
cmake -S fox-http -B build -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build -j && sudo cmake --install build
```

```cmake
find_package(fox-http REQUIRED)
target_link_libraries(my_app PRIVATE fox-http::fox-http)
```

### 开启内部调试日志

```bash
cmake -S . -B build -DFOX_HTTP_DEBUG_LOG=ON
```

---

## 生产部署建议

### 请求体大小上限（必读）

fox-http **默认不限制请求体大小**：固定长度体按 Content-Length 一次性
`reserve` + 全量读入内存，chunked 体逐块累积。不设上限时，恶意客户端
声明超大 Content-Length（或持续发送 chunk）即可制造内存压力。生产环境
请二选一（或都做）：

1. **库内限制**（推荐，零依赖）：

   ```cpp
   HttpServer server(8080);
   server.set_max_body_size(8 * 1024 * 1024);   // 8 MB，超限直接回 413 并断开
   ```

   对 Content-Length 与 chunked 两种编码均生效；固定长度体在读 body
   之前仅凭声明值即拒绝，不产生大内存分配。`0`（默认值）表示不限制。

2. **前置 nginx** 并设置 [`client_max_body_size`](https://nginx.org/en/docs/http/ngx_http_core_module.html#client_max_body_size)：

   ```nginx
   client_max_body_size 8m;
   ```

两者语义一致（超限均为 413）；直接暴露公网的服务必须至少配置其一。

---

## 测试与压测

测试目标默认不构建，需显式打开：

```bash
cmake -S . -B build -DFOX_HTTP_BUILD_TESTS=ON
cmake --build build -j
cd build
ctest --output-on-failure                       # 运行单元 + 集成测试（37 个）
./hello_world 18080 &                           # 起 demo server
ab -n 100000 -c 1000 -k http://127.0.0.1:18080/hello
```

`hello_world` demo 内置以下路由可直接体验：

| 路径 | 模式 | 演示 |
|---|---|---|
| `GET /hello` | Buffered | 最小 handler |
| `GET /user/:id` | Buffered + 动态参 | `req.param("id")` |
| `GET /files/*path` | Buffered + catchall | `req.param("path")` |
| `POST /echo` | Buffered | 读 `req.body()` 回显 |
| `GET /weave/:name` | Immediate `writev` | 静态 + 动态 iov 零拷贝 |
| `GET /slow` | Stream + chunked | 每 100ms 发一个 chunk |
| `GET /sse` | Stream + SSE | 每 200ms 发一个 event |
| `GET /boom` | 异常路径 | handler 抛异常 → 500 |

---

## 项目结构

```
fox-http/
├── include/fox-http/          # 公共头文件
│   ├── http_server.h          # HttpServer 外观
│   ├── http_handler.h         # HttpHandler 基类
│   ├── http_request.h         # HttpRequest
│   ├── http_response.h        # HttpResponse（三模式）
│   ├── http_router.h          # HttpRouter
│   └── http_util.h            # url_decode / parse_form_urlencoded / stringify
├── src/                       # 实现（Connection / TimerManager 等不导出）
├── cmake/
│   └── fox-httpConfig.cmake.in  # find_package() 入口模板
├── test/
│   ├── hello_world.cpp        # 可执行 demo
│   ├── router_test.cpp        # gtest 用例（35 个）
│   ├── response_test.cpp
│   ├── request_test.cpp
│   ├── util_test.cpp
│   └── integration/
│       └── chunked_body_test.py  # 集成测试（chunked 请求体）
├── CMakeLists.txt
├── DESIGN.md                  # 架构决策与权衡
├── MIGRATION.md               # 从 PhotonLibOS 工具链迁移到 fox 生态
└── CHANGELOG.md               # 阶段变更记录
```

---

## 配套工具（姊妹项目）

- [`fox-route`](../fox-route) — 从 CRDL DSL 生成 Router 类的代码生成器
- [`fox-page`](../fox-page) — 从 HTML 模板生成 `void fn(HttpResponse&, ...)`
  的代码生成器，利用 Immediate `writev` 做零拷贝
- [`fox-http-example`](https://github.com/forestye/fox-http-example) — 三者组合
  的端到端示例（类似 Web 框架的全栈 demo，含路由、页面模板、MySQL、JSON 等）

---

## License

（按项目实际 license 填写）
