// main.cpp - entry point: one copy at a time, GDI+, the window and the
// message loop.
//
//   BatteryStatus.exe          open the window
//   BatteryStatus.exe /tray    start in the notification area (used at sign-in)
//   BatteryStatus.exe /demo    simulated battery, for trying it on a desktop PC
#include "Common.h"

#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include "Battery.h"
#include "GdiPlusInc.h"
#include "MainWindow.h"

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0);

    bool tray = false, demo = false;
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i) {
        if (lstrcmpiW(argv[i], L"/tray") == 0) tray = true;
        if (lstrcmpiW(argv[i], L"/demo") == 0) demo = true;
    }
    if (argv) LocalFree(argv);

    // One copy: starting it again shows the running one.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\BatteryStatus.SingleInstance");
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
    Battery::SetDemo(demo);

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
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
                if (!IsDialogMessageW(window.Hwnd(), &msg)) {
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
