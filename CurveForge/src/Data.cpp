// Data.cpp - table storage, text import/export, project files.
#include "Data.h"

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace {
const std::wstring kEmpty;

std::wstring Trim(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && iswspace(s[a])) ++a;
    while (b > a && iswspace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::vector<std::wstring> SplitLines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    std::wstring cur;
    for (size_t i = 0; i < text.size(); ++i) {
        const wchar_t c = text[i];
        if (c == L'\r' || c == L'\n') {
            lines.push_back(cur);
            cur.clear();
            if (c == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n') ++i;
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

// Splits one line. sep == ' ' means runs of spaces/tabs.
std::vector<std::wstring> SplitFields(const std::wstring& line, wchar_t sep) {
    std::vector<std::wstring> f;
    if (sep == L' ') {
        std::wstring cur;
        bool inQ = false;
        for (wchar_t c : line) {
            if (c == L'"') {
                inQ = !inQ;
                continue;
            }
            if (!inQ && (c == L' ' || c == L'\t')) {
                if (!cur.empty()) f.push_back(cur);
                cur.clear();
            } else {
                cur += c;
            }
        }
        if (!cur.empty()) f.push_back(cur);
        return f;
    }
    std::wstring cur;
    bool inQ = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const wchar_t c = line[i];
        if (inQ) {
            if (c == L'"') {
                if (i + 1 < line.size() && line[i + 1] == L'"') {
                    cur += L'"';
                    ++i;
                } else {
                    inQ = false;
                }
            } else {
                cur += c;
            }
        } else if (c == L'"' && Trim(cur).empty()) {
            cur.clear();
            inQ = true;
        } else if (c == sep) {
            f.push_back(Trim(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    f.push_back(Trim(cur));
    return f;
}

bool LooksDecimalComma(const std::wstring& s) {
    // "12,5" or "-0,003" or "1,5e-3": digits, one comma, digits, no dot.
    const std::wstring t = Trim(s);
    if (t.empty() || t.find(L'.') != std::wstring::npos) return false;
    const size_t comma = t.find(L',');
    if (comma == std::wstring::npos || t.find(L',', comma + 1) != std::wstring::npos) return false;
    std::wstring u = t;
    u[comma] = L'.';
    double v;
    return ParseNumber(u, v);
}
}  // namespace

bool ParseNumber(const std::wstring& s, double& v) {
    std::wstring t = Trim(s);
    if (t.empty()) return false;
    for (wchar_t& c : t)
        if (c == 0x2212) c = L'-';  // Unicode minus
    const std::wstring low = [&] {
        std::wstring l = t;
        for (wchar_t& c : l) c = (wchar_t)towlower(c);
        return l;
    }();
    if (low == L"nan") {
        v = NAN;
        return false;
    }
    if (low == L"inf" || low == L"+inf" || low == L"infinity") {
        v = INFINITY;
        return true;
    }
    if (low == L"-inf" || low == L"-infinity") {
        v = -INFINITY;
        return true;
    }
    wchar_t* end = nullptr;
    v = wcstod(t.c_str(), &end);
    return end && *end == 0 && end != t.c_str();
}

// ---------------------------------------------------------------------------
// Table
// ---------------------------------------------------------------------------
const std::wstring& Table::Cell(int r, int c) const {
    if (r < 0 || r >= Rows() || c < 0) return kEmpty;
    const auto& row = rows[(size_t)r];
    return c < (int)row.size() ? row[(size_t)c] : kEmpty;
}

void Table::SetCell(int r, int c, const std::wstring& v) {
    if (r < 0 || c < 0) return;
    while (Cols() <= c) names.push_back(L"Column " + std::to_wstring(Cols() + 1));
    if (Rows() <= r) rows.resize((size_t)r + 1);
    auto& row = rows[(size_t)r];
    if ((int)row.size() <= c) row.resize((size_t)c + 1);
    row[(size_t)c] = v;
}

double Table::Value(int r, int c) const {
    double v;
    return ParseNumber(Cell(r, c), v) ? v : NAN;
}

bool Table::IsNumericColumn(int c) const {
    int numbers = 0, filled = 0;
    for (int r = 0; r < Rows(); ++r) {
        const std::wstring& s = Cell(r, c);
        if (Trim(s).empty()) continue;
        ++filled;
        double v;
        if (ParseNumber(s, v)) ++numbers;
    }
    return numbers > 0 && numbers * 10 >= filled * 8;
}

void Table::TrimEmptyRows() {
    while (!rows.empty()) {
        bool empty = true;
        for (const auto& s : rows.back())
            if (!Trim(s).empty()) empty = false;
        if (!empty) break;
        rows.pop_back();
    }
}

// ---------------------------------------------------------------------------
// Import / export
// ---------------------------------------------------------------------------
bool Import::ParseText(const std::wstring& text, Table& out, std::wstring& error) {
    out = Table{};
    std::vector<std::wstring> lines;
    for (std::wstring& l : SplitLines(text)) {
        const std::wstring t = Trim(l);
        if (t.empty() || t[0] == L'#') continue;
        lines.push_back(l);
    }
    if (lines.empty()) {
        error = L"There is no data in it.";
        return false;
    }

    // Choose the separator that splits the lines most consistently.
    const wchar_t seps[] = {L'\t', L';', L',', L'|', L' '};
    wchar_t best = L',';
    double bestScore = -1;
    const size_t sample = std::min<size_t>(lines.size(), 60);
    for (wchar_t sep : seps) {
        std::vector<size_t> counts;
        for (size_t i = 0; i < sample; ++i) counts.push_back(SplitFields(lines[i], sep).size());
        size_t mode = 0, modeCount = 0;
        for (size_t c : counts) {
            const size_t k = (size_t)std::count(counts.begin(), counts.end(), c);
            if (k > modeCount || (k == modeCount && c > mode)) {
                mode = c;
                modeCount = k;
            }
        }
        if (mode < 2) continue;
        const double score = (double)modeCount / (double)counts.size() + 0.001 * (double)std::min<size_t>(mode, 50);
        if (score > bestScore + 1e-9) {
            bestScore = score;
            best = sep;
        }
    }
    if (bestScore < 0) best = L',';  // a single column

    std::vector<std::vector<std::wstring>> rows;
    size_t cols = 0;
    for (const std::wstring& l : lines) {
        rows.push_back(SplitFields(l, best));
        cols = std::max(cols, rows.back().size());
    }

    // Decimal commas ("12,5") are common in Europe when the separator is ';' or tab.
    if (best != L',') {
        int commaNums = 0, dotNums = 0;
        for (size_t r = 0; r < rows.size() && r < 200; ++r)
            for (const std::wstring& f : rows[r]) {
                if (LooksDecimalComma(f)) ++commaNums;
                else if (f.find(L'.') != std::wstring::npos) {
                    double v;
                    if (ParseNumber(f, v)) ++dotNums;
                }
            }
        if (commaNums > dotNums)
            for (auto& row : rows)
                for (std::wstring& f : row)
                    if (LooksDecimalComma(f)) std::replace(f.begin(), f.end(), L',', L'.');
    }

    // A header row: some text where the next row has numbers.
    bool header = false;
    if (rows.size() >= 2) {
        int textOverNumber = 0, numbersFirst = 0;
        for (size_t c = 0; c < rows[0].size(); ++c) {
            double v;
            const bool firstNum = ParseNumber(rows[0][c], v);
            const bool secondNum = c < rows[1].size() && ParseNumber(rows[1][c], v);
            if (firstNum) ++numbersFirst;
            if (!firstNum && !Trim(rows[0][c]).empty() && secondNum) ++textOverNumber;
        }
        header = textOverNumber > 0 || (numbersFirst == 0 && rows[0].size() == cols);
    } else if (rows.size() == 1) {
        double v;
        header = !rows[0].empty() && !ParseNumber(rows[0][0], v);
    }

    for (size_t c = 0; c < cols; ++c) {
        std::wstring name = header && c < rows[0].size() ? Trim(rows[0][c]) : L"";
        if (name.empty()) name = cols == 2 && !header ? (c == 0 ? L"x" : L"y") : L"Column " + std::to_wstring(c + 1);
        std::wstring unique = name;
        for (int k = 2; std::find(out.names.begin(), out.names.end(), unique) != out.names.end(); ++k)
            unique = name + L" (" + std::to_wstring(k) + L")";
        out.names.push_back(unique);
    }
    for (size_t r = header ? 1 : 0; r < rows.size(); ++r) out.rows.push_back(std::move(rows[r]));
    out.TrimEmptyRows();
    return true;
}

bool Import::ReadTextFile(const std::wstring& path, std::wstring& text) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    GetFileSizeEx(f, &size);
    if (size.QuadPart > 512ll * 1024 * 1024) {
        CloseHandle(f);
        SetLastError(ERROR_FILE_TOO_LARGE);
        return false;
    }
    std::string bytes((size_t)size.QuadPart, '\0');
    DWORD read = 0;
    const BOOL ok = bytes.empty() || ReadFile(f, bytes.data(), (DWORD)bytes.size(), &read, nullptr);
    CloseHandle(f);
    if (!ok) return false;
    bytes.resize(read);
    text.clear();
    if (bytes.size() >= 2 && (unsigned char)bytes[0] == 0xFF && (unsigned char)bytes[1] == 0xFE) {
        text.assign((const wchar_t*)(bytes.data() + 2), (bytes.size() - 2) / 2);
        return true;
    }
    if (bytes.size() >= 2 && (unsigned char)bytes[0] == 0xFE && (unsigned char)bytes[1] == 0xFF) {
        for (size_t i = 2; i + 1 < bytes.size(); i += 2)
            text += (wchar_t)(((unsigned char)bytes[i] << 8) | (unsigned char)bytes[i + 1]);
        return true;
    }
    size_t start = 0;
    if (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xEF && (unsigned char)bytes[1] == 0xBB &&
        (unsigned char)bytes[2] == 0xBF)
        start = 3;
    const char* p = bytes.data() + start;
    const int len = (int)(bytes.size() - start);
    if (len == 0) return true;
    UINT cp = CP_UTF8;
    int n = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, p, len, nullptr, 0);
    if (n <= 0) {
        cp = CP_ACP;
        n = MultiByteToWideChar(cp, 0, p, len, nullptr, 0);
    }
    text.resize((size_t)n);
    MultiByteToWideChar(cp, 0, p, len, text.data(), n);
    return true;
}

bool Import::WriteTextFile(const std::wstring& path, const std::wstring& text) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string bytes = "\xEF\xBB\xBF";
    const size_t at = bytes.size();
    bytes.resize(at + (size_t)std::max(n, 0));
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), bytes.data() + at, n, nullptr, nullptr);
    // Write to a temporary file first, so a failure never destroys the old file.
    const std::wstring tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = WriteFile(f, bytes.data(), (DWORD)bytes.size(), &written, nullptr) && written == bytes.size();
    CloseHandle(f);
    if (!ok || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD err = GetLastError();
        DeleteFileW(tmp.c_str());
        SetLastError(err);
        return false;
    }
    return true;
}

std::wstring Import::ToCsvField(const std::wstring& s, wchar_t sep) {
    if (s.find_first_of(std::wstring(1, sep) + L"\"\r\n") == std::wstring::npos) return s;
    std::wstring out = L"\"";
    for (wchar_t c : s) {
        if (c == L'"') out += L'"';
        out += c;
    }
    return out + L"\"";
}

// ---------------------------------------------------------------------------
// Document
// ---------------------------------------------------------------------------
void Document::SetExcluded(int row, bool ex) {
    if (row < 0) return;
    if ((int)excluded.size() <= row) excluded.resize((size_t)row + 1, false);
    excluded[(size_t)row] = ex;
}

std::wstring Document::Equation() const {
    return model == ModelKind::Custom ? customEquation : Models::Formula(model, order);
}

namespace {
std::wstring Num(double v) {
    if (std::isnan(v)) return L"nan";
    if (std::isinf(v)) return v > 0 ? L"inf" : L"-inf";
    wchar_t b[40];
    swprintf_s(b, L"%.17g", v);
    return b;
}

double ParseNum(const std::wstring& s) {
    double v;
    if (ParseNumber(s, v)) return v;
    return s == L"nan" ? NAN : 0.0;
}

std::wstring Clean(const std::wstring& s) {
    std::wstring o = s;
    for (wchar_t& c : o)
        if (c == L'\t' || c == L'\r' || c == L'\n') c = L' ';
    return o;
}
}  // namespace

std::wstring Document::Serialize() const {
    std::wstring s = L"CurveForge project 1\r\n";
    s += L"x=" + std::to_wstring(xCol) + L"\r\n";
    s += L"y=" + std::to_wstring(yCol) + L"\r\n";
    s += L"sigma=" + std::to_wstring(sigmaCol) + L"\r\n";
    s += std::wstring(L"model=") + Models::Info(model).key + L"\r\n";
    s += L"order=" + std::to_wstring(order) + L"\r\n";
    s += L"custom=" + Clean(customEquation) + L"\r\n";
    s += std::wstring(L"robust=") + (robust ? L"1" : L"0") + L"\r\n";
    s += std::wstring(L"logx=") + (logX ? L"1" : L"0") + L"\r\n";
    s += std::wstring(L"logy=") + (logY ? L"1" : L"0") + L"\r\n";
    for (const ParamOption& p : params)
        s += L"param=" + Clean(p.name) + L"|" + (p.hasInit ? L"1" : L"0") + L"|" + Num(p.init) + L"|" +
             (p.fixed ? L"1" : L"0") + L"|" + Num(p.lo) + L"|" + Num(p.hi) + L"\r\n";
    std::wstring ex;
    for (size_t i = 0; i < excluded.size(); ++i)
        if (excluded[i]) ex += (ex.empty() ? L"" : L",") + std::to_wstring(i);
    if (!ex.empty()) s += L"excluded=" + ex + L"\r\n";
    s += L"[data]\r\n";
    for (int c = 0; c < table.Cols(); ++c) s += (c ? L"\t" : L"") + Clean(table.names[(size_t)c]);
    s += L"\r\n";
    for (int r = 0; r < table.Rows(); ++r) {
        for (int c = 0; c < table.Cols(); ++c) s += (c ? L"\t" : L"") + Clean(table.Cell(r, c));
        s += L"\r\n";
    }
    return s;
}

bool Document::Deserialize(const std::wstring& text, std::wstring& error) {
    const std::vector<std::wstring> lines = SplitLines(text);
    if (lines.empty() || lines[0].rfind(L"CurveForge project", 0) != 0) {
        error = L"This is not a CurveForge project.";
        return false;
    }
    Document d;
    d.params.clear();
    size_t i = 1;
    for (; i < lines.size(); ++i) {
        const std::wstring& l = lines[i];
        if (l == L"[data]") {
            ++i;
            break;
        }
        const size_t eq = l.find(L'=');
        if (eq == std::wstring::npos) continue;
        const std::wstring k = l.substr(0, eq), v = l.substr(eq + 1);
        if (k == L"x") d.xCol = _wtoi(v.c_str());
        else if (k == L"y") d.yCol = _wtoi(v.c_str());
        else if (k == L"sigma") d.sigmaCol = _wtoi(v.c_str());
        else if (k == L"model") d.model = Models::FromKey(v);
        else if (k == L"order") d.order = _wtoi(v.c_str());
        else if (k == L"custom") d.customEquation = v;
        else if (k == L"robust") d.robust = v == L"1";
        else if (k == L"logx") d.logX = v == L"1";
        else if (k == L"logy") d.logY = v == L"1";
        else if (k == L"excluded") {
            size_t pos = 0;
            while (pos < v.size()) {
                const size_t comma = v.find(L',', pos);
                const std::wstring part = v.substr(pos, comma == std::wstring::npos ? std::wstring::npos : comma - pos);
                if (!part.empty()) d.SetExcluded(_wtoi(part.c_str()), true);
                if (comma == std::wstring::npos) break;
                pos = comma + 1;
            }
        } else if (k == L"param") {
            std::vector<std::wstring> f;
            size_t pos = 0;
            for (;;) {
                const size_t bar = v.find(L'|', pos);
                f.push_back(v.substr(pos, bar == std::wstring::npos ? std::wstring::npos : bar - pos));
                if (bar == std::wstring::npos) break;
                pos = bar + 1;
            }
            if (f.size() >= 6) {
                ParamOption p;
                p.name = f[0];
                p.hasInit = f[1] == L"1";
                p.init = ParseNum(f[2]);
                p.fixed = f[3] == L"1";
                p.lo = ParseNum(f[4]);
                p.hi = ParseNum(f[5]);
                d.params.push_back(p);
            }
        }
    }
    if (i <= lines.size() && i > 0 && i - 1 < lines.size() && lines[i - 1] == L"[data]") {
        if (i < lines.size()) {
            d.table.names = SplitFields(lines[i], L'\t');
            for (size_t r = i + 1; r < lines.size(); ++r) d.table.rows.push_back(SplitFields(lines[r], L'\t'));
        }
    }
    // SplitFields trims; empty trailing cells are fine.
    const int cols = d.table.Cols();
    if (d.xCol >= cols) d.xCol = 0;
    if (d.yCol >= cols) d.yCol = cols > 1 ? 1 : 0;
    if (d.sigmaCol >= cols) d.sigmaCol = -1;
    *this = std::move(d);
    return true;
}
