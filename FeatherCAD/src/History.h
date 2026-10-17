// History.h - auto-save and version history.
//
// Each drawing has a folder under %LOCALAPPDATA%\FeatherCAD\History holding
// complete copies of the drawing: one every few minutes while it changes
// (auto-save) and one each time it is saved. Version history lists them and
// restores any of them; after a crash, Recover opens the newest copy of a
// drawing that was not saved.
#pragma once
#include "Common.h"

struct VersionInfo {
    std::wstring file;
    FILETIME time{};
    bool autosave = false;
    uint64_t bytes = 0;
};

struct HistoryDrawing {
    std::wstring folder;
    std::wstring source;  // the drawing's file, empty if it was never saved
    std::wstring title;   // shown name
    VersionInfo latest;
};

class History {
public:
    // Which drawing the next versions belong to. `path` empty: an unsaved
    // drawing (it gets a folder of its own).
    void SetDrawing(const std::wstring& path, const std::wstring& title);
    bool AddVersion(const std::string& text, bool autosave, int keep);
    std::vector<VersionInfo> Versions() const;  // newest first
    const std::wstring& Folder() const { return m_folder; }

    // Every drawing with versions, newest first (for Recover).
    static std::vector<HistoryDrawing> AllDrawings();
    // Deletes the folders of drawings not touched for `days` days.
    static void CleanOld(int days);

private:
    std::wstring m_folder, m_source, m_title;
    bool m_described = false;
};
