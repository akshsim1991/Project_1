// Personal.h - what a student sets once: their details (filled into the
// title block of new drawings and exam sheets) and their own command
// aliases (aliases.txt: "BX = RECTANG").
//
// Plain C++ (no Windows): the window keeps them in the registry and a file.
#pragma once
#include <map>
#include <string>

class Document;

// name, usn, branch, institution (empty when not given).
std::map<std::wstring, std::wstring>& MyDetails();
const wchar_t* const kMyDetailKeys[] = {L"name", L"usn", L"branch", L"institution"};
// The details into the drawing's properties (Drawn by, USN, Branch,
// Company) where those are empty, or all of them with `overwrite`. True if
// anything changed.
bool ApplyMyDetails(Document& doc, bool overwrite);

// The user's aliases: alias (capitals) -> command name.
std::map<std::wstring, std::wstring>& UserAliases();
// Lines "ALIAS = COMMAND" or "ALIAS, *COMMAND" (as AutoCAD's acad.pgp); #
// and ; start comments. Unknown commands are listed in `problems`.
// Returns the number read.
size_t ReadAliases(const std::wstring& text, std::map<std::wstring, std::wstring>& out, std::wstring& problems);
// The text of a new aliases file: how to write one, with examples.
std::wstring AliasesTemplate();
