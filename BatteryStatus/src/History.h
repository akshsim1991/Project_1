// History.h - the battery level over time, kept in
// %LOCALAPPDATA%\BatteryStatus\history.csv (one line per minute, or when
// something changes), for the graph, the session times and the estimates.
#pragma once
#include "Common.h"

struct Sample {
    int64_t time = 0;   // seconds since 1970 (UTC)
    int percent = 0;
    bool ac = false;
    bool charging = false;
    int rate = 0;       // mW (0 = unknown)
};

class History {
public:
    // Reads the file and drops samples older than `keepDays`. In demo mode
    // nothing is read or written; a made-up day is shown instead.
    void Load(int keepDays, bool demo);
    // Adds a sample if a minute has passed or the level or power changed.
    void Record(const Sample& s);
    const std::vector<Sample>& Samples() const { return m_samples; }

    // Percent per hour over the last `windowSec` seconds of the current
    // state (charging or not). NaN if there is not enough data yet.
    double PercentPerHour(int64_t now, int windowSec) const;
    // When the power source last changed to `ac` (0 if not in the history).
    int64_t LastChange(bool toAc) const;
    // Average discharge speed (%/h) over every stretch on battery in the
    // history. NaN if unknown.
    double AverageDrain() const;

    std::wstring Path() const { return m_path; }
    bool Export(const std::wstring& path) const;

    static int64_t Now();

private:
    void Append(const Sample& s);
    std::vector<Sample> m_samples;
    std::wstring m_path;
    bool m_demo = false;
};

std::wstring FormatClock(int64_t t);                 // "14:05" (today) or "Mon 14:05"
std::wstring FormatDuration(int64_t seconds);        // "1 h 05 min", "12 min"
