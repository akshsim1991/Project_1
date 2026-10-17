// Compare.cpp - word difference and the marked copy (see Compare.h).
#include "Compare.h"

#include <algorithm>
#include <unordered_map>

#include "Util.h"

namespace {
// One step of an edit script: '=' both, '-' only in a (removed), '+' only in b (added).
struct Step {
    char op;
    size_t a, b;  // positions in a and b
};

// Myers' O(ND) difference of a[a0, a1) and b[b0, b1). Appends the steps;
// false (nothing appended) when more than `maxD` edits are needed.
bool Myers(const std::vector<uint32_t>& a, size_t a0, size_t a1, const std::vector<uint32_t>& b, size_t b0, size_t b1,
           int maxD, std::vector<Step>& out) {
    const int n = (int)(a1 - a0), m = (int)(b1 - b0);
    const int limit = std::min(maxD, n + m);
    const int offset = limit + 1;
    std::vector<int> v((size_t)(2 * limit + 3), 0);
    std::vector<std::vector<int>> trace;  // v before step d, for k in [-d-1, d+1]
    int found = -1;
    for (int d = 0; d <= limit && found < 0; ++d) {
        trace.emplace_back(v.begin() + (offset - d - 1), v.begin() + (offset + d + 2));
        for (int k = -d; k <= d; k += 2) {
            int x = (k == -d || (k != d && v[(size_t)(offset + k - 1)] < v[(size_t)(offset + k + 1)]))
                        ? v[(size_t)(offset + k + 1)]
                        : v[(size_t)(offset + k - 1)] + 1;
            int y = x - k;
            while (x < n && y < m && a[a0 + (size_t)x] == b[b0 + (size_t)y]) ++x, ++y;
            v[(size_t)(offset + k)] = x;
            if (x >= n && y >= m) {
                found = d;
                break;
            }
        }
    }
    if (found < 0) return false;
    std::vector<Step> rev;
    int x = n, y = m;
    for (int d = found; d >= 0; --d) {
        const std::vector<int>& tv = trace[(size_t)d];
        auto at = [&](int k) { return tv[(size_t)(k + d + 1)]; };
        const int k = x - y;
        const int prevK = (k == -d || (k != d && at(k - 1) < at(k + 1))) ? k + 1 : k - 1;
        const int prevX = d == 0 ? 0 : at(prevK), prevY = prevX - prevK;
        while (x > prevX && y > prevY) {
            --x, --y;
            rev.push_back({'=', a0 + (size_t)x, b0 + (size_t)y});
        }
        if (d > 0) {
            if (x == prevX)
                rev.push_back({'+', a0 + (size_t)prevX, b0 + (size_t)prevY});
            else
                rev.push_back({'-', a0 + (size_t)prevX, b0 + (size_t)prevY});
        }
        x = prevX, y = prevY;
    }
    out.insert(out.end(), rev.rbegin(), rev.rend());
    return true;
}

std::wstring Join(const std::vector<DocWord>& w, size_t from, size_t to, size_t maxChars) {
    std::wstring s;
    for (size_t i = from; i < to && i < w.size(); ++i) {
        if (!s.empty()) s += L' ';
        s += w[i].text;
        if (s.size() > maxChars) {
            s.resize(maxChars);
            s += L"\x2026";
            break;
        }
    }
    return s;
}
}  // namespace

void CompareDocuments(PdfEngine& current, const std::wstring& otherPath, const std::string& otherPassword,
                      const std::wstring& outPath, CompareResult& result) {
    PdfEngine other;
    std::vector<SizeF> sizes;
    const OpenError err = other.Open(otherPath, otherPassword, sizes);
    if (err != OpenError::None) {
        result.ok = false;
        result.error = err == OpenError::Password
                           ? L"That file is password-protected. Open it in a tab first, then compare with the tab."
                           : L"That file could not be read, or is not a PDF document.";
        return;
    }
    std::vector<DocWord> a, b;
    current.ReadWords(a);
    other.ReadWords(b);
    if (a.empty() && b.empty()) {
        result.ok = false;
        result.error = L"Neither document has text to compare. Scanned pages need Edit PDF \x203A Recognise "
                       L"text (OCR) first.";
        return;
    }

    // Words as numbers.
    std::unordered_map<std::wstring, uint32_t> ids;
    auto idsOf = [&](const std::vector<DocWord>& words) {
        std::vector<uint32_t> out;
        out.reserve(words.size());
        for (const DocWord& w : words) out.push_back(ids.emplace(w.text, (uint32_t)ids.size()).first->second);
        return out;
    };
    const std::vector<uint32_t> ia = idsOf(a), ib = idsOf(b);

    // The common start and end need no search; the middle is diffed.
    size_t pre = 0;
    while (pre < ia.size() && pre < ib.size() && ia[pre] == ib[pre]) ++pre;
    size_t suf = 0;
    while (suf < ia.size() - pre && suf < ib.size() - pre && ia[ia.size() - 1 - suf] == ib[ib.size() - 1 - suf]) ++suf;
    std::vector<Step> steps;
    for (size_t i = 0; i < pre; ++i) steps.push_back({'=', i, i});
    const size_t aEnd = ia.size() - suf, bEnd = ib.size() - suf;
    constexpr int kMaxD = 2500;
    if (!Myers(ia, pre, aEnd, ib, pre, bEnd, kMaxD, steps)) {
        // Very different: compare page by page (page N with page N) instead.
        auto pageStart = [](const std::vector<DocWord>& w, size_t from, size_t to, int page) {
            size_t i = from;
            while (i < to && w[i].page < page) ++i;
            return i;
        };
        const int pages = std::max(a.empty() ? 0 : a.back().page, b.empty() ? 0 : b.back().page) + 1;
        size_t pa = pre, pb = pre;
        for (int p = 0; p < pages; ++p) {
            const size_t ea = pageStart(a, pa, aEnd, p + 1), eb = pageStart(b, pb, bEnd, p + 1);
            if (!Myers(ia, pa, ea, ib, pb, eb, kMaxD, steps)) {
                for (size_t i = pa; i < ea; ++i) steps.push_back({'-', i, pb});
                for (size_t j = pb; j < eb; ++j) steps.push_back({'+', ea, j});
            }
            pa = ea, pb = eb;
        }
    }
    for (size_t i = 0; i < suf; ++i) steps.push_back({'=', aEnd + i, bEnd + i});

    // Runs of differences become marks in the other document.
    const std::wstring author = L"Feather PDF comparison";
    int marks = 0;
    for (size_t s = 0; s < steps.size();) {
        if (steps[s].op == '=') {
            ++s;
            continue;
        }
        size_t a0 = SIZE_MAX, a1 = 0, b0 = SIZE_MAX, b1 = 0;
        size_t e = s;
        for (; e < steps.size() && steps[e].op != '='; ++e) {
            if (steps[e].op == '-') {
                a0 = std::min(a0, steps[e].a);
                a1 = std::max(a1, steps[e].a + 1);
            } else {
                b0 = std::min(b0, steps[e].b);
                b1 = std::max(b1, steps[e].b + 1);
            }
        }
        const size_t at = steps[s].b;  // where in b the difference is
        if (++marks <= 5000) {
            if (b0 == SIZE_MAX) {
                // Removed: a red note before the next word (or after the last one).
                ++result.removed;
                const std::wstring note = L"Removed: \x201C" + Join(a, a0, a1, 400) + L"\x201D";
                if (!b.empty()) {
                    const DocWord& w = at < b.size() ? b[at] : b.back();
                    other.AddNoteAtPoint(w.page, at < b.size() ? w.l : w.r, w.t, RGB(220, 40, 40), note, author);
                } else {
                    other.AddNoteAtPoint(0, 36, 36, RGB(220, 40, 40), note, author);
                }
            } else if (a0 == SIZE_MAX) {
                ++result.added;
                other.MarkWords(b, b0, b1, RGB(110, 210, 110), L"Added", author);
            } else {
                ++result.changed;
                other.MarkWords(b, b0, b1, RGB(255, 175, 60), L"Changed. Before: \x201C" + Join(a, a0, a1, 400) + L"\x201D",
                                author);
            }
        }
        s = e;
    }
    if (!other.WriteTo(outPath)) {
        result.ok = false;
        result.error = L"The compared copy could not be written to the temporary folder.";
    }
    result.path = outPath;
}
