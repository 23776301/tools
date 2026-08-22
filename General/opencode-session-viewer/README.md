# opencode-session-viewer

快速查看和进入 opencode 历史会话的命令行工具。

## 安装

```bash
curl -fsSL https://raw.githubusercontent.com/23776301/tools/main/opencode-session-viewer/install.sh | bash
source ~/.bashrc
```

## 使用

```bash
sss           # 列出最近10个会话
sss -c 3      # 列出3个会话
sss -n 10     # 每个会话显示10次交互
sss in 0      # 进入最新会话
sss in 1      # 进入第二新的会话
```

## 颜色说明

| 元素 | 颜色 |
|------|------|
| 序号+标题 | 亮黄色 |
| Session ID | 绿色 |
| 用户问题 [Q] | 浅灰色 |
| Agent回复 [A] | 暗灰色 |
| 时间戳 | 浅蓝色 |

## 依赖

- Python 3
- opencode
