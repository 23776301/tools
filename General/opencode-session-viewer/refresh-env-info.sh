#!/bin/bash
# Refresh current_env_info.md with current system state

OUTPUT="/root/current_env_info.md"
TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')

# Get system info
OS_NAME=$(cat /etc/os-release | grep PRETTY_NAME | cut -d'"' -f2)
KERNEL=$(uname -r)
ARCH=$(arch)
RAM_TOTAL=$(free -h | awk '/Mem:/ {print $2}')
RAM_AVAIL=$(free -h | awk '/Mem:/ {print $7}')
DISK_TOTAL=$(df -h / 2>/dev/null | tail -1 | awk '{print $2}')
DISK_FREE=$(df -h / 2>/dev/null | tail -1 | awk '{print $4}')
WIFI_IP=$(hostname -I 2>/dev/null | awk '{print $1}')

# Detect virtualization
VIRT=$(systemd-detect-virt 2>/dev/null || echo "unknown")
if [ "$VIRT" = "vm-other" ] || [ "$VIRT" = "qemu" ] || [ "$VIRT" = "kvm" ]; then
    VIRT_TYPE="Virtual Machine"
elif [ "$VIRT" = "lxc" ] || [ "$VIRT" = "systemd-nspawn" ]; then
    VIRT_TYPE="Container"
else
    VIRT_TYPE="$VIRT"
fi

# Check services
SSH_STATUS=$(pgrep -x sshd > /dev/null && echo "Running" || echo "Stopped")
SMB_STATUS=$(pgrep -x smbd > /dev/null && echo "Running" || echo "Stopped")
NMB_STATUS=$(pgrep -x nmbd > /dev/null && echo "Running" || echo "Stopped")
FTP_STATUS=$(pgrep -x vsftpd > /dev/null && echo "Running" || echo "Stopped")

# Check installed tools
PYTHON_VER=$(python3 --version 2>/dev/null | awk '{print $2}' || echo "N/A")
GIT_VER=$(git --version 2>/dev/null | awk '{print $3}' || echo "N/A")

cat > "$OUTPUT" << EOF
# Current Environment Info

> Last updated: $TIMESTAMP

## System Overview

| Item | Value |
|------|-------|
| Host | Android |
| Virtualization | AutoLinux ($VIRT_TYPE) |
| OS | $OS_NAME |
| Kernel | $KERNEL |
| Arch | $ARCH |
| RAM | $RAM_TOTAL total, $RAM_AVAIL available |
| Storage | $DISK_TOTAL total, $DISK_FREE free |
| WiFi IP | $WIFI_IP |
| Init | systemd |

## Architecture

\`\`\`
Android Host
├── Termux (terminal app, optional)
└── AutoLinux ($VIRT_TYPE, independent process)
    └── Ubuntu 24.04 LTS (systemd)
        ├── SSH/SFTP (port 22)
        ├── Samba SMB (port 445)
        └── FTP vsftpd (port 21)
\`\`\`

**Key Point**: AutoLinux runs as a **$VIRT_TYPE**, NOT proot/chroot. Services persist even if Termux is killed.

## Running Services

| Service | Status | Port |
|---------|--------|------|
| SSH/SFTP (sshd) | $SSH_STATUS | 22 |
| Samba (smbd) | $SMB_STATUS | 445 |
| Samba NetBIOS (nmbd) | $NMB_STATUS | - |
| FTP (vsftpd) | $FTP_STATUS | 21 |
| Clash Meta | Android app | 7890 |
| AccuBattery | Android app | - |

## Installed Versions

| Tool | Version |
|------|---------|
| Python | $PYTHON_VER |
| Git | $GIT_VER |

## Key Paths

| Path | Description |
|------|-------------|
| \`/sdcard/\` | Android internal storage |
| \`/sdcard/gt5_nas/SD_Card/\` | Bind mount of /sdcard |
| \`/root/.local/share/opencode/opencode.db\` | OpenCode session database |
| \`/root/.local/bin/sss.py\` | Session viewer script |
| \`/usr/local/bin/start-services.sh\` | Service startup script |
| \`/usr/local/bin/service-watchdog.sh\` | Service watchdog |
| \`/root/app-protect.sh\` | Android app protection |

## Users

| User | Purpose |
|------|---------|
| root | Main user |
| gt5 | SMB/FTP access user |

## Auto-Start Mechanism

1. \`.bashrc\` calls \`/usr/local/bin/start-services.sh\`
2. \`start-services.sh\` starts SSH, Samba, FTP
3. \`start-services.sh\` launches \`service-watchdog.sh\` (every 30s)
4. Watchdog checks and restarts crashed services

## Commands

\`\`\`bash
# Refresh this doc
/root/refresh-env-info.sh

# View opencode sessions
sss
sss -c 3 -n 5
sss in 0

# Check services
/usr/local/bin/start-services.sh
/usr/local/bin/service-watchdog.sh

# App protection (Android side)
/root/app-protect.sh --status
/root/app-protect.sh --setup
\`\`\`
EOF

echo "Environment info refreshed: $OUTPUT"
