// Sheet.cpp - see Sheet.h.
#include "Sheet.h"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "DxfImport.h"

const std::vector<PaperSize>& PaperSizes() {
    static const std::vector<PaperSize> p = {
        {L"A4", 210, 297},       {L"A3", 297, 420},          {L"A2", 420, 594},          {L"A1", 594, 841},
        {L"A0", 841, 1189},      {L"Letter", 215.9, 279.4},  {L"Legal", 215.9, 355.6},   {L"ANSI B (Tabloid)", 279.4, 431.8},
        {L"ANSI C", 431.8, 558.8}, {L"ANSI D", 558.8, 863.6}, {L"ANSI E", 863.6, 1117.6},
    };
    return p;
}

const PaperSize* FindPaper(const std::wstring& name) {
    for (const PaperSize& p : PaperSizes())
        if (SameName(p.name, name)) return &p;
    return nullptr;
}

void ApplyPaper(SheetSetup& s) {
    const PaperSize* p = FindPaper(s.paper);
    if (!p) return;  // custom: as it is
    s.width = s.landscape ? p->height : p->width;
    s.height = s.landscape ? p->width : p->height;
}

Sheet::Sheet() {
    DrawingSettings ds;
    ds.units = Units::Millimetres;
    doc.SetSettings(ds);
    doc.MarkSaved();
}

double MmPerUnit(Units u) {
    switch (u) {
        case Units::Centimetres: return 10;
        case Units::Metres: return 1000;
        case Units::Inches: return 25.4;
        case Units::Feet: return 304.8;
        default: return 1;
    }
}

std::wstring ScaleText(double ratio) {
    if (!(ratio > 0)) return L"";
    auto num = [](double v) {
        DimStyle s;
        s.precision = 3;
        return DimNumber(v, s);
    };
    if (ratio >= 1 - 1e-9) return num(ratio) + L":1";
    return L"1:" + num(1 / ratio);
}

double ViewportRatio(const Entity& vp, const Document& model) {
    if (vp.type != EntityType::Viewport || !vp.annot) return 0;
    return vp.annot->viewScale / MmPerUnit(model.Settings().units);
}

const std::vector<double>& StandardScales() {
    static const std::vector<double> s = {10, 5, 2, 1, 0.5, 0.2, 0.1, 0.05, 0.02, 0.01, 0.005, 0.002, 0.001};
    return s;
}

double FitScale(const Box& extent, double w, double h, Units units) {
    if (extent.Empty() || (extent.Width() <= 0 && extent.Height() <= 0)) return 1;
    const double k = MmPerUnit(units);
    for (double r : StandardScales())
        if (extent.Width() * k * r <= w && extent.Height() * k * r <= h) return r;
    return StandardScales().back();
}

const std::vector<PropertyKey>& DrawingPropertyKeys() {
    static const std::vector<PropertyKey> k = {
        {L"title", L"Title"},       {L"number", L"Drawing number"}, {L"drawnby", L"Drawn by"},
        {L"checkedby", L"Checked by"}, {L"company", L"Company"},    {L"material", L"Material"},
        {L"revision", L"Revision"},
    };
    return k;
}

FieldValues SheetFields(const Document& model, const SheetList& sheets, size_t index, const FieldValues& base) {
    FieldValues f = base;
    for (const PropertyKey& k : DrawingPropertyKeys()) f[k.key] = L"";
    for (const auto& kv : model.Properties()) f[kv.first] = kv.second;
    if (index < sheets.size()) {
        f[L"sheetname"] = sheets[index]->setup.name;
        f[L"sheet"] = std::to_wstring(index + 1);
        f[L"sheets"] = std::to_wstring(sheets.size());
        f[L"paper"] = sheets[index]->setup.paper;
        for (const auto& kv : sheets[index]->doc.Entities())
            if (kv.second.type == EntityType::Viewport) {
                f[L"scale"] = ScaleText(ViewportRatio(kv.second, model));
                break;
            }
    }
    return f;
}

Box DrawingArea(const SheetSetup& s) {
    const double m = s.margin;
    // Above the title block (40 mm), a little in from the border.
    return Box(Vec2{m + 5, m + 45}, Vec2{s.width - m - 5, s.height - m - 5});
}

namespace {
constexpr wchar_t kTitleBlock[] = L"Title block";

Entity Styled(Entity e, int lineweight) {
    e.layer = 0;
    e.lineweight = (int16_t)lineweight;
    return e;
}

TextData Label(const std::wstring& s, double h, int justify = 0) {
    TextData t;
    t.content = s;
    t.height = h;
    t.justify = justify;
    return t;
}

// ISO-style title block, 180 x 40 mm, hanging from its bottom right corner.
BlockData TitleBlock() {
    BlockData b;
    b.name = kTitleBlock;
    const double W = 180, H = 40, R = 10;  // width, height, row height
    auto line = [&](double x0, double y0, double x1, double y1, int lw = 25) {
        b.items.push_back(Styled(Entity::Line({x0 - W, y0}, {x1 - W, y1}), lw));
    };
    auto cell = [&](double x, double y, const wchar_t* label, const wchar_t* field, double h = 3) {
        b.items.push_back(Styled(MakeText({x - W + 1.5, y + R - 1.2}, Label(label, 1.8)), 18));
        b.items.push_back(Styled(MakeText({x - W + 1.5, y + 1.8}, Label(field, h, 6)), 25));
    };
    b.items.push_back(Styled(Entity::Rectangle({-W, 0}, {0, H}), 50));
    for (int r = 1; r < 4; ++r) line(0, r * R, W, r * R);
    line(120, 0, 120, H);
    line(60, 0, 60, 3 * R);
    cell(0, 30, L"TITLE", L"{title}", 5);
    cell(120, 30, L"DRAWING NO.", L"{number}");
    cell(0, 20, L"DRAWN BY", L"{drawnby}");
    cell(60, 20, L"CHECKED BY", L"{checkedby}");
    cell(120, 20, L"DATE", L"{date}");
    cell(0, 10, L"COMPANY", L"{company}");
    cell(60, 10, L"SCALE", L"{scale}");
    cell(120, 10, L"SHEET", L"{sheet} / {sheets}");
    cell(0, 0, L"FILE", L"{filename}");
    cell(60, 0, L"MATERIAL", L"{material}");
    cell(120, 0, L"REVISION", L"{revision}");
    return b;
}

// The border is the closed rectangle drawn 0.7 mm wide.
bool IsBorder(const Entity& e) {
    return e.type == EntityType::Polyline && e.closed && e.verts.size() == 4 && e.lineweight == 70;
}
}  // namespace

void AddBorder(Sheet& sheet) {
    Document& d = sheet.doc;
    const SheetSetup& s = sheet.setup;
    d.BeginChange(L"Border");
    // The old border and title block go.
    std::vector<uint64_t> old;
    for (const auto& kv : d.Entities()) {
        const Entity& e = kv.second;
        if (IsBorder(e) || (e.type == EntityType::Insert && e.annot && e.annot->block &&
                               SameName(e.annot->block->name, kTitleBlock)))
            old.push_back(kv.first);
    }
    for (uint64_t id : old) d.Erase(id);
    const double m = s.margin;
    Entity border = Styled(Entity::Rectangle({m, m}, {s.width - m, s.height - m}), 70);
    d.Add(border);
    auto def = d.FindBlock(kTitleBlock);
    if (!def) def = d.DefineBlock(TitleBlock());
    Entity tb = MakeInsert(def, {s.width - m, m}, 1, 0);
    tb.layer = 0;
    d.Add(tb);
    d.EndChange();
}

std::unique_ptr<Sheet> NewSheet(const Document& model, const std::wstring& name, const std::wstring& paper, bool landscape) {
    auto sh = std::make_unique<Sheet>();
    sh->setup.name = name;
    sh->setup.paper = paper;
    sh->setup.landscape = landscape;
    ApplyPaper(sh->setup);
    if (sh->setup.width < 250 || sh->setup.height < 200) sh->setup.margin = 7;  // A4 and Letter
    AddBorder(*sh);
    const Box area = DrawingArea(sh->setup);
    const Box ext = model.Extents();
    const double ratio = FitScale(ext, area.Width(), area.Height(), model.Settings().units);
    const Vec2 centre = ext.Empty() ? Vec2{} : ext.Center();
    Entity vp = MakeViewport({area.minX, area.minY}, {area.maxX, area.maxY}, centre,
                             ratio * MmPerUnit(model.Settings().units));
    vp.layer = 0;
    sh->doc.Add(vp);
    sh->doc.MarkSaved();
    return sh;
}

// ---------------------------------------------------------------------------
// File
// ---------------------------------------------------------------------------

std::string DrawingToDxf(const Document& model, const SheetList& sheets) {
    if (sheets.empty()) return model.ToDxf();
    return model.ToDxf(&sheets[0]->doc, {sheets[0]->setup.width, sheets[0]->setup.height});
}

bool DrawingFromDxf(const std::string& text, Document& model, SheetList& sheets, std::wstring& error, std::wstring& notes) {
    DxfResult r;
    if (!model.FromDxf(text, error, notes, r)) return false;
    sheets.clear();
    for (DxfResult::Layout& l : r.layouts) {
        auto sh = std::make_unique<Sheet>();
        sh->setup.name = l.name;
        // The paper: its limits, or what is on it; a usual size if it is one.
        Box page = r.paperLimits;
        if (page.Empty() || page.Width() < 10 || page.Height() < 10)
            for (const Entity& e : l.entities) page.Add(Bounds(e));
        double w = page.Empty() ? 420 : page.maxX, h = page.Empty() ? 297 : page.maxY;
        sh->setup.paper = L"Custom";
        sh->setup.width = w;
        sh->setup.height = h;
        for (const PaperSize& p : PaperSizes()) {
            for (int land = 0; land < 2; ++land) {
                const double pw = land ? p.height : p.width, ph = land ? p.width : p.height;
                if (std::fabs(pw - w) <= 3 && std::fabs(ph - h) <= 3) {
                    sh->setup.paper = p.name;
                    sh->setup.landscape = land != 0;
                    ApplyPaper(sh->setup);
                }
            }
        }
        if (sh->setup.paper == L"Custom") sh->setup.landscape = w >= h;
        sh->doc.AddForeign(std::move(l.entities), r.layers, r.blocks);
        sh->doc.MarkSaved();
        sheets.push_back(std::move(sh));
    }
    return true;
}

std::string DrawingToText(const Document& model, const SheetList& sheets) {
    std::string out = model.ToText();
    for (const auto& sh : sheets) {
        const SheetSetup& s = sh->setup;
        out += "sheet " + FormatNumber(s.width) + " " + FormatNumber(s.height) + " " + (s.landscape ? "1" : "0") + " " +
               FormatNumber(s.margin) + " " + std::to_string(s.colourMode) + " " + (s.lineWeights ? "1" : "0");
        for (int p : s.pens) out += " " + std::to_string(p);
        out += " paper=" + FcadEncode(s.paper) + " name=" + FcadEncode(s.name) + "\n";
        out += sh->doc.ToText();
    }
    return out;
}

bool DrawingFromText(const std::string& text, Document& model, SheetList& sheets, std::wstring& error) {
    // The drawing comes first, up to its "end"; then the sheets, each
    // a header line and a document of its own up to its "end".
    std::vector<std::string> lines;
    {
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(std::move(line));
        }
    }
    size_t i = 0;
    while (i < lines.size() && lines[i] != "end") ++i;
    SheetList read;
    for (size_t k = i + 1; k < lines.size(); ++k) {
        if (lines[k].compare(0, 6, "sheet ") != 0) continue;
        std::istringstream words(lines[k].substr(6));
        std::vector<std::string> w;
        for (std::string x; words >> x;) w.push_back(x);
        auto sh = std::make_unique<Sheet>();
        SheetSetup& s = sh->setup;
        std::vector<double> v;
        for (const std::string& x : w) {
            double d;
            if (x.find('=') == std::string::npos && ParseNumber(x, d)) v.push_back(d);
            else if (x.compare(0, 6, "paper=") == 0) s.paper = FcadDecode(x.substr(6));
            else if (x.compare(0, 5, "name=") == 0) s.name = FcadDecode(x.substr(5));
        }
        if (v.size() >= 6) {
            s.width = v[0] > 1 ? v[0] : 420;
            s.height = v[1] > 1 ? v[1] : 297;
            s.landscape = v[2] != 0;
            s.margin = std::clamp(v[3], 0.0, std::min(s.width, s.height) / 3);
            s.colourMode = std::clamp((int)v[4], 0, 2);
            s.lineWeights = v[5] != 0;
            for (size_t p = 0; p < 10 && 6 + p < v.size(); ++p) s.pens[p] = std::clamp((int)v[6 + p], -1, 211);
        }
        std::string body;
        size_t j = k + 1;
        for (; j < lines.size(); ++j) {
            body += lines[j] + "\n";
            if (lines[j] == "end") break;
        }
        std::wstring err;
        if (!sh->doc.FromText(body, err)) {
            error = L"Sheet \"" + s.name + L"\": " + err;
            return false;
        }
        sh->doc.MarkSaved();
        read.push_back(std::move(sh));
        k = j;
    }
    if (!model.FromText(text, error)) return false;
    sheets = std::move(read);
    return true;
}
