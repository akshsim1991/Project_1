// Recovery.cpp - MainWindow: keeping unsaved changes safe from a crash.
//
// Every few seconds, each document with unsaved changes that have not been
// copied yet gets a recovery copy: the worker writes it as it would be
// saved (with its password protection, if any) into
// %LOCALAPPDATA%\FeatherPDF\Recovery. A list per window,
// "session-<id>.txt" (one "original file \t copy \t page" line per
// document, UTF-8), says what the copies ("<id>-<n>.pdf") belong to. The
// window holds "session-<id>.lock" open while it runs; Windows lets go of
// it however the program ends, so a list whose lock is free belongs to a
// window that is gone. (Process numbers alone are not enough: a new
// process may get the number of one that crashed.)
//
// Saving a document, undoing back to its saved state, closing its tab and
// quitting normally all remove the copies. When Feather PDF starts and
// finds the list of a window that is gone (a crash, a power cut, Windows
// restarting), it offers to reopen those documents with their changes.
// They then belong to their own files again: Save writes there.
#include "MainWindow.h"

#include <shlobj.h>

#include "Util.h"

namespace {
std::wstring RecoveryFolder() {
    PWSTR base = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base)) && base) dir = base;
    CoTaskMemFree(base);
    if (dir.empty()) return {};
    dir += L"\\FeatherPDF";
    CreateDirectoryW(dir.c_str(), nullptr);
    dir += L"\\Recovery";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring ListPath(const std::wstring& dir, const std::wstring& id) {
    return dir + L"\\session-" + id + L".txt";
}

std::wstring LockPath(const std::wstring& dir, const std::wstring& id) {
    return dir + L"\\session-" + id + L".lock";
}

// Is the window of session `id` still running (its lock still held)?
bool SessionRunning(const std::wstring& dir, const std::wstring& id) {
    HANDLE h = CreateFileW(LockPath(dir, id).c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
        return false;
    }
    return GetLastError() == ERROR_SHARING_VIOLATION;
}

bool ReadAll(const std::wstring& path, std::string& out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0,
                           nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(f, &size) && size.QuadPart < (1 << 20);
    if (ok) {
        out.resize((size_t)size.QuadPart);
        DWORD read = 0;
        ok = out.empty() || (ReadFile(f, out.data(), (DWORD)out.size(), &read, nullptr) && read == out.size());
    }
    CloseHandle(f);
    return ok;
}

bool WriteAll(const std::wstring& path, const std::string& data) {
    const std::wstring temp = path + L".tmp";
    HANDLE f = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(f, data.data(), (DWORD)data.size(), &written, nullptr) && written == data.size();
    ok = FlushFileBuffers(f) && ok;
    CloseHandle(f);
    ok = ok && MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) DeleteFileW(temp.c_str());
    return ok;
}

struct Entry {
    std::wstring original, copy;
    int page = 0;
};

std::vector<Entry> ReadList(const std::wstring& path) {
    std::vector<Entry> out;
    std::string data;
    if (!ReadAll(path, data)) return out;
    size_t pos = 0;
    while (pos < data.size()) {
        size_t end = data.find('\n', pos);
        if (end == std::string::npos) end = data.size();
        std::string line = data.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t a = line.find('\t'), b = a == std::string::npos ? a : line.find('\t', a + 1);
        if (b == std::string::npos) continue;
        Entry e;
        e.original = Utf8ToWide(line.substr(0, a));
        e.copy = Utf8ToWide(line.substr(a + 1, b - a - 1));
        e.page = atoi(line.substr(b + 1).c_str());
        if (!e.original.empty() && FileExists(e.copy)) out.push_back(e);
    }
    return out;
}

std::wstring Absolute(const std::wstring& path) {
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetFullPathNameW(path.c_str(), MAX_PATH * 4, buf, nullptr);
    return n && n < MAX_PATH * 4 ? std::wstring(buf, n) : path;
}

// The session a copy belongs to: its name without "-<n>.pdf".
std::wstring CopyOwner(const std::wstring& name) {
    const size_t dash = name.rfind(L'-');
    return dash == std::wstring::npos ? std::wstring() : name.substr(0, dash);
}
}  // namespace

void MainWindow::InitRecovery() {
    m_recoveryDir = RecoveryFolder();
    if (m_recoveryDir.empty()) return;
    wchar_t id[64];
    swprintf_s(id, L"%lu-%llx", GetCurrentProcessId(), GetTickCount64());
    m_recoveryId = id;
    m_recoveryLock = CreateFileW(LockPath(m_recoveryDir, m_recoveryId).c_str(), GENERIC_WRITE, 0, nullptr,
                                 CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    SetTimer(m_hwnd, kAutosaveTimer, 10000, nullptr);

    // Lists of windows that are gone.
    std::vector<Entry> found;
    std::vector<std::wstring> lists;
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((m_recoveryDir + L"\\session-*.txt").c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            std::wstring session = fd.cFileName + 8;  // after "session-"
            if (session.size() <= 4) continue;
            session.resize(session.size() - 4);  // without ".txt"
            if (session == m_recoveryId || SessionRunning(m_recoveryDir, session)) continue;
            const std::wstring list = m_recoveryDir + L"\\" + fd.cFileName;
            for (const Entry& e : ReadList(list)) found.push_back(e);
            lists.push_back(list);
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    for (const auto& list : lists) DeleteFileW(list.c_str());

    std::vector<std::wstring> keep;  // copies that are reopened
    if (!found.empty()) {
        std::wstring names;
        for (const Entry& e : found) names += L"\x2022 " + FileNameFromPath(e.original) + L"\n";
        const std::wstring msg =
            L"Feather PDF did not close normally last time (the program or the PC stopped). These documents "
            L"had changes that were not saved:\n\n" +
            names +
            L"\nOpen them with those changes? You can then save them, or close them without saving.\n\n"
            L"Choose No to throw the changes away.";
        if (MessageBoxW(m_hwnd, msg.c_str(), APP_NAME, MB_YESNO | MB_ICONWARNING) == IDYES) {
            for (const Entry& e : found) {
                OpenFile(e.copy, e.page, {}, -1, false);
                for (auto& t : m_tabs) {
                    if (t->pendingDocId && SamePath(t->path, Absolute(e.copy))) {
                        t->recoveredFrom = e.original;
                        t->recoveryBase = Absolute(e.copy);
                        keep.push_back(t->recoveryBase);
                    }
                }
            }
        }
    }

    // Copies nobody needs any more: those of windows that are gone (unless
    // just reopened), and half-written ones.
    find = FindFirstFileW((m_recoveryDir + L"\\*.*").c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            const std::wstring name = fd.cFileName;
            if (name.rfind(L"session-", 0) == 0) continue;
            const std::wstring path = m_recoveryDir + L"\\" + name;
            const std::wstring owner = CopyOwner(name);
            bool needed = !owner.empty() && (owner == m_recoveryId || SessionRunning(m_recoveryDir, owner));
            for (const auto& k : keep) needed = needed || SamePath(k, path);
            if (!needed) DeleteFileW(path.c_str());
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
}

std::vector<std::wstring> MainWindow::RecoveredOriginals() const {
    std::vector<std::wstring> out;
    for (const auto& t : m_tabs)
        if (!t->recoveredFrom.empty()) out.push_back(t->recoveredFrom);
    return out;
}

void MainWindow::AutosaveTick() {
    if (m_recoveryDir.empty()) return;
    for (auto& t : m_tabs) {
        if (!t->docId || t->pendingDocId || !t->dirty || !t->autosavingTo.empty() || t->docId == t->autosavedDocId)
            continue;
        const std::wstring path =
            m_recoveryDir + L"\\" + m_recoveryId + L"-" + std::to_wstring(m_recoverySerial++) + L".pdf";
        t->autosavingTo = path;
        m_worker.Autosave(t->docId, path);
    }
}

void MainWindow::OnAutosaved(AutosaveResult* result) {
    std::unique_ptr<AutosaveResult> res(result);
    Tab* tab = nullptr;
    for (auto& t : m_tabs)
        if (SamePath(t->autosavingTo, res->path)) tab = t.get();
    if (!tab) {  // the tab was closed meanwhile
        DeleteFileW(res->path.c_str());
        return;
    }
    tab->autosavingTo.clear();
    if (!res->ok || !tab->dirty) {
        DeleteFileW(res->path.c_str());
        if (!res->ok) tab->autosavedDocId = tab->docId;  // not again until it changes
        return;
    }
    if (!tab->recoveryFile.empty() && !SamePath(tab->recoveryFile, tab->recoveryBase))
        DeleteFileW(tab->recoveryFile.c_str());
    tab->recoveryFile = res->path;
    tab->autosavedDocId = res->docId;
    WriteRecoveryList();
}

void MainWindow::ForgetRecovery(Tab& tab, bool closing) {
    bool changed = false;
    if (!tab.recoveryFile.empty()) {
        if (!SamePath(tab.recoveryFile, tab.recoveryBase)) DeleteFileW(tab.recoveryFile.c_str());
        tab.recoveryFile.clear();
        changed = true;
    }
    tab.autosavedDocId = 0;
    // The copy a recovered document was opened from is what undo replays
    // from, so it stays until the tab is closed.
    if (closing && !tab.recoveryBase.empty()) {
        DeleteFileW(tab.recoveryBase.c_str());
        tab.recoveryBase.clear();
    }
    if (changed) WriteRecoveryList();
}

void MainWindow::WriteRecoveryList() {
    if (m_recoveryDir.empty()) return;
    std::string data;
    for (const auto& t : m_tabs) {
        if (t->recoveryFile.empty()) continue;
        data += WideToUtf8(t->path) + "\t" + WideToUtf8(t->recoveryFile) + "\t" +
                std::to_string(t->view->HasDocument() ? t->view->CurrentPage() : 0) + "\n";
    }
    const std::wstring list = ListPath(m_recoveryDir, m_recoveryId);
    if (data.empty())
        DeleteFileW(list.c_str());
    else
        WriteAll(list, data);
}

void MainWindow::EndRecovery() {
    if (m_recoveryDir.empty()) return;
    KillTimer(m_hwnd, kAutosaveTimer);
    for (auto& t : m_tabs) ForgetRecovery(*t, true);
    DeleteFileW(ListPath(m_recoveryDir, m_recoveryId).c_str());
    if (m_recoveryLock != INVALID_HANDLE_VALUE) CloseHandle(m_recoveryLock);  // also deletes the lock
    m_recoveryLock = INVALID_HANDLE_VALUE;
}
