// Store.cpp - files, index and history operations.
#include "Store.h"

#include <algorithm>
#include <ctime>
#include <map>

#include <shlobj.h>

#include "Util.h"

namespace {
const wchar_t kIndexName[] = L"index.txt";
constexpr size_t kMemoryText = 1 << 20;  // characters kept in memory per item (search, preview)

std::string Utf8(const std::wstring& s) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)std::max(n, 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring FromUtf8(const char* p, size_t len) {
    if (len >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) {
        p += 3;
        len -= 3;
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, p, (int)len, nullptr, 0);
    std::wstring out((size_t)std::max(n, 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, p, (int)len, out.data(), n);
    return out;
}

bool WriteFileAll(const std::wstring& path, const std::string& data, bool hidden = false) {
    const std::wstring tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           hidden ? FILE_ATTRIBUTE_HIDDEN : FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    const bool ok = WriteFile(f, data.data(), (DWORD)data.size(), &w, nullptr) && w == data.size();
    CloseHandle(f);
    if (!ok || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    if (hidden) SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_HIDDEN);
    return true;
}

bool ReadFileAll(const std::wstring& path, std::string& out, size_t maxBytes = SIZE_MAX) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    GetFileSizeEx(f, &size);
    const size_t n = (size_t)std::min<unsigned long long>((unsigned long long)size.QuadPart, maxBytes);
    out.assign(n, '\0');
    DWORD got = 0;
    const bool ok = n == 0 || ReadFile(f, out.data(), (DWORD)n, &got, nullptr);
    CloseHandle(f);
    out.resize(got);
    return ok;
}

uint64_t FileSize(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a)) return 0;
    return ((uint64_t)a.nFileSizeHigh << 32) | a.nFileSizeLow;
}

bool CanWrite(const std::wstring& dir) {
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::wstring probe = dir + L"\\.write-test";
    HANDLE f = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    CloseHandle(f);
    return true;
}

wchar_t KindCode(ClipKind k) { return k == ClipKind::Image ? L'I' : k == ClipKind::Files ? L'F' : L'T'; }
ClipKind KindFrom(wchar_t c) { return c == L'I' ? ClipKind::Image : c == L'F' ? ClipKind::Files : ClipKind::Text; }

std::vector<std::wstring> Split(const std::wstring& s, wchar_t sep) {
    std::vector<std::wstring> out;
    size_t pos = 0;
    for (;;) {
        const size_t e = s.find(sep, pos);
        out.push_back(s.substr(pos, e == std::wstring::npos ? std::wstring::npos : e - pos));
        if (e == std::wstring::npos) break;
        pos = e + 1;
    }
    return out;
}

std::wstring Clean(std::wstring s) {
    for (wchar_t& c : s)
        if (c == L'\t' || c == L'\r' || c == L'\n') c = L' ';
    return s;
}

// PNG width/height from the IHDR chunk.
bool PngSize(const std::string& png, int& w, int& h) {
    if (png.size() < 24 || png.compare(1, 3, "PNG") != 0) return false;
    auto be = [&](size_t at) {
        return (int)(((unsigned char)png[at] << 24) | ((unsigned char)png[at + 1] << 16) |
                     ((unsigned char)png[at + 2] << 8) | (unsigned char)png[at + 3]);
    };
    w = be(16);
    h = be(20);
    return w > 0 && h > 0;
}
}  // namespace

// ===========================================================================
// Basics
// ===========================================================================
uint64_t Store::Hash(const void* data, size_t size, uint64_t h) {
    const unsigned char* p = (const unsigned char*)data;
    for (size_t i = 0; i < size; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

int64_t Store::NowMs() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    const uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (int64_t)(t / 10000 - 11644473600000ull);
}

int64_t Store::NewId() const {
    int64_t id = NowMs();
    if (!m_items.empty() && id <= m_items.front().id) id = m_items.front().id + 1;
    return id;
}

std::wstring Store::NewFileName(int64_t id, const wchar_t* suffix) const {
    const time_t t = (time_t)(id / 1000);
    tm local{};
    localtime_s(&local, &t);
    wchar_t b[64];
    wcsftime(b, 64, L"%Y-%m-%d %H.%M.%S", &local);
    wchar_t ms[8];
    swprintf_s(ms, L".%03d", (int)(id % 1000));
    std::wstring base = std::wstring(b) + ms;
    std::wstring name = base + suffix;
    for (int k = 2; FileExists(m_folder + L"\\" + name); ++k) name = base + L"-" + std::to_wstring(k) + suffix;
    return name;
}

int Store::IndexOf(int64_t id) const {
    for (size_t i = 0; i < m_items.size(); ++i)
        if (m_items[i].id == id) return (int)i;
    return -1;
}

uint64_t Store::TotalBytes() const {
    uint64_t t = 0;
    for (const ClipItem& it : m_items) t += it.bytes;
    return t;
}

// ===========================================================================
// Loading and saving
// ===========================================================================
bool Store::LoadText(ClipItem& it) const {
    std::string data;
    // Up to 4 bytes per character in UTF-8; read a little more than needed.
    if (!ReadFileAll(PathOf(it), data, kMemoryText * 4 + 4)) return false;
    it.text = FromUtf8(data.data(), data.size());
    it.partial = it.bytes > data.size();
    if (it.text.size() > kMemoryText) {
        it.text.resize(kMemoryText);
        it.partial = true;
    }
    it.length = it.text.size();  // for very long texts: the part in memory
    return true;
}

bool Store::Open(std::wstring& note) {
    note.clear();
    m_items.clear();
    m_folder = DirectoryFromPath(ExecutablePath()) + L"\\" APP_FOLDER_NAME;
    if (!CanWrite(m_folder)) {
        // For example when installed in Program Files.
        PWSTR p = nullptr;
        std::wstring dir;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) dir = p;
        CoTaskMemFree(p);
        dir += L"\\ClipboardManager";
        CreateDirectoryW(dir.c_str(), nullptr);
        const std::wstring wanted = m_folder;
        m_folder = dir + L"\\" APP_FOLDER_NAME;
        if (!CanWrite(m_folder)) {
            note = L"The history can not be saved: no folder is writable.";
            return false;
        }
        note = L"The program's folder is read-only, so the history is kept in " + m_folder + L" instead.";
        (void)wanted;
    }

    // The index: order, pins, sources.
    std::map<std::wstring, size_t> byFile;
    std::string data;
    if (ReadFileAll(m_folder + L"\\" + kIndexName, data)) {
        const std::wstring text = FromUtf8(data.data(), data.size());
        for (const std::wstring& line : Split(text, L'\n')) {
            std::wstring l = line;
            if (!l.empty() && l.back() == L'\r') l.pop_back();
            const auto f = Split(l, L'\t');
            if (f.size() < 8 || f[0].empty() || f[0][0] == L'#') continue;
            ClipItem it;
            it.id = _wtoi64(f[0].c_str());
            it.kind = KindFrom(f[1].empty() ? L'T' : f[1][0]);
            it.pinned = f[2] == L"1";
            it.file = f[3];
            it.source = f[4];
            it.hash = wcstoull(f[5].c_str(), nullptr, 16);
            it.width = _wtoi(f[6].c_str());
            it.height = _wtoi(f[7].c_str());
            if (it.id <= 0 || it.file.empty() || it.file.find_first_of(L"\\/:") != std::wstring::npos) continue;
            if (byFile.count(it.file) || !FileExists(PathOf(it))) continue;  // deleted by hand
            byFile[it.file] = m_items.size();
            m_items.push_back(std::move(it));
        }
    }

    // Files without an index entry (added by hand, or the index was lost).
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((m_folder + L"\\*").c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            const std::wstring name = fd.cFileName;
            if (_wcsicmp(name.c_str(), kIndexName) == 0 || byFile.count(name)) continue;
            const std::wstring low = ToLower(name);
            const bool png = low.size() > 4 && low.compare(low.size() - 4, 4, L".png") == 0;
            const bool txt = low.size() > 4 && low.compare(low.size() - 4, 4, L".txt") == 0;
            if (!png && !txt) continue;
            ClipItem it;
            it.file = name;
            it.kind = png ? ClipKind::Image : (low.find(L" files.txt") != std::wstring::npos ? ClipKind::Files : ClipKind::Text);
            const uint64_t t = ((uint64_t)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
            it.id = (int64_t)(t / 10000 - 11644473600000ull);
            byFile[name] = m_items.size();
            m_items.push_back(std::move(it));
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }

    // Unique ids, newest first.
    std::stable_sort(m_items.begin(), m_items.end(), [](const ClipItem& a, const ClipItem& b) { return a.id > b.id; });
    for (size_t i = 1; i < m_items.size(); ++i)
        if (m_items[i].id >= m_items[i - 1].id) m_items[i].id = m_items[i - 1].id - 1;

    // Sizes, texts, picture sizes and hashes.
    for (ClipItem& it : m_items) {
        it.bytes = FileSize(PathOf(it));
        if (it.kind == ClipKind::Image) {
            if (!it.width || !it.height) {
                std::string head;
                if (ReadFileAll(PathOf(it), head, 64)) PngSize(head, it.width, it.height);
            }
        } else {
            LoadText(it);
            if (!it.hash) it.hash = Hash(it.text.data(), it.text.size() * sizeof(wchar_t));
        }
    }
    SaveIndex();
    return true;
}

void Store::SaveIndex() const {
    std::wstring out = L"# Clipboard Manager index: id, kind, pinned, file, source, hash, width, height\r\n";
    wchar_t hash[24];
    for (const ClipItem& it : m_items) {
        swprintf_s(hash, L"%016llx", (unsigned long long)it.hash);
        out += std::to_wstring(it.id) + L"\t" + KindCode(it.kind) + L"\t" + (it.pinned ? L"1" : L"0") + L"\t" + it.file +
               L"\t" + Clean(it.source) + L"\t" + hash + L"\t" + std::to_wstring(it.width) + L"\t" +
               std::to_wstring(it.height) + L"\r\n";
    }
    WriteFileAll(m_folder + L"\\" + kIndexName, Utf8(out), true);
}

// ===========================================================================
// Changes
// ===========================================================================
void Store::MoveToTop(size_t index, const std::wstring& source) {
    ClipItem it = std::move(m_items[index]);
    m_items.erase(m_items.begin() + (long)index);
    it.id = NewId();
    if (!source.empty()) it.source = source;
    m_items.insert(m_items.begin(), std::move(it));
}

int64_t Store::AddText(const std::wstring& text, const std::wstring& source, bool files) {
    if (text.empty()) return 0;
    const ClipKind kind = files ? ClipKind::Files : ClipKind::Text;
    const uint64_t h = Hash(text.data(), std::min(text.size(), kMemoryText) * sizeof(wchar_t));
    for (size_t i = 0; i < m_items.size(); ++i) {
        const ClipItem& it = m_items[i];
        if (it.kind == kind && it.hash == h && (it.partial || it.text == text) &&
            (it.partial ? text.size() >= kMemoryText : true)) {
            MoveToTop(i, source);
            SaveIndex();
            return m_items.front().id;
        }
    }
    ClipItem it;
    it.id = NewId();
    it.kind = kind;
    it.source = source;
    it.hash = h;
    it.file = NewFileName(it.id, files ? L" files.txt" : L".txt");
    // UTF-8 with a byte-order mark, so Notepad shows every language correctly.
    std::wstring body = text;
    if (files) {
        // One path per line, with Windows line ends.
        std::wstring crlf;
        for (wchar_t c : body) {
            if (c == L'\n' && (crlf.empty() || crlf.back() != L'\r')) crlf += L'\r';
            crlf += c;
        }
        body = crlf;
    }
    const std::string data = "\xEF\xBB\xBF" + Utf8(body);
    if (!WriteFileAll(PathOf(it), data)) return 0;
    it.bytes = data.size();
    it.length = text.size();
    it.text = text.size() > kMemoryText ? text.substr(0, kMemoryText) : text;
    it.partial = text.size() > kMemoryText;
    m_items.insert(m_items.begin(), std::move(it));
    SaveIndex();
    return m_items.front().id;
}

int64_t Store::AddImage(const std::string& png, uint64_t pixelHash, int width, int height, const std::wstring& source) {
    if (png.empty()) return 0;
    for (size_t i = 0; i < m_items.size(); ++i) {
        const ClipItem& it = m_items[i];
        if (it.kind == ClipKind::Image && it.hash == pixelHash && it.width == width && it.height == height) {
            MoveToTop(i, source);
            SaveIndex();
            return m_items.front().id;
        }
    }
    ClipItem it;
    it.id = NewId();
    it.kind = ClipKind::Image;
    it.source = source;
    it.hash = pixelHash;
    it.width = width;
    it.height = height;
    it.file = NewFileName(it.id, L".png");
    if (!WriteFileAll(PathOf(it), png)) return 0;
    it.bytes = png.size();
    m_items.insert(m_items.begin(), std::move(it));
    SaveIndex();
    return m_items.front().id;
}

int64_t Store::Touch(int64_t id) {
    const int i = IndexOf(id);
    if (i <= 0) return id;
    MoveToTop((size_t)i, L"");
    SaveIndex();
    return m_items.front().id;
}

void Store::Remove(const std::vector<int64_t>& ids) {
    bool changed = false;
    for (int64_t id : ids) {
        const int i = IndexOf(id);
        if (i < 0) continue;
        DeleteFileW(PathOf(m_items[(size_t)i]).c_str());
        m_items.erase(m_items.begin() + i);
        changed = true;
    }
    if (changed) SaveIndex();
}

void Store::SetPinned(const std::vector<int64_t>& ids, bool pinned) {
    for (int64_t id : ids) {
        const int i = IndexOf(id);
        if (i >= 0) m_items[(size_t)i].pinned = pinned;
    }
    SaveIndex();
}

int Store::RemoveUnpinnedOlderThan(int days) {
    if (days <= 0) return 0;
    const int64_t cutoff = NowMs() - (int64_t)days * 86400000ll;
    std::vector<int64_t> ids;
    for (const ClipItem& it : m_items)
        if (!it.pinned && it.id < cutoff) ids.push_back(it.id);
    Remove(ids);
    return (int)ids.size();
}

int Store::RemoveAllUnpinned() {
    std::vector<int64_t> ids;
    for (const ClipItem& it : m_items)
        if (!it.pinned) ids.push_back(it.id);
    Remove(ids);
    return (int)ids.size();
}

bool Store::FullText(const ClipItem& it, std::wstring& out) const {
    if (!it.partial) {
        out = it.text;
        return true;
    }
    std::string data;
    if (!ReadFileAll(PathOf(it), data)) return false;
    out = FromUtf8(data.data(), data.size());
    return true;
}

// ===========================================================================
// Formatting
// ===========================================================================
std::wstring FormatWhen(int64_t ms) {
    const time_t t = (time_t)(ms / 1000), now = time(nullptr);
    tm a{}, b{};
    localtime_s(&a, &t);
    localtime_s(&b, &now);
    wchar_t buf[64];
    wcsftime(buf, 64, L"%H:%M", &a);
    const std::wstring clock = buf;
    if (a.tm_year == b.tm_year && a.tm_yday == b.tm_yday) return L"Today " + clock;
    const time_t y = now - 86400;
    tm c{};
    localtime_s(&c, &y);
    if (a.tm_year == c.tm_year && a.tm_yday == c.tm_yday) return L"Yesterday " + clock;
    wcsftime(buf, 64, a.tm_year == b.tm_year ? L"%a %d %b, %H:%M" : L"%d %b %Y, %H:%M", &a);
    return buf;
}

std::wstring FormatSize(uint64_t bytes) {
    wchar_t b[32];
    if (bytes < 1024) swprintf_s(b, L"%llu bytes", (unsigned long long)bytes);
    else if (bytes < 1024 * 1024) swprintf_s(b, L"%.1f KB", bytes / 1024.0);
    else if (bytes < 1024ull * 1024 * 1024) swprintf_s(b, L"%.1f MB", bytes / (1024.0 * 1024));
    else swprintf_s(b, L"%.2f GB", bytes / (1024.0 * 1024 * 1024));
    return b;
}
