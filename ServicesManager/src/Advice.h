// Advice.h - "can this service be disabled?" advice, downloaded from a list
// kept in the project's GitHub repository (data/service-advice.tsv), so the
// advice can be improved without releasing a new version of the program.
//
// The list is fetched in the background at most once per session (when
// details are first shown) and saved under %LOCALAPPDATA%, so the last
// downloaded copy is still shown when the PC is offline.
#pragma once
#include "Common.h"

enum class Verdict { Unknown, Safe, Caution, Keep };

struct AdviceEntry {
    Verdict verdict = Verdict::Unknown;
    std::wstring text;
};

namespace Advice {
enum class Source { None, Loading, Online, Cached, Offline, Disabled };

// Starts a download if none has run this session (or `force`). When it
// finishes, `msg` is posted to `notify` (if the window still exists).
void Fetch(HWND notify, UINT msg, bool force = false);
void SetEnabled(bool enabled);  // the "look up online" setting

Source State();
std::wstring Updated();       // date the list was last updated
std::wstring CachedOn();      // when the saved copy was downloaded
bool Lookup(const std::wstring& serviceName, AdviceEntry& out);

const wchar_t* VerdictTitle(Verdict v);  // "Safe to disable", ...
// One paragraph for the details window, e.g. "Usually safe to disable: ...".
// `sourceNote` adds "(showing the saved copy …)" when offline.
std::wstring Describe(const std::wstring& serviceName, bool sourceNote = true);
// Just that note ("" when the advice is current).
std::wstring SourceNote();
}  // namespace Advice
