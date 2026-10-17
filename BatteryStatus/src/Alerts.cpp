// Alerts.cpp - alert rules.
#include "Alerts.h"

namespace {
// Fire now? `last` is 0 when the condition just started.
bool Due(int64_t last, int64_t now, int repeatMinutes) {
    if (last == 0) return true;
    return repeatMinutes > 0 && now - last >= (int64_t)repeatMinutes * 60;
}
}  // namespace

std::vector<AlertEvent> Alerts::Update(const PowerSnapshot& s, const Settings& set, int64_t now) {
    std::vector<AlertEvent> out;
    if (!s.hasBattery || s.percent < 0) {
        m_first = false;
        return out;
    }
    const bool paused = Paused(now);
    const int pct = s.percent;

    // Charger connected / disconnected.
    if (!m_first && s.onAc != m_lastAc && set.plugAlert && !paused) {
        AlertEvent e{s.onAc ? AlertEvent::Plugged : AlertEvent::Unplugged};
        e.title = s.onAc ? L"Charger connected" : L"Charger disconnected";
        e.text = L"The battery is at " + std::to_wstring(pct) + L"%." +
                 (s.onAc ? L"" : L" Now running on battery.");
        out.push_back(e);
    }
    m_first = false;
    m_lastAc = s.onAc;

    // Critical (takes priority over low).
    const bool critical = set.criticalAlert && !s.onAc && pct <= set.criticalPercent;
    if (s.onAc || pct > set.criticalPercent + 2) m_critical = 0;
    if (critical && Due(m_critical, now, set.repeatMinutes)) {
        if (!paused) {
            AlertEvent e{AlertEvent::Critical};
            e.title = L"Battery critically low: " + std::to_wstring(pct) + L"%";
            e.text = L"Plug in the charger now, or save your work. Windows may soon sleep or shut down.";
            e.urgent = true;
            out.push_back(e);
        }
        m_critical = now;
        m_low = now;  // no separate "low" message at the same time
    }

    // Low.
    if (s.onAc || pct > set.lowPercent + 2) m_low = 0;
    const bool low = set.lowAlert && !s.onAc && pct <= set.lowPercent && !critical;
    if (low && Due(m_low, now, set.repeatMinutes)) {
        if (!paused) {
            AlertEvent e{AlertEvent::Low};
            e.title = L"Battery low: " + std::to_wstring(pct) + L"%";
            e.text = L"Plug in the charger.";
            e.urgent = true;
            out.push_back(e);
        }
        m_low = now;
    }

    // Charged: unplug.
    if (!s.onAc || pct < set.fullPercent - 3) m_charged = 0;
    const bool charged = set.fullAlert && s.onAc && pct >= set.fullPercent;
    if (charged && Due(m_charged, now, set.repeatMinutes)) {
        if (!paused) {
            AlertEvent e{AlertEvent::Charged};
            e.title = pct >= 100 ? L"Battery fully charged" : L"Battery charged to " + std::to_wstring(pct) + L"%";
            e.text = L"You can unplug the charger.";
            out.push_back(e);
        }
        m_charged = now;
    }
    return out;
}
