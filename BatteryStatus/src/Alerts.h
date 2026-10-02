// Alerts.h - deciding when to warn: low, critical, charged (unplug),
// charger connected/disconnected. Each alert fires once when its level is
// crossed, then again every few minutes (if set) until something changes.
#pragma once
#include "Battery.h"
#include "Settings.h"

struct AlertEvent {
    enum Kind { Low, Critical, Charged, Plugged, Unplugged } kind;
    std::wstring title, text;
    bool urgent = false;  // low and critical: may also show a message box
};

class Alerts {
public:
    std::vector<AlertEvent> Update(const PowerSnapshot& s, const Settings& set, int64_t now);
    void PauseUntil(int64_t t) { m_pausedUntil = t; }
    bool Paused(int64_t now) const { return m_pausedUntil > now; }
    int64_t PausedUntil() const { return m_pausedUntil; }

private:
    bool m_first = true;
    bool m_lastAc = false;
    int64_t m_low = 0, m_critical = 0, m_charged = 0;  // when last shown (0 = not since reset)
    int64_t m_pausedUntil = 0;
};
