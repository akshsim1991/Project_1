// Summary.cpp - estimates and wording.
#include "Summary.h"

#include <algorithm>

namespace {
std::wstring Wh(unsigned mWh) {
    wchar_t b[32];
    swprintf_s(b, L"%.1f Wh", mWh / 1000.0);
    return b;
}

std::wstring Watts(int mW) {
    wchar_t b[32];
    swprintf_s(b, L"%.1f W", std::abs(mW) / 1000.0);
    return b;
}

std::wstring PerHour(double v) {
    wchar_t b[32];
    swprintf_s(b, L"%.0f%% per hour", std::fabs(v));
    return b;
}

// Percent at a moment, from the history (nearest sample at or after t).
int PercentAt(const History& h, int64_t t) {
    for (const Sample& s : h.Samples())
        if (s.time >= t) return s.percent;
    return -1;
}
}  // namespace

Summary Summarize(const PowerSnapshot& s, const History& h, const Settings& set, int64_t now, int64_t pausedUntil) {
    Summary m;
    m.hasBattery = s.hasBattery;
    m.percent = s.percent;
    m.onAc = s.onAc;
    m.charging = s.charging;
    if (!s.hasBattery) {
        m.state = L"No battery";
        m.estimate = L"This PC runs on mains power, or Windows does not report a battery.";
        return m;
    }
    const int pct = std::max(0, s.percent);
    const double speed = h.PercentPerHour(now, 30 * 60);  // + charging, - discharging
    const unsigned full = s.FullCapacity(), cur = s.CurrentCapacity();
    const int rate = s.RateKnown() ? s.Rate() : 0;

    // State and estimate.
    if (s.onAc) {
        if (s.charging) {
            m.state = L"Charging";
            int64_t secs = -1;
            if (rate > 0 && full > cur) secs = (int64_t)((full - cur) * 3600.0 / rate);
            else if (speed > 0) secs = (int64_t)((100 - pct) / speed * 3600);
            if (secs >= 0 && secs < 48 * 3600) {
                m.estimate = L"About " + FormatDuration(secs) + L" until full";
                if (set.fullAlert && set.fullPercent < 100 && pct < set.fullPercent) {
                    int64_t toLimit = -1;
                    if (rate > 0 && full) toLimit = (int64_t)((full * set.fullPercent / 100.0 - cur) * 3600.0 / rate);
                    else if (speed > 0) toLimit = (int64_t)((set.fullPercent - pct) / speed * 3600);
                    if (toLimit > 0) m.estimate += L" (" + std::to_wstring(set.fullPercent) + L"% in " + FormatDuration(toLimit) + L")";
                }
            } else {
                m.estimate = L"Working out the time until full\x2026";
            }
        } else {
            m.state = s.full || pct >= 99 ? L"Fully charged" : L"Plugged in, not charging";
            m.estimate = pct >= 99 ? L"Running on the charger." :
                                     L"Windows or the laptop is holding the charge (battery care or a charge limit).";
        }
    } else {
        m.state = s.saver ? L"On battery \x00B7 battery saver on" : L"On battery";
        int64_t secs = -1;
        if (rate < 0 && cur) secs = (int64_t)(cur * 3600.0 / -rate);
        else if (s.secondsLeft > 0) secs = s.secondsLeft;
        else if (speed < 0) secs = (int64_t)(pct / -speed * 3600);
        m.estimate = secs > 0 && secs < 72 * 3600 ? L"About " + FormatDuration(secs) + L" left"
                                                   : L"Working out the time left\x2026";
    }

    // Advice.
    if (!s.onAc && pct <= set.criticalPercent) {
        m.advice = L"Battery critically low. Plug in the charger now.";
        m.adviceTone = kBad;
    } else if (!s.onAc && pct <= set.lowPercent) {
        m.advice = L"Battery is low. Plug in the charger.";
        m.adviceTone = kBad;
    } else if (s.onAc && pct >= set.fullPercent) {
        m.advice = pct >= 99 ? L"Battery is fully charged. You can unplug the charger."
                             : L"Battery reached " + std::to_wstring(set.fullPercent) + L"%. You can unplug the charger.";
        m.adviceTone = kGood;
    }
    if (pausedUntil > now) {
        if (!m.advice.empty()) m.advice += L"  ";
        m.advice += L"(Alerts paused until " + FormatClock(pausedUntil) + L".)";
        if (m.adviceTone == kNormal) m.adviceTone = kWarn;
    }

    // Health card.
    const int health = s.HealthPercent();
    if (health >= 0) {
        InfoRow r{L"Health", std::to_wstring(health) + L"%"};
        if (health >= 80) {
            r.value += L" \x2014 good";
            r.tone = kGood;
        } else if (health >= 60) {
            r.value += L" \x2014 worn";
            r.tone = kWarn;
        } else {
            r.value += L" \x2014 replace soon";
            r.tone = kBad;
        }
        m.health.push_back(r);
        m.health.push_back({L"A full charge holds", Wh(full) + L"  (new: " + Wh(s.DesignCapacity()) + L")"});
    } else {
        m.health.push_back({L"Health", L"Not reported by this battery"});
    }
    if (cur) m.health.push_back({L"Energy now", Wh(cur)});
    int cycles = -1;
    double temp = NAN;
    unsigned volts = 0;
    for (const auto& b : s.batteries) {
        if (b.cycleCount >= 0) cycles = std::max(cycles, 0) + b.cycleCount;
        if (!std::isnan(b.temperature)) temp = b.temperature;
        if (b.voltage) volts = b.voltage;
    }
    if (cycles >= 0) m.health.push_back({L"Charge cycles", std::to_wstring(cycles)});
    if (s.RateKnown()) {
        std::wstring v = rate > 0 ? L"Charging at " + Watts(rate) : rate < 0 ? L"Using " + Watts(rate) : L"0 W";
        m.health.push_back({L"Power", v});
    }
    if (volts) {
        wchar_t b[32];
        swprintf_s(b, L"%.2f V", volts / 1000.0);
        m.health.push_back({L"Voltage", b});
    }
    if (!std::isnan(temp)) {
        wchar_t b[32];
        swprintf_s(b, L"%.0f \x00B0" L"C", temp);
        m.health.push_back({L"Temperature", b, temp >= 45 ? kWarn : kNormal});
    }
    if (!s.batteries.empty()) {
        const BatteryDetails& b = s.batteries[0];
        std::wstring model = b.name;
        if (!b.manufacturer.empty()) model += (model.empty() ? L"" : L"  \x00B7  ") + b.manufacturer;
        if (!model.empty()) m.health.push_back({L"Model", model});
        if (!b.chemistry.empty()) m.health.push_back({L"Type", b.chemistry});
        if (!b.manufactureDate.empty()) m.health.push_back({L"Made", b.manufactureDate});
        if (s.batteries.size() > 1) m.health.push_back({L"Batteries", std::to_wstring(s.batteries.size()) + L" (totals shown)"});
    }

    // Session card.
    const int64_t pluggedAt = h.LastChange(true), unpluggedAt = h.LastChange(false);
    if (s.onAc) {
        if (pluggedAt) {
            m.session.push_back({s.charging ? L"Charging since" : L"Plugged in since",
                                 FormatClock(pluggedAt) + L"  (" + FormatDuration(now - pluggedAt) + L")"});
            const int start = PercentAt(h, pluggedAt);
            if (start >= 0) {
                const int gained = pct - start;
                m.session.push_back({L"Charged since plugged in", (gained >= 0 ? L"+" : L"") + std::to_wstring(gained) + L"%"});
            }
        }
        if (s.charging && speed > 0) m.session.push_back({L"Charging speed", PerHour(speed)});
        if (unpluggedAt) m.session.push_back({L"Last unplugged", FormatClock(unpluggedAt)});
    } else {
        if (unpluggedAt) {
            m.session.push_back({L"On battery since", FormatClock(unpluggedAt) + L"  (" + FormatDuration(now - unpluggedAt) + L")"});
            const int start = PercentAt(h, unpluggedAt);
            if (start >= 0) m.session.push_back({L"Used since unplugged", std::to_wstring(std::max(0, start - pct)) + L"%"});
        }
        if (speed < 0) m.session.push_back({L"Drain speed", PerHour(speed)});
        if (s.secondsLeft > 0) m.session.push_back({L"Windows' estimate", FormatDuration(s.secondsLeft) + L" left"});
        if (pluggedAt) m.session.push_back({L"Last plugged in", FormatClock(pluggedAt)});
    }
    const double avg = h.AverageDrain();
    if (!std::isnan(avg) && avg > 0.5)
        m.session.push_back({L"A full battery lasts", L"About " + FormatDuration((int64_t)(100 / avg * 3600)) + L" (your usage)"});
    m.session.push_back({L"Battery saver", s.saver ? L"On" : L"Off"});
    return m;
}

std::wstring Summary::Tooltip() const {
    if (!hasBattery) return APP_NAME L"\nNo battery";
    std::wstring t = std::to_wstring(percent) + L"% \x00B7 " + state;
    std::wstring e = estimate;
    if (e.size() > 60 || e.find(L"Working") == 0 || e.find(L"Windows or") == 0 || e.find(L"Running") == 0) e.clear();
    std::wstring tip = APP_NAME L"\n" + t + (e.empty() ? L"" : L"\n" + e);
    if (tip.size() > 127) tip.resize(127);
    return tip;
}

std::wstring Summary::Text() const {
    std::wstring t = APP_NAME L"\r\n";
    if (!hasBattery) return t + state + L"\r\n" + estimate + L"\r\n";
    t += std::to_wstring(percent) + L"%  " + state + L"\r\n" + estimate + L"\r\n";
    if (!advice.empty()) t += advice + L"\r\n";
    t += L"\r\nBattery\r\n";
    for (const InfoRow& r : health) t += L"  " + r.label + L": " + r.value + L"\r\n";
    t += L"\r\nThis session\r\n";
    for (const InfoRow& r : session) t += L"  " + r.label + L": " + r.value + L"\r\n";
    return t;
}
