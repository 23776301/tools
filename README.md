# Tools Repository

个人工具集合，涵盖 Android、Linux、Windows 三个平台。

## 目录结构

```
tools/
├── General/                      # 通用工具（跨平台）
│   ├── opencode-session-viewer/  # OpenCode 会话历史查看器
│   └── keep-my-commit-clean/     # Git 提交规范文档
├── OS-Android/                   # Android 平台工具
│   └── autostart/                # AutoLinux VM 自启动探索（WIP）
├── OS-Linux/                     # Linux 平台工具
│   └── watchdog/                 # 基于 pidfd 的进程保活守护进程
├── OS-Windows/                   # Windows 平台工具
│   ├── tm-plugins/               # TrafficMonitor 插件集合
│   └── gui-vision-agent/         # GUI 视觉代理
└── README.md
```

## 通用工具

### opencode-session-viewer

OpenCode 会话历史查看器，快速查看和进入历史会话。

```bash
sss           # 列出最近10个会话
sss -c 3      # 列出3个会话
sss in 0      # 进入最新会话
sss 0      # 为最新会话生成 HTML 审计报告
```

**依赖：** Python 3、opencode

### keep-my-commit-clean

Git 提交规范：每个提交 = 单个模块 + 一项完整功能。

**核心原则：**
1. 单模块：一个提交只涉及一个模块
2. 完整功能：一个提交包含该功能的所有代码、配置、文档
3. 可编译：每个提交后代码应能正常编译运行
4. 有意义：提交消息描述"做了什么"和"为什么"

## 平台工具

### Android

运行环境：AutoLinux（Ubuntu 24.04 chroot，非 proot/VM）

| 工具 | 功能 | 状态 |
|------|------|------|
| autostart | Android 开机自启动探索 | WIP |

**环境限制：**
- systemd 不可用，需手动管理进程
- PID 1: `/system/bin/init second_stage`
- 需要 root 权限

### Linux

运行环境：Ubuntu 24.04 LTS (aarch64)，内核 5.15.149-android13

| 工具 | 功能 | 状态 |
|------|------|------|
| watchdog | 基于 pidfd + epoll 的进程保活，~570ms 响应 | ✅ |

**服务端口：**
- SSH: 22
- Samba: 445
- vsftpd: 21

### Windows

| 工具 | 功能 | 状态 |
|------|------|------|
| tm-plugins | TrafficMonitor 插件（电池电流/功率显示） | ✅ |
| gui-vision-agent | GUI 视觉代理 | ✅ |

## 快速开始

### Linux watchdog

```bash
cd OS-Linux/watchdog
bash build-deploy.sh
```

### Windows TrafficMonitor 插件

```bash
cd OS-Windows/tm-plugins/battery-current
windres -O coff BatteryCurrentPlugin.rc -o BatteryCurrentPlugin.res
g++ -shared -std=c++17 -O2 -static -DBUILDING_DLL \
    BatteryCurrentPlugin.cpp BatteryCurrentPlugin.res -o BatteryCurrentPlugin.dll \
    -Wl,--subsystem,windows -Wl,--kill-at \
    -lole32 -loleaut32 -luuid -lwbemuuid
```

## Git 配置

```bash
git config --global user.name "23776301"
git config --global user.email "23776301@users.noreply.github.com"
```

## 相关文档

- [Session Viewer 文档](General/opencode-session-viewer/README.md)
- [Git 提交规范](General/keep-my-commit-clean/README.md)
- [Android 自启动探索](OS-Android/autostart/README.md)
- [Watchdog 详细文档](OS-Linux/watchdog/README.md)
- [TrafficMonitor 插件文档](OS-Windows/tm-plugins/README.md)
