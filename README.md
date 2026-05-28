# Monitor Dashboard

轻量级 Linux 进程与 NVIDIA GPU 监控仪表盘。C 语言后端采集系统指标，React 前端展示。

## 项目结构

```
dashboard/
├── ctl.sh                  # 启动/停止/重启 控制脚本
├── backend/                # C 后端
│   ├── src/                # 源码 (12 模块)
│   ├── vendor/             # 第三方库 (mongoose, yyjson, tomlc99)
│   └── config/             # 配置文件
├── frontend/               # React 前端
│   └── src/
│       ├── pages/          # 进程页面 + GPU 页面
│       ├── components/     # 通用组件 (表格、指标卡、抽屉等)
│       ├── hooks/          # 数据轮询 hooks
│       └── types/          # TypeScript 类型定义
└── deploy/                 # 部署配置
    ├── nginx/              # nginx 站点配置
    └── systemd/            # systemd 服务单元
```

## 前置依赖

| 依赖 | 用途 | 安装 |
|------|------|------|
| gcc (≥ 13) | C 编译器 | `apt install build-essential` |
| libnvidia-ml.so | NVML GPU 监控 | 随 NVIDIA 驱动安装 |
| **nvml.h** | GPU 监控头文件 | 需手动获取（见下） |
| Node.js (≥ 18) | 前端构建 | 推荐使用 nvm |
| nginx | 生产环境反向代理 | `apt install nginx` |

### 获取 nvml.h

`nvml.h` 是 NVIDIA 专有头文件，未纳入本仓库。编译 GPU 监控功能前需自行获取：

```bash
# 方式一：从系统 NVIDIA 驱动复制（推荐）
sudo find / -name nvml.h 2>/dev/null -exec cp {} backend/vendor/nvml.h \;

# 方式二：下载到 vendor 目录
wget https://raw.githubusercontent.com/NVIDIA/gpu-monitoring-tools/master/nvml.h \
     -O backend/vendor/nvml.h
```

如果不需要 GPU 监控，将配置中 `enable_gpu = false`，编译时不会链接 NVML。

## 快速开始

**使用控制脚本（推荐）：**
```bash
./ctl.sh start              # 后台启动后端
./ctl.sh status             # 查看运行状态
./ctl.sh stop               # 停止
./ctl.sh restart            # 重启
./ctl.sh dev                # 开发模式：后端 + 前端同时启动
```

**手动启动后端：**
```bash
cd backend
make
./monitor-backend --config config/monitor.conf
```

**手动启动前端开发服务器：**
```bash
cd frontend
npm install
npm run dev
```

浏览器打开 `http://localhost:5173`，Vite 会自动代理 API 请求到后端 `127.0.0.1:18080`。

## 配置说明

后端配置文件为 TOML 格式，默认路径 `backend/config/monitor.conf`：

```toml
listen_host = "127.0.0.1"       # 监听地址
listen_port = 18080             # 监听端口
sample_interval_ms = 1000       # 采样间隔（毫秒，最小 250）
enable_gpu = true               # 是否启用 GPU 监控
enable_process_ops = false      # 进程操作（v1 禁用）
enable_auth = false             # 身份验证（v1 禁用）
```

修改配置后需要重启后端生效。

## API 接口

所有接口前缀 `/api/v1`，响应格式：

```json
{
  "ok": true,
  "data": { ... },
  "meta": { "sequence": 12, "sampled_at_ms": 1760000000000, "sample_interval_ms": 1000 }
}
```

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/v1/config` | 运行时配置 |
| GET | `/api/v1/system/summary` | 系统 CPU/内存摘要 |
| GET | `/api/v1/processes` | 全部进程列表 |
| GET | `/api/v1/processes/{pid}` | 单进程详情 |
| GET | `/api/v1/gpus` | GPU 设备列表 |
| GET | `/api/v1/gpus/{gpu_id}` | 单个 GPU 详情 |
| GET | `/api/v1/gpu-processes` | GPU 进程列表 |

## 生产构建

```bash
make all                         # 构建后端 + 前端
sudo make install                # 安装到系统路径
```

## 生产部署（nginx + systemd）

### 1. 部署 nginx 配置

```bash
sudo cp deploy/nginx/monitor-dashboard.conf /etc/nginx/sites-available/
sudo ln -s /etc/nginx/sites-available/monitor-dashboard.conf /etc/nginx/sites-enabled/
sudo nginx -t && sudo nginx -s reload
```

nginx 负责两件事：
- 提供前端静态文件（`/usr/share/monitor-dashboard/public`）
- 反向代理 `/api/v1/*` 到后端 `127.0.0.1:18080`

### 2. 部署 systemd 服务

```bash
sudo cp deploy/systemd/monitor-dashboard.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now monitor-dashboard
```

### 3. 访问

确保 DNS 已配置 `dashboard.cpu.cialab.com` 指向该服务器，浏览器直接访问即可。

## 技术栈

| 层 | 技术 | 说明 |
|----|------|------|
| 后端语言 | C11 | 低开销系统监控 |
| HTTP 服务 | Mongoose 7.15 | 单文件嵌入式 HTTP 服务器 |
| JSON | yyjson | 高性能 JSON 序列化 |
| 配置解析 | tomlc99 | 轻量 TOML 解析器 |
| GPU 监控 | NVML | NVIDIA 官方 GPU 管理库 |
| 进程数据 | Linux /proc | procfs 文件系统 |
| 前端框架 | React 19 + TypeScript | 现代 Web 开发 |
| 构建工具 | Vite 6 | 快速开发与生产构建 |
| 表格组件 | TanStack Table v8 | 排序、搜索、筛选 |
| 样式方案 | CSS Modules | Vite 原生支持，样式隔离 |

## 设计原则

- **快照模型** — 独立采样线程定期采集，HTTP 处理器从内存快照读取，请求延迟可控
- **降级容错** — GPU 探测失败不影响进程监控，权限不足返回 `unavailable` 标记
- **扩展预留** — 身份验证、进程操作等模块留有占位桩，后续可逐步开启

## 开发计划

完整设计文档见 `doc/design.md`，需求文档见 `doc/requirements.md`。
