// DocEditor.cpp - edit history, undo/redo and safe saving (see DocEditor.h).
#include "DocEditor.h"

#include <algorithm>

#include "Util.h"

DocEditor::~DocEditor() {
    m_engine.reset();  // close files before deleting them
    SecureZeroMemory(m_password.data(), m_password.size());
    for (const std::wstring& t : m_temps) DeleteFileW(t.c_str());
}

OpenError DocEditor::Open(const std::wstring& path, const std::string& password,
                          std::vector<SizeF>& pageSizes) {
    auto engine = std::make_unique<PdfEngine>();
    const OpenError err = engine->Open(path, password, pageSizes);
    if (err != OpenError::None) return err;
    m_engine = std::move(engine);
    m_path = m_basePath = path;
    m_password = password;  // needed to re-open the file for undo
    return OpenError::None;
}

void DocEditor::Describe(const EditOp& op, int before, int after, int& focus,
                         std::vector<int>& select) {
    focus = -1;
    select.clear();
    switch (op.kind) {
        case EditOp::DeletePages:
            focus = std::min(op.pages.front(), after - 1);
            select.push_back(focus);
            break;
        case EditOp::MovePages:
            focus = op.index;
            for (size_t i = 0; i < op.pages.size(); ++i) select.push_back(op.index + (int)i);
            break;
        case EditOp::RotatePages:
            focus = op.pages.front();
            select = op.pages;
            break;
        case EditOp::InsertBlank:
        case EditOp::InsertFiles:
            focus = op.index;
            for (int p = op.index; p < op.index + (after - before); ++p) select.push_back(p);
            break;
        case EditOp::Markup:
        case EditOp::EditText:
        case EditOp::FindReplace:
        case EditOp::AddNote:
        case EditOp::EditComment:
        case EditOp::DeleteAnnot:
        case EditOp::SetField:
        case EditOp::AddShape:
        case EditOp::AddStamp:
        case EditOp::AddImage:
        case EditOp::AddText:
        case EditOp::StyleText:
            break;  // the reader stays where they are
    }
}

bool DocEditor::Apply(EditOp&& op, std::wstring& error, int& focus, std::vector<int>& select) {
    if (!m_engine) return false;
    const int before = m_engine->PageCount();
    if (!m_engine->ApplyEdit(op, error)) {
        // The document may be half-changed: restore the last good state.
        std::wstring ignored;
        Rebuild(m_cursor, ignored);
        return false;
    }
    Describe(op, before, m_engine->PageCount(), focus, select);
    // A new edit discards the redo history. If the saved state was in it,
    // the document can no longer return to "saved" without saving again.
    m_ops.resize(m_cursor);
    if (m_saved > m_cursor) m_saved = SIZE_MAX;
    m_ops.push_back(std::move(op));
    ++m_cursor;
    return true;
}

bool DocEditor::Undo(std::wstring& error) {
    if (!CanUndo()) return false;
    if (!Rebuild(m_cursor - 1, error)) return false;
    --m_cursor;
    return true;
}

bool DocEditor::Redo(std::wstring& error, int& focus, std::vector<int>& select) {
    if (!CanRedo() || !m_engine) return false;
    const int before = m_engine->PageCount();
    if (!m_engine->ApplyEdit(m_ops[m_cursor], error)) {
        std::wstring ignored;
        Rebuild(m_cursor, ignored);
        return false;
    }
    Describe(m_ops[m_cursor], before, m_engine->PageCount(), focus, select);
    ++m_cursor;
    return true;
}

bool DocEditor::Rebuild(size_t count, std::wstring& error) {
    auto engine = std::make_unique<PdfEngine>();
    std::vector<SizeF> sizes;
    if (engine->Open(m_basePath, m_password, sizes) != OpenError::None) {
        error = L"The original file could not be read again, so the change can not be undone.";
        return false;
    }
    for (size_t i = 0; i < count && i < m_ops.size(); ++i) {
        std::wstring ignored;
        if (!engine->ApplyEdit(m_ops[i], ignored)) {
            error = L"The document could not be restored to that state.";
            return false;
        }
    }
    m_engine = std::move(engine);
    return true;
}

bool DocEditor::Save(const std::wstring& target, std::wstring& error) {
    if (!m_engine) return false;
    const int pages = m_engine->PageCount();

    // 1. Write the new version next to the target (same volume, so it can
    //    replace it atomically) and flush it to disk.
    wchar_t suffix[32];
    swprintf_s(suffix, L".%llx.tmp", GetTickCount64());
    const std::wstring dir = DirectoryFromPath(target);
    const std::wstring temp =
        (dir.empty() ? L"" : dir + L"\\") + L"~" + FileNameFromPath(target) + suffix;
    if (!m_engine->WriteTo(temp)) {
        DeleteFileW(temp.c_str());
        error = L"The file could not be written. The folder may be read-only, or the disk may "
                L"be full. Try \x201CSave as\x201D to save a copy somewhere else.";
        return false;
    }

    // 2. Check that what was written opens and has every page.
    auto fresh = std::make_unique<PdfEngine>();
    std::vector<SizeF> sizes;
    if (fresh->Open(temp, m_password, sizes) != OpenError::None || (int)sizes.size() != pages) {
        fresh.reset();
        DeleteFileW(temp.c_str());
        error = L"The saved file could not be verified, so the original was left unchanged.";
        return false;
    }

    // 3. If the file about to be overwritten is what undo replays from,
    //    keep a private copy of it first.
    if (SamePath(m_basePath, target) && m_cursor > 0) {
        const std::wstring copy = MakeTempPdfPath();
        if (!CopyFileW(target.c_str(), copy.c_str(), FALSE)) {
            fresh.reset();
            DeleteFileW(temp.c_str());
            error = L"There is not enough space to keep the undo history. The file was not saved.";
            return false;
        }
        m_temps.push_back(copy);
        m_basePath = copy;
    }

    // 4. Replace the target. Our reader of the old file is closed first
    //    (the live document is identical to `fresh` now anyway).
    m_engine.reset();
    auto replace = [&] {
        if (FileExists(target))
            return ReplaceFileW(target.c_str(), temp.c_str(), nullptr,
                                REPLACEFILE_IGNORE_MERGE_ERRORS | REPLACEFILE_IGNORE_ACL_ERRORS,
                                nullptr, nullptr) != 0;
        return MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH) != 0;
    };
    bool replaced = replace();
    if (!replaced) {
        // Some file systems refuse to move a file that is open: retry
        // without our reader on the new file.
        fresh.reset();
        replaced = replace();
    }
    if (!replaced) {
        const DWORD err = GetLastError();
        fresh.reset();
        DeleteFileW(temp.c_str());
        std::wstring ignored;
        Rebuild(m_cursor, ignored);  // back to the unsaved document
        error = err == ERROR_ACCESS_DENIED || err == ERROR_SHARING_VIOLATION ||
                        err == ERROR_UNABLE_TO_REMOVE_REPLACED || err == ERROR_LOCK_VIOLATION
                    ? L"The file is read-only or open in another program. Close it there, or "
                      L"use \x201CSave as\x201D to save a copy."
                    : L"The file could not be replaced. Use \x201CSave as\x201D to save a copy.";
        return false;
    }
    if (!fresh) {
        fresh = std::make_unique<PdfEngine>();
        if (fresh->Open(target, m_password, sizes) != OpenError::None) {
            // Saved, but can not be read back: keep working from the history.
            fresh.reset();
            std::wstring ignored;
            Rebuild(m_cursor, ignored);
            m_path = target;
            m_saved = m_cursor;
            return true;
        }
    }
    m_engine = std::move(fresh);
    m_path = target;
    m_saved = m_cursor;
    return true;
}
