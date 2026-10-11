// MainWindow.h - the Feather CAD window: menu strip, tool bar, drawing
// tabs, drawing canvas, command line and status bar. Each open drawing
// (one per tab) has its own document, command engine, undo history,
// auto-save history and view; the window handles what the engines leave
// to it (files, dialogs, zooming, the clipboard).
#pragma once
#include <memory>

#include "Canvas.h"
#include "CommandLine.h"
#include "CommandPalette.h"
#include "Common.h"
#include "Document.h"
#include "Engine.h"
#include "History.h"
#include "PropertiesPanel.h"
#include "Settings.h"
#include "StatusBar.h"
#include "TabBar.h"
#include "Toolbar.h"

class MainWindow : public EngineHost {
public:
    MainWindow();
    ~MainWindow() override;
    bool Create(HINSTANCE inst, int showCmd, const std::vector<std::wstring>& files);
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
    void SetClipboard(const std::string& text) override;
    bool GetClipboard(std::string& text) override;

private:
    // One open drawing.
    struct Drawing {
        explicit Drawing(EngineHost& host) : engine(doc, host) {}
        Document doc;
        Engine engine;
        History history;
        std::wstring path;           // empty: never saved
        std::wstring suggestedPath;  // for Save of a recovered or imported drawing
        std::wstring untitledName;
        uint64_t autosavedRevision = 0;
        unsigned long long lastAutosave = 0;
        Canvas::View view;
        bool Pristine() const { return path.empty() && doc.Count() == 0 && !doc.CanUndo() && !doc.Modified(); }
    };

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
    void UpdateTabs();

    // drawings
    Drawing& Cur() const { return *m_drawings[m_active]; }
    Document& Doc() const { return Cur().doc; }
    Engine& Eng() const { return Cur().engine; }
    std::unique_ptr<Drawing> MakeDrawing();
    // Puts a drawing in a tab of its own (or in place of an empty, unused one) and shows it.
    void AddDrawing(std::unique_ptr<Drawing> d);
    void SwitchTo(size_t index);
    bool CloseDrawing(size_t index);
    int FindOpen(const std::wstring& path) const;

    // menus
    HMENU BuildMenu(int menuId);
    void ShowMenu(int menuId);
    void ShowContextMenu(int x, int y);
    void ShowPalette();
    void ShowTips();

    // files
    std::wstring DrawingName(const Drawing& d) const;
    std::wstring DrawingName() const { return DrawingName(Cur()); }
    bool ConfirmDiscard(size_t index);
    void NewDrawing();
    void OpenDialog();
    bool OpenFile(const std::wstring& path);
    bool Save(bool saveAs);
    bool SaveTo(const std::wstring& path);
    void ExportDxf();
    void ImportDxf();
    void ShowHistory();
    void Recover(bool afterCrash);
    void Autosave();

    // dimensions and the clipboard
    void EditDimension(uint64_t id);
    void DimensionStyle();
    void ClipboardCommand(int id);

    // layers and the Properties panel
    void ShowLayers();
    void UpdateLayerCombo();
    void UpdateProperties(bool force = false);
    // Changes every selected object (one undo step).
    void ChangeSelected(const wchar_t* stepName, const std::function<void(Entity&)>& change);

    void ApplyTheme(int mode);
    void ToggleDrafting(int id);
    void QuickSelect();

    HWND m_hwnd = nullptr;
    bool m_ready = false;  // all child windows created
    HACCEL m_accel = nullptr;
    int m_dpi = 96;
    Settings m_settings;
    std::vector<std::unique_ptr<Drawing>> m_drawings;
    size_t m_active = 0;
    Canvas m_canvas;
    CommandLine m_cmdline;
    StatusBar m_status;
    Toolbar m_menuBar, m_tools;
    TabBar m_tabs;
    CommandPalette m_palette;
    PropertiesPanel m_props;
    HWND m_layerCombo = nullptr;
    std::vector<std::wstring> m_comboLayers;  // what the layer box shows
    struct PropsKey {
        size_t drawing = 0;
        uint64_t revision = 0, selection = 0;
        bool operator==(const PropsKey& o) const {
            return drawing == o.drawing && revision == o.revision && selection == o.selection;
        }
    } m_propsShown;
    int m_untitledCount = 0;
    std::wstring m_shownTitle;
    UINT m_clipFormat = 0;
};
