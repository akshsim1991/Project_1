// Battery.h - reading the battery: Windows' power status, and details from
// the battery driver (capacity, design capacity, cycle count, voltage,
// charge/discharge rate, chemistry, temperature).
#pragma once
#include "Common.h"

struct BatteryDetails {
    std::wstring name, manufacturer, serial, chemistry, manufactureDate;
    unsigned designCapacity = 0;   // mWh (0 = unknown)
    unsigned fullCapacity = 0;     // mWh, what a full charge holds now
    unsigned currentCapacity = 0;  // mWh
    unsigned voltage = 0;          // mV
    int rate = 0;                  // mW: + charging, - discharging
    bool rateKnown = false;
    int cycleCount = -1;           // -1 = not reported
    double temperature = NAN;      // degrees C (NaN = not reported)
    bool relative = false;         // capacities are percentages, not mWh
};

struct PowerSnapshot {
    bool hasBattery = false;
    bool onAc = false;
    int percent = -1;              // -1 = unknown
    bool charging = false;
    bool full = false;             // on AC and 100% (or not charging at the top)
    bool saver = false;            // battery saver is on
    int secondsLeft = -1;          // Windows' estimate on battery (-1 = unknown)
    std::vector<BatteryDetails> batteries;

    // Totals over all batteries.
    unsigned DesignCapacity() const;
    unsigned FullCapacity() const;
    unsigned CurrentCapacity() const;
    bool RateKnown() const;
    int Rate() const;              // mW
    int HealthPercent() const;     // full / design (-1 = unknown)
};

namespace Battery {
// Reads everything. In demo mode (for testing without a battery) the
// values are simulated.
void Read(PowerSnapshot& out, bool details);
void SetDemo(bool on);
bool Demo();
}  // namespace Battery
