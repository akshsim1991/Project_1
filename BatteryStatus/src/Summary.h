// Summary.h - turns the readings into words and numbers for the window,
// the tray tooltip and "Copy summary": state, estimates, advice, health
// and session details.
#pragma once
#include "Battery.h"
#include "History.h"
#include "Settings.h"

enum Tone : int { kNormal, kGood, kWarn, kBad };

struct InfoRow {
    std::wstring label, value;
    int tone = kNormal;
};

struct Summary {
    bool hasBattery = false;
    int percent = -1;
    bool onAc = false, charging = false;
    std::wstring state;     // "Charging", "On battery", ...
    std::wstring estimate;  // "1 h 05 min until full", "About 3 h 20 min left"
    std::wstring advice;
    int adviceTone = kNormal;
    std::vector<InfoRow> health, session;

    std::wstring Tooltip() const;  // at most 127 characters
    std::wstring Text() const;     // everything, for the clipboard
};

Summary Summarize(const PowerSnapshot& s, const History& h, const Settings& set, int64_t now, int64_t pausedUntil);
