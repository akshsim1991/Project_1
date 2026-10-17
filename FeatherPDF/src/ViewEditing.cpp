// ViewEditing.cpp - PdfView's editing of blocks of text and pictures
// (the "Edit text" tool):
//   * Blocks: lines of text that belong together (a paragraph, a heading,
//     a table cell) are found from the lines' positions and sizes. A click
//     selects the block (and edits the clicked line in place), Ctrl+click
//     adds blocks, a box drawn on an empty part of the page selects exactly
//     the lines inside it.
//   * Pictures: placed directly on the page; a click selects one, its
//     corners resize it (keeping its proportions unless Shift is held).
//   * The selection is dragged to move it, nudged with the arrow keys
//     (Shift: further) and removed with Delete. Every change is one edit
//     (Ctrl+Z takes it back).
//   * Formatting (bold, italic, underline, strike-through, font, size,
//     colour) applies to the selected letters of the line being typed in,
//     or to all the selected lines; alignment lines up the selected lines.
#include "PdfView.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include <windowsx.h>

#include "RenderWorker.h"
#include "Util.h"

namespace {
const COLORREF kSelColor = RGB(0, 103, 192);
const COLORREF kImageColor = RGB(225, 120, 20);
constexpr DWORD kRopDestAndPattern = 0x00A000C9;  // DPa: tints like a highlighter

// Do two lines belong to the same block?
bool SameBlock(const TextRun& a, const TextRun& b) {
    const float sa = std::max(a.size, 1.0f), sb = std::max(b.size, 1.0f);
    if (std::max(sa, sb) / std::min(sa, sb) > 1.3f) return false;
    const float s = std::max(sa, sb);
    const RectF& p = a.rect;
    const RectF& q = b.rect;
    const float hGap = std::max(p.left, q.left) - std::min(p.right, q.right);  // < 0: they overlap
    const float vOverlap = std::min(p.bottom, q.bottom) - std::max(p.top, q.top);
    const float hp = p.bottom - p.top, hq = q.bottom - q.top;
    // Parts of one line (words in another font).
    if (vOverlap > 0.5f * std::min(hp, hq) && hGap < 1.0f * s) return true;
    // The next line of a paragraph: close below, overlapping, in the same weight.
    const float vGap = std::max(p.top, q.top) - std::min(p.bottom, q.bottom);
    return vGap < 0.6f * s && vGap > -0.3f * s && hGap < 0 && a.bold == b.bold;
}

std::vector<int> GroupBlocks(const std::vector<TextRun>& runs) {
    const int n = (int)runs.size();
    std::vector<int> parent((size_t)n);
    std::iota(parent.begin(), parent.end(), 0);
    auto find = [&](int i) {
        while (parent[(size_t)i] != i) i = parent[(size_t)i] = parent[(size_t)parent[(size_t)i]];
        return i;
    };
    std::vector<int> order((size_t)n);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int x, int y) { return runs[(size_t)x].rect.top < runs[(size_t)y].rect.top; });
    for (int k = 0; k < n; ++k) {
        const TextRun& a = runs[(size_t)order[(size_t)k]];
        const float reach = a.rect.bottom + 1.0f * std::max(a.size, 1.0f);
        for (int m = k + 1; m < n; ++m) {
            const TextRun& b = runs[(size_t)order[(size_t)m]];
            if (b.rect.top > reach) break;
            if (SameBlock(a, b)) parent[(size_t)find(order[(size_t)k])] = find(order[(size_t)m]);
        }
    }
    std::vector<int> block((size_t)n);
    for (int i = 0; i < n; ++i) block[(size_t)i] = find(i);
    return block;
}

RectF Union(const RectF& a, const RectF& b) {
    return {std::min(a.left, b.left), std::min(a.top, b.top), std::max(a.right, b.right), std::max(a.bottom, b.bottom)};
}
}  // namespace

const std::vector<PageImage>* PdfView::GetImages(int page) const {
    auto it = m_images.find(page);
    return it == m_images.end() ? nullptr : &it->second;
}

const std::vector<int>* PdfView::GetBlocks(int page) {
    auto it = m_blocks.find(page);
    if (it != m_blocks.end()) return &it->second;
    const std::vector<TextRun>* runs = GetRuns(page, false);
    if (!runs) return nullptr;
    return &(m_blocks[page] = GroupBlocks(*runs));
}

bool PdfView::HitImage(POINT pt, int& page, int& image) {
    image = -1;
    float x, y;
    if (!PagePoint(pt, page, x, y)) return false;
    GetRuns(page, true);  // pictures arrive with the lines
    const std::vector<PageImage>* images = GetImages(page);
    if (!images || page < 0 || page >= PageCount()) return false;
    const SizeF size = m_sizes[(size_t)page];
    // The topmost one; pictures covering (almost) the whole page are the
    // page's background or a scan and are left alone.
    for (int i = (int)images->size() - 1; i >= 0; --i) {
        const RectF& r = (*images)[(size_t)i].rect;
        if (!r.Contains(x, y)) continue;
        if (r.right - r.left > size.w * 0.9f && r.bottom - r.top > size.h * 0.9f) continue;
        image = i;
        return true;
    }
    return false;
}

const PageImage* PdfView::SelectedImageInfo() const {
    if (m_selImage < 0) return nullptr;
    const std::vector<PageImage>* images = GetImages(m_selPage);
    if (!images) return nullptr;
    for (const PageImage& img : *images)
        if (img.index == m_selImage) return &img;
    return nullptr;
}

bool PdfView::SelectedImage(int& page, int& index) const {
    if (!SelectedImageInfo()) return false;
    page = m_selPage;
    index = m_selImage;
    return true;
}

int PdfView::HitHandle(POINT pt) const {
    const PageImage* img = SelectedImageInfo();
    if (!img || m_selPage < 0 || m_selPage >= PageCount()) return -1;
    const RECT r = ClientRectOf(m_selPage, img->rect);
    const int reach = Dpi(7, m_dpi);
    const POINT corners[4] = {{r.left, r.top}, {r.right, r.top}, {r.right, r.bottom}, {r.left, r.bottom}};
    // The corners as seen (DraggedImageRect finds which page corner it is).
    for (int i = 0; i < 4; ++i)
        if (std::abs(pt.x - corners[i].x) <= reach && std::abs(pt.y - corners[i].y) <= reach) return i;
    return -1;
}

std::vector<int> PdfView::SelectedRuns() const {
    std::vector<int> out;
    if (m_selFirsts.empty()) return out;
    auto it = m_runs.find(m_selPage);
    if (it == m_runs.end()) return out;
    for (size_t i = 0; i < it->second.size(); ++i)
        if (std::find(m_selFirsts.begin(), m_selFirsts.end(), it->second[i].first) != m_selFirsts.end())
            out.push_back((int)i);
    return out;
}

bool PdfView::SelectionRect(RectF& out) const {
    if (const PageImage* img = SelectedImageInfo()) {
        out = img->rect;
        return true;
    }
    const std::vector<int> sel = SelectedRuns();
    if (sel.empty()) return false;
    const std::vector<TextRun>& runs = m_runs.at(m_selPage);
    out = runs[(size_t)sel.front()].rect;
    for (int i : sel) out = Union(out, runs[(size_t)i].rect);
    return true;
}

void PdfView::ClearEditSelection() {
    if (m_selFirsts.empty() && m_selImage < 0 && m_selPage < 0) return;
    m_selFirsts.clear();
    m_selImage = -1;
    m_selPage = -1;
    m_reselectPage = -1;
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
    Notify();
}

void PdfView::SelectBlock(int page, int run, bool toggle) {
    const std::vector<TextRun>* runs = GetRuns(page, false);
    const std::vector<int>* blocks = GetBlocks(page);
    if (!runs || !blocks || run < 0 || run >= (int)runs->size()) return;
    if (page != m_selPage || m_selImage >= 0 || !toggle) {
        m_selFirsts.clear();
        m_selImage = -1;
    }
    m_selPage = page;
    const int block = (*blocks)[(size_t)run];
    const bool had = std::find(m_selFirsts.begin(), m_selFirsts.end(), (*runs)[(size_t)run].first) != m_selFirsts.end();
    for (size_t i = 0; i < runs->size(); ++i) {
        if ((*blocks)[i] != block) continue;
        const int first = (*runs)[i].first;
        auto it = std::find(m_selFirsts.begin(), m_selFirsts.end(), first);
        if (toggle && had) {
            if (it != m_selFirsts.end()) m_selFirsts.erase(it);
        } else if (it == m_selFirsts.end()) {
            m_selFirsts.push_back(first);
        }
    }
    if (m_selFirsts.empty()) m_selPage = -1;
    InvalidateRect(m_hwnd, nullptr, FALSE);
    Notify();
}

// New lines arrived for the page with the selection: keep only the
// selected lines that are still there, or (after formatting) select the
// lines in the formatted area again.
void PdfView::Reselect(int page, const std::vector<TextRun>& runs) {
    if (m_reselectPage == page) {
        m_reselectPage = -1;
        m_selFirsts.clear();
        for (const TextRun& r : runs) {
            const float cx = (r.rect.left + r.rect.right) / 2, cy = (r.rect.top + r.rect.bottom) / 2;
            if (cx >= m_reselectRect.left - 1 && cx <= m_reselectRect.right + 1 && cy >= m_reselectRect.top - 1 &&
                cy <= m_reselectRect.bottom + 1)
                m_selFirsts.push_back(r.first);
        }
        if (m_selFirsts.empty() && m_selImage < 0) m_selPage = -1;
        Notify();
        return;
    }
    std::vector<int> kept;
    for (int first : m_selFirsts)
        for (const TextRun& r : runs)
            if (r.first == first) kept.push_back(first);
    m_selFirsts = kept;
    if (m_selImage >= 0) {
        bool found = false;
        if (const std::vector<PageImage>* images = GetImages(page))
            for (const PageImage& img : *images) found = found || img.index == m_selImage;
        if (!found) m_selImage = -1;
    }
    if (m_selFirsts.empty() && m_selImage < 0) m_selPage = -1;
    Notify();
}

PointF PdfView::ClampedPagePoint(POINT pt, int page) const {
    const PageLayout& L = m_layout[(size_t)page];
    const int64_t docX = m_scrollX + pt.x, docY = m_scrollY + pt.y;
    float x = (float)(std::min<int64_t>(std::max<int64_t>(docX - L.left, 0), L.w) / m_scale);
    float y = (float)(std::min<int64_t>(std::max<int64_t>(docY - L.top, 0), L.h) / m_scale);
    FromView(x, y, page);
    return {x, y};
}

// The selected picture's rectangle while a corner is dragged.
RectF PdfView::DraggedImageRect() const {
    const PageImage* img = SelectedImageInfo();
    if (!img) return {};
    const RectF r = img->rect;
    const float w0 = std::max(1.0f, r.right - r.left), h0 = std::max(1.0f, r.bottom - r.top);
    // The corner held is found from where the press was (nearest corner,
    // in page points), so a turned view needs no special case.
    const float px = m_pressAt.x, py = m_pressAt.y;
    const bool leftSide = std::fabs(px - r.left) < std::fabs(px - r.right);
    const bool topSide = std::fabs(py - r.top) < std::fabs(py - r.bottom);
    const float ax = leftSide ? r.right : r.left, ay = topSide ? r.bottom : r.top;  // the corner that stays
    const float mx = (leftSide ? r.left : r.right) + (m_dragAt.x - m_pressAt.x);
    const float my = (topSide ? r.top : r.bottom) + (m_dragAt.y - m_pressAt.y);
    float w = std::fabs(mx - ax), h = std::fabs(my - ay);
    if (GetKeyState(VK_SHIFT) >= 0) {  // keep the proportions
        const float k = std::max(w / w0, h / h0);
        w = w0 * k;
        h = h0 * k;
    }
    w = std::max(w, 4.0f);
    h = std::max(h, 4.0f);
    RectF out;
    out.left = mx < ax ? ax - w : ax;
    out.top = my < ay ? ay - h : ay;
    out.right = out.left + w;
    out.bottom = out.top + h;
    return out;
}

// ---------------------------------------------------------------------------
// Mouse and keys
// ---------------------------------------------------------------------------
bool PdfView::EditMouseDown(POINT pt, WPARAM wp) {
    const bool ctrl = (wp & MK_CONTROL) != 0;
    m_pressPt = pt;
    m_pressRun = -1;
    m_corner = HitHandle(pt);
    if (m_corner >= 0) {  // resize the selected picture
        m_pressPage = m_selPage;
        m_pressAt = m_dragAt = ClampedPagePoint(pt, m_selPage);
        m_editDrag = EditDrag::Resize;
        SetCapture(m_hwnd);
        return true;
    }
    int page, run;
    if (HitRun(pt, page, run)) {
        if (ctrl) {
            SelectBlock(page, run, true);
            return true;
        }
        m_pressPage = page;
        m_pressRun = run;
        m_pressAt = m_dragAt = ClampedPagePoint(pt, page);
        m_editDrag = EditDrag::Armed;
        SetCapture(m_hwnd);
        return true;
    }
    int image;
    if (HitImage(pt, page, image)) {
        m_selFirsts.clear();
        m_selPage = page;
        m_selImage = (*GetImages(page))[(size_t)image].index;
        m_pressPage = page;
        m_pressAt = m_dragAt = ClampedPagePoint(pt, page);
        m_editDrag = EditDrag::Armed;
        SetCapture(m_hwnd);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        Notify();
        return true;
    }
    float x, y;
    if (PagePoint(pt, page, x, y)) {  // an empty part of a page: draw a box
        if (!ctrl || page != m_selPage) ClearEditSelection();
        m_pressPage = page;
        m_pressAt = m_dragAt = {x, y};
        m_editDrag = EditDrag::Box;
        SetCapture(m_hwnd);
        return true;
    }
    ClearEditSelection();
    return false;
}

void PdfView::EditMouseMove(POINT pt) {
    if (m_pressPage < 0 || m_pressPage >= PageCount()) return;
    m_dragAt = ClampedPagePoint(pt, m_pressPage);
    if (m_editDrag == EditDrag::Armed) {
        const int slop = Dpi(4, m_dpi);
        if (std::abs(pt.x - m_pressPt.x) <= slop && std::abs(pt.y - m_pressPt.y) <= slop) return;
        // Dragging a line moves its block (or the selection it is part of).
        if (m_pressRun >= 0) {
            const std::vector<TextRun>* runs = GetRuns(m_pressPage, false);
            const bool selected = runs && m_pressPage == m_selPage &&
                                  std::find(m_selFirsts.begin(), m_selFirsts.end(), (*runs)[(size_t)m_pressRun].first) !=
                                      m_selFirsts.end();
            if (!selected) SelectBlock(m_pressPage, m_pressRun, false);
        }
        m_editDrag = EditDrag::Move;
        SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void PdfView::EditMouseUp(POINT pt) {
    const EditDrag drag = m_editDrag;
    m_editDrag = EditDrag::None;  // before ReleaseCapture (WM_CAPTURECHANGED)
    ReleaseCapture();
    if (m_pressPage < 0 || m_pressPage >= PageCount()) return;
    m_dragAt = ClampedPagePoint(pt, m_pressPage);
    const float dx = m_dragAt.x - m_pressAt.x, dy = m_dragAt.y - m_pressAt.y;
    switch (drag) {
        case EditDrag::Armed:
            if (m_pressRun >= 0) {  // a click on a line: select its block and type in the line
                SelectBlock(m_pressPage, m_pressRun, false);
                BeginInlineEdit(m_pressPage, m_pressRun);
            }
            break;
        case EditDrag::Move:
            if (std::fabs(dx) >= 0.5f || std::fabs(dy) >= 0.5f) MoveSelection(dx, dy);
            break;
        case EditDrag::Resize: {
            const PageImage* img = SelectedImageInfo();
            if (!img || !onEdit) break;
            const RectF r = DraggedImageRect();
            if (std::fabs(r.left - img->rect.left) < 0.5f && std::fabs(r.right - img->rect.right) < 0.5f &&
                std::fabs(r.top - img->rect.top) < 0.5f && std::fabs(r.bottom - img->rect.bottom) < 0.5f)
                break;
            EditOp op;
            op.kind = EditOp::SetImageRect;
            op.page = m_selPage;
            op.index = m_selImage;
            op.rect = r;
            onEdit(std::move(op));
            break;
        }
        case EditDrag::Box: {
            const RectF box{std::min(m_pressAt.x, m_dragAt.x), std::min(m_pressAt.y, m_dragAt.y),
                            std::max(m_pressAt.x, m_dragAt.x), std::max(m_pressAt.y, m_dragAt.y)};
            if (box.right - box.left < 2 && box.bottom - box.top < 2) break;  // a click
            const std::vector<TextRun>* runs = GetRuns(m_pressPage, false);
            if (m_pressPage != m_selPage || m_selImage >= 0) {
                m_selFirsts.clear();
                m_selImage = -1;
            }
            m_selPage = m_pressPage;
            // Lines mostly inside the box.
            if (runs)
                for (const TextRun& r : *runs) {
                    const float w = std::min(r.rect.right, box.right) - std::max(r.rect.left, box.left);
                    const float h = std::min(r.rect.bottom, box.bottom) - std::max(r.rect.top, box.top);
                    const float area = (r.rect.right - r.rect.left) * (r.rect.bottom - r.rect.top);
                    if (w > 0 && h > 0 && w * h >= 0.6f * area &&
                        std::find(m_selFirsts.begin(), m_selFirsts.end(), r.first) == m_selFirsts.end())
                        m_selFirsts.push_back(r.first);
                }
            // No lines: a picture inside the box.
            if (m_selFirsts.empty())
                if (const std::vector<PageImage>* images = GetImages(m_pressPage))
                    for (const PageImage& img : *images)
                        if (img.rect.left >= box.left && img.rect.right <= box.right && img.rect.top >= box.top &&
                            img.rect.bottom <= box.bottom)
                            m_selImage = img.index;
            if (m_selFirsts.empty() && m_selImage < 0) m_selPage = -1;
            Notify();
            break;
        }
        default: break;
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

bool PdfView::EditKeyDown(WPARAM wp) {
    const bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
    if (ctrl && (wp == 'I' || wp == 'U')) {
        SendMessageW(GetParent(m_hwnd), WM_COMMAND, wp == 'I' ? ID_FMT_ITALIC : ID_FMT_UNDERLINE, 0);
        return true;
    }
    RectF sel;
    if (!SelectionRect(sel)) return false;
    switch (wp) {
        case VK_DELETE:
        case VK_BACK:
            DeleteSelection();
            return true;
        case VK_ESCAPE:
            ClearEditSelection();
            return true;
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN: {
            if (ctrl) return false;
            const float step = shift ? 10.0f : 1.0f;
            // The arrow is a direction on the screen; on a turned view it is
            // another direction on the page.
            float vx = wp == VK_LEFT ? -step : wp == VK_RIGHT ? step : 0;
            float vy = wp == VK_UP ? -step : wp == VK_DOWN ? step : 0;
            const float cx = DispW(m_selPage) / 2, cy = DispH(m_selPage) / 2;
            float ax = cx, ay = cy, bx = cx + vx, by = cy + vy;
            FromView(ax, ay, m_selPage);
            FromView(bx, by, m_selPage);
            MoveSelection(bx - ax, by - ay);
            return true;
        }
        default: return false;
    }
}

// ---------------------------------------------------------------------------
// Edits
// ---------------------------------------------------------------------------
void PdfView::MoveSelection(float dx, float dy) {
    if (!onEdit || m_selPage < 0) return;
    EditOp op;
    op.kind = EditOp::MoveObjects;
    op.page = m_selPage;
    op.x = dx;
    op.y = dy;
    if (m_selImage >= 0) {
        op.objects = {m_selImage};
    } else {
        const std::vector<int> sel = SelectedRuns();
        if (sel.empty()) return;
        const std::vector<TextRun>& runs = m_runs.at(m_selPage);
        for (int i : sel)
            for (int k = 0; k < runs[(size_t)i].count; ++k) op.objects.push_back(runs[(size_t)i].first + k);
        SelectionRect(op.rect);
    }
    // Moving keeps every object's place in the page's list, so the
    // selection stays selected.
    onEdit(std::move(op));
}

void PdfView::DeleteSelection() {
    if (!onEdit || m_selPage < 0) return;
    EditOp op;
    op.kind = EditOp::DeleteObjects;
    op.page = m_selPage;
    if (m_selImage >= 0) {
        op.objects = {m_selImage};
    } else {
        const std::vector<int> sel = SelectedRuns();
        if (sel.empty()) return;
        const std::vector<TextRun>& runs = m_runs.at(m_selPage);
        for (int i : sel)
            for (int k = 0; k < runs[(size_t)i].count; ++k) op.objects.push_back(runs[(size_t)i].first + k);
        SelectionRect(op.rect);
    }
    ClearEditSelection();
    onEdit(std::move(op));
}

bool PdfView::FormatTarget(TextRun& run) const {
    if (m_inlineEdit && m_inlineKind == InlineKind::Run) {
        run = m_inlineRun;
        return true;
    }
    const std::vector<int> sel = SelectedRuns();
    if (sel.empty()) return false;
    run = m_runs.at(m_selPage)[(size_t)sel.front()];
    return true;
}

bool PdfView::ApplyFormat(const EditOp::Format& format) {
    if (!onEdit) return false;
    EditOp op;
    op.kind = EditOp::FormatText;
    op.format = format;
    if (m_inlineEdit && m_inlineKind == InlineKind::Run) {
        // The selected letters of the line being typed in (none: all of it),
        // with what was typed.
        std::wstring text((size_t)GetWindowTextLengthW(m_inlineEdit) + 1, L'\0');
        text.resize((size_t)GetWindowTextW(m_inlineEdit, text.data(), (int)text.size()));
        DWORD from = 0, to = 0;
        SendMessageW(m_inlineEdit, EM_GETSEL, (WPARAM)&from, (LPARAM)&to);
        EditOp::RunRef ref;
        ref.index = m_inlineRun.first;
        ref.count = m_inlineRun.count;
        ref.text = m_inlineRun.text;
        if (text != m_inlineRun.text) ref.newText = text;
        if (from != to) {
            ref.from = (int)std::min(from, to);
            ref.to = (int)std::max(from, to);
        }
        op.page = m_inlinePage;
        op.runs.push_back(ref);
        EndInlineEdit(false);
        ClearEditSelection();
        onEdit(std::move(op));
        return true;
    }
    const std::vector<int> sel = SelectedRuns();
    if (sel.empty()) return false;
    const std::vector<TextRun>& runs = m_runs.at(m_selPage);
    for (int i : sel) {
        EditOp::RunRef ref;
        ref.index = runs[(size_t)i].first;
        ref.count = runs[(size_t)i].count;
        ref.text = runs[(size_t)i].text;
        op.runs.push_back(ref);
    }
    op.page = m_selPage;
    // The lines get new page objects: select what is in the area again.
    RectF area;
    SelectionRect(area);
    area.right += std::max(0.0f, area.right - area.left) * 0.5f;  // bold text is wider
    const int page = m_selPage;
    m_selFirsts.clear();
    m_reselectPage = page;
    m_reselectRect = area;
    onEdit(std::move(op));
    return true;
}

// Formats one whole line (the right-click menu).
void PdfView::FormatRun(int page, int run, const EditOp::Format& format) {
    const std::vector<TextRun>* runs = GetRuns(page, false);
    if (!runs || run < 0 || run >= (int)runs->size() || !onEdit) return;
    EditOp op;
    op.kind = EditOp::FormatText;
    op.page = page;
    op.format = format;
    EditOp::RunRef ref;
    ref.index = (*runs)[(size_t)run].first;
    ref.count = (*runs)[(size_t)run].count;
    ref.text = (*runs)[(size_t)run].text;
    op.runs.push_back(ref);
    ClearEditSelection();
    onEdit(std::move(op));
}

bool PdfView::AlignSelection(int how) {
    if (!onEdit) return false;
    if (m_inlineEdit && m_inlineKind == InlineKind::Run) {
        // Aligning the block of the line being typed in.
        const int page = m_inlinePage;
        const int first = m_inlineRun.first;
        std::wstring text((size_t)GetWindowTextLengthW(m_inlineEdit) + 1, L'\0');
        text.resize((size_t)GetWindowTextW(m_inlineEdit, text.data(), (int)text.size()));
        const bool typed = text != m_inlineRun.text;
        EndInlineEdit(typed);
        if (typed) return true;  // the typing is kept first; its line changes
        const std::vector<TextRun>* runs = GetRuns(page, false);
        if (!runs) return false;
        for (size_t i = 0; i < runs->size(); ++i)
            if ((*runs)[i].first == first) SelectBlock(page, (int)i, false);
    }
    const std::vector<int> sel = SelectedRuns();
    if (sel.empty()) return false;
    const std::vector<TextRun>& runs = m_runs.at(m_selPage);
    RectF bounds;
    SelectionRect(bounds);
    // The selected parts that share a line move together.
    struct Line {
        RectF rect;
        std::vector<int> runs;
    };
    std::vector<Line> lines;
    for (int i : sel) {
        const RectF& r = runs[(size_t)i].rect;
        Line* found = nullptr;
        for (Line& l : lines) {
            const float overlap = std::min(l.rect.bottom, r.bottom) - std::max(l.rect.top, r.top);
            if (overlap > 0.5f * std::min(l.rect.bottom - l.rect.top, r.bottom - r.top)) found = &l;
        }
        if (found) {
            found->rect = Union(found->rect, r);
            found->runs.push_back(i);
        } else {
            lines.push_back({r, {i}});
        }
    }
    // One line (or lines all as wide): line up with the page's text instead.
    bool sameWidth = true;
    for (const Line& l : lines)
        if (std::fabs(l.rect.left - bounds.left) > 1 || std::fabs(l.rect.right - bounds.right) > 1) sameWidth = false;
    RectF area = bounds;
    if (sameWidth) {
        area = runs.front().rect;
        for (const TextRun& r : runs) area = Union(area, r.rect);
        if (how == 1) {  // centred on the page
            const float w = m_sizes[(size_t)m_selPage].w;
            const float half = std::max(area.right - w / 2, w / 2 - area.left);
            area.left = w / 2 - half;
            area.right = w / 2 + half;
        }
    }
    EditOp op;
    op.kind = EditOp::MoveObjects;
    op.page = m_selPage;
    op.rect = bounds;
    bool any = false;
    for (const Line& l : lines) {
        const RectF& r = l.rect;
        const float dx = how == 0 ? area.left - r.left
                         : how == 2 ? area.right - r.right
                                    : (area.left + area.right) / 2 - (r.left + r.right) / 2;
        if (std::fabs(dx) >= 0.3f) any = true;
        for (int i : l.runs)
            for (int k = 0; k < runs[(size_t)i].count; ++k) {
                op.objects.push_back(runs[(size_t)i].first + k);
                op.points.push_back({dx, 0});
            }
    }
    if (any) onEdit(std::move(op));
    return true;
}

// Right-click on a picture while editing.
bool PdfView::ShowImageMenu(POINT screen, POINT client) {
    int page, image;
    if (!HitImage(client, page, image)) return false;
    m_selFirsts.clear();
    m_selPage = page;
    m_selImage = (*GetImages(page))[(size_t)image].index;
    InvalidateRect(m_hwnd, nullptr, FALSE);
    Notify();
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1, L"&Replace picture\x2026");
    AppendMenuW(m, MF_STRING, 2, L"&Delete picture\tDel");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 3, L"&Insert a new picture\x2026");
    AppendMenuW(m, MF_STRING, 4, L"&Paste a picture\tCtrl+V");
    const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, m_hwnd, nullptr);
    DestroyMenu(m);
    HWND main = GetParent(m_hwnd);
    if (cmd == 1) SendMessageW(main, WM_COMMAND, ID_REPLACE_IMAGE, 0);
    if (cmd == 2) DeleteSelection();
    if (cmd == 3) SendMessageW(main, WM_COMMAND, ID_INSERT_IMAGE, 0);
    if (cmd == 4) SendMessageW(main, WM_COMMAND, ID_PASTE_IMAGE, 0);
    return true;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
void PdfView::DrawEditSelection(HDC dc, int page, const RECT& vis) {
    const int pad = std::max(1, Dpi(3, m_dpi));
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    // Pictures: a dotted frame each.
    if (const std::vector<PageImage>* images = GetImages(page)) {
        HPEN dotted = CreatePen(PS_DOT, 1, kImageColor);
        HGDIOBJ oldPen = SelectObject(dc, dotted);
        SetBkMode(dc, TRANSPARENT);
        const SizeF size = m_sizes[(size_t)page];
        for (const PageImage& img : *images) {
            if (img.rect.right - img.rect.left > size.w * 0.9f && img.rect.bottom - img.rect.top > size.h * 0.9f) continue;
            const RECT r = ClientRectOf(page, img.rect);
            if (r.right <= vis.left || r.left >= vis.right || r.bottom <= vis.top || r.top >= vis.bottom) continue;
            Rectangle(dc, r.left, r.top, r.right, r.bottom);
        }
        SelectObject(dc, oldPen);
        DeleteObject(dotted);
    }
    // The block under the mouse: a frame around all of it.
    if (page == m_hoverPage && m_hoverRun >= 0 && m_editDrag == EditDrag::None) {
        const std::vector<TextRun>* runs = GetRuns(page, false);
        const std::vector<int>* blocks = GetBlocks(page);
        if (runs && blocks && m_hoverRun < (int)runs->size()) {
            const int block = (*blocks)[(size_t)m_hoverRun];
            RectF area = (*runs)[(size_t)m_hoverRun].rect;
            for (size_t i = 0; i < runs->size(); ++i)
                if ((*blocks)[i] == block) area = Union(area, (*runs)[i].rect);
            RECT r = ClientRectOf(page, area);
            InflateRect(&r, pad * 2, pad * 2);
            HPEN dash = CreatePen(PS_DASH, 1, kSelColor);
            HGDIOBJ oldPen = SelectObject(dc, dash);
            SetBkMode(dc, TRANSPARENT);
            Rectangle(dc, r.left, r.top, r.right, r.bottom);
            SelectObject(dc, oldPen);
            DeleteObject(dash);
        }
    }
    // The selection.
    if (page == m_selPage) {
        HPEN solid = CreatePen(PS_SOLID, std::max(1, Dpi(2, m_dpi)), kSelColor);
        HGDIOBJ oldPen = SelectObject(dc, solid);
        if (const PageImage* img = SelectedImageInfo()) {
            const RECT r = ClientRectOf(page, img->rect);
            Rectangle(dc, r.left, r.top, r.right, r.bottom);
            const int hs = Dpi(4, m_dpi);
            HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
            HGDIOBJ b = SelectObject(dc, white);
            const POINT corners[4] = {{r.left, r.top}, {r.right, r.top}, {r.right, r.bottom}, {r.left, r.bottom}};
            for (const POINT& c : corners) Rectangle(dc, c.x - hs, c.y - hs, c.x + hs + 1, c.y + hs + 1);
            SelectObject(dc, b);
            DeleteObject(white);
        } else {
            const std::vector<int> sel = SelectedRuns();
            if (!sel.empty()) {
                const std::vector<TextRun>& runs = m_runs.at(page);
                HBRUSH tint = CreateSolidBrush(RGB(200, 222, 255));
                for (int i : sel) {
                    RECT r = ClientRectOf(page, runs[(size_t)i].rect);
                    InflateRect(&r, pad / 2, pad / 2);
                    HGDIOBJ b = SelectObject(dc, tint);
                    BitBlt(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, nullptr, 0, 0, kRopDestAndPattern);
                    SelectObject(dc, b);
                }
                DeleteObject(tint);
                RectF area;
                SelectionRect(area);
                RECT r = ClientRectOf(page, area);
                InflateRect(&r, pad * 2, pad * 2);
                SelectObject(dc, GetStockObject(NULL_BRUSH));
                Rectangle(dc, r.left, r.top, r.right, r.bottom);
            }
        }
        SelectObject(dc, oldPen);
        DeleteObject(solid);
    }
    SelectObject(dc, oldBrush);
}

// What a drag will do: the selection's outline at its new place, the
// picture's new size, or the box being drawn.
void PdfView::DrawEditDrag(HDC dc) {
    if (m_editDrag != EditDrag::Move && m_editDrag != EditDrag::Resize && m_editDrag != EditDrag::Box) return;
    if (m_pressPage < 0 || m_pressPage >= PageCount()) return;
    HPEN dash = CreatePen(PS_DASH, 1, kSelColor);
    HGDIOBJ oldPen = SelectObject(dc, dash);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    SetBkMode(dc, TRANSPARENT);
    const float dx = m_dragAt.x - m_pressAt.x, dy = m_dragAt.y - m_pressAt.y;
    if (m_editDrag == EditDrag::Move) {
        if (const PageImage* img = SelectedImageInfo()) {
            const RECT r = ClientRectOf(m_selPage, {img->rect.left + dx, img->rect.top + dy, img->rect.right + dx, img->rect.bottom + dy});
            Rectangle(dc, r.left, r.top, r.right, r.bottom);
        } else if (m_selPage >= 0) {
            const std::vector<int> sel = SelectedRuns();
            const std::vector<TextRun>& runs = m_runs.at(m_selPage);
            for (int i : sel) {
                const RectF& q = runs[(size_t)i].rect;
                const RECT r = ClientRectOf(m_selPage, {q.left + dx, q.top + dy, q.right + dx, q.bottom + dy});
                Rectangle(dc, r.left, r.top, r.right, r.bottom);
            }
        }
    } else if (m_editDrag == EditDrag::Resize) {
        const RECT r = ClientRectOf(m_selPage, DraggedImageRect());
        Rectangle(dc, r.left, r.top, r.right, r.bottom);
    } else {
        const RECT r = ClientRectOf(m_pressPage, {std::min(m_pressAt.x, m_dragAt.x), std::min(m_pressAt.y, m_dragAt.y),
                                                  std::max(m_pressAt.x, m_dragAt.x), std::max(m_pressAt.y, m_dragAt.y)});
        Rectangle(dc, r.left, r.top, r.right, r.bottom);
    }
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(dash);
}
