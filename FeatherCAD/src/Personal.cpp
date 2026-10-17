// Personal.cpp - see Personal.h.
#include "Personal.h"

#include <cwctype>

#include "Document.h"
#include "Engine.h"

std::map<std::wstring, std::wstring>& MyDetails() {
    static std::map<std::wstring, std::wstring> details;
    return details;
}

bool ApplyMyDetails(Document& doc, bool overwrite) {
    // My detail -> the drawing property the title blocks show.
    static const std::pair<const wchar_t*, const wchar_t*> map[] = {
        {L"name", L"drawnby"}, {L"usn", L"usn"}, {L"branch", L"branch"}, {L"institution", L"company"}};
    bool changed = false;
    for (const auto& m : map) {
        auto it = MyDetails().find(m.first);
        if (it == MyDetails().end() || it->second.empty()) continue;
        auto have = doc.Properties().find(m.second);
        const bool empty = have == doc.Properties().end() || have->second.empty();
        if ((empty || overwrite) && (empty || have->second != it->second)) {
            doc.SetProperty(m.second, it->second);
            changed = true;
        }
    }
    return changed;
}

std::map<std::wstring, std::wstring>& UserAliases() {
    static std::map<std::wstring, std::wstring> aliases;
    return aliases;
}

namespace {
std::wstring Clean(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && (std::iswspace(s[a]) || s[a] == L'*' || s[a] == 0xFEFF)) ++a;
    while (b > a && std::iswspace(s[b - 1])) --b;
    std::wstring out = s.substr(a, b - a);
    for (wchar_t& c : out) c = (wchar_t)std::towupper(c);
    return out;
}
}  // namespace

size_t ReadAliases(const std::wstring& text, std::map<std::wstring, std::wstring>& out, std::wstring& problems) {
    size_t n = 0, start = 0, lineNo = 0;
    while (start <= text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = text.substr(start, end - start);
        start = end + 1;
        ++lineNo;
        const size_t hash = line.find_first_of(L"#;");
        if (hash != std::wstring::npos) line = line.substr(0, hash);
        const size_t sep = line.find_first_of(L"=,");
        if (sep == std::wstring::npos) {
            if (!Clean(line).empty()) problems += L"line " + std::to_wstring(lineNo) + L": write it as ALIAS = COMMAND\n";
            continue;
        }
        const std::wstring alias = Clean(line.substr(0, sep)), command = Clean(line.substr(sep + 1));
        if (alias.empty() || command.empty() || alias.find(L' ') != std::wstring::npos) {
            problems += L"line " + std::to_wstring(lineNo) + L": write it as ALIAS = COMMAND\n";
            continue;
        }
        const CommandInfo* c = nullptr;
        for (const CommandInfo& info : AllCommands()) {
            if (info.name == command) c = &info;
            for (const std::wstring& a : info.aliases)
                if (!c && a == command) c = &info;
        }
        if (!c) {
            problems += L"line " + std::to_wstring(lineNo) + L": there is no command " + command + L"\n";
            continue;
        }
        out[alias] = c->name;
        ++n;
    }
    return n;
}

std::wstring AliasesTemplate() {
    return L"# Feather CAD: your own short names for commands.\r\n"
           L"# One per line: SHORT NAME = COMMAND. Lines starting with # are notes.\r\n"
           L"# Save this file; Feather CAD reads it again when you go back to it.\r\n"
           L"# Your names come before the built-in ones (L for LINE and so on).\r\n"
           L"#\r\n"
           L"# Examples (remove the # in front to use one):\r\n"
           L"# BX = RECTANG\r\n"
           L"# CC = CIRCLE\r\n"
           L"# TL = TRUELENGTH\r\n"
           L"# DV = DEVELOP\r\n";
}
