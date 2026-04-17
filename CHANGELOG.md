# 更改日志

记录每次修改的问题背景、分析思路和解决方案。

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
