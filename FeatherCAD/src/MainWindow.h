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
#include "GuideBar.h"
#include "History.h"
#include "PropertiesPanel.h"
#include "Plot.h"
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
    int EditText(TextData& text, const std::wstring& title) override;

private:
    // A sheet's own command engine and view.
    struct SheetSpace {
        SheetSpace(Sheet& sheet, EngineHost& host, const Document& model) : engine(sheet.doc, host) { engine.model = &model; }
        Engine engine;
        Canvas::View view;
    };
    // One open drawing, with its sheets.
    struct Drawing {
        explicit Drawing(EngineHost& host) : engine(doc, host) {}
        Document doc;
        Engine engine;
        SheetList sheets;
        std::vector<std::unique_ptr<SheetSpace>> spaces;  // one per sheet
        int space = -1;              // the sheet shown (-1: the drawing)
        bool sheetsChanged = false;  // sheets added, removed or set up since saved
        Document& ActiveDoc() { return space < 0 ? doc : sheets[(size_t)space]->doc; }
        Engine& ActiveEngine() { return space < 0 ? engine : spaces[(size_t)space]->engine; }
        Canvas::View& ActiveView() { return space < 0 ? view : spaces[(size_t)space]->view; }
        bool Modified() const {
            if (doc.Modified() || sheetsChanged) return true;
            for (const auto& s : sheets)
                if (s->doc.Modified()) return true;
            return false;
        }
        uint64_t Revision() const {
            uint64_t r = doc.Revision() + (sheetsChanged ? 1 : 0);
            for (const auto& s : sheets) r += s->doc.Revision() * 7;
            return r + sheets.size() * 1000003;
        }
        void MarkSaved() {
            doc.MarkSaved();
            for (auto& s : sheets) s->doc.MarkSaved();
            sheetsChanged = false;
        }
        void AddSheet(std::unique_ptr<Sheet> s, EngineHost& host) {
            spaces.push_back(std::make_unique<SheetSpace>(*s, host, doc));
            sheets.push_back(std::move(s));
        }
        History history;
        std::wstring path;           // empty: never saved
        std::wstring suggestedPath;  // for Save of a recovered or imported drawing
        std::wstring untitledName;
        uint64_t autosavedRevision = 0;
        unsigned long long lastAutosave = 0;
        Canvas::View view;
        bool Pristine() const {
            return path.empty() && doc.Count() == 0 && !doc.CanUndo() && !doc.Modified() && sheets.empty();
        }
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
    void UpdateGuide();
    void ApplyColour(int32_t color);
    void ThicknessMenu();
    void UpdateStatus();
    void UpdateTitle();
    void UpdateTabs();

    // drawings
    Drawing& Cur() const { return *m_drawings[m_active]; }
    Document& Doc() const { return Cur().ActiveDoc(); }  // of the drawing, or of the sheet shown
    Engine& Eng() const { return Cur().ActiveEngine(); }
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
    // Double-click: the dimension, text, leader or block's own editor.
    void EditObject(uint64_t id);
    void UpdateFields();
    void AttachCanvas();  // the canvas shows the active drawing's active sheet (or the drawing)
    void DimensionStyle();
    void ClipboardCommand(int id);

    // layers and the Properties panel
    void ShowLayers();
    void UpdateLayerCombo();
    void UpdateProperties(bool force = false);
    void AnnotationRows(const Entity& e, std::vector<PropertiesPanel::Row>& rows);
    void AnnotPointRows(std::vector<PropertiesPanel::Row>& rows, const Entity& e, const wchar_t* lx, const wchar_t* ly, Vec2 p,
                   std::function<void(Entity&, Vec2)> set);
    // Changes every selected object (one undo step).
    void ChangeSelected(const wchar_t* stepName, const std::function<void(Entity&)>& change);

    // sheets
    void ShowSpace(int space);  // -1: the drawing, else a sheet
    void UpdateSheetTabs();
    void NewSheetTab(const std::wstring& paper = L"");
    std::wstring m_examPaper;
    void SheetMenu(int index);
    void PageSetup();
    void DeleteSheet(int index);
    void DrawingProps();
    void ShowVariables();
    bool ChooseImage(std::wstring& path, int& width, int& height) override;
    int ChooseSymbol(std::wstring& name) override;
    bool NewExamSheet(const std::wstring& paper) override;
    void MyDetailsChanged() override;
    void LanguageChanged() override;
    void ApplyLanguage();            // the menu bar and hints in the chosen language (Lang.h)
    void LoadAliases(bool report);   // the user's aliases.txt (Personal.h), if it changed
    void EditAliases();              // ALIASEDIT: the file in Notepad
    void StartReplay();              // REPLAY: the drawing's steps again, one by one
    void ReplayStep();
    void StopReplay(bool toEnd);
    size_t m_replayLeft = 0, m_replayTotal = 0;
    FILETIME m_aliasStamp{};
    void ExportImage(bool png);  // the sheet shown, or the whole drawing, as a picture
    void OpenExample();          // Help > Examples: a solved drawing from the samples folder
    std::wstring m_lastSymbol;
    void TitleBlock();
    std::vector<PlotPage> BuildPages(const struct PlotRequest& r);
    void Plot();
    void ExportPdf(const std::vector<PlotPage>* pages = nullptr);
    void SetSheets(Drawing& d, SheetList sheets);
    FieldValues FieldsFor(int space) const;

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
    GuideBar m_guide;  // what to do next, choices, colour and thickness
    TabBar m_sheetTabs;  // under the drawing: the drawing and its sheets
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
    unsigned m_solveShown = 0;  // the last relationship failure told (SolveReport::serial)
    FieldValues m_fields;  // shown in texts' {fields}
};
