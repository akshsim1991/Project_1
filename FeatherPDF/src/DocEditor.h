// DocEditor.h - one open document on the render worker thread: its
// PdfEngine plus the edit history behind undo/redo and safe saving.
//
// UNDO / REDO
// -----------
// PDFium can change a document in memory but can not undo a change. So
// every edit is recorded (EditOp) and applied to the live document; undo
// re-opens the "base" file (the document as last loaded from disk) and
// replays every edit before the undone one. Page edits and annotations
// replay in milliseconds, since nothing is rendered while replaying.
// Redo simply applies the next recorded edit again.
//
// SAFE SAVING
// -----------
//   1. the document is written to a new file next to the target;
//   2. that file is flushed to disk and opened again to check it;
//   3. only then does it replace the target (ReplaceFileW), so a crash
//      or full disk at any point leaves the original file untouched.
// If the file being overwritten is also the undo base, a private copy is
// kept first so the edits made before saving can still be undone.
#pragma once
#include <cstdint>
#include <memory>

#include "PdfEngine.h"

class DocEditor {
public:
    DocEditor() = default;
    ~DocEditor();
    DocEditor(const DocEditor&) = delete;
    DocEditor& operator=(const DocEditor&) = delete;

    OpenError Open(const std::wstring& path, const std::string& password,
                   std::vector<SizeF>& pageSizes);
    PdfEngine* Engine() { return m_engine.get(); }
    const std::wstring& Path() const { return m_path; }
    const std::string& Password() const { return m_password; }

    // Applies and records an edit. `focus` / `select` receive the page to
    // show and the pages to select afterwards.
    bool Apply(EditOp&& op, std::wstring& error, int& focus, std::vector<int>& select);
    bool Undo(std::wstring& error);
    bool Redo(std::wstring& error, int& focus, std::vector<int>& select);
    // Saves to `target` (the current file for Save, another for Save As).
    bool Save(const std::wstring& target, std::wstring& error);

    bool CanUndo() const { return m_cursor > 0; }
    bool CanRedo() const { return m_cursor < m_ops.size(); }
    bool Dirty() const { return m_cursor != m_saved; }

    // Keeps a file the history depends on; deleted with the editor.
    void AdoptTempFile(const std::wstring& path) { m_temps.push_back(path); }

private:
    // Re-opens the base file and replays the first `count` edits.
    bool Rebuild(size_t count, std::wstring& error);
    static void Describe(const EditOp& op, int before, int after, int& focus,
                         std::vector<int>& select);

    std::unique_ptr<PdfEngine> m_engine;
    std::wstring m_path;      // where the document is saved
    std::wstring m_basePath;  // file the history replays from
    std::string m_password;
    std::vector<EditOp> m_ops;
    size_t m_cursor = 0;      // edits applied: m_ops[0, m_cursor)
    size_t m_saved = 0;       // m_cursor when last saved (SIZE_MAX: unreachable)
    std::vector<std::wstring> m_temps;
};
