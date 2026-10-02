// TrayIcon.cpp - Shell_NotifyIcon and the drawn icon.
#include "TrayIcon.h"

#include <shellapi.h>

#include "GdiPlusInc.h"

using namespace Gdiplus;

namespace {
constexpr UINT kIconId = 1;

bool LightTaskbar() {
    DWORD v = 0, size = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &size) != ERROR_SUCCESS)
        return false;
    return v != 0;
}

Color LevelColor(int percent, int low, bool charging) {
    if (charging) return Color(255, 76, 196, 86);
    if (percent <= low) return Color(255, 232, 64, 52);
    if (percent <= 40) return Color(255, 240, 170, 40);
    return Color(255, 76, 196, 86);
}
}  // namespace

TrayIcon::~TrayIcon() {
    Remove();
    if (m_icon) DestroyIcon(m_icon);
}

bool TrayIcon::Add(HWND owner, UINT callbackMessage) {
    m_owner = owner;
    m_message = callbackMessage;
    m_lightTaskbar = LightTaskbar();
    if (!m_icon) m_icon = Draw(GetSystemMetrics(SM_CXSMICON));
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
    Add(m_owner, m_message);
}

void TrayIcon::Remove() {
    if (!m_added) return;
    NOTIFYICONDATAW nid{sizeof(nid)};
    nid.hWnd = m_owner;
    nid.uID = kIconId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    m_added = false;
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

void TrayIcon::Update(bool hasBattery, int percent, bool charging, bool onAc, int lowPercent, int style,
                      const std::wstring& tooltip) {
    const bool light = LightTaskbar();
    const bool redraw = hasBattery != m_hasBattery || percent != m_percent || charging != m_charging ||
                        onAc != m_onAc || lowPercent != m_low || style != m_style || light != m_lightTaskbar || !m_icon;
    const bool retip = tooltip != m_tip;
    if (!redraw && !retip) return;
    m_hasBattery = hasBattery;
    m_percent = percent;
    m_charging = charging;
    m_onAc = onAc;
    m_low = lowPercent;
    m_style = style;
    m_lightTaskbar = light;
    m_tip = tooltip;
    if (redraw) {
        HICON old = m_icon;
        m_icon = Draw(GetSystemMetrics(SM_CXSMICON));
        Apply(NIM_MODIFY);
        if (old) DestroyIcon(old);
    } else {
        Apply(NIM_MODIFY);
    }
}

void TrayIcon::Notify(const std::wstring& title, const std::wstring& text, bool warning) {
    NOTIFYICONDATAW nid{sizeof(nid)};
    nid.hWnd = m_owner;
    nid.uID = kIconId;
    nid.uFlags = NIF_INFO;
    // Our own sound plays (or not) as set in Settings.
    nid.dwInfoFlags = (warning ? NIIF_WARNING : NIIF_INFO) | NIIF_NOSOUND | NIIF_RESPECT_QUIET_TIME;
    wcsncpy_s(nid.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(nid.szInfo, text.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

HICON TrayIcon::Draw(int size) const {
    if (size < 16) size = 16;
    Bitmap bmp(size, size, PixelFormat32bppARGB);
    Graphics g(&bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.Clear(Color(0, 0, 0, 0));
    const Color fg = m_lightTaskbar ? Color(255, 30, 30, 30) : Color(255, 255, 255, 255);
    const Color outline = m_lightTaskbar ? Color(255, 255, 255, 255) : Color(255, 20, 20, 20);
    const float s = size / 16.0f;
    const int pct = m_percent < 0 ? 0 : m_percent > 100 ? 100 : m_percent;

    if (!m_hasBattery) {
        // A plug: mains power only.
        Pen pen(fg, 1.4f * s);
        g.DrawLine(&pen, 5 * s, 2 * s, 5 * s, 6 * s);
        g.DrawLine(&pen, 11 * s, 2 * s, 11 * s, 6 * s);
        SolidBrush b(fg);
        g.FillRectangle(&b, 3 * s, 6 * s, 10 * s, 4 * s);
        g.DrawLine(&pen, 8 * s, 10 * s, 8 * s, 14 * s);
    } else if (m_style == 1) {
        // The percentage as a number.
        const std::wstring text = pct >= 100 ? L"F" : std::to_wstring(pct);
        FontFamily family(L"Segoe UI");
        const float px = (text.size() >= 2 ? 11.5f : 13.0f) * s;
        Font font(family.IsAvailable() ? &family : FontFamily::GenericSansSerif(), px, FontStyleBold, UnitPixel);
        StringFormat fmt;
        fmt.SetAlignment(StringAlignmentCenter);
        fmt.SetLineAlignment(StringAlignmentCenter);
        const Color c = m_charging ? Color(255, 76, 196, 86) : pct <= m_low ? Color(255, 232, 64, 52) : fg;
        SolidBrush b(c);
        g.DrawString(text.c_str(), (INT)text.size(), &font, RectF(-2 * s, -1 * s, 20 * s, 16 * s), &fmt, &b);
        // A thin bar under the number shows the level.
        SolidBrush bar(LevelColor(pct, m_low, m_charging));
        g.FillRectangle(&bar, 1 * s, 13.5f * s, 14 * s * pct / 100.0f, 2 * s);
    } else {
        // A battery picture, filled to the level.
        const RectF body(0.75f * s, 4 * s, 12.5f * s, 8 * s);
        Pen pen(fg, 1.3f * s);
        SolidBrush tip(fg);
        g.FillRectangle(&tip, 13.6f * s, 6.2f * s, 1.8f * s, 3.6f * s);
        const float inner = 10.3f * s * pct / 100.0f;
        SolidBrush fill(LevelColor(pct, m_low, m_charging));
        if (inner > 0.5f) g.FillRectangle(&fill, body.X + 1.1f * s, body.Y + 1.1f * s, inner, body.Height - 2.2f * s);
        g.DrawRectangle(&pen, body);
        if (m_charging || m_onAc) {
            // A lightning bolt (charging) or a plug mark (on charger, not charging).
            PointF bolt[] = {{8.6f * s, 1.5f * s}, {4.6f * s, 8.6f * s}, {7.6f * s, 8.6f * s},
                             {6.4f * s, 14.5f * s}, {11.4f * s, 6.8f * s}, {8.3f * s, 6.8f * s}};
            SolidBrush boltFill(m_lightTaskbar ? Color(255, 30, 30, 30) : Color(255, 255, 255, 255));
            Pen boltEdge(outline, 1.0f * s);
            g.FillPolygon(&boltFill, bolt, 6);
            g.DrawPolygon(&boltEdge, bolt, 6);
        } else if (pct <= m_low) {
            // An exclamation mark when low.
            SolidBrush red(Color(255, 232, 64, 52));
            g.FillRectangle(&red, 7.2f * s, 0.2f * s, 1.6f * s, 2.6f * s);
        }
    }
    HICON icon = nullptr;
    bmp.GetHICON(&icon);
    return icon;
}
