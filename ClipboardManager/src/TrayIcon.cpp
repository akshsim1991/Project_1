// TrayIcon.cpp - Shell_NotifyIcon.
#include "TrayIcon.h"

#include <shellapi.h>

namespace {
constexpr UINT kIconId = 1;
}

TrayIcon::~TrayIcon() { Remove(); }

bool TrayIcon::Add(HWND owner, UINT callbackMessage, HICON icon) {
    m_owner = owner;
    m_message = callbackMessage;
    m_icon = icon;
    Apply(NIM_ADD);
    NOTIFYICONDATAW nid{sizeof(nid)};
    nid.hWnd = m_owner;
    nid.uID = kIconId;
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
    return m_added;
}

void TrayIcon::Readd() {
    m_added = false;
    Add(m_owner, m_message, m_icon);
}

void TrayIcon::Remove() {
    if (!m_added) return;
    NOTIFYICONDATAW nid{sizeof(nid)};
    nid.hWnd = m_owner;
    nid.uID = kIconId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    m_added = false;
}

void TrayIcon::SetTip(const std::wstring& tip) {
    if (tip == m_tip) return;
    m_tip = tip;
    if (m_added) Apply(NIM_MODIFY);
}

void TrayIcon::Apply(DWORD message) {
    NOTIFYICONDATAW nid{sizeof(nid)};
    nid.hWnd = m_owner;
    nid.uID = kIconId;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = m_message;
    nid.hIcon = m_icon;
    wcsncpy_s(nid.szTip, m_tip.empty() ? APP_NAME : m_tip.c_str(), _TRUNCATE);
    if (Shell_NotifyIconW(message, &nid)) {
        if (message == NIM_ADD) m_added = true;
    } else if (message == NIM_MODIFY) {
        // The taskbar may have been recreated: add the icon again.
        if (Shell_NotifyIconW(NIM_ADD, &nid)) m_added = true;
    }
}

void TrayIcon::Notify(const std::wstring& title, const std::wstring& text) {
    NOTIFYICONDATAW nid{sizeof(nid)};
    nid.hWnd = m_owner;
    nid.uID = kIconId;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND | NIIF_RESPECT_QUIET_TIME;
    wcsncpy_s(nid.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(nid.szInfo, text.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}
