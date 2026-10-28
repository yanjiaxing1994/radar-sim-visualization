# nginx 前端部署

## 结构

前端改为由 nginx 直接提供静态文件，后端只提供 API 和 WebSocket：

```text
浏览器
  |
  v
nginx :8088
  |-- /            -> frontend/index.html
  |-- /api/        -> 127.0.0.1:8080
  |-- /health      -> 127.0.0.1:8080
  `-- /ws          -> 127.0.0.1:8081
```

相关文件：

- 前端静态目录：`frontend/`
- nginx 配置：`docs/deploy/nginx/radar_sim.conf`
- 后端可执行文件：`build/radar_sim`

前端 WebSocket 地址已改为同源 `/ws`，因此页面通过 nginx 访问时不需要再写死
`8081` 端口。

## 一、启动顺序

1. 启动后端：
   ```bash
   cd /opt/radar_sim
   ./build/radar_sim --config config.json --http-port 8080 --ws-port 8081
   ```
2. 启动或重载 nginx。
3. 浏览器访问 `http://<服务器IP>:8088/`。

后端不再提供 `GET /` 页面；直接访问 `http://<服务器IP>:8080/` 返回 404 是
预期行为。前端必须通过 nginx 访问。

## 二、银河麒麟 V10 部署

安装 nginx：

```bash
sudo apt update
sudo apt install -y nginx
```

准备目录。以下示例假设部署目录为 `/opt/radar_sim`：

```bash
sudo mkdir -p /opt/radar_sim
sudo chown -R "$USER":"$USER" /opt/radar_sim

# 复制可执行文件、配置和前端
cp build/radar_sim /opt/radar_sim/
cp config.json /opt/radar_sim/
cp -a frontend /opt/radar_sim/
```

复制 nginx 配置：

```bash
sudo cp docs/deploy/nginx/radar_sim.conf /etc/nginx/conf.d/radar_sim.conf
```

确认 `/etc/nginx/nginx.conf` 的 `http { }` 中包含：

```nginx
include /etc/nginx/conf.d/*.conf;
```

如果系统使用 `sites-enabled`，也可以放到：

```bash
sudo cp docs/deploy/nginx/radar_sim.conf /etc/nginx/sites-available/radar_sim
sudo ln -s /etc/nginx/sites-available/radar_sim /etc/nginx/sites-enabled/radar_sim
```

配置检查并启动：

```bash
sudo nginx -t

sudo systemctl enable --now nginx
# 修改配置后重载：
sudo systemctl reload nginx
```

如果 nginx 启动失败，查看：

```bash
sudo tail -f /var/log/nginx/error.log
```

默认配置监听 `8088`。如果改成 `listen 80;`，需要同步调整防火墙放行端口：

```bash
# 以 firewalld 为例
sudo firewall-cmd --add-port=8088/tcp --permanent
sudo firewall-cmd --reload
```

> 生产环境建议只对外开放 nginx 的端口；后端的 `8080/8081` 通过防火墙或
> 监听地址限制为仅本机访问。

## 三、macOS 本地调试

### 方式 A：Homebrew 安装 nginx

```bash
brew install nginx
```

复制配置：

```bash
mkdir -p /opt/homebrew/etc/nginx/servers
cp docs/deploy/nginx/radar_sim.conf \
   /opt/homebrew/etc/nginx/servers/radar_sim.conf
```

把配置中的 `root` 改成 Mac 上的项目路径：

```nginx
root /Users/<user>/code/radar_sim/frontend;
```

> Homebrew 的默认 `nginx.conf` 也监听 `8080`，会和本项目后端的 8080 端口冲突。
> 启动前把默认 server 的 `listen 8080;` 改成 `listen 8888;`（或注释掉默认
> server）。本项目自己的站点配置监听 `8088`。

检查并启动：

```bash
nginx -t

# 仅本次启动：
brew services run nginx

# 或设置登录自启：
# brew services start nginx
```

访问：

```text
http://localhost:8088/
```

### 方式 B：Docker 运行 nginx（不安装本机 nginx）

先生成一份本地测试配置，把静态目录和后端地址替换成 Docker 可访问的形式：

```bash
sed \
  -e 's#root /opt/radar_sim/frontend;#root /usr/share/nginx/html;#' \
  -e 's#server 127.0.0.1:8080;#server host.docker.internal:8080;#' \
  -e 's#server 127.0.0.1:8081;#server host.docker.internal:8081;#' \
  docs/deploy/nginx/radar_sim.conf > /tmp/radar_sim.nginx.conf
```

启动容器：

```bash
docker run --rm -d \
  --name radar-nginx \
  --add-host=host.docker.internal:host-gateway \
  -p 8088:8088 \
  -v "$HOME/code/radar_sim/frontend":/usr/share/nginx/html:ro \
  -v /tmp/radar_sim.nginx.conf:/etc/nginx/conf.d/default.conf:ro \
  nginx:alpine
```

访问：

```text
http://localhost:8088/
```

停止：

```bash
docker stop radar-nginx
```

## 四、验证

### 1. 健康检查

通过 nginx 访问：

```bash
curl -sS http://127.0.0.1:8088/health
# {"ok":true}
```

### 2. 首页

```bash
curl -I http://127.0.0.1:8088/
# 期望：HTTP/1.1 200 OK
# 期望：Content-Type: text/html; charset=utf-8
```

### 3. API 代理

```bash
curl -sS -X POST http://127.0.0.1:8088/api/simulate \
  -H 'Content-Type: application/json' \
  -d '{"target":{"x":25000,"y":0},"jammer_on":true}' \
  | python3 -m json.tool
```

### 4. WebSocket 代理

用 WebSocket 握手确认 nginx 已正确转发：

```bash
curl -i -N --max-time 2 \
  -H 'Connection: Upgrade' \
  -H 'Upgrade: websocket' \
  -H 'Sec-WebSocket-Version: 13' \
  -H 'Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==' \
  http://127.0.0.1:8088/ws
```

期望看到：

```text
HTTP/1.1 101 Switching Protocols
```

也可以用 `websocat ws://127.0.0.1:8088/ws` 持续观察状态数据。

## 五、前端维护方式

前端源码位于：

```text
frontend/index.html
```

现在由 nginx 直接读取这个文件。修改后：

```bash
# 如果 nginx root 指向项目目录，刷新浏览器即可
# 如果指向 /opt/radar_sim/frontend，则同步文件：
cp frontend/index.html /opt/radar_sim/frontend/index.html
```

不需要重新编译 `radar_sim`。`index.html` 已配置 `Cache-Control: no-store`，
普通刷新即可看到新版本；如仍看到旧页面，执行强制刷新。

## 六、注意事项

- nginx 配置使用了 `map` 和 `upstream`，必须放在 nginx 的 `http { }` 上下文
  中；不要把整个文件直接粘进某个 `server { }` 块。
- 前端地图仍依赖 `unpkg.com` 的 Leaflet 和 OpenStreetMap 瓦片。离线部署需
  单独本地化静态资源；PPI 和 API 不受影响。
- 如果 nginx 和 `radar_sim` 不在同一台机器，修改
  `docs/deploy/nginx/radar_sim.conf` 中两个 `upstream` 的地址，并确保 8080/8081
  可被 nginx 访问。
- 后端默认监听所有地址；生产环境应通过防火墙限制 8080/8081，只暴露 nginx。
