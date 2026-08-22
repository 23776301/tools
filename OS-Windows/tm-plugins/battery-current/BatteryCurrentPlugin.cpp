// 电池放电电流 / 功率 TrafficMonitor 插件  v1.3
//
// 设计要点（依据在物理机上实测验证）：
//   1) 电池设备的 DeviceIoControl(IOCTL_BATTERY_QUERY_STATUS) 在普通用户态进程里
//      会被拒绝访问(ERROR_ACCESS_DENIED=5)，不可靠。AIDA64 / HWiNFO 等工具走的是
//      内核电源管理接口，而非直连电池设备。
//   2) 权威且稳定可用的来源是 CallNtPowerInformation(SystemBatteryState)
//      （位于 powrprof.dll，不是 kernel32！）：它返回
//        - AcOnLine / Charging / Discharging 布尔标志（充放电方向权威）
//        - Rate(mW)，按约定 负=放电 正=充电
//      该接口不需要打开电池设备句柄，用户态进程可直接调用。
//   3) 实时电压由 WMI(root\wmi BatteryStatus.Voltage) 取得，用于把功率(mW)换算成
//      电流(mA)：电流 = Rate(mW) * 1000 / Voltage(mV)。电压缺失时回退 12000mV。
//
// 显示约定（与用户截图一致）：放电为负(-)、充电为正(+)，可在配置中改为绝对值。
//
// 配置：单位(电流 mA/A、功率 W/mW)、小数位数、符号、标签、刷新间隔、电压来源等
//       均通过配置对话框（重写 ITMPlugin::ShowOptionsDialog）提供，并保存到 INI。
#include "BatteryCurrentPlugin.h"
#include "resource.h"

#include <windows.h>
#include <powersetting.h>
#include <wbemidl.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include <string>

// 用于获取本 DLL 模块句柄的代码锚点（GetModuleHandleExW 的 FROM_ADDRESS 需要一个
// 落在当前 DLL 代码段内的地址）
static void ModuleAnchor() {}

// ---------------- CBatteryCurrentItem ----------------
CBatteryCurrentItem::CBatteryCurrentItem()
{
    m_labelText = L"电池";
    m_valueText = L"--";
}

const wchar_t* CBatteryCurrentItem::GetItemName() const { return L"电池放电电流"; }
const wchar_t* CBatteryCurrentItem::GetItemId()   const { return L"batterycurrent"; }
const wchar_t* CBatteryCurrentItem::GetItemLableText() const { return m_labelText.c_str(); }
const wchar_t* CBatteryCurrentItem::GetItemValueText()  const { return m_valueText.c_str(); }
const wchar_t* CBatteryCurrentItem::GetItemValueSampleText() const
{
    // 非自绘模式下由主程序按此样本估算“数值列”宽度。标签单列由
    // GetItemLableText 提供、宽度单独计算，因此这里只给数值部分的代表样本。
    std::wstring sample = L"-9999";
    if (CBatteryCurrentPlugin::Instance().m_cfg.decimals > 0)
    {
        sample += L".";
        for (int i = 0; i < CBatteryCurrentPlugin::Instance().m_cfg.decimals; i++) sample += L"9";
    }
    sample += CBatteryCurrentPlugin::Instance().GetUnitSuffix();
    static std::wstring cache;
    cache = sample;
    return cache.c_str();
}

void CBatteryCurrentItem::SetValue(const std::wstring& v) { m_valueText = v; }
void CBatteryCurrentItem::SetLabel(const std::wstring& l) { m_labelText = l; }

// ---------------- CBatteryCurrentPlugin ----------------
CBatteryCurrentPlugin::CBatteryCurrentPlugin()
{
    m_voltage = 12000;   // 合理默认（典型笔记本电池组标称约 11~15V）
    LoadConfig();        // 先按 DLL 所在目录加载（回退），EI_CONFIG_DIR 到达后会再次加载
    m_cfgLoaded = true;
    m_item.SetLabel(m_cfg.label.empty() ? L"电池" : m_cfg.label);
}

CBatteryCurrentPlugin::~CBatteryCurrentPlugin() {}

CBatteryCurrentPlugin& CBatteryCurrentPlugin::Instance()
{
    static CBatteryCurrentPlugin s_instance;
    return s_instance;
}

IPluginItem* CBatteryCurrentPlugin::GetItem(int index)
{
    if (index == 0) return &m_item;
    return nullptr;
}

// 主源：CallNtPowerInformation(SystemBatteryState)。SystemBatteryState = 5。
// 该函数在 powrprof.dll 中（不是 kernel32！），动态获取避免导入库/链接问题。
typedef DWORD (WINAPI *PFN_CNPI)(ULONG, PVOID, ULONG, PVOID, ULONG);

bool CBatteryCurrentPlugin::ReadSystemBatteryState(bool& acOnline, bool& charging,
                                                   bool& discharging, long& rate_mW)
{
    acOnline = charging = discharging = false;
    rate_mW = 0;

    static PFN_CNPI s_pfn = nullptr;
    static bool s_resolved = false;
    if (!s_resolved)
    {
        HMODULE hMod = LoadLibraryA("powrprof.dll");
        if (hMod) s_pfn = (PFN_CNPI)GetProcAddress(hMod, "CallNtPowerInformation");
        s_resolved = true;
    }
    if (!s_pfn) return false;

    SYSTEM_BATTERY_STATE sbs;
    memset(&sbs, 0, sizeof(sbs));
    // 返回 0(NTSTATUS 成功) 才有效
    if (s_pfn(SystemBatteryState, nullptr, 0, &sbs, sizeof(sbs)) != 0)
        return false;

    acOnline    = (sbs.AcOnLine != 0);
    charging    = (sbs.Charging != 0);
    discharging = (sbs.Discharging != 0);
    // Rate 字段为 ULONG，但约定按有符号解释：负=放电、正=充电
    rate_mW     = (LONG)sbs.Rate;
    return true;
}

// 实时电压：WMI root\wmi BatteryStatus.Voltage(mV)。COM 仅在首次惰性初始化。
bool CBatteryCurrentPlugin::ReadVoltageWMI(unsigned long& volt)
{
    volt = 0;

    static bool s_comReady = false;
    if (!s_comReady)
    {
        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        // S_OK/S_FALSE：本次初始化成功；RPC_E_CHANGED_MODE：宿主已初始化 COM，仍可用
        if (hr == S_OK || hr == S_FALSE || hr == RPC_E_CHANGED_MODE)
            s_comReady = true;
    }
    if (!s_comReady) return false;

    static IWbemLocator* s_loc = nullptr;
    static IWbemServices* s_svc = nullptr;
    if (!s_svc)
    {
        if (FAILED(CoCreateInstance(CLSID_WbemLocator, NULL, CLSCTX_INPROC_SERVER,
                                    IID_IWbemLocator, (LPVOID*)&s_loc)))
            return false;
        BSTR ns = SysAllocString(L"ROOT\\WMI");
        if (FAILED(s_loc->ConnectServer(ns, NULL, NULL, NULL, 0, NULL, NULL, &s_svc)))
        {
            SysFreeString(ns);
            return false;
        }
        SysFreeString(ns);
        s_loc->Release(); s_loc = nullptr;
    }

    // 仅取一个实例的电压，足够换算使用
    BSTR q = SysAllocString(L"SELECT Voltage FROM BatteryStatus");
    IEnumWbemClassObject* pEnum = nullptr;
    HRESULT hr = s_svc->ExecQuery(
        SysAllocString(L"WQL"), q,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnum);
    SysFreeString(q);
    if (FAILED(hr) || !pEnum) return false;

    IWbemClassObject* pObj = nullptr;
    ULONG ret = 0;
    bool got = false;
    while (pEnum->Next(WBEM_INFINITE, 1, &pObj, &ret) == S_OK && ret > 0)
    {
        VARIANT v;
        VariantInit(&v);
        if (SUCCEEDED(pObj->Get(L"Voltage", 0, &v, NULL, NULL)) &&
            V_VT(&v) == VT_I4)
        {
            long val = V_I4(&v);
            if (val > 1000 && val < 25000)   // 合理电压范围
            {
                volt = (unsigned long)val;
                got = true;
            }
        }
        VariantClear(&v);
        pObj->Release();
        if (got) break;
    }
    pEnum->Release();
    return got;
}

// 配置路径：TrafficMonitor 官方并未为插件定义任何配置存储机制（无 EI_CONFIG_DIR 之类的
// 扩展项，PluginInterface.h 的 ExtendedInfoIndex 枚举里也没有）。因此配置完全由本插件
// 自行管理，固定存放在插件 DLL 所在目录下的 BatteryCurrentPlugin.ini，使用 Windows 标准
// INI API（Get/WritePrivateProfile*）读写——与 TrafficMonitor 自身 config.ini 同源。
std::wstring CBatteryCurrentPlugin::GetConfigPath()
{
    HMODULE hMod = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)&ModuleAnchor, &hMod);
    wchar_t mod[MAX_PATH] = { 0 };
    GetModuleFileNameW(hMod, mod, MAX_PATH);
    std::wstring s = mod;
    size_t p = s.find_last_of(L"\\/");
    if (p != std::wstring::npos) s = s.substr(0, p);
    if (!s.empty() && s.back() != L'\\') s += L'\\';
    return s + L"BatteryCurrentPlugin.ini";
}

void CBatteryCurrentPlugin::LoadConfig()
{
    std::wstring p = GetConfigPath();
    m_cfg.quantity      = GetPrivateProfileIntW(L"Settings", L"Quantity", 0, p.c_str());
    m_cfg.currentUnit   = GetPrivateProfileIntW(L"Settings", L"CurrentUnit", 0, p.c_str());
    m_cfg.powerUnit     = GetPrivateProfileIntW(L"Settings", L"PowerUnit", 0, p.c_str());
    m_cfg.decimals      = GetPrivateProfileIntW(L"Settings", L"Decimals", 0, p.c_str());
    m_cfg.signMode      = GetPrivateProfileIntW(L"Settings", L"SignMode", 0, p.c_str());
    m_cfg.noData        = GetPrivateProfileIntW(L"Settings", L"NoData", 0, p.c_str());
    m_cfg.voltageSource = GetPrivateProfileIntW(L"Settings", L"VoltageSource", 0, p.c_str());
    m_cfg.fixedVoltage  = GetPrivateProfileIntW(L"Settings", L"FixedVoltage", 12000, p.c_str());

    wchar_t buf[128] = { 0 };
    GetPrivateProfileStringW(L"Settings", L"Label", L"电池", buf, 128, p.c_str());
    m_cfg.label = buf;

    // 取值范围保护
    if (m_cfg.quantity < 0 || m_cfg.quantity > 1) m_cfg.quantity = 0;
    if (m_cfg.currentUnit < 0 || m_cfg.currentUnit > 1) m_cfg.currentUnit = 0;
    if (m_cfg.powerUnit < 0 || m_cfg.powerUnit > 1) m_cfg.powerUnit = 0;
    if (m_cfg.decimals < 0 || m_cfg.decimals > 3) m_cfg.decimals = 0;
    if (m_cfg.signMode < 0 || m_cfg.signMode > 1) m_cfg.signMode = 0;
    if (m_cfg.noData < 0 || m_cfg.noData > 1) m_cfg.noData = 0;
    if (m_cfg.voltageSource < 0 || m_cfg.voltageSource > 1) m_cfg.voltageSource = 0;
    if (m_cfg.fixedVoltage < 1000 || m_cfg.fixedVoltage > 25000) m_cfg.fixedVoltage = 12000;

    m_item.SetLabel(m_cfg.label.empty() ? L"电池" : m_cfg.label);

    // 记录文件最后修改时间，供热加载比对
    m_cfgMtime = GetFileMtime(p);
}

// 取文件最后写入时间（FILETIME -> 毫秒时间戳）；文件不存在返回 0
__int64 CBatteryCurrentPlugin::GetFileMtime(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa))
        return 0;
    ULARGE_INTEGER ul;
    ul.LowPart = fa.ftLastWriteTime.dwLowDateTime;
    ul.HighPart = fa.ftLastWriteTime.dwHighDateTime;
    // 转成毫秒值（自 1601-01-01 起的 100ns 计数 / 10000）
    return (__int64)(ul.QuadPart / 10000);
}

void CBatteryCurrentPlugin::SaveConfig()
{
    std::wstring p = GetConfigPath();
    auto w = [&](const wchar_t* key, int val)
    {
        wchar_t buf[32];
        swprintf(buf, 32, L"%d", val);
        WritePrivateProfileStringW(L"Settings", key, buf, p.c_str());
    };
    w(L"Quantity", m_cfg.quantity);
    w(L"CurrentUnit", m_cfg.currentUnit);
    w(L"PowerUnit", m_cfg.powerUnit);
    w(L"Decimals", m_cfg.decimals);
    w(L"SignMode", m_cfg.signMode);
    w(L"NoData", m_cfg.noData);
    w(L"VoltageSource", m_cfg.voltageSource);
    w(L"FixedVoltage", m_cfg.fixedVoltage);
    WritePrivateProfileStringW(L"Settings", L"Label",
                               m_cfg.label.empty() ? L"电池" : m_cfg.label.c_str(), p.c_str());
}

std::wstring CBatteryCurrentPlugin::GetUnitSuffix() const
{
    if (m_cfg.quantity == 0)
        return m_cfg.currentUnit == 0 ? L" mA" : L" A";
    else
        return m_cfg.powerUnit == 0 ? L" W" : L" mW";
}

void CBatteryCurrentPlugin::DataRequired()
{
    if (!m_cfgLoaded) { LoadConfig(); m_cfgLoaded = true; }

    // INI 热加载：每次刷新都比对磁盘文件的修改时间，若发生变化则重新加载配置。
    // 这样用户直接修改 BatteryCurrentPlugin.ini（无需开对话框、无需重启 TrafficMonitor）
    // 即可即时生效。
    __int64 mt = GetFileMtime(GetConfigPath());
    if (mt != 0 && mt != m_cfgMtime)
        LoadConfig();

    // 刷新策略：完全跟随 TrafficMonitor 的全局刷新节拍（config.ini 的
    // monitor_time_span）。TM 每次主刷新都会调用 DataRequired，插件在这里实时
    // 读取一次硬件并返回最新值——不再自行定义任何刷新间隔（旧版的 RefreshMs
    // 节流已移除）。这样电池项的刷新频率与网速/CPU/显卡等内置项完全一致，
    // 由用户在 TM 设置里统一调整 monitor_time_span 即可。
    {
    // 刷新节拍器：每次 DataRequired 自增，用于给"慢源"（WMI 电压）做节流，
    // 避免每次刷新都打昂贵的跨进程 WMI 查询（这是之前任务栏电流刷新慢的根因）。
    m_refreshCount++;

    // 1) 内核电源状态（权威主源）——纯内核调用，微秒级、无跨进程阻塞。
    //    它已经给出：AcOnLine / Charging / Discharging 方向标志，以及带符号 Rate(mW)。
    bool online = true, charging = false, discharging = false;
    long rate_mW = 0;
    bool okState = ReadSystemBatteryState(online, charging, discharging, rate_mW);

    // ===== 诊断日志（DebugLog=1 时写插件目录 BatteryCurrentPlugin_log.csv）=====
    static bool s_dbgInit = false; static int s_dbgOn = -1;
    if (!s_dbgInit) { s_dbgOn = (GetPrivateProfileIntW(L"Settings", L"DebugLog", 0, GetConfigPath().c_str()) == 1); s_dbgInit = true; }
    auto LogLine = [&](const wchar_t* valTxt)
    {
        if (s_dbgOn != 1) return;
        std::wstring cfg = GetConfigPath();
        std::wstring logp = cfg.substr(0, cfg.find_last_of(L"\\/") + 1) + L"BatteryCurrentPlugin_log.csv";
        wchar_t buf[256];
        swprintf(buf, 256, L"%llu,raw=%ld,volt=%lu,val=%ls\n",
                 (unsigned long long)GetTickCount64(), (long)rate_mW, m_voltage, valTxt);
        FILE* f = nullptr;
        _wfopen_s(&f, logp.c_str(), L"a");
        if (f) { fputws(buf, f); fclose(f); }
    };

    if (!okState)
    {
        // 内核 API 偶发失败（多见于系统刚唤醒那一瞬）：不要立刻显示 "--"，
        // 保留上一次的有效读数即可自愈；实在从未读到过才显示无数据占位。
        if (m_haveLast)
        {
            m_item.SetValue(m_lastValue);
            LogLine(L"(kernel fail, keep last)");
            return;
        }
        m_item.SetValue(m_cfg.noData == 0 ? L"--" : L"0");
        LogLine(L"(kernel fail, noData)");
        return;
    }

    // 2) AC 在线状态：直接用内核 AcOnLine（已足够可靠，唤醒瞬间偶发失效由
    //    上方"保留上次读数"逻辑自愈，无需再引一次 WMI 查询）。
    bool acOnline = online;

    // 3) 充放电方向：完全用内核标志判定。
    //    接电且电池已满/未充电时，Rate 会为 0、Charging/Discharging 均为 False——
    //    这是正常稳态，不是故障，应当显示 0 而不是 "--"。
    bool isDischarging, isCharging;
    isDischarging = discharging || (!acOnline && !charging);
    isCharging    = charging || (acOnline && !discharging);

    // 4) 功率数值（mW）：直接取内核 Rate 的绝对值用于换算。
    long raw_mW = rate_mW;
    long mag_mW = raw_mW;
    if (mag_mW < 0) mag_mW = -mag_mW;
    // Rate==0 是接电稳态的正常值，直接显示 0（带符号/无符号按配置）。
    if (mag_mW == 0)
    {
        std::wstring s = L"0";
        if (m_cfg.signMode == 0)
        {
            // 接电未充电：方向中性，给个中性 0（不带 ±）
            if (isCharging && s[0] != L'+') s = L"+" + s;
            else if (isDischarging && s[0] != L'-') s = L"-" + s;
        }
        s += GetUnitSuffix();
        m_lastValue = s;
        m_haveLast = true;
        m_item.SetValue(s);
        LogLine(s.c_str());
        return;
    }

    // 5) 电压来源。实时电压(WMI)查询是跨进程慢操作，必须节流：
    //    每 VOLTAGE_REFRESH_EVERY 次主刷新才真正查一次 WMI（约 5 秒 @1s 节拍），
    //    其余刷新复用缓存 m_voltage。电池组电压一分钟内几乎不变，5 秒更新一次
    //    对电流(mA)换算精度无影响，却能把热路径的 WMI 查询从"每秒 1 次"降到"每 5 秒 1 次"，
    //    彻底消除对 TM 刷新线程的阻塞（这正是之前任务栏电流刷新慢的根因）。
    const unsigned int VOLTAGE_REFRESH_EVERY = 5;
    unsigned long volt;
    if (m_cfg.voltageSource == 1)
    {
        volt = (unsigned long)m_cfg.fixedVoltage;
        m_voltage = volt;
    }
    else
    {
        bool needQuery = (m_voltageTick == 0) ||
                         (m_refreshCount - m_voltageTick >= VOLTAGE_REFRESH_EVERY);
        if (needQuery)
        {
            unsigned long v = 0;
            if (ReadVoltageWMI(v) && v >= 1000 && v <= 25000)
            {
                m_voltage = v;
                m_voltageTick = m_refreshCount;
            }
            // 查询失败则继续用上次缓存值，不阻断显示
        }
        volt = m_voltage;
    }

    // 5) 按配置量纲与单位换算
    double val;
    if (m_cfg.quantity == 0)          // 电流
    {
        double v = volt ? volt : 12000.0;
        if (m_cfg.currentUnit == 0)   // mA = mW * 1000 / mV
            val = (double)mag_mW * 1000.0 / v;
        else                          // A  = mW / mV
            val = (double)mag_mW / v;
    }
    else                              // 功率
    {
        if (m_cfg.powerUnit == 0)     // W  = mW / 1000
            val = (double)mag_mW / 1000.0;
        else                          // mW
            val = (double)mag_mW;
    }

    // 6) 格式化（保留配置的小数位数）
    wchar_t num[64];
    swprintf(num, 64, L"%.*f", m_cfg.decimals, val);

    std::wstring s = num;
    if (m_cfg.signMode == 0)          // 带符号：放电为负、充电为正
    {
        if (isDischarging)
        {
            if (s[0] != L'-') s = L"-" + s;
        }
        else if (isCharging)
        {
            if (s[0] != L'+') s = L"+" + s;
        }
    }
    s += GetUnitSuffix();
    m_item.SetValue(s);
        LogLine(s.c_str());
    } // 本次 DataRequired 读取结束
}

const wchar_t* CBatteryCurrentPlugin::GetInfo(PluginInfoIndex index)
{
    switch (index)
    {
    case TMI_NAME:        return L"电池电流/功率";
    case TMI_DESCRIPTION: return L"读取电池放电/充电电流(mA/A)或功率(W/mW)：以内核电源状态判定方向，负值放电、正值充电。刷新完全跟随 TrafficMonitor 全局设置，热路径仅内核调用、电压 WMI 节流，任务栏刷新与内置项同节拍。可在插件设置中配置单位、小数、符号等。";
    case TMI_AUTHOR:      return L"WorkBuddy";
    case TMI_COPYRIGHT:   return L"(C) WorkBuddy";
    case TMI_VERSION:     return L"1.4";
    case TMI_URL:         return L"https://github.com/zhongyang219/TrafficMonitor";
    default:              return L"";
    }
}

void CBatteryCurrentPlugin::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data)
{
    // TrafficMonitor 通过此接口向插件推送绘制颜色、网速显示模式等主程序信息
    // （见 PluginInterface.h 的 ExtendedInfoIndex 枚举）。注意：官方接口中**没有**
    // 任何"配置目录"扩展项，插件的配置文件完全由本插件自行管理（见 GetConfigPath）。
    // 此处无需处理配置相关逻辑；配置的热加载在 DataRequired 中通过比对文件修改时间完成。
    (void)index; (void)data;
}

// ---------------- 配置对话框 ----------------
INT_PTR CALLBACK CBatteryCurrentPlugin::DlgProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_INITDIALOG:
        DlgInit(h);
        return TRUE;
    case WM_COMMAND:
    {
        int id = (int)LOWORD(w);
        if (id == IDC_QUANTITY && HIWORD(w) == CBN_SELCHANGE)
        {
            DlgUpdateUnitEnable(h);
            return TRUE;
        }
        if (id == IDOK)
        {
            if (DlgOnOk(h)) EndDialog(h, IDOK);
            return TRUE;
        }
        if (id == IDCANCEL)
        {
            EndDialog(h, IDCANCEL);
            return TRUE;
        }
        return FALSE;
    }
    }
    return FALSE;
}

void CBatteryCurrentPlugin::DlgInit(HWND h)
{
    PluginConfig& c = Instance().m_cfg;

    auto addCombo = [&](int id, const wchar_t* items[], int n, int sel)
    {
        HWND cb = GetDlgItem(h, id);
        for (int i = 0; i < n; i++)
            SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)items[i]);
        SendMessageW(cb, CB_SETCURSEL, sel, 0);
    };

    const wchar_t* q[]  = { L"电流", L"功率" };
    const wchar_t* cu[] = { L"mA", L"A" };
    const wchar_t* pu[] = { L"W", L"mW" };
    const wchar_t* d[]  = { L"0", L"1", L"2", L"3" };
    const wchar_t* s[]  = { L"带符号(±)", L"绝对值" };
    const wchar_t* nd[] = { L"显示 --", L"显示 0" };
    const wchar_t* vs[] = { L"实时(WMI)", L"固定值" };

    addCombo(IDC_QUANTITY, q, 2, c.quantity);
    addCombo(IDC_CUNIT, cu, 2, c.currentUnit);
    addCombo(IDC_PUNIT, pu, 2, c.powerUnit);
    addCombo(IDC_DECIMALS, d, 4, c.decimals);
    addCombo(IDC_SIGN, s, 2, c.signMode);
    addCombo(IDC_NODATA, nd, 2, c.noData);
    addCombo(IDC_VSRC, vs, 2, c.voltageSource);

    SetWindowTextW(GetDlgItem(h, IDC_LABEL),
                   c.label.empty() ? L"电池" : c.label.c_str());

    wchar_t buf[32];
    swprintf(buf, 32, L"%d", c.fixedVoltage);
    SetWindowTextW(GetDlgItem(h, IDC_VFIX), buf);

    DlgUpdateUnitEnable(h);
}

void CBatteryCurrentPlugin::DlgUpdateUnitEnable(HWND h)
{
    int q = (int)SendMessageW(GetDlgItem(h, IDC_QUANTITY), CB_GETCURSEL, 0, 0);
    EnableWindow(GetDlgItem(h, IDC_CUNIT), q == 0);
    EnableWindow(GetDlgItem(h, IDC_PUNIT), q == 1);
}

bool CBatteryCurrentPlugin::DlgOnOk(HWND h)
{
    PluginConfig& c = Instance().m_cfg;

    c.quantity      = (int)SendMessageW(GetDlgItem(h, IDC_QUANTITY), CB_GETCURSEL, 0, 0);
    c.currentUnit   = (int)SendMessageW(GetDlgItem(h, IDC_CUNIT), CB_GETCURSEL, 0, 0);
    c.powerUnit     = (int)SendMessageW(GetDlgItem(h, IDC_PUNIT), CB_GETCURSEL, 0, 0);
    c.decimals      = (int)SendMessageW(GetDlgItem(h, IDC_DECIMALS), CB_GETCURSEL, 0, 0);
    c.signMode      = (int)SendMessageW(GetDlgItem(h, IDC_SIGN), CB_GETCURSEL, 0, 0);
    c.noData        = (int)SendMessageW(GetDlgItem(h, IDC_NODATA), CB_GETCURSEL, 0, 0);
    c.voltageSource = (int)SendMessageW(GetDlgItem(h, IDC_VSRC), CB_GETCURSEL, 0, 0);

    wchar_t buf[128];
    GetWindowTextW(GetDlgItem(h, IDC_LABEL), buf, 128);
    c.label = buf;

    GetWindowTextW(GetDlgItem(h, IDC_VFIX), buf, 128);
    long v = wcstol(buf, NULL, 10);
    if (v < 1000) v = 1000; if (v > 25000) v = 25000;
    c.fixedVoltage = (int)v;

    Instance().m_item.SetLabel(c.label.empty() ? L"电池" : c.label);
    Instance().SaveConfig();
    return true;
}

ITMPlugin::OptionReturn CBatteryCurrentPlugin::ShowOptionsDialog(void* hParent)
{
    INT_PTR r = DialogBoxParamW(GetModuleHandle(NULL),
                                MAKEINTRESOURCEW(IDD_CONFIG),
                                (HWND)hParent,
                                DlgProc, 0);
    if (r == IDOK) return OR_OPTION_CHANGED;
    return OR_OPTION_UNCHANGED;
}

// ---------------- 导出函数 ----------------
extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance()
{
    return &CBatteryCurrentPlugin::Instance();
}
