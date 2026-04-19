# 从 `httpserver` 迁移到 `fox-http`

2026-04-19 起本库更名为 `fox-http`。本文是**给已有使用 `httpserver` API 的项目**
的机械迁移指南。API 语义没有改变，只是名字换了；迁移本质上是字符串替换 +
CMake 调整。

> 若你是从更老版本（基于 PhotonLibOS 的 `photon_http_server`，或更早的
> `RoutingModule` API）迁移过来，先参考 `CHANGELOG.md` 里对应阶段的说明，
> 完成到当前 `httpserver` API，再按本文继续。

---

## 1. 改动速览

| 分类 | 旧 | 新 |
|---|---|---|
| 项目目录 / CMake 包名 | `httpserver` | `fox-http` |
| 头文件目录 | `include/httpserver/` | `include/fox-http/` |
| Include 路径 | `#include "httpserver/http_handler.h"` | `#include "fox-http/http_handler.h"` |
| C++ 命名空间 | `namespace httpserver { ... }` | `namespace fox::http { ... }` |
| 命名空间使用 | `httpserver::HttpHandler` | `fox::http::HttpHandler` |
| 静态库名 | `libhttpserver.a` | `libfox-http.a` |
| CMake target | `httpserver` | `fox-http` |
| CMake alias | `httpserver::httpserver` | `fox-http::fox-http` |
| CMake 选项 | `HTTPSERVER_BUILD_TESTS` | `FOX_HTTP_BUILD_TESTS` |
| CMake 选项 | `HTTPSERVER_DEBUG_LOG` | `FOX_HTTP_DEBUG_LOG` |
| 内部日志宏 | `HTTPSERVER_LOG(...)` | `FOX_HTTP_LOG(...)` |
| 内部编译定义 | `HTTPSERVER_DEBUG_LOG` | `FOX_HTTP_DEBUG_LOG` |

公开类名（`HttpServer` / `HttpHandler` / `HttpRequest` / `HttpResponse` /
`HttpRouter` / `Headers` / `Method` / ...）**完全不变**，方法签名也不变。
只是命名空间外壳替换。

---

## 2. 为什么改名

- **`fox-http`** 是更具辨识度的品牌名，准备跟 `fox-route`（CRDL 路由代码生成器，
  原 `route_hs`）、`fox-page`（HTML 模板转 C++，原 `weave++_hs`）构成一套生态。
- 命名空间选 `fox::http`（C++17 嵌套），风格对齐 `boost::asio` / `absl`；
  留下 `fox::` 根给未来可能的兄弟库（比如 `fox::route`、`fox::page` 如果加运行时代码）。
- 宏前缀从 `HTTPSERVER_` 改为 `FOX_HTTP_`，避免和通用词撞名。

---

## 3. 代码层迁移

### 3.1 机械替换（推荐用 sed / IDE 全局替换）

在你项目根目录下跑：

```bash
# C++ 源/头
find . -type f \( -name "*.cpp" -o -name "*.cc" -o -name "*.h" -o -name "*.hpp" \) \
  ! -path "*/build/*" ! -path "*/.git/*" -exec sed -i \
    -e 's|#include "httpserver/|#include "fox-http/|g' \
    -e 's|#include <httpserver/|#include <fox-http/|g' \
    -e 's|namespace httpserver {|namespace fox::http {|g' \
    -e 's|namespace httpserver::util {|namespace fox::http::util {|g' \
    -e 's|}  // namespace httpserver|}  // namespace fox::http|g' \
    -e 's|}  // namespace httpserver::util|}  // namespace fox::http::util|g' \
    -e 's|using namespace httpserver;|using namespace fox::http;|g' \
    -e 's|httpserver::|fox::http::|g' \
    -e 's|HTTPSERVER_LOG|FOX_HTTP_LOG|g' \
    -e 's|HTTPSERVER_DEBUG_LOG|FOX_HTTP_DEBUG_LOG|g' \
    {} +

# CMakeLists.txt / cmake 文件
find . -type f \( -name "CMakeLists.txt" -o -name "*.cmake" \) \
  ! -path "*/build/*" ! -path "*/.git/*" -exec sed -i \
    -e 's|httpserver::httpserver|fox-http::fox-http|g' \
    -e 's|HTTPSERVER_BUILD_TESTS|FOX_HTTP_BUILD_TESTS|g' \
    -e 's|HTTPSERVER_DEBUG_LOG|FOX_HTTP_DEBUG_LOG|g' \
    {} +
```

跑完后再全局 grep 一次 `httpserver` / `HTTPSERVER`，人工检查剩余匹配（通常是
注释、字符串字面量、历史文档等，按情况酌情处理）。

### 3.2 Before / After 示例

#### 最小 handler

```cpp
// 旧
#include "httpserver/http_handler.h"
#include "httpserver/http_request.h"
#include "httpserver/http_response.h"

class Hello : public httpserver::HttpHandler {
public:
    void handle(httpserver::HttpRequest& req,
                httpserver::HttpResponse& resp) override {
        resp.set_status(200);
        resp.set_body("hello\n");
    }
};
```

```cpp
// 新
#include "fox-http/http_handler.h"
#include "fox-http/http_request.h"
#include "fox-http/http_response.h"

class Hello : public fox::http::HttpHandler {
public:
    void handle(fox::http::HttpRequest& req,
                fox::http::HttpResponse& resp) override {
        resp.set_status(200);
        resp.set_body("hello\n");
    }
};
```

#### 主程序 + Router

```cpp
// 旧
#include "httpserver/http_server.h"
#include "httpserver/http_router.h"
using namespace httpserver;

int main() {
    HttpRouter router;
    router.get("/hello", [](HttpRequest&, HttpResponse& r){ r.set_body("hi"); });
    HttpServer server(8080);
    server.set_handler(&router);
    return server.run();
}
```

```cpp
// 新
#include "fox-http/http_server.h"
#include "fox-http/http_router.h"
using namespace fox::http;

int main() {
    HttpRouter router;
    router.get("/hello", [](HttpRequest&, HttpResponse& r){ r.set_body("hi"); });
    HttpServer server(8080);
    server.set_handler(&router);
    return server.run();
}
```

#### 工具函数

```cpp
// 旧
#include "httpserver/http_util.h"
auto form = httpserver::util::parse_form_urlencoded(body);
httpserver::util::bad_request(resp, "missing username");
```

```cpp
// 新
#include "fox-http/http_util.h"
auto form = fox::http::util::parse_form_urlencoded(body);
fox::http::util::bad_request(resp, "missing username");
```

---

## 4. CMake 集成

### 4.1 `add_subdirectory`（本地构建，无需安装）

```cmake
# 旧
if(NOT TARGET httpserver::httpserver)
    set(HTTPSERVER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../httpserver
                     ${CMAKE_BINARY_DIR}/httpserver_build)
endif()
target_link_libraries(my_app PRIVATE httpserver::httpserver)
```

```cmake
# 新
if(NOT TARGET fox-http::fox-http)
    set(FOX_HTTP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../fox-http
                     ${CMAKE_BINARY_DIR}/fox-http_build)
endif()
target_link_libraries(my_app PRIVATE fox-http::fox-http)
```

### 4.2 `find_package`（安装后使用）

```cmake
# 旧
find_package(httpserver REQUIRED)
target_link_libraries(my_app PRIVATE httpserver::httpserver)
```

```cmake
# 新
find_package(fox-http REQUIRED)
target_link_libraries(my_app PRIVATE fox-http::fox-http)
```

### 4.3 调试日志

```cmake
# 打开 FOX_HTTP_LOG（等价于旧的 HTTPSERVER_LOG）
set(FOX_HTTP_DEBUG_LOG ON CACHE BOOL "" FORCE)
```

---

## 5. 周边项目同步重命名

如果你的构建还用到了 `route_hs` / `weave++_hs`，它们也改名了：

| 旧项目 | 新项目 | 旧二进制 | 新二进制 |
|---|---|---|---|
| `route_hs` | `fox-route` | `crdl_compiler`, `crdl_func` | `fox-route`, `fox-route-func` |
| `weave++_hs` | `fox-page` | `weave++` | `fox-page` |

CMake 里的 `find_program` 对应改：

```cmake
# 旧
find_program(CRDL_COMPILER NAMES crdl_compiler
    HINTS ${CMAKE_CURRENT_SOURCE_DIR}/../route_hs/build)
find_program(CRDL_FUNC NAMES crdl_func
    HINTS ${CMAKE_CURRENT_SOURCE_DIR}/../route_hs/build)
find_program(WEAVEPP_EXECUTABLE NAMES weave++
    HINTS ${CMAKE_CURRENT_SOURCE_DIR}/../weave++_hs/build)
```

```cmake
# 新
find_program(CRDL_COMPILER NAMES fox-route
    HINTS ${CMAKE_CURRENT_SOURCE_DIR}/../fox-route/build)
find_program(CRDL_FUNC NAMES fox-route-func
    HINTS ${CMAKE_CURRENT_SOURCE_DIR}/../fox-route/build)
find_program(FOX_PAGE_EXECUTABLE NAMES fox-page
    HINTS ${CMAKE_CURRENT_SOURCE_DIR}/../fox-page/build)
```

### 生成器产物的变化

`fox-route` 和 `fox-page` 生成的 C++ 代码已内置新头路径 + 新命名空间，**无须
手工改生成产物**。重新生成一次即可：

```bash
cd my_project
rm -rf build
cmake -S . -B build  # 触发 fox-route / fox-page 重生 router.generated.*, pages/*.cpp
cmake --build build
```

手写的 `handlers.cpp`（填 CRDL 里声明的 handler 实现）需要按 §3 的方式改
include 路径和命名空间。

---

## 6. 验证清单

做完替换后，依次跑：

1. **构建通过**：`cmake --build build -j` 无新错。
2. **老测试无回归**：若原项目有 gtest，`ctest --output-on-failure` 全绿。
3. **运行冒烟**：起服务 + `curl` 一条 handler 路径，确认响应正确。
4. **残留检查**：
   ```bash
   grep -rn "httpserver\|HTTPSERVER" --include="*.cpp" --include="*.h" --include="CMakeLists.txt" .
   ```
   应当为空，或仅剩 CHANGELOG / 历史文档中的合理保留。

---

## 7. 常见问题

**Q: 我能不能 `using namespace fox::http::` 然后只写 `HttpHandler`？**
可以。`fox::http` 是普通嵌套命名空间，`using namespace fox::http;` 和旧的
`using namespace httpserver;` 行为一致。

**Q: 编译报 `fox-http/http_xxx.h: No such file`，include 路径写对了吗？**
确认 `target_link_libraries(my_app PRIVATE fox-http::fox-http)` 已写对。
`fox-http::fox-http` 会自动传递 `include/fox-http` 给下游。

**Q: 静态库找不到（`cannot find -lhttpserver`）？**
你的 linker 命令还在找旧库名。检查 CMakeLists.txt 里有没有硬编码的
`target_link_libraries(... httpserver)` 或 `-lhttpserver`，改成
`fox-http::fox-http`（用 target alias，CMake 自动处理库文件名）。

**Q: `namespace httpserver { namespace util { ... }}` 嵌套声明要怎么改？**
要么改成嵌套 `namespace fox::http { namespace util { ... }}`，要么 C++17
风格 `namespace fox::http::util { ... }`。两种都行，随便选。

**Q: 我是第三方用户，能不能临时 `namespace httpserver = fox::http;` 过渡？**
可以：在你项目某个总包含头里加一行命名空间别名，代码不用立刻改：
```cpp
namespace httpserver = fox::http;
```
include 路径仍需要改到 `fox-http/`，因为头文件物理位置变了。

**Q: 三个项目（fox-http / fox-route / fox-page）的 git remote 还指向旧地址？**
`fox-http` 目前仍推到 `gitrepo/httpserver.git`；服务端裸仓重命名后，在本地
`git remote set-url origin git@server:/gitrepo/fox-http.git` 即可。
`fox-route` / `fox-page` 当前是全新本地仓库，没有 remote，自行加即可。

---

## 8. 变更历史位置

- 每阶段的详细改动记录：`CHANGELOG.md`
- 架构决策和理由：`DESIGN.md`
- 本文只覆盖 `httpserver → fox-http` 的重命名迁移，不涉及 Phase 1–4 的架构演进
  （RoutingModule 删除、三模式响应、Router 下放、Photon 解耦），那些发生在
  还叫 `httpserver` 的时期，对使用者已经透明。
