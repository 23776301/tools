#pragma once
#include <windows.h>
#include "PluginInterface.h"
#include <string>

// 插件配置（全部可在配置对话框中调整）
struct PluginConfig
{
    int quantity = 0;        // 0=电流, 1=功率
    int currentUnit = 0;     // 电流单位：0=mA, 1=A
    int powerUnit = 0;       // 功率单位：0=W, 1=mW
    int decimals = 0;        // 小数位数 0~3
    int signMode = 0;        // 0=带符号(放电-, 充电+), 1=绝对值
    int noData = 0;          // 无电池/无数据时：0=显示 "--", 1=显示 "0"
    std::wstring label = L"电池"; // 标签文本
    int voltageSource = 0;   // 0=实时电压(WMI), 1=固定电压
    int fixedVoltage = 12000;// 固定电压(mV)，当 voltageSource==1 时生效
};

// 电池放电电流/功率显示项
class CBatteryCurrentItem : public IPluginItem
{
public:
    CBatteryCurrentItem();

    // IPluginItem 接口
    virtual const wchar_t* GetItemName() const override;
    virtual const wchar_t* GetItemId() const override;
    virtual const wchar_t* GetItemLableText() const override;
    virtual const wchar_t* GetItemValueText() const override;
    virtual const wchar_t* GetItemValueSampleText() const override;

    // 不使用自绘（用户明确要求）：直接返回 false，宽度与绘制完全交给 TrafficMonitor
    // 默认逻辑，与内置的网速/CPU/显卡项走同一条路径，最稳定。
    virtual bool IsCustomDraw() const override { return false; }

    // 由插件更新显示值/标签
    void SetValue(const std::wstring& v);
    void SetLabel(const std::wstring& l);

private:
    std::wstring m_valueText;   // 实际显示的数值文本
    std::wstring m_labelText;   // 标签文本
};

// 插件主体
class CBatteryCurrentPlugin : public ITMPlugin
{
public:
    static CBatteryCurrentPlugin& Instance();

    // ITMPlugin 接口
    virtual IPluginItem* GetItem(int index) override;
    virtual void DataRequired() override;
    virtual const wchar_t* GetInfo(PluginInfoIndex index) override;
    virtual OptionReturn ShowOptionsDialog(void* hParent) override;
    virtual void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override;

    friend class CBatteryCurrentItem;
private:
    CBatteryCurrentPlugin();
    ~CBatteryCurrentPlugin();

    CBatteryCurrentItem m_item;
    unsigned long m_voltage;         // 实时电压(mV)基准，用于把 mW 换算成 mA（缓存）
    unsigned long m_voltageTick = 0; // 上次查电压时的"刷新计数"（用于节流）
    unsigned int  m_refreshCount = 0;// 每次 DataRequired 自增，作为节流节拍器
    PluginConfig m_cfg;              // 当前配置
    bool m_cfgLoaded = false;        // 配置是否已加载
    std::wstring m_lastValue;          // 上一次的有效显示值（API 瞬断时复用，避免 "--"）
    bool m_haveLast = false;           // 是否已产生过有效读数
    __int64 m_cfgMtime = 0;            // INI 文件最后修改时间（用于热加载）

    // 主源：内核电源状态（权威、无需打开电池设备句柄）
    bool ReadSystemBatteryState(bool& acOnline, bool& charging, bool& discharging, long& rate_mW);
    // 电压：WMI root\wmi BatteryStatus.Voltage（节流缓存，非每次刷新都查），回退默认 12V
    bool ReadVoltageWMI(unsigned long& volt);

    // 配置读写
    std::wstring GetConfigPath();
    void LoadConfig();
    void SaveConfig();
    static __int64 GetFileMtime(const std::wstring& path); // INI 文件最后修改时间(ms)
    std::wstring GetUnitSuffix() const;

    // 配置对话框
    static INT_PTR CALLBACK DlgProc(HWND h, UINT m, WPARAM w, LPARAM l);
    static void DlgInit(HWND h);
    static bool DlgOnOk(HWND h);
    static void DlgUpdateUnitEnable(HWND h);
};
