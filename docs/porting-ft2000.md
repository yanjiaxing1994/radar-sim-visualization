# FT-2000/4 + 银河麒麟 V10 移植指南

本文档说明如何把 `radar_sim` 从 Apple Silicon Mac 开发机迁移到
**飞腾 FT-2000/4（ARMv8，aarch64）+ 银河麒麟 V10** 目标平台。

> 适用代码版本：当前 `radar_sim` 源码（Qt 5.12.12 / C++17 / CMake 3.15+）。
> 目标平台必须使用 **ARM64 版 Qt 5.12.12**，不能使用 Mac 上的 Intel Qt。

---

## 0. 先确认当前工程的实际结构

移植前先了解本项目与通用 Qt 桌面项目不同的地方：

| 项目 | 当前实际情况 |
| --- | --- |
| 前端源码 | `frontend/index.html` |
| 前端部署方式 | nginx 直接托管 `frontend/` 静态文件；后端不再提供 `GET /` |
| 前端目录 | `frontend/`（早期文档中的 `frontend/` 已重命名） |
| nginx 端口 | 配置默认 `8088`，可按需改为 `80` |
| HTTP 端口 | 默认 `8080`，可用 `--http-port` 修改；由 nginx 反向代理 |
| WebSocket 端口 | 默认 `8081`，可用 `--ws-port` 修改；路径为 `/ws`，由 nginx 反向代理 |
| Qt 模块 | Core、Network、WebSockets；**当前不依赖 Qt GUI / WebEngine** |
| 物理层 | `radar_model` / `jammer_model` / `esm_model` / `detection` 为纯 C++17，不依赖 Qt |
| 地图 | Leaflet + OpenStreetMap，默认从 CDN 加载；离线环境需要额外处理 |

因此：

- 部署时需要把 `frontend/` 复制到 nginx 的站点根目录，并安装
  `docs/deploy/nginx/radar_sim.conf`。
- 修改 `frontend/index.html` 后**不需要重新编译后端**，同步文件并刷新浏览器即可。
- 后端只提供 `/health`、`/api/*` 和 WebSocket；直接访问后端的 `/` 返回 404
  是预期行为。
- 如果希望使用 Qt WebEngine 嵌入页面，属于可选的后续改造，不属于当前默认部署方式。

---

## 一、目标平台确认

### 1.1 CPU 与操作系统

飞腾 FT-2000/4 是 4 核 ARMv8 架构 CPU；银河麒麟 V10 对应 **aarch64** 操作系统。

登录目标机后先确认：

```bash
uname -m
# 期望：aarch64

getconf LONG_BIT
# 期望：64

nproc
# 期望：4（FT-2000/4）
```

### 1.2 Qt 必须是 ARM64 版本

Qt 版本保持 **5.12.12**，但必须使用麒麟 ARM64 版本。Mac 上使用的 Intel Qt
（`clang_64`）不能复制到 FT-2000 上使用。

确认 Qt 架构时不要只看 `qmake -v` 的版本号，至少检查可执行文件和核心库的
ELF 架构：

```bash
/opt/qt5.12.12/bin/qmake -v
# 期望包含：Using Qt version 5.12.12

file /opt/qt5.12.12/bin/qmake
# 期望包含：ELF 64-bit LSB ... ARM aarch64

file /opt/qt5.12.12/lib/libQt5Core.so.5.12.12
# 期望包含：ELF 64-bit LSB ... ARM aarch64
```

如果 `file` 显示 `x86-64`，说明拿到的是 Intel 版 Qt，不能用于本目标机。

---

## 二、获取 Qt 5.12.12 ARM64

### 路径 A（推荐）：社区/厂商已验证的预编译离线包

适合目标机不能长时间编译、或需要快速验证环境的场景。

可以搜索：

```text
qt5.12.12 飞腾2000 银河麒麟
```

优先使用单位内部或社区已经验证过的离线包，并注意：

1. 确认包是 **aarch64/ARM64**，不是 x86_64。
2. 确认 Qt 版本是 **5.12.12**。
3. 尽量使用与麒麟系统 GCC ABI 匹配的构建。
4. 校验包的校验和，不要直接运行来源不明的安装脚本。
5. 安装后再次执行本文 1.2 节的 `file` 检查。

典型安装位置可以统一为 `/opt/qt5.12.12`：

```bash
sudo mkdir -p /opt/qt5.12.12
# 按离线包说明解压或安装到 /opt/qt5.12.12
```

### 路径 B（保底）：在 FT-2000 上从源码原生编译

Qt 官方源码包：

```bash
cd "$HOME"
wget https://download.qt.io/archive/qt/5.12/5.12.12/single/qt-everywhere-src-5.12.12.tar.xz
tar -xf qt-everywhere-src-5.12.12.tar.xz
cd qt-everywhere-src-5.12.12
```

配置并编译：

```bash
./configure \
    -prefix /opt/qt5.12.12 \
    -opensource -confirm-license \
    -qt-xcb \
    -nomake tests \
    -nomake examples

make -j4
sudo make install
```

说明：

- FT-2000/4 只有 4 个核心，`-j4` 足够，不要为了追求速度设置 `-j8` / `-j16`；
  内存较小（例如 4 GB）时建议降到 `-j2`，避免编译过程被 OOM 杀掉。
- 本项目只需要 Qt Core / Network / WebSockets。若不需要 Qt WebEngine，可在
  `configure` 时额外加 `-skip qtwebengine`，显著缩短编译时间：
  ```bash
  ./configure -prefix /opt/qt5.12.12 -opensource -confirm-license \
      -qt-xcb -nomake tests -nomake examples -skip qtwebengine
  ```
- `-qt-xcb` 使用 Qt 自带的 xcb 源码，但仍需要系统 libxcb。安装依赖见第三节。
- 全量 Qt 编译时间可能较长，建议在业务低峰期执行，并保证磁盘空间充足。
- 如果使用厂商提供的定制 Qt 源码包，配置参数以其说明为准。

编译完成后再执行 1.2 节的 `file` 检查。

---

## 三、依赖安装

在银河麒麟上编译前安装开发库。以下命令适用于基于 apt 的麒麟 V10 环境：

```bash
sudo apt update
sudo apt install -y build-essential cmake
sudo apt install -y libgl1-mesa-dev libglu1-mesa-dev
sudo apt install -y libxcb-xinerama0-dev libxkbcommon-x11-dev
sudo apt install -y nginx
```

如果编译时提示 OpenGL 缺失，先确认 `libgl1-mesa-dev` 已安装；如果使用
`-qt-xcb` 构建 Qt，确认 libxcb 相关开发包也完整：

```bash
dpkg -l | grep -E 'mesa|xcb|xkbcommon'
```

建议补充（Qt 源码编译和运行时常用）：

```bash
sudo apt install -y libssl-dev zlib1g-dev libicu-dev
```

如果需要中文字体渲染，尤其是前端使用等宽字体时：

```bash
sudo apt install -y fonts-noto-cjk
```

检查编译工具版本：

```bash
cmake --version
# 需要 >= 3.15

g++ --version
# 需要支持 C++17
```

如果系统 `cmake` 低于 3.15，需要安装新版 CMake，或使用单位提供的工具链。
不要为了适配旧 CMake 而随意降低项目要求。

---

## 四、项目源码迁移

不要把 Mac 上的 `build/`、`CMakeLists.txt.user`、`.DS_Store` 复制到目标机。
这些文件包含 macOS 路径或 x86 架构缓存。

在 Mac 上打包源码：

```bash
cd "$HOME/code"
tar \
  --exclude='build' \
  --exclude='.DS_Store' \
  --exclude='CMakeLists.txt.user' \
  -czf radar_sim-src.tgz radar_sim
scp radar_sim-src.tgz <user>@<ft2000-ip>:/opt/
```

在目标机解压：

```bash
cd /opt
tar -xzf radar_sim-src.tgz
cd radar_sim
```

如果目标机可以直接访问源码目录，也可以使用 `rsync`，但仍建议排除
`build/`：

```bash
rsync -av --exclude build --exclude .DS_Store \
    "$HOME/code/radar_sim/" <user>@<ft2000-ip>:/opt/radar_sim/
```

---

## 五、项目编译

### 5.1 标准编译命令

```bash
cd /opt/radar_sim

cmake -S . -B build \
    -DCMAKE_PREFIX_PATH=/opt/qt5.12.12

cmake --build build -j4
```

验收产物：

```bash
./build/sim_test
./build/radar_sim --help
```

`sim_test` 应输出 7 组物理模型结果；`radar_sim --help` 应显示：

```text
--http-port <port>  HTTP listen port (default: 8080).
--ws-port <port>    WebSocket listen port (default: 8081).
```

确认二进制架构：

```bash
file build/radar_sim
# 期望包含：ELF 64-bit LSB ... ARM aarch64
```

### 5.2 CMake 找不到 Qt5

如果出现：

```text
Could not find a package configuration file provided by "Qt5"
```

先清理 CMake 缓存，再使用 `Qt5_DIR` 显式指定：

```bash
rm -rf build

cmake -S . -B build \
    -DQt5_DIR=/opt/qt5.12.12/lib/cmake/Qt5
```

或者同时指定前缀和 Qt5_DIR（通常只选一种即可）：

```bash
cmake -S . -B build \
    -DCMAKE_PREFIX_PATH=/opt/qt5.12.12 \
    -DQt5_DIR=/opt/qt5.12.12/lib/cmake/Qt5
```

检查：

```bash
grep -E 'Qt5_DIR|CMAKE_PREFIX_PATH' build/CMakeCache.txt
```

如果曾经用错误 Qt 配置过，直接删除 `build/` 重新来，不要复用
`CMakeCache.txt`。

---

## 六、WebSocket 模块检查

麒麟系统上的 Qt 安装包可能没有带 WebSockets 开发组件。编译前先确认：

```bash
ls /opt/qt5.12.12/lib/cmake/ | grep -i websockets
# 期望看到：Qt5WebSockets
```

更精确地查找：

```bash
find /opt/qt5.12.12 -name 'Qt5WebSocketsConfig.cmake'
ls /opt/qt5.12.12/lib/libQt5WebSockets.so*
```

如果缺失：

1. **源码构建的 Qt 5.12.12**
   - Qt 5.12.12 的 `qt-everywhere-src` 中包含 `qtwebsockets` 独立模块，默认
     会参与构建。
   - 确认源码目录存在（按实际解压位置替换 `$HOME`）：
     ```bash
     ls "$HOME/qt-everywhere-src-5.12.12" | grep -i websocket
     ```
   - 重新配置时**不要**加 `-skip qtwebsockets`。
   - 单独补编译 `qtwebsockets` 模块：
     ```bash
     cd "$HOME/qt-everywhere-src-5.12.12/qtwebsockets"
     /opt/qt5.12.12/bin/qmake
     make -j4
     sudo make install
     ```
   - 注意：Qt 5.12.12 标准源码的 `configure` 参数中没有通用的
     `-qt-websockets` 开关；WebSockets 是模块而不是普通的 `-qt-*` 功能开关。
     某些厂商定制脚本可能提供该参数，若确有支持可按其文档使用，但不要假定
     所有 Qt 5.12 包都支持。

2. **使用系统自带的 Qt**
   - 安装匹配的 WebSockets 开发包，例如：
     ```bash
     sudo apt install -y libqt5websockets5-dev
     ```
   - 这种情况下应让 CMake 使用系统 Qt 的配置目录，而不是 `/opt/qt5.12.12`。
     不要把系统 Qt 的 WebSockets 头文件/库混到 `/opt` 的 Qt 前缀中。

确认后再重新配置和编译项目。

---

## 七、部署与打包

### 7.1 最简单的部署方式

> 以下路径以 `/opt` 为例。如果当前用户没有 `/opt` 写权限，可以改用
> `$HOME/radar_sim`，或先执行
> `sudo mkdir -p /opt/radar_sim && sudo chown "$USER":"$USER" /opt/radar_sim`。

前端由 nginx 托管，后端只运行 API/WebSocket 服务。最简单的部署内容是：

```text
/opt/radar_sim/
├── radar_sim          # 可执行文件
├── config.json        # 可选；不存在时使用程序内置默认值
├── frontend/
│   └── index.html     # nginx 静态站点根目录
└── run.sh             # 可选启动脚本
```

示例后端启动脚本：

```bash
#!/bin/sh
cd "$(dirname "$0")"
exec ./radar_sim \
    --config config.json \
    --http-port 8080 \
    --ws-port 8081
```

复制到目标机：

```bash
scp -r build/radar_sim config.json frontend <user>@<ft2000-ip>:/opt/radar_sim/
```

需要给启动脚本执行权限：

```bash
chmod +x /opt/radar_sim/radar_sim /opt/radar_sim/run.sh
```

然后安装 nginx 配置，详见 `docs/deploy/nginx/README.md`：

```bash
sudo cp docs/deploy/nginx/radar_sim.conf /etc/nginx/conf.d/radar_sim.conf
sudo nginx -t
sudo systemctl reload nginx
```

如果目标机没有安装 Qt，或者希望应用不依赖系统 Qt，需要继续打包 Qt 库。

### 7.2 使用 linuxdeployqt（飞腾/ARM64 版本）

`linuxdeployqt` 可以把 Qt 库和依赖打包到应用目录。注意：它主要面向
x86_64 AppImage 场景，ARM64 版本通常来自厂商或社区构建，使用前先确认工具
本身是 aarch64：

```bash
file linuxdeployqt
# 期望包含：ELF 64-bit LSB ... ARM aarch64
```

常见用法（不同版本的参数可能略有差异，以 `./linuxdeployqt --help` 为准）：

```bash
chmod +x linuxdeployqt

./linuxdeployqt build/radar_sim \
    -bundle-non-qt-libs \
    -qmake=/opt/qt5.12.12/bin/qmake
```

如需生成 AppImage，可再使用 `-appimage` 参数。但本项目是后台服务程序，
没有桌面入口和图标，通常不需要 AppImage；优先使用普通目录打包。

打包后检查：

```bash
ldd <部署目录>/radar_sim
# 不应出现 "not found"
```

注意：前端已经不在可执行文件中，`linuxdeployqt` 只负责 Qt 库和可执行文件。
打包后仍需要把 `frontend/` 目录放到 nginx 的 `root` 下，并安装 nginx 配置。

如果 `linuxdeployqt` 在 ARM64 上不可用，使用下面的手工方式。

### 7.3 手工打包 Qt 依赖（保底）

准备目录（若 `/opt` 无写权限，先 `sudo mkdir -p /opt/radar_sim-dist` 并
`sudo chown -R "$USER":"$USER" /opt/radar_sim-dist`）：

```bash
mkdir -p /opt/radar_sim-dist/bin
mkdir -p /opt/radar_sim-dist/lib
```

复制程序和配置，同时复制 nginx 要托管的前端目录：

```bash
cp build/radar_sim /opt/radar_sim-dist/bin/
cp config.json /opt/radar_sim-dist/bin/
cp -a frontend /opt/radar_sim-dist/
```

此时 nginx 配置中的 `root` 应指向 `/opt/radar_sim-dist/frontend`。

复制项目直接依赖的 Qt 库（保留 `.so` 版本符号链接）：

```bash
cp -a /opt/qt5.12.12/lib/libQt5Core.so.5* \
      /opt/qt5.12.12/lib/libQt5Network.so.5* \
      /opt/qt5.12.12/lib/libQt5WebSockets.so.5* \
      /opt/radar_sim-dist/lib/
```

设置运行时库搜索路径。推荐使用 `patchelf` 写入 `$ORIGIN` 相对路径：

```bash
sudo apt install -y patchelf

patchelf --set-rpath '$ORIGIN/../lib' \
    /opt/radar_sim-dist/bin/radar_sim
```

查看还缺哪些库，并按需复制非系统库：

```bash
ldd /opt/radar_sim-dist/bin/radar_sim
```

Qt 构建如果依赖 ICU、OpenSSL、pcre2 等库，也需要从 Qt 前缀或系统中补齐。
根据 `ldd` 结果处理，不要盲目复制整个 `/usr/lib`。

如果 Qt 库本身带有指向 `/opt/qt5.12.12` 的 RUNPATH，而目标机不保留该前缀，
还需要给复制后的 Qt 库设置相对 rpath：

```bash
find /opt/radar_sim-dist/lib -maxdepth 1 -name '*.so.*' -type f \
    -exec patchelf --set-rpath '$ORIGIN' {} \;
```

之后启动时不需要设置 `LD_LIBRARY_PATH`：

```bash
/opt/radar_sim-dist/bin/radar_sim --config /opt/radar_sim-dist/bin/config.json
```

如果暂时不想改 rpath，也可以在启动脚本中设置：

```bash
export LD_LIBRARY_PATH=/opt/radar_sim-dist/lib:$LD_LIBRARY_PATH
```

### 7.4 前端与 Qt WebEngine 说明

当前版本：

- 前端是 `frontend/index.html`，由 nginx 直接提供静态文件。
- nginx 将 `/api/`、`/health` 代理到后端 8080，将 `/ws` 代理到后端 8081。
- 浏览器访问 nginx 的端口，默认是 `http://<目标机IP>:8088/`。
- 后端不再提供 `GET /`；直接访问 `http://<目标机IP>:8080/` 返回 404 是预期行为。
- 修改前端不需要重新编译 `radar_sim`，同步 `frontend/` 后刷新页面即可。
- 当前版本没有链接 Qt WebEngine。使用系统浏览器（麒麟自带浏览器、
  Firefox、Chromium）即可，不需要为了本应用编译 WebEngine。

只有在确实需要把页面嵌入 Qt 桌面窗口时，才考虑 Qt WebEngine。此时需要：

1. 使用 ARM64 Qt 并编译 WebEngine 模块（体积大、编译时间长）。
2. 新增一个 Qt GUI/WebEngine 宿主程序。
3. 处理 OpenGL/软件渲染和显卡驱动兼容性。

建议第一轮移植先使用系统浏览器 + nginx，确认后端和前端功能正常后，再评估
WebEngine。

### 7.5 离线地图注意事项

当前 `frontend/index.html` 引用：

- Leaflet 1.9.4：`https://unpkg.com/leaflet@1.9.4/...`
- OpenStreetMap 瓦片：`https://{s}.tile.openstreetmap.org/...`

如果目标机处于离线或受限网络：

- Leaflet 加载失败时，页面中的雷达 PPI 仍可用，地图区域会显示不可用提示。
- Leaflet 能加载但 OSM 瓦片不可达时，地图底图可能为空，PPI 不受影响。
- 要完整离线部署，需要：
  1. 把 Leaflet 的 JS/CSS/图标资源本地化；
  2. 修改 `index.html` 的资源引用；
  3. 增加本地瓦片服务，或把地图区域改为可选的离线底图；
  4. 前端已经由 nginx 托管，把 Leaflet 资源放到 `frontend/` 下并修改
     `index.html` 的引用即可，不需要改后端路由；如果地图瓦片也要离线，
     还需要准备本地瓦片服务或静态瓦片目录。

地图离线化不属于本次 FT2000 移植的最小闭环；如果现场网络受限，建议先完成
后端、PPI 和接口验证，再单独处理地图资源。

---

## 八、启动与验证清单

### 8.1 启动

先启动后端（终端 A）：

```bash
cd /opt/radar_sim

./build/radar_sim \
    --config config.json \
    --http-port 8080 \
    --ws-port 8081
```

正常启动日志应包含：

```text
Qt version: 5.12.12 (runtime 5.12.12)
HTTP listening on http://localhost:8080
WebSocket listening on ws://localhost:8081/ws
```

再安装并启动 nginx（终端 B）：

```bash
sudo cp docs/deploy/nginx/radar_sim.conf /etc/nginx/conf.d/radar_sim.conf
sudo nginx -t
sudo systemctl enable --now nginx

# 修改 nginx 配置后重载：
sudo systemctl reload nginx
```

nginx 默认监听 `8088`，前端目录为 `/opt/radar_sim/frontend`。如果使用其他部署
目录，修改配置文件中的 `root`；如果修改了后端端口，同时修改两个
`upstream` 地址。

如果端口被占用，可以先用其他端口验证后端：

```bash
./build/radar_sim --http-port 18080 --ws-port 18081
```

此时需要同步修改 nginx 配置中的 `upstream`，否则 nginx 仍然代理到
8080/8081。

### 8.2 健康检查

通过 nginx 检查：

```bash
curl -sS http://127.0.0.1:8088/health
# 期望：{"ok":true}
```

也可以直接检查后端：

```bash
curl -sS http://127.0.0.1:8080/health
```

### 8.3 仿真接口检查

通过 nginx 调用 API：

```bash
curl -sS -X POST http://127.0.0.1:8088/api/simulate \
    -H 'Content-Type: application/json' \
    -d '{"target":{"x":25000,"y":0},"jammer_on":true}' \
    | python3 -m json.tool
```

检查返回中至少包含：

- `R0`
- `R_eff`
- `R_target`
- `js_ratio_db`
- `target_detected`
- `status`

### 8.4 WebSocket 检查

如果目标机安装了 `websocat`：

```bash
websocat ws://127.0.0.1:8088/ws
```

应持续收到约每 50 ms 一次的 JSON 状态消息。没有 `websocat` 时，可以跳过
命令行检查，改用浏览器打开页面，观察右上角 WebSocket 连接状态。

### 8.5 浏览器功能检查

在目标机或同一网段的机器上访问 nginx：

```text
http://<FT2000-IP>:8088/
```

逐项确认：

- [ ] 页面正常加载，显示雷达 PPI。
- [ ] WebSocket 显示已连接。
- [ ] 拖动目标点，距离/方位/RCS 和状态随之变化。
- [ ] 打开“自卫干扰”，J/S 数值出现，R_eff 探测圈收缩。
- [ ] 目标超出 R_eff 时，状态显示“目标丢失 / LOST”。
- [ ] 关闭干扰，状态恢复。
- [ ] ESM 开关能独立影响 ESM 截获状态。
- [ ] 地图可用（有网络时）；离线时 PPI 仍正常工作。

### 8.6 30 分钟稳定性检查

启动后另开终端观察进程：

```bash
watch -n 5 'ps -o pid,rss,etime,cmd -C radar_sim'
```

或者：

```bash
top -p "$(pgrep -x radar_sim)"
```

验收标准：

- 进程不退出、不崩溃。
- RSS 在前几分钟加载完成后基本稳定，不应持续单调增长。
- 反复刷新浏览器、连接/断开 WebSocket 后，RSS 不应无限增长。
- 日志中无持续重复的异常或内存分配失败。

建议至少执行一次：

1. 连接一个 WebSocket 客户端并保持 30 分钟；
2. 中途多次打开/关闭浏览器页面；
3. 反复切换干扰、ESM，拖动雷达/目标/干扰机位置并修改平台参数；
4. 观察 RSS 是否回落到稳定区间。

---

## 九、常见问题

### 9.1 CMake 找到了错误版本的 Qt

症状：配置成功，但链接到 x86 库，或 `file build/radar_sim` 显示 x86-64。

处理：

```bash
rm -rf build
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/qt5.12.12
grep -E 'Qt5Core_DIR|CMAKE_PREFIX_PATH' build/CMakeCache.txt
```

确认 `Qt5Core_DIR` 指向 `/opt/qt5.12.12`。

### 9.2 `Qt5WebSockets` 找不到

先按第六节检查 CMake 配置文件和库文件。源码构建时确认没有
`-skip qtwebsockets`；系统 Qt 场景安装匹配的 dev 包。

### 9.3 端口被占用

```bash
ss -ltnp | grep -E '8080|8081'
```

使用其他端口：

```bash
./build/radar_sim --http-port 18080 --ws-port 18081
```

### 9.4 运行时找不到 `libQt5Core.so.5`

```bash
ldd /path/to/radar_sim | grep 'not found'
```

使用 `linuxdeployqt` 打包，或按 7.3 节设置 rpath/`LD_LIBRARY_PATH`。

### 9.5 地图空白

先确认目标机能访问 `unpkg.com` 和 `tile.openstreetmap.org`。离线环境按
7.5 节处理；不要因为地图空白误判后端故障，PPI 和 API 可以独立工作。

### 9.6 字体显示异常

安装中文字体：

```bash
sudo apt install -y fonts-noto-cjk
```

前端已经在等宽字体栈中加入 `"Noto Sans Mono CJK SC"` 回退；如仍异常，
可在 `frontend/index.html` 的 `--mono` 中改为系统实际可用的等宽字体后重新编译。

### 9.7 编译时内存不足

FT-2000/4 核心数有限，Qt 全量编译对内存更敏感：

```bash
make -j2
```

或增加 swap 后再使用 `-j4`。不要盲目提高并行度。

---

## 十、最小移植闭环

如果只想尽快验证“能编译、能启动、能访问”，按以下顺序执行：

```bash
# 1. 目标机架构
uname -m                       # aarch64

# 2. Qt 架构
file /opt/qt5.12.12/bin/qmake  # ARM aarch64

# 3. 清理并编译
cd /opt/radar_sim
rm -rf build
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/qt5.12.12
cmake --build build -j4

# 4. 物理模型自检
./build/sim_test

# 5. 终端 A：启动后端
./build/radar_sim --http-port 8080 --ws-port 8081

# 6. 终端 B：安装并启动 nginx
sudo cp docs/deploy/nginx/radar_sim.conf /etc/nginx/conf.d/radar_sim.conf
sudo nginx -t
sudo systemctl enable --now nginx

# 7. 通过 nginx 健康检查
curl -sS http://127.0.0.1:8088/health
# {"ok":true}

# 8. 浏览器
# http://<FT2000-IP>:8088/
```

通过以上步骤后，再进入 30 分钟稳定性测试和离线地图/打包优化。
