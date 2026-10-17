// MainWindow.h - the CurveForge window: toolbars, data table, graph,
// results, and every command.
#pragma once
#include "Chart.h"
#include "DataGrid.h"
#include "Engine.h"
#include "Settings.h"
#include "Toolbar.h"

class MainWindow {
public:
    bool Create(HINSTANCE inst, int showCmd, const std::wstring& openPath);
    HWND Hwnd() const { return m_hwnd; }
    HACCEL Accelerators() const { return m_accel; }
    Settings& GetSettings() { return m_settings; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    void CreateChildren();
    void CreateAccelerators();
    void Layout();
    RECT ContentRect() const;
    int Splitter(POINT pt) const;  // 0 none, 1 table|graph, 2 graph|results
    void OnCommand(int id, int code, HWND ctl);

    // state shown in the window
    void UpdateTitle();
    void UpdateUi();
    void UpdateStatus();
    void SetEquationText();

    // documents
    bool ConfirmDiscard();
    void NewDocument();
    void OpenDialog();
    bool OpenPath(const std::wstring& path);
    void LoadTable(Table&& t, const std::wstring& path);
    bool Save(bool saveAs);
    void LoadExample(int index);

    // changes and undo
    void BeginChange();
    void Changed(bool columns, bool resetView);
    void Undo();
    void Redo();
    void ApplyDocument(bool resetView);

    // fitting
    void ScheduleFit(bool resetView);
    void RunFit();
    std::wstring ResultsText() const;
    std::wstring FittedEquation(bool excel) const;
    void UpdateChart();

    // data editing
    void CopyRows(bool cut);
    void Paste(bool asNewTable);
    void InsertRow();
    void DeleteRows();
    void ToggleExcluded();
    void IncludeAll();
    void TogglePoint(int point);
    void AddColumn();
    void FormulaColumn();
    void RenameColumn(int col);
    void DeleteColumn(int col);
    void SortRows(int col, bool ascending);
    void SetRole(int role, int col);  // 0 X, 1 Y, 2 sigma (col -1: none)
    void AddColumns(const std::vector<std::wstring>& names, const std::vector<std::vector<double>>& cols);

    // menus
    void ShowModelMenu();
    void ShowOrderMenu();
    void ShowRoleMenu(int role);
    void ShowExportMenu();
    void ShowMoreMenu();
    void ShowTableMenu(POINT pt);
    void ShowHeaderMenu(POINT pt, int col);
    void ShowChartMenu(POINT pt);
    void SetModel(ModelKind k, int order);

    // tools
    void BestFit();
    void Parameters();
    void Predict();
    void Batch();

    // export
    void ExportFittedCsv();
    void ExportChart(bool svg);
    void CopyResults();
    void SaveReport();
    void SaveTableCsv();

    void ShowSettings();
    void ShowAbout();
    void ShowShortcuts();
    void ApplyTheme();
    void SetOp(const std::wstring& text);

    HINSTANCE m_inst = nullptr;
    HWND m_hwnd = nullptr;
    HACCEL m_accel = nullptr;
    Settings m_settings;
    Toolbar m_toolbar, m_setup, m_status;
    HWND m_equation = nullptr;
    HWND m_results = nullptr;
    HFONT m_monoFont = nullptr;
    HBRUSH m_bgBrush = nullptr;
    DataGrid m_grid;
    Chart m_chart;
    int m_dpi = 96;

    Document m_doc;
    std::wstring m_path;       // project or data file
    bool m_isProject = false;  // m_path is a .cforge file
    bool m_modified = false;
    std::vector<Document> m_undo, m_redo;

    // the current fit
    FitRun m_fit;
    Points m_points;
    bool m_pendingReset = false;
    bool m_settingEquation = false;
    bool m_equationUndoTaken = false;

    int m_dragSplitter = 0;
    std::wstring m_opText, m_hoverText;
};
