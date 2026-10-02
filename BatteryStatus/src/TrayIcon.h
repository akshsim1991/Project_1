// TrayIcon.h - the notification-area icon: drawn live (a battery picture
// or the percentage), with a tooltip and Windows notifications.
#pragma once
#include "Common.h"

class TrayIcon {
public:
    ~TrayIcon();
    bool Add(HWND owner, UINT callbackMessage);
    void Remove();
    void Readd();  // after Explorer restarts

    // Redraws the icon only when something visible changed.
    void Update(bool hasBattery, int percent, bool charging, bool onAc, int lowPercent, int style,
                const std::wstring& tooltip);
    void Notify(const std::wstring& title, const std::wstring& text, bool warning);

private:
    HICON Draw(int size) const;
    void Apply(DWORD message);

    HWND m_owner = nullptr;
    UINT m_message = 0;
    bool m_added = false;
    HICON m_icon = nullptr;
    std::wstring m_tip;
    // what the icon shows
    bool m_hasBattery = false, m_charging = false, m_onAc = false, m_lightTaskbar = false;
    int m_percent = -2, m_low = 20, m_style = -1;
};
