// Battery.cpp - GetSystemPowerStatus plus the battery class driver IOCTLs.
#include "Battery.h"

#include <algorithm>

#include <setupapi.h>
#include <winioctl.h>

namespace {
// The battery class driver interface (documented in batclass.h). Declared
// here because the Windows SDK and MinGW put it in different headers.
const GUID kBatteryClass = {0x72631e54, 0x78a4, 0x11d0, {0xbc, 0xf7, 0x00, 0xaa, 0x00, 0xb7, 0xb3, 0x2a}};
constexpr DWORD kDeviceBattery = 0x00000029;
constexpr DWORD kQueryTag = CTL_CODE(kDeviceBattery, 0x10, METHOD_BUFFERED, FILE_READ_ACCESS);
constexpr DWORD kQueryInformation = CTL_CODE(kDeviceBattery, 0x11, METHOD_BUFFERED, FILE_READ_ACCESS);
constexpr DWORD kQueryStatus = CTL_CODE(kDeviceBattery, 0x13, METHOD_BUFFERED, FILE_READ_ACCESS);

enum InfoLevel : int {
    kInformation = 0,
    kTemperature = 2,
    kDeviceName = 4,
    kManufactureDate = 5,
    kManufactureName = 6,
    kSerialNumber = 8,
};

struct QueryInformation {
    ULONG tag;
    int level;
    LONG atRate;
};

struct Information {
    ULONG capabilities;
    UCHAR technology;
    UCHAR reserved[3];
    UCHAR chemistry[4];
    ULONG designedCapacity;
    ULONG fullChargedCapacity;
    ULONG defaultAlert1;
    ULONG defaultAlert2;
    ULONG criticalBias;
    ULONG cycleCount;
};

struct WaitStatus {
    ULONG tag;
    ULONG timeout;
    ULONG powerState;
    ULONG lowCapacity;
    ULONG highCapacity;
};

struct Status {
    ULONG powerState;
    ULONG capacity;
    ULONG voltage;
    LONG rate;
};

struct ManufactureDate {
    UCHAR day;
    UCHAR month;
    USHORT year;
};

constexpr ULONG kCapRelative = 0x40000000;
constexpr ULONG kUnknown = 0xFFFFFFFF;
constexpr LONG kUnknownRate = (LONG)0x80000000;

bool g_demo = false;

std::wstring QueryString(HANDLE h, ULONG tag, int level) {
    QueryInformation q{tag, level, 0};
    wchar_t buf[256] = L"";
    DWORD got = 0;
    if (!DeviceIoControl(h, kQueryInformation, &q, sizeof(q), buf, sizeof(buf) - sizeof(wchar_t), &got, nullptr))
        return {};
    buf[std::min<size_t>(got / sizeof(wchar_t), 255)] = 0;
    std::wstring s = buf;
    while (!s.empty() && (s.back() == L' ' || s.back() == 0)) s.pop_back();
    return s;
}

std::wstring ChemistryName(const UCHAR c[4]) {
    std::string code(reinterpret_cast<const char*>(c), 4);
    while (!code.empty() && (code.back() == 0 || code.back() == ' ')) code.pop_back();
    if (code == "LION" || code == "Li-I" || code == "LI-I") return L"Lithium-ion";
    if (code == "LiP" || code == "LIP" || code == "LiPo") return L"Lithium polymer";
    if (code == "NiMH") return L"Nickel metal hydride";
    if (code == "NiCd") return L"Nickel cadmium";
    if (code == "PbAc") return L"Lead acid";
    if (code == "RAM") return L"Rechargeable alkaline";
    return std::wstring(code.begin(), code.end());
}

void ReadDetails(std::vector<BatteryDetails>& out) {
    HDEVINFO devs = SetupDiGetClassDevsW(&kBatteryClass, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devs == INVALID_HANDLE_VALUE) return;
    for (DWORD i = 0; i < 8; ++i) {
        SP_DEVICE_INTERFACE_DATA did{sizeof(did)};
        if (!SetupDiEnumDeviceInterfaces(devs, nullptr, &kBatteryClass, i, &did)) break;
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(devs, &did, nullptr, 0, &need, nullptr);
        if (!need) continue;
        std::vector<BYTE> buf(need);
        auto* detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)buf.data();
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(devs, &did, detail, need, nullptr, nullptr)) continue;
        HANDLE h = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        ULONG wait = 0, tag = 0;
        DWORD got = 0;
        if (DeviceIoControl(h, kQueryTag, &wait, sizeof(wait), &tag, sizeof(tag), &got, nullptr) && tag) {
            BatteryDetails b;
            QueryInformation q{tag, kInformation, 0};
            Information info{};
            if (DeviceIoControl(h, kQueryInformation, &q, sizeof(q), &info, sizeof(info), &got, nullptr)) {
                b.relative = (info.capabilities & kCapRelative) != 0;
                b.designCapacity = info.designedCapacity == kUnknown ? 0 : info.designedCapacity;
                b.fullCapacity = info.fullChargedCapacity == kUnknown ? 0 : info.fullChargedCapacity;
                b.cycleCount = info.cycleCount > 0 && info.cycleCount != kUnknown ? (int)info.cycleCount : -1;
                b.chemistry = ChemistryName(info.chemistry);
            }
            WaitStatus ws{tag, 0, 0, 0, 0};
            Status st{};
            if (DeviceIoControl(h, kQueryStatus, &ws, sizeof(ws), &st, sizeof(st), &got, nullptr)) {
                b.currentCapacity = st.capacity == kUnknown ? 0 : st.capacity;
                b.voltage = st.voltage == kUnknown ? 0 : st.voltage;
                b.rateKnown = st.rate != kUnknownRate;
                b.rate = b.rateKnown ? st.rate : 0;
            }
            b.name = QueryString(h, tag, kDeviceName);
            b.manufacturer = QueryString(h, tag, kManufactureName);
            b.serial = QueryString(h, tag, kSerialNumber);
            q.level = kTemperature;
            ULONG temp = 0;
            if (DeviceIoControl(h, kQueryInformation, &q, sizeof(q), &temp, sizeof(temp), &got, nullptr) && temp > 0)
                b.temperature = temp / 10.0 - 273.15;  // tenths of a kelvin
            q.level = kManufactureDate;
            ManufactureDate md{};
            if (DeviceIoControl(h, kQueryInformation, &q, sizeof(q), &md, sizeof(md), &got, nullptr) && md.year > 1990) {
                wchar_t d[32];
                swprintf_s(d, L"%04u-%02u-%02u", md.year, md.month, md.day);
                b.manufactureDate = d;
            }
            out.push_back(std::move(b));
        }
        CloseHandle(h);
    }
    SetupDiDestroyDeviceInfoList(devs);
}

// A laptop on a desk, for testing without a battery: it discharges for a
// while, then charges, and repeats (one cycle every few minutes).
void Simulate(PowerSnapshot& s) {
    const double t = GetTickCount64() / 1000.0;
    const double phase = std::fmod(t, 360.0);  // 6-minute cycle
    const bool charging = phase >= 240;
    double pct = charging ? 18 + (phase - 240) * 0.7 : 100 - phase * 0.34;
    if (pct > 100) pct = 100;
    s.hasBattery = true;
    s.onAc = charging;
    s.percent = (int)std::lround(pct);
    s.charging = charging && s.percent < 100;
    s.full = charging && s.percent >= 100;
    s.saver = !charging && s.percent <= 20;
    s.secondsLeft = charging ? -1 : (int)(pct * 140);
    BatteryDetails b;
    b.name = L"DELL 7FHHV91";
    b.manufacturer = L"SMP";
    b.serial = L"1234";
    b.chemistry = L"Lithium polymer";
    b.manufactureDate = L"2023-04-12";
    b.designCapacity = 56000;
    b.fullCapacity = 49840;
    b.currentCapacity = (unsigned)(b.fullCapacity * pct / 100);
    b.voltage = (unsigned)(11400 + pct * 18);
    b.rateKnown = true;
    b.rate = s.full ? 0 : charging ? 31500 : -9800;
    b.cycleCount = 214;
    b.temperature = charging ? 34.5 : 31.0;
    s.batteries.push_back(b);
}
}  // namespace

unsigned PowerSnapshot::DesignCapacity() const {
    unsigned v = 0;
    for (const auto& b : batteries) v += b.relative ? 0 : b.designCapacity;
    return v;
}
unsigned PowerSnapshot::FullCapacity() const {
    unsigned v = 0;
    for (const auto& b : batteries) v += b.relative ? 0 : b.fullCapacity;
    return v;
}
unsigned PowerSnapshot::CurrentCapacity() const {
    unsigned v = 0;
    for (const auto& b : batteries) v += b.relative ? 0 : b.currentCapacity;
    return v;
}
bool PowerSnapshot::RateKnown() const {
    for (const auto& b : batteries)
        if (b.rateKnown && !b.relative) return true;
    return false;
}
int PowerSnapshot::Rate() const {
    int v = 0;
    for (const auto& b : batteries)
        if (b.rateKnown && !b.relative) v += b.rate;
    return v;
}
int PowerSnapshot::HealthPercent() const {
    const unsigned d = DesignCapacity(), f = FullCapacity();
    if (!d || !f) return -1;
    return (int)std::lround(100.0 * f / d);
}

void Battery::SetDemo(bool on) { g_demo = on; }
bool Battery::Demo() { return g_demo; }

void Battery::Read(PowerSnapshot& out, bool details) {
    out = PowerSnapshot{};
    if (g_demo) {
        Simulate(out);
        return;
    }
    SYSTEM_POWER_STATUS sps{};
    if (GetSystemPowerStatus(&sps)) {
        out.onAc = sps.ACLineStatus == 1;
        out.hasBattery = !(sps.BatteryFlag & 128) && sps.BatteryFlag != 255;
        out.percent = sps.BatteryLifePercent <= 100 ? sps.BatteryLifePercent : -1;
        out.charging = (sps.BatteryFlag & 8) != 0;
        out.saver = sps.SystemStatusFlag == 1;
        out.secondsLeft = sps.BatteryLifeTime == (DWORD)-1 ? -1 : (int)sps.BatteryLifeTime;
    }
    if (details && out.hasBattery) ReadDetails(out.batteries);
    // Windows does not always set the charging flag; the driver's rate tells.
    if (!out.charging && out.onAc && out.RateKnown() && out.Rate() > 0) out.charging = true;
    out.full = out.onAc && !out.charging && out.percent >= 95;
    if (out.onAc) out.secondsLeft = -1;  // only meaningful on battery
}
