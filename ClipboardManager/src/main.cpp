// main.cpp - entry point: one copy at a time, GDI+, the window and the
// message loop.
//
//   ClipboardManager.exe          open the window
//   ClipboardManager.exe /tray    start in the notification area (used at sign-in)
#include "Common.h"

#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include "GdiPlusInc.h"
#include "MainWindow.h"

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0);

    bool tray = false;
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i)
        if (lstrcmpiW(argv[i], L"/tray") == 0) tray = true;
    if (argv) LocalFree(argv);

    // One copy: starting it again shows the running one.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\ClipboardManager.SingleInstance");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (!tray)
            if (HWND existing = FindWindowW(APP_WINDOW_CLASS, nullptr)) {
                DWORD pid = 0;
                GetWindowThreadProcessId(existing, &pid);
                AllowSetForegroundWindow(pid);
                PostMessageW(existing, WM_APP_SHOW, 0, 0);
            }
        CloseHandle(mutex);
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&icc);
    Gdiplus::GdiplusStartupInput gdiInput;
    ULONG_PTR gdiToken = 0;
    Gdiplus::GdiplusStartup(&gdiToken, &gdiInput, nullptr);

    int code = 0;
    {
        MainWindow window;
        window.GetSettings().Load();
        if (!window.Create(inst, tray && window.GetSettings().startInTray)) {
            MessageBoxW(nullptr, APP_NAME L" could not start.", APP_NAME, MB_ICONERROR);
            code = 1;
        } else {
            MSG msg;
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                if (window.PreTranslate(msg)) continue;
                // Keyboard navigation (Tab, Enter, Esc) in whichever of our
                // windows the message is for.
                HWND root = msg.hwnd ? GetAncestor(msg.hwnd, GA_ROOT) : nullptr;
                if (!root || !IsDialogMessageW(root, &msg)) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            }
        }
    }
    Gdiplus::GdiplusShutdown(gdiToken);
    CoUninitialize();
    if (mutex) CloseHandle(mutex);
    return code;
}
