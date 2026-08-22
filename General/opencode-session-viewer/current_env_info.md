# Current Environment Info

> Last updated: 2026-08-21

## System Overview

| Item | Value |
|------|-------|
| Host | Android |
| Virtualization | AutoLinux (VM, independent) |
| OS | Ubuntu 24.04.4 LTS (Noble Numbat) |
| Kernel | 5.15.149-android13 |
| Arch | aarch64 (ARM64) |
| Init | systemd |

## Architecture

```
Android 手机开机
    ↓
AutoLinux VM 自动启动 (Android系统级)
    ↓
systemd 启动
    ↓
┌─────────────────────────────────────────┐
│ custom-services.service                 │
│ → start-services.sh                     │
│   ├── SSH/SFTP (port 22)                │
│   ├── Samba (port 445)                  │
│   ├── FTP (port 21)                     │
│   └── Bind mount /sdcard                │
└─────────────────────────────────────────┘
    ↓
┌─────────────────────────────────────────┐
│ service-watchdog.service                │
│ → service-watchdog-daemon.sh            │
│   └── 每30秒检查服务，挂了就重启          │
└─────────────────────────────────────────┘
```

## Systemd Services

| Service | Status | Description |
|---------|--------|-------------|
| custom-services.service | Enabled | 启动SSH/Samba/FTP |
| service-watchdog.service | Enabled | 服务保活守护 |
| smbd.service | Enabled | Samba SMB服务 |
| nmbd.service | Enabled | Samba NetBIOS |
| cron.service | Enabled | 定时任务 |

## Commands

```bash
# 查看服务状态
systemctl status custom-services.service
systemctl status service-watchdog.service

# 手动启动服务
systemctl start custom-services.service

# 查看日志
journalctl -u custom-services.service
tail -f /var/log/service-watchdog.log

# 刷新环境信息
/root/refresh-env-info.sh
```

## Key Paths

| Path | Description |
|------|-------------|
| `/usr/local/bin/start-services.sh` | 服务启动脚本 |
| `/usr/local/bin/service-watchdog-daemon.sh` | Watchdog守护进程 |
| `/etc/systemd/system/custom-services.service` | 自定义服务单元 |
| `/etc/systemd/system/service-watchdog.service` | Watchdog服务单元 |
| `/root/current_env_info.md` | 环境信息文档 |
| `/root/refresh-env-info.sh` | 刷新环境信息脚本 |

## 注意事项

1. AutoLinux是独立VM，不依赖Termux
2. 服务通过systemd管理，开机自启
3. Watchdog每30秒检查一次服务状态
4. 所有日志写入 `/var/log/` 或通过 `journalctl` 查看
