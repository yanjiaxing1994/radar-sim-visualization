# 雷达对抗仿真可视化系统

基于 C++/Qt 后端 + HTML/Canvas 前端的雷达探测-干扰-压制-目标丢失全过程仿真演示。

## 功能特性

- 雷达探测：基于雷达方程计算基准探测距离 `R0`
- 侦察截获：ESM 设备截获雷达信号判定
- 干扰压制：自卫干扰 J/S 计算，压制后有效探测距离 `R_eff` 收缩
- 状态机：`NORMAL → ESM → JAMMED → LOST` 四态切换
- 实时可视化：探测圈收缩动画、干扰波纹、PPI 雷达屏、目标丢失告警
- 双通道通信：HTTP REST（调试用）+ WebSocket（实时推送到前端）
- 国产化预留：物理层零 Qt 依赖，可直接移植到飞腾 FT-2000 + 银河麒麟 V10

## 技术栈

- 后端：C++17 + Qt 5.12.12（Core / Network / WebSockets）
- 前端：原生 HTML + Canvas + JavaScript（无框架依赖）
- 通信：HTTP（`QTcpServer` 手写）+ WebSocket（`QWebSocketServer`）
- 构建：CMake 3.15+
- 开发机：Apple Silicon Mac（Qt 5.12.12 x86_64 + Rosetta 2）
- 目标平台：飞腾 FT-2000/4 + 银河麒麟 V10（ARMv8）

## 项目结构

```text
radar_sim/
├── src/                          # C++/Qt 后端
│   ├── radar_model.*             # 雷达方程与 R0、R_eff
│   ├── jammer_model.*            # 干扰策略、J/S、压制模型
│   ├── jammer_strategy.h         # 干扰策略接口
│   ├── esm_model.*               # ESM 侦察截获
│   ├── detection.*               # 场景评估与状态机
│   ├── types.h                   # 纯 C++ 输入/输出结构
│   ├── http_server.*             # HTTP、WebSocket、JSON 映射
│   ├── main.cpp                  # 命令行入口
│   └── sim_test.cpp              # 物理层冒烟测试
├── frontend/                     # 前端静态资源
│   └── index.html                # HTML + Canvas + Leaflet 单页应用
├── docs/                         # 文档
│   ├── dev-macos.md              # macOS 开发与运行
│   ├── porting-ft2000.md         # FT-2000/麒麟移植
│   ├── extension-guide.md        # 扩展与维护指南
│   └── deploy/nginx/             # nginx 站点配置与部署说明
├── CMakeLists.txt                # CMake 构建定义
├── config.json                   # 本地运行时配置（已被 .gitignore 忽略）
├── README.md
├── .gitignore
└── LICENSE
```

`build/` 为本地构建产物，不会进入版本库。仓库没有 `third_party/`，也不
vendoring `httplib.h`、`json.hpp` 等第三方源码。

## 快速开始

### 环境要求

- CMake ≥ 3.15
- Qt 5.12.12，安装时必须包含 **Core、Network、WebSockets** 模块
- 现代浏览器（Chrome / Edge / Firefox）

macOS 使用 Qt 官方安装器时，在 Qt 5.12.12 组件中勾选 `Qt WebSockets`。
麒麟/Ubuntu 对应开发包通常是 `qt5-websockets-dev`（或发行版中的等价包）。

### macOS 开发机

```bash
# 1. 安装 Rosetta 2（M 芯片必需，Qt 5.12 工具是 x86 二进制）
softwareupdate --install-rosetta

# 2. 确认 Qt 安装路径，比如 ~/Qt/5.12.12/clang_64
export QT_PREFIX="$HOME/Qt5.12.12/5.12.12/clang_64"

# 3. 编译后端（Apple Silicon 使用 x86_64 Qt 时必须指定架构）
cmake -B build \
  -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
  -DCMAKE_OSX_ARCHITECTURES=x86_64
cmake --build build -j4

# 4. 启动后端
./build/radar_sim
# 输出：
# HTTP listening on http://localhost:8080
# WebSocket listening on ws://localhost:8081/ws

# 5. 另开终端启动前端
cd frontend
python3 -m http.server 3000

# 6. 浏览器打开 http://localhost:3000
```

使用静态服务器时，前端会自动连接同一主机的后端 WebSocket 端口 `8081`。
通过 nginx 访问时则使用同源 `/ws` 反向代理，两种方式都不需要改代码。

前端地图依赖 `unpkg.com` 的 Leaflet 和 OpenStreetMap 瓦片，首次打开需要联网。

## FT-2000 / 银河麒麟 V10

目标平台使用 **ARM64 Qt 5.12.12**，不能复制 macOS 的 x86_64 Qt。物理层
`radar_core` 零 Qt 依赖，后端仅通过 Core、Network、WebSockets 模块通信。
目标机上的典型构建命令：

```bash
cmake -B build -DCMAKE_PREFIX_PATH=/opt/qt5.12.12
cmake --build build -j4
./build/sim_test
```

完整依赖检查、交叉部署、nginx 配置和验收步骤见
[docs/porting-ft2000.md](docs/porting-ft2000.md)。

## 使用说明

- 页面初始状态：雷达正常探测，探测圈显示基准距离 `R0`。当前默认参数约为
  **27.4 km**；若验收要求约 150 km，需要按文档调整默认参数（例如把 `Pmin`
  调到约 `1.12e-16`，或把 `Gt` 调到约 `5.47e3`）。
- 勾选“ESM 开机”（侦察）：ESM 开始扫描，目标进入截获距离后状态切换为
  `ESM`（仅 ESM 截获）。
- 勾选“干扰机开机”：探测圈出现琥珀色受扰区域，`R_eff` 收缩，J/S 数值上升，
  PPI 显示干扰机标记。
- 目标飞出收缩后的探测圈：目标状态变为 `LOST`（目标丢失），顶部状态徽标同步
  变为红色。
- 调整“干扰功率 Pj”输入框：实时观察 J/S 和 `R_eff` 的变化。
- 关闭干扰：`R_eff` 恢复到 `R0`，目标重新进入可探测状态。
- 目标距离、方位、RCS 可用左侧滑块调整；雷达/目标/干扰机坐标和雷达参数可在
  “平台参数”卡片中直接输入。

## API 接口

### HTTP

- `GET /health` — 健康检查
- `POST /api/simulate` — 单次仿真请求，请求体为 `SimInput` JSON，返回 `SimOutput`

```bash
curl -X POST localhost:8080/api/simulate \
  -H "Content-Type: application/json" \
  -d '{"radar":{"Pt":100000,"Gt":30,"lambda":0.03,"Pmin":1e-13,"loss":4},"jammer":{"Pj":1000,"Gj":20,"js_threshold_db":10},"target":{"x":80000,"y":60000,"rcs":5},"esm_on":true,"jammer_on":false}'
```

### WebSocket

- `ws://localhost:8081/ws` — 连接后每 50 ms 收到
  `{"type":"state","scene":{...},"result":{...}}`
- 客户端发送 `{"type":"update","scene":{...}}` 更新场景

`SimInput` 同时接受 `radar_position`、`jammer_position` 独立坐标；旧客户端
只发送 `target` 时，后端按自卫干扰处理，即 `R_jammer = R_target`。

## 物理模型简述

基准探测距离由雷达方程给出：

```text
R0 = [ Pt * Gt^2 * lambda^2 * sigma / ((4*pi)^3 * Pmin * L) ]^(1/4)
```

其中 `Pt` 为发射功率、`Gt` 为天线增益、`lambda` 为波长、`sigma` 为目标 RCS、
`Pmin` 为最小可检测功率、`L` 为系统损耗。开启干扰后，支援干扰几何的
干信比为：

```text
J/S = Pj * Gj * 4*pi * Rt^4 / (Pt * Gt * sigma * Rj^2)
```

`Rt` 是雷达到目标的距离，`Rj` 是雷达到干扰机的距离；自卫干扰时
`Rj = Rt`，公式退化为常见的 `Pj * Gj * 4*pi * Rt^2 / (Pt * Gt * sigma)`。
当 `J/S` 超过门限时，演示模型按

```text
R_eff = R0 / (1 + (J/S_dB - threshold_dB) / 10)^(1/4)
```

收缩有效探测距离。直观解释是：干扰噪声抬高了接收机端的目标检测门限，
目标回波必须更近、更强才能被检测到，因此 PPI 上的有效探测圈变小。

状态判定优先级为 `LOST` > `JAMMED` > `ESM` > `NORMAL`：干扰开启且目标已在
`R_eff` 内时仍可探测为 `JAMMED`，飞出 `R_eff` 后为 `LOST`；无干扰但被 ESM
截获时为 `ESM`。

## 配置

`config.json` 保存默认雷达和干扰机参数，不存在时后端使用内置默认值：

```json
{
  "radar": {
    "Pt": 100000,
    "Gt": 1000,
    "lambda": 0.03,
    "Pmin": 1e-13,
    "loss": 4
  },
  "jammer": {
    "Pj": 1000,
    "Gj": 100,
    "js_threshold_db": 10,
    "strategy": "noise"
  }
}
```

`config.json` 是本地运行配置，已被 `.gitignore` 忽略。

## 扩展与维护

详见 [docs/extension-guide.md](docs/extension-guide.md)，包含：

- 三种架构红线
- 6 种常见扩展场景的加代码指南
- 待优化项清单（P0/P1/P2）
- 关键决策记录（ADR）

生产部署由 nginx 托管 `frontend/`，并将 `/api/`、`/health`、`/ws` 反向代理
到后端，配置见 [docs/deploy/nginx/README.md](docs/deploy/nginx/README.md)。

## 移植说明

- macOS 开发环境：[docs/dev-macos.md](docs/dev-macos.md)
- FT-2000 移植指南：[docs/porting-ft2000.md](docs/porting-ft2000.md)

## 许可证

MIT License，详见 [LICENSE](LICENSE)。
