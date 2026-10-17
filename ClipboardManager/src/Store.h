// Store.h - the clipboard history, kept as ordinary files in the
// "Clipboard contents" folder next to ClipboardManager.exe:
//
//   2026-10-03 10.15.30.123.txt          text (UTF-8)
//   2026-10-03 10.15.31.456.png          a picture
//   2026-10-03 10.15.32.789 files.txt    copied files: one path per line
//   index.txt (hidden)                   pins, source app, order
//
// There is no limit on how many items or how large they are. If the index
// is lost, the history is rebuilt from the files themselves.
#pragma once
#include "Common.h"

enum class ClipKind { Text, Image, Files };

struct ClipItem {
    int64_t id = 0;            // milliseconds since 1970 when last copied (unique, newest first)
    ClipKind kind = ClipKind::Text;
    bool pinned = false;
    std::wstring file;         // file name inside the folder
    std::wstring source;       // the program it was copied from
    uint64_t hash = 0;         // to recognise the same content copied again
    int width = 0, height = 0; // pictures
    uint64_t bytes = 0;        // file size
    std::wstring text;         // text and file lists (the first part, for very long texts)
    size_t length = 0;         // characters of text in the file
    bool partial = false;      // `text` holds only the first part
};

class Store {
public:
    // Opens the folder next to the program (or, if that is not writable, in
    // the user's AppData; `note` then says so) and loads the history.
    bool Open(std::wstring& note);
    const std::wstring& Folder() const { return m_folder; }
    std::wstring PathOf(const ClipItem& it) const { return m_folder + L"\\" + it.file; }

    const std::vector<ClipItem>& Items() const { return m_items; }
    int IndexOf(int64_t id) const;

    // Adds new content, or moves the same content already stored to the top.
    // Returns the item's id (0 on failure).
    int64_t AddText(const std::wstring& text, const std::wstring& source, bool files);
    int64_t AddImage(const std::string& png, uint64_t pixelHash, int width, int height, const std::wstring& source);

    // Moves an item to the top (it was copied again). Returns its new id.
    int64_t Touch(int64_t id);
    void Remove(const std::vector<int64_t>& ids);
    void SetPinned(const std::vector<int64_t>& ids, bool pinned);
    int RemoveUnpinnedOlderThan(int days);  // returns how many
    int RemoveAllUnpinned();

    // The complete text of a text or file-list item.
    bool FullText(const ClipItem& it, std::wstring& out) const;
    uint64_t TotalBytes() const;

    static uint64_t Hash(const void* data, size_t size, uint64_t seed = 1469598103934665603ull);
    static int64_t NowMs();

private:
    std::wstring NewFileName(int64_t id, const wchar_t* suffix) const;
    int64_t NewId() const;
    void MoveToTop(size_t index, const std::wstring& source);
    void SaveIndex() const;
    bool LoadText(ClipItem& it) const;

    std::wstring m_folder;
    std::vector<ClipItem> m_items;  // newest first
};

std::wstring FormatWhen(int64_t ms);  // "10:15", "Yesterday 10:15", "Mon 28 Sep, 10:15"
std::wstring FormatSize(uint64_t bytes);
