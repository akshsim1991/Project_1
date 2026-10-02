// Dialogs.h - Settings, Parameters, Predict, results tables, Batch fit,
// formula columns, small text prompts and file pickers.
#pragma once
#include <functional>

#include "Fit.h"
#include "Settings.h"

bool ShowSettingsDialog(HINSTANCE inst, HWND owner, Settings& s);

// Starting values, fixed values and limits for the current equation's
// parameters. `fitted` holds the last fitted values (may be empty).
bool ShowParamsDialog(HINSTANCE inst, HWND owner, const std::vector<std::wstring>& names,
                      const std::vector<double>& fitted, std::vector<ParamOption>& options);

struct PredictContext {
    std::function<double(double)> f;
    std::function<bool(double x, double& conf, double& pred)> bands;  // may be empty
    double xmin = 0, xmax = 1;
    int digits = 6, confidence = 95;
    std::wstring xName = L"x", yName = L"y";
    std::function<void(const std::vector<double>& xs, const std::vector<double>& ys)> addColumns;
};
void ShowPredictDialog(HINSTANCE inst, HWND owner, const PredictContext& ctx);

struct TableDialogData {
    std::wstring title, info;
    std::vector<std::wstring> headers;
    std::vector<std::vector<std::wstring>> rows;
    bool canUse = false;  // "Use selected model" button
    int selected = 0;
};
// Returns the row chosen with "Use", or -1.
int ShowTableDialog(HINSTANCE inst, HWND owner, const TableDialogData& data, std::wstring& folder);

struct BatchChoice {
    bool columns = true;
    std::vector<std::wstring> files;
};
bool ShowBatchDialog(HINSTANCE inst, HWND owner, const std::wstring& info, std::wstring& folder, BatchChoice& choice);

bool ShowFormulaDialog(HINSTANCE inst, HWND owner, const std::vector<std::wstring>& variables, std::wstring& name,
                       std::wstring& formula);
bool ShowInputDialog(HINSTANCE inst, HWND owner, const std::wstring& title, const std::wstring& prompt,
                     std::wstring& text);

// File pickers. `folder` is the starting folder and receives the chosen one.
bool PickOpenFiles(HWND owner, const wchar_t* filter, std::wstring& folder, std::vector<std::wstring>& out,
                   bool multiple);
bool PickSaveFile(HWND owner, const wchar_t* filter, const wchar_t* ext, std::wstring& folder, std::wstring& name);

// "name, name2" text table helpers.
std::wstring JoinTsv(const std::vector<std::wstring>& headers, const std::vector<std::vector<std::wstring>>& rows);
std::wstring JoinCsv(const std::vector<std::wstring>& headers, const std::vector<std::vector<std::wstring>>& rows);
