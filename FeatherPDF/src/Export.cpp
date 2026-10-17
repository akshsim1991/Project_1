// Export.cpp - pictures through GDI+, text files, and the Markdown builder.
#include "Export.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <map>

#include "GdiPlusInc.h"
#include "Util.h"

using namespace Gdiplus;

namespace {
bool EncoderFor(const wchar_t* mime, CLSID& clsid) {
    UINT count = 0, size = 0;
    if (GetImageEncodersSize(&count, &size) != Ok || !size) return false;
    std::vector<BYTE> buf(size);
    auto* codecs = (ImageCodecInfo*)buf.data();
    if (GetImageEncoders(count, size, codecs) != Ok) return false;
    for (UINT i = 0; i < count; ++i)
        if (wcscmp(codecs[i].MimeType, mime) == 0) {
            clsid = codecs[i].Clsid;
            return true;
        }
    return false;
}

// Moves a finished temporary file into place.
bool Commit(const std::wstring& temp, const std::wstring& path, bool ok) {
    if (ok && MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return true;
    DeleteFileW(temp.c_str());
    return false;
}
}  // namespace

bool SavePicture(const PixelBuffer& px, const std::wstring& path, bool jpeg, int dpi) {
    CLSID clsid;
    if (!px.bits || !EncoderFor(jpeg ? L"image/jpeg" : L"image/png", clsid)) return false;
    const std::wstring temp = path + L".tmp";
    bool ok = false;
    {
        Bitmap bmp(px.width, px.height, px.width * 4, PixelFormat32bppRGB, px.bits);
        if (bmp.GetLastStatus() == Ok) {
            bmp.SetResolution((REAL)dpi, (REAL)dpi);
            ULONG quality = 90;
            EncoderParameters params;
            params.Count = 1;
            params.Parameter[0].Guid = EncoderQuality;
            params.Parameter[0].Type = EncoderParameterValueTypeLong;
            params.Parameter[0].NumberOfValues = 1;
            params.Parameter[0].Value = &quality;
            ok = bmp.Save(temp.c_str(), &clsid, jpeg ? &params : nullptr) == Ok;
        }
    }
    return Commit(temp, path, ok);
}

bool EncodeJpeg(const uint8_t* bits, int w, int h, int stride, int quality, std::string& out) {
    CLSID clsid;
    if (!bits || w <= 0 || h <= 0 || !EncoderFor(L"image/jpeg", clsid)) return false;
    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) return false;
    bool ok = false;
    {
        Bitmap bmp(w, h, stride, PixelFormat32bppRGB, const_cast<BYTE*>(bits));
        ULONG q = (ULONG)quality;
        EncoderParameters params;
        params.Count = 1;
        params.Parameter[0].Guid = EncoderQuality;
        params.Parameter[0].Type = EncoderParameterValueTypeLong;
        params.Parameter[0].NumberOfValues = 1;
        params.Parameter[0].Value = &q;
        ok = bmp.GetLastStatus() == Ok && bmp.Save(stream, &clsid, &params) == Ok;
    }
    HGLOBAL mem = nullptr;
    if (ok && SUCCEEDED(GetHGlobalFromStream(stream, &mem))) {
        STATSTG st{};
        stream->Stat(&st, STATFLAG_NONAME);
        const void* p = GlobalLock(mem);
        ok = p != nullptr;
        if (ok) out.assign(static_cast<const char*>(p), (size_t)st.cbSize.QuadPart);
        GlobalUnlock(mem);
    } else {
        ok = false;
    }
    stream->Release();
    return ok;
}

bool WriteTextFile(const std::wstring& path, const std::wstring& text, bool bom) {
    std::wstring crlf;
    crlf.reserve(text.size() + text.size() / 16);
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\r') continue;
        if (text[i] == L'\n') crlf += L'\r';
        crlf += text[i];
    }
    std::string data = bom ? "\xEF\xBB\xBF" : "";
    data += WideToUtf8(crlf);
    const std::wstring temp = path + L".tmp";
    HANDLE f = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(f, data.data(), (DWORD)data.size(), &written, nullptr) && written == data.size();
    ok = FlushFileBuffers(f) && ok;
    CloseHandle(f);
    return Commit(temp, path, ok);
}

std::wstring TidyText(const std::wstring& text) {
    std::wstring out, line;
    auto flush = [&] {
        while (!line.empty() && (line.back() == L' ' || line.back() == L'\t')) line.pop_back();
        out += line;
        out += L'\n';
        line.clear();
    };
    for (wchar_t c : text) {
        if (c == L'\r') continue;
        if (c == L'\n')
            flush();
        else if (c != 0xFFFE && c != 0xFFFF && c != 0)
            line += c;
    }
    if (!line.empty()) flush();
    return out;
}

// ---------------------------------------------------------------------------
// Markdown
//
// A PDF only knows where letters are, not what they mean, so the structure
// is inferred the way a reader would see it:
//   * the size used by most letters is the body text;
//   * clearly larger sizes are headings, the largest "#", then "##", "###";
//   * a line starting with a bullet or "1." / "a)" is a list item;
//   * lines in the body size with normal line spacing are one paragraph
//     (a word broken with a hyphen at the end of a line is joined again);
//   * a short line in bold on its own is kept bold.
// ---------------------------------------------------------------------------
namespace {
struct Block {
    enum Kind { Paragraph, Heading, Bullet, Numbered, Bold } kind = Paragraph;
    int level = 0;         // Heading: 1-3
    std::wstring text;
    float size = 0, left = 0, top = 0, bottom = 0;
    std::wstring marker;   // Numbered: "1."
};

std::wstring Trim(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && iswspace(s[a])) ++a;
    while (b > a && iswspace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

bool IsBulletChar(wchar_t c) {
    return wcschr(L"\x2022\x25E6\x25AA\x2023\x25CF\x25CB\x25A0\x25A1\x2043\x2219\x00B7\x2013\x2014-*\xF0B7\xF0A7", c) &&
           c != 0;
}

// "1.", "12)", "a)", "iv." at the start of a line: the marker, else empty.
std::wstring NumberMarker(const std::wstring& s, std::wstring& rest) {
    size_t i = 0;
    if (i < s.size() && iswdigit(s[i])) {
        while (i < s.size() && iswdigit(s[i]) && i < 3) ++i;
    } else if (i < s.size() && iswalpha(s[i]) && s.size() > 2 && (s[1] == L')' || s[1] == L'.') &&
               iswlower(s[0])) {
        i = 1;
    } else {
        return {};
    }
    if (i >= s.size() || (s[i] != L'.' && s[i] != L')') || i + 1 >= s.size() || !iswspace(s[i + 1])) return {};
    rest = Trim(s.substr(i + 1));
    if (rest.empty()) return {};
    if (iswdigit(s[0])) return s.substr(0, i) + L".";
    return {};  // lettered lists are not Markdown; kept as text
}

// Characters that would start Markdown syntax at the beginning of a line.
std::wstring EscapeStart(const std::wstring& s) {
    if (s.empty()) return s;
    if (wcschr(L"#>+=|`", s[0]) || ((s[0] == L'-' || s[0] == L'*') && s.size() > 1 && s[1] == L' '))
        return L"\\" + s;
    size_t i = 0;
    while (i < s.size() && iswdigit(s[i])) ++i;
    if (i > 0 && i < s.size() && (s[i] == L'.' || s[i] == L')')) return s.substr(0, i) + L"\\" + s.substr(i);
    return s;
}

// Joins a wrapped line to the text before it.
void Append(std::wstring& text, const std::wstring& next) {
    if (text.empty()) {
        text = next;
        return;
    }
    if (text.size() >= 2 && text.back() == L'-' && iswalpha(text[text.size() - 2]) && !next.empty() &&
        iswlower(next[0])) {
        text.pop_back();  // "docu-" + "ment"
        text += next;
        return;
    }
    text += L' ';
    text += next;
}
}  // namespace

std::wstring LinesToMarkdown(const std::vector<std::vector<TextLine>>& pages) {
    // Body size and heading sizes, in half points, weighted by letters.
    std::map<int, size_t> letters;
    size_t total = 0;
    for (const auto& page : pages)
        for (const TextLine& l : page) {
            letters[(int)std::lround(l.size * 2)] += l.text.size();
            total += l.text.size();
        }
    if (total == 0) return {};
    int body = 22;
    size_t most = 0;
    for (const auto& s : letters)
        if (s.second > most) body = s.first, most = s.second;
    std::vector<int> headings;  // descending
    for (auto it = letters.rbegin(); it != letters.rend(); ++it)
        if (it->first >= body * 1.15 + 0.5 && it->second * 4 < total) headings.push_back(it->first);
    auto levelOf = [&](float size) {
        const int s = (int)std::lround(size * 2);
        for (size_t i = 0; i < headings.size(); ++i)
            if (s >= headings[i]) return (int)std::min<size_t>(i + 1, 3);
        return 0;
    };

    std::vector<Block> blocks;
    for (const auto& page : pages) {
        bool prevOnPage = false;  // blocks.back() is from this page
        for (const TextLine& line : page) {
            const std::wstring text = Trim(line.text);
            if (text.empty()) continue;
            const float height = std::max(1.0f, line.bottom - line.top);
            const int level = text.size() <= 200 ? levelOf(line.size) : 0;
            Block b;
            b.size = line.size;
            b.left = line.left;
            b.top = line.top;
            b.bottom = line.bottom;
            std::wstring rest;
            if (level > 0) {
                b.kind = Block::Heading;
                b.level = level;
                b.text = text;
            } else if (IsBulletChar(text[0]) && (text.size() == 1 || iswspace(text[1]) || text[0] > 0x2000)) {
                b.kind = Block::Bullet;
                b.text = Trim(text.substr(1));
                if (b.text.empty()) continue;
            } else if (!(b.marker = NumberMarker(text, rest)).empty()) {
                b.kind = Block::Numbered;
                b.text = rest;
            } else {
                b.kind = line.bold && text.size() <= 80 ? Block::Bold : Block::Paragraph;
                b.text = text;
            }

            // Continue the previous block? Only lines that follow closely
            // (normal line spacing) in the same size.
            if (prevOnPage && !blocks.empty()) {
                Block& prev = blocks.back();
                const float prevHeight = std::max(1.0f, prev.bottom - prev.top);
                const float gap = line.top - prev.bottom;
                const bool close = gap < std::max(prevHeight, height) * 0.9f && gap > -height * 0.5f &&
                                   std::fabs(prev.size - line.size) < 0.6f;
                bool join = false;
                if (close) {
                    if (b.kind == Block::Heading && prev.kind == Block::Heading && prev.level == b.level)
                        join = true;  // a heading over two lines
                    else if ((b.kind == Block::Paragraph || b.kind == Block::Bold) &&
                             (prev.kind == Block::Paragraph ||
                              ((prev.kind == Block::Bullet || prev.kind == Block::Numbered) &&
                               line.left > prev.left + 2)))
                        join = true;  // wrapped text, or the rest of a list item
                    else if (b.kind == Block::Bold && prev.kind == Block::Bold)
                        join = true;
                }
                if (join) {
                    Append(prev.text, b.text);
                    if (b.kind == Block::Paragraph && prev.kind == Block::Bold) prev.kind = Block::Paragraph;
                    prev.bottom = line.bottom;
                    continue;
                }
            }
            blocks.push_back(std::move(b));
            prevOnPage = true;
        }
    }

    std::wstring out;
    Block::Kind last = Block::Paragraph;
    for (size_t i = 0; i < blocks.size(); ++i) {
        const Block& b = blocks[i];
        const bool list = b.kind == Block::Bullet || b.kind == Block::Numbered;
        // List items follow each other directly; everything else is
        // separated by a blank line.
        if (i > 0) out += list && (last == Block::Bullet || last == Block::Numbered) ? L"\n" : L"\n\n";
        switch (b.kind) {
            case Block::Heading: out += std::wstring((size_t)b.level, L'#') + L" " + b.text; break;
            case Block::Bullet: out += L"- " + b.text; break;
            case Block::Numbered: out += b.marker + L" " + b.text; break;
            case Block::Bold: out += L"**" + b.text + L"**"; break;
            case Block::Paragraph: out += EscapeStart(b.text); break;
        }
        last = b.kind;
    }
    out += L"\n";
    return out;
}
