# TM 的插件

TrafficMonitor 插件归档目录。后续开发的各类 TrafficMonitor 插件都放在这里，按子文件夹分类。

## 目录结构

- `battery-current/` — 电池放电电流 / 充电功率显示插件（非自绘版）

## 编译说明（battery-current）

使用 MinGW-w64（WinLibs UCRT）编译。关键：**必须 `-static` 静态链接运行时**，否则生成的 DLL 依赖
`libgcc_s_seh-1.dll` / `libstdc++-6.dll`，而 TrafficMonitor 进程加载时找不到这两个文件，会静默跳过插件。

```bash
windres -O coff BatteryCurrentPlugin.rc -o BatteryCurrentPlugin.res
g++ -shared -std=c++17 -O2 -static -DBUILDING_DLL \
    BatteryCurrentPlugin.cpp BatteryCurrentPlugin.res -o BatteryCurrentPlugin.dll \
    -Wl,--subsystem,windows -Wl,--kill-at \
    -lole32 -loleaut32 -luuid -lwbemuuid
```

## 部署

将 `BatteryCurrentPlugin.dll` 与 `BatteryCurrentPlugin.ini` 复制到 TrafficMonitor 的插件目录
（如 `C://Program Files\TrafficMonitor\TrafficMonitor\plugins\`），重启 TrafficMonitor 即可。

## 配置（BatteryCurrentPlugin.ini）

所有显示参数均由磁盘 INI 文件控制，支持热加载（修改后下次读取自动生效）：

| 键 | 说明 | 取值 |
|----|------|------|
| `Quantity`   | 显示电流或功率 | 0=电流, 1=功率 |
| `CurrentUnit`| 电流单位 | 0=mA, 1=A |
| `PowerUnit`  | 功率单位 | 0=W, 1=mW |
| `Decimals`   | 小数位 | 0-3 |
| `Label`      | 任务栏标签文本 | 任意字符串 |
| `RefreshMs`  | 应用层读取间隔(ms) | 默认 500 |
| `VoltageSource` | 电压来源 | 见源码注释 |
| `FixedVoltage`  | 固定电压(mV) | 默认 12000 |
| `DebugLog`   | 诊断日志开关 | 0/1 |
