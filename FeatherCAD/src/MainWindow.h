// MainWindow.h - the Feather CAD window: menu strip, tool bar, drawing
// canvas, command line and status bar. It owns the drawing and the command
// engine, and handles what the engine leaves to the window (files,
// dialogs, zooming).
#pragma once
#include "Canvas.h"
#include "CommandLine.h"
#include "CommandPalette.h"
#include "Common.h"
#include "Document.h"
#include "Engine.h"
#include "History.h"
#include "Settings.h"
#include "StatusBar.h"
#include "Toolbar.h"

class MainWindow : public EngineHost {
public:
    MainWindow();
    ~MainWindow() override;
    bool Create(HINSTANCE inst, int showCmd, const std::wstring& file);
    HWND Hwnd() const { return m_hwnd; }
    HACCEL Accelerators() const { return m_accel; }

    // EngineHost
    void Message(const std::wstring& line) override;
    Vec2 TrackedCursor() override;
    double PickRadius() override;
    bool HostCommand(const std::wstring& name) override;
    void ZoomExtents() override;
    void ZoomWindow(const Box& box) override;
    void ZoomPrevious() override;
    void ZoomFactor(double factor) override;
    void DraftingChanged() override;

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    void CreateChildren();
    void CreateAccelerators();
    void Layout();
    void OnCommand(int id);
    void Run(const std::wstring& name, const wchar_t* keyword = nullptr);
    void AfterInput();
    void FocusCommandLine();
    void UpdatePrompt();
    void UpdateStatus();
    void UpdateTitle();

    // menus
    HMENU BuildMenu(int menuId);
    void ShowMenu(int menuId);
    void ShowContextMenu(int x, int y);
    void ShowPalette();
    void ShowTips();

    // files
    std::wstring DrawingName() const;
    bool ConfirmDiscard();
    void NewDrawing();
    void OpenDialog();
    bool OpenFile(const std::wstring& path);
    bool Save(bool saveAs);
    bool SaveTo(const std::wstring& path);
    void ExportDxf();
    void ShowHistory();
    void Recover(bool afterCrash);
    void Autosave();

    void ApplyTheme(int mode);
    void ToggleDrafting(int id);
    void QuickSelect();

    HWND m_hwnd = nullptr;
    bool m_ready = false;  // all child windows created
    HACCEL m_accel = nullptr;
    int m_dpi = 96;
    Settings m_settings;
    Document m_doc;
    Engine m_engine;
    Canvas m_canvas;
    CommandLine m_cmdline;
    StatusBar m_status;
    Toolbar m_menuBar, m_tools;
    CommandPalette m_palette;
    History m_history;

    std::wstring m_path;           // empty: never saved
    std::wstring m_suggestedPath;  // for Save of a recovered drawing
    std::wstring m_untitledName = L"Drawing1";
    int m_untitledCount = 1;
    uint64_t m_autosavedRevision = 0;
    ULONGLONG m_lastAutosave = 0;
    std::wstring m_shownTitle;
};
