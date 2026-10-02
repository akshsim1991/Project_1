// Data.h - the data table, importing text tables (CSV, TSV, pasted from
// Excel) and the project document.
#pragma once
#include "Models.h"
#include "Fit.h"

struct Table {
    std::vector<std::wstring> names;               // column names
    std::vector<std::vector<std::wstring>> rows;   // rows[r][c]; rows may be shorter than names

    int Cols() const { return (int)names.size(); }
    int Rows() const { return (int)rows.size(); }
    const std::wstring& Cell(int r, int c) const;
    void SetCell(int r, int c, const std::wstring& v);  // grows the table as needed
    double Value(int r, int c) const;                    // NaN if empty or not a number
    bool IsNumericColumn(int c) const;                   // mostly numbers
    void TrimEmptyRows();
};

// Parses a number written with '.' as the decimal separator. Also accepts
// a leading '+', exponents and "inf"/"nan". Returns false otherwise.
bool ParseNumber(const std::wstring& s, double& v);

namespace Import {
// Reads a text table: detects the encoding, the separator (tab, ';', ',',
// spaces), a header row and decimal commas. Lines starting with '#' are
// skipped.
bool ParseText(const std::wstring& text, Table& out, std::wstring& error);
bool ReadTextFile(const std::wstring& path, std::wstring& text);
bool WriteTextFile(const std::wstring& path, const std::wstring& text);  // UTF-8 with BOM
std::wstring ToCsvField(const std::wstring& s, wchar_t sep);
}  // namespace Import

// Everything that is saved in a project (.cforge) file.
struct Document {
    Table table;
    std::vector<bool> excluded;  // per row
    int xCol = 0, yCol = 1, sigmaCol = -1;
    ModelKind model = ModelKind::Linear;
    int order = 2;
    std::wstring customEquation = L"y = a*exp(-b*x) + c";
    std::vector<ParamOption> params;  // starting values, fixed, bounds
    bool robust = false;
    bool logX = false, logY = false;  // graph axes

    bool IsExcluded(int row) const { return row >= 0 && row < (int)excluded.size() && excluded[(size_t)row]; }
    void SetExcluded(int row, bool ex);
    std::wstring Equation() const;  // the equation that is fitted

    std::wstring Serialize() const;
    bool Deserialize(const std::wstring& text, std::wstring& error);
};
