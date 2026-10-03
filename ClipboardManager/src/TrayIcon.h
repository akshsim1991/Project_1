// TrayIcon.h - the notification-area icon, its tooltip and notifications.
#pragma once
#include "Common.h"

class TrayIcon {
public:
    ~TrayIcon();
    bool Add(HWND owner, UINT callbackMessage, HICON icon);
    void Remove();
    void Readd();  // after Explorer restarts
    void SetTip(const std::wstring& tip);
    void Notify(const std::wstring& title, const std::wstring& text);

private:
    void Apply(DWORD message);

    HWND m_owner = nullptr;
    UINT m_message = 0;
    HICON m_icon = nullptr;
    bool m_added = false;
    std::wstring m_tip;
};
