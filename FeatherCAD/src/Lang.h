// Lang.h - the program's words in other languages: menus, the menu bar and
// the first hint. English is the program's own; Hindi and Kannada are
// built in (first drafts, to be checked by native speakers). A file
// lang\<code>.txt beside the program, "English = translation" per line,
// adds to or corrects a language without rebuilding.
//
// Plain C++ (no Windows).
#pragma once
#include <map>
#include <string>
#include <vector>

struct Language {
    const wchar_t* code;    // "hi"
    const wchar_t* name;    // "Hindi"
    const wchar_t* native;  // in its own script
};
const std::vector<Language>& Languages();  // English first

// Makes `code` the language (unknown codes: English). `extra`: lines of a
// translation file read beside the program (UTF-8), added on top.
void SetLanguage(const std::wstring& code, const std::string& extra = "");
const std::wstring& CurrentLanguage();  // "en", "hi", "kn"

// `english` in the current language, or as it is when there is no
// translation. A menu text's "&" marks are ignored and its shortcut after a
// tab is kept: "&Line\tL" -> "<Line in Hindi>\tL".
std::wstring Tr(const std::wstring& english);

// "English = translation" lines (UTF-8; # starts a comment) into `out`.
// Returns the number read.
size_t ReadTranslations(const std::string& utf8, std::map<std::wstring, std::wstring>& out);
