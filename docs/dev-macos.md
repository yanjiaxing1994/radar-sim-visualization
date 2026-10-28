# macOS 开发与运行

本文档说明在 macOS 上构建、测试和运行 `radar_sim` 的推荐流程。目标平台移植
见 [porting-ft2000.md](porting-ft2000.md)，架构分层和扩展点见
[extension-guide.md](extension-guide.md)。

## 1. 环境要求

- Xcode Command Line Tools
- CMake 3.15+
- Qt 5.12.12，包含 Core、Network、WebSockets
- nginx（推荐用 Homebrew 安装）

检查工具链：

```bash
xcode-select -p
cmake --version
```

确认 Qt 目录中存在 `bin/qmake`、`lib/cmake/Qt5` 和 WebSockets 模块。Qt 5.12
没有 `QHttpServer`，本项目使用 Qt Network 和手写 HTTP 服务。

## 2. 配置与构建

### 2.1 ARM64 Qt

```bash
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/5.12.12/clang_64
cmake --build build -j4
```

### 2.2 Apple Silicon 上的 x86_64 Qt

Qt 5.12.12 的官方 macOS 包通常是 x86_64。如果本机是 Apple Silicon 且链接到
的是 x86_64 Qt，必须显式指定架构，否则会出现架构不匹配：

```bash
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH="$HOME/Qt5.12.12/5.12.12/clang_64" \
  -DCMAKE_OSX_ARCHITECTURES=x86_64
cmake --build build -j4
```

可以用以下命令确认架构：

```bash
file "$HOME/Qt5.12.12/5.12.12/clang_64/bin/qmake"
```

需要重新配置时，先删除 `build/`，避免沿用旧的 CMake 缓存。

## 3. 测试

```bash
./build/sim_test
```

该程序输出 10 个物理场景，包括默认探测、自卫干扰、ESM、雷达偏移和远距支援
干扰。接口改动后还应通过 nginx 做一次 HTTP/WebSocket 联调。

## 4. 启动后端

```bash
./build/radar_sim \
  --config config.json \
  --http-port 8080 \
  --ws-port 8081
```

`config.json` 可省略。文件不存在或格式错误时，后端会记录日志并使用内置默认
参数。`config.json` 属于本地运行配置，已被 `.gitignore` 忽略。

## 5. 启动前端

前端位于 `frontend/index.html`。通过 nginx 访问时使用同源 `/ws`；用 3000
端口静态服务器预览时会自动直连后端 `ws://<host>:8081/ws`。不要用 `file://`
直接打开页面。

### 5.1 快速预览（不安装 nginx）

在仓库根目录执行：

```bash
python3 -m http.server 3000 --directory frontend
```

然后访问 `http://localhost:3000/`。后端需已按上一节启动。

### 5.2 使用 nginx

安装 nginx：

```bash
brew install nginx
```

复制站点配置：

```bash
mkdir -p /opt/homebrew/etc/nginx/servers
cp docs/deploy/nginx/radar_sim.conf \
  /opt/homebrew/etc/nginx/servers/radar_sim.conf
```

把 `root` 改为本机项目路径：

```nginx
root /Users/<user>/code/radar_sim/frontend;
```

Homebrew nginx 默认站点可能占用 8080，而后端也需要 8080。启动前确认默认
`server` 已改为其他端口或注释掉。检查并启动：

```bash
nginx -t
brew services run nginx
```

访问：

```text
http://localhost:8088/
```

完整 nginx 配置、Docker 方式和排错步骤见
[deploy/nginx/README.md](deploy/nginx/README.md)。

## 6. 联调检查

```bash
curl -sS http://127.0.0.1:8088/health
curl -sS -o /dev/null -w '%{http_code}\n' http://127.0.0.1:8088/
```

也可以直接检查后端：

```bash
curl -sS http://127.0.0.1:8080/health
```

期望健康检查返回 `{"ok":true}`，首页返回 `200`。

## 7. 常见问题

### CMake 找到错误架构的 Qt

清除构建目录，重新指定 `CMAKE_PREFIX_PATH`；Apple Silicon + x86_64 Qt 还要
指定 `CMAKE_OSX_ARCHITECTURES=x86_64`。

### nginx 返回 404

确认站点配置的 `root` 指向仓库中的 `frontend/`，并确认 `index.html` 存在。
修改配置后执行：

```bash
nginx -t
brew services restart nginx
```

### 页面打开但没有数据

确认后端 8080/8081 已启动，且 nginx 的 `/api/`、`/health`、`/ws` 代理目标
与实际端口一致。浏览器开发者工具的 Network 面板可查看 `/ws` 是否返回 101。
