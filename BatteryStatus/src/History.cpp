// History.cpp - storing and querying the battery history.
#include "History.h"

#include <algorithm>
#include <ctime>

#include <shlobj.h>

namespace {
std::wstring DataFolder() {
    PWSTR p = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) dir = p;
    CoTaskMemFree(p);
    if (dir.empty()) return {};
    dir += L"\\BatteryStatus";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::string Line(const Sample& s) {
    char b[96];
    snprintf(b, sizeof(b), "%lld,%d,%d,%d,%d\n", (long long)s.time, s.percent, s.ac ? 1 : 0, s.charging ? 1 : 0, s.rate);
    return b;
}

bool WriteAll(const std::wstring& path, const std::string& data, bool append) {
    HANDLE f = CreateFileW(path.c_str(), append ? FILE_APPEND_DATA : GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           append ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    const bool ok = WriteFile(f, data.data(), (DWORD)data.size(), &w, nullptr) && w == data.size();
    CloseHandle(f);
    return ok;
}
}  // namespace

int64_t History::Now() { return (int64_t)time(nullptr); }

void History::Load(int keepDays, bool demo) {
    m_samples.clear();
    m_demo = demo;
    const int64_t now = Now();
    if (demo) {
        // A made-up day: discharging during work, charging at lunch and at night.
        int64_t t = now - 26 * 3600;
        double pct = 100;
        bool ac = false;
        while (t < now) {
            const int hour = (int)((t / 3600) % 24);
            ac = hour == 12 || hour >= 19 || hour < 1;
            pct += ac ? 0.55 : -0.24;
            pct = std::min(100.0, std::max(6.0, pct));
            Sample s;
            s.time = t;
            s.percent = (int)std::lround(pct);
            s.ac = ac;
            s.charging = ac && s.percent < 100;
            s.rate = ac ? (s.charging ? 30000 : 0) : -9000;
            m_samples.push_back(s);
            t += 60;
        }
        return;
    }
    const std::wstring dir = DataFolder();
    if (dir.empty()) return;
    m_path = dir + L"\\history.csv";
    HANDLE f = CreateFileW(m_path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size{};
    GetFileSizeEx(f, &size);
    std::string data((size_t)std::min<LONGLONG>(size.QuadPart, 64ll << 20), '\0');
    DWORD got = 0;
    if (!data.empty()) ReadFile(f, data.data(), (DWORD)data.size(), &got, nullptr);
    CloseHandle(f);
    data.resize(got);
    const int64_t oldest = now - (int64_t)keepDays * 86400;
    size_t pos = 0;
    bool dropped = false;
    while (pos < data.size()) {
        size_t end = data.find('\n', pos);
        if (end == std::string::npos) end = data.size();
        long long t = 0;
        int p = 0, ac = 0, ch = 0, rate = 0;
        if (sscanf_s(data.c_str() + pos, "%lld,%d,%d,%d,%d", &t, &p, &ac, &ch, &rate) >= 4 && t > 0 && p >= 0 && p <= 100) {
            if (t >= oldest && t <= now + 300) {
                Sample s{t, p, ac != 0, ch != 0, rate};
                m_samples.push_back(s);
            } else {
                dropped = true;
            }
        }
        pos = end + 1;
    }
    std::sort(m_samples.begin(), m_samples.end(), [](const Sample& a, const Sample& b) { return a.time < b.time; });
    if (dropped) {
        // Rewrite without the old lines so the file does not grow forever.
        std::string out;
        for (const Sample& s : m_samples) out += Line(s);
        WriteAll(m_path, out, false);
    }
}

void History::Append(const Sample& s) {
    m_samples.push_back(s);
    if (!m_demo && !m_path.empty()) WriteAll(m_path, Line(s), true);
}

void History::Record(const Sample& s) {
    if (m_samples.empty()) {
        Append(s);
        return;
    }
    const Sample& last = m_samples.back();
    if (s.time - last.time >= 60 || s.percent != last.percent || s.ac != last.ac || s.charging != last.charging)
        Append(s);
}

double History::PercentPerHour(int64_t now, int windowSec) const {
    if (m_samples.size() < 2) return NAN;
    const Sample& last = m_samples.back();
    // Walk back while the state is the same and within the window.
    size_t i = m_samples.size() - 1;
    while (i > 0) {
        const Sample& prev = m_samples[i - 1];
        if (prev.ac != last.ac || prev.time < now - windowSec || last.time - prev.time > 3 * 3600) break;
        if (m_samples[i].time - prev.time > 30 * 60) break;  // a gap (sleep): start after it
        --i;
    }
    const Sample& first = m_samples[i];
    const double hours = (last.time - first.time) / 3600.0;
    const int dp = last.percent - first.percent;
    if (hours < 3.0 / 60 || std::abs(dp) < 2) return NAN;
    return dp / hours;
}

int64_t History::LastChange(bool toAc) const {
    for (size_t i = m_samples.size(); i-- > 1;)
        if (m_samples[i].ac == toAc && m_samples[i - 1].ac != toAc) return m_samples[i].time;
    return 0;
}

double History::AverageDrain() const {
    double drop = 0, hours = 0;
    for (size_t i = 1; i < m_samples.size(); ++i) {
        const Sample& a = m_samples[i - 1];
        const Sample& b = m_samples[i];
        const int64_t dt = b.time - a.time;
        if (a.ac || b.ac || dt <= 0 || dt > 30 * 60) continue;  // skip charging and sleep gaps
        drop += a.percent - b.percent;
        hours += dt / 3600.0;
    }
    if (hours < 0.25 || drop <= 0) return NAN;
    return drop / hours;
}

bool History::Export(const std::wstring& path) const {
    std::string out = "Time,Battery (%),On charger,Charging,Rate (W)\r\n";
    for (const Sample& s : m_samples) {
        const time_t t = (time_t)s.time;
        tm local{};
        localtime_s(&local, &t);
        char line[128];
        strftime(line, sizeof(line), "%Y-%m-%d %H:%M:%S", &local);
        char rest[96];
        snprintf(rest, sizeof(rest), ",%d,%s,%s,%.2f\r\n", s.percent, s.ac ? "yes" : "no", s.charging ? "yes" : "no",
                 s.rate / 1000.0);
        out += std::string(line) + rest;
    }
    return WriteAll(path, "\xEF\xBB\xBF" + out, false);
}

std::wstring FormatClock(int64_t t) {
    if (t <= 0) return L"\x2013";
    const time_t tt = (time_t)t, now = time(nullptr);
    tm a{}, b{};
    localtime_s(&a, &tt);
    localtime_s(&b, &now);
    wchar_t buf[64];
    if (a.tm_yday == b.tm_yday && a.tm_year == b.tm_year) wcsftime(buf, 64, L"%H:%M", &a);
    else wcsftime(buf, 64, L"%a %d %b, %H:%M", &a);
    return buf;
}

std::wstring FormatDuration(int64_t s) {
    if (s < 0) return L"\x2013";
    if (s < 60) return L"less than a minute";
    const int64_t m = s / 60;
    if (m < 60) return std::to_wstring(m) + L" min";
    const int64_t h = m / 60, mm = m % 60;
    if (h >= 48) return std::to_wstring(h / 24) + L" days " + std::to_wstring(h % 24) + L" h";
    wchar_t b[32];
    swprintf_s(b, L"%lld h %02lld min", (long long)h, (long long)mm);
    return b;
}
