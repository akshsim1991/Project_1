// main.cpp - entry point: command line, DPI awareness, message loop.
//
//   FeatherCAD.exe [drawing.fcad]
#include "Common.h"

#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include "GdiPlusInc.h"
#include "MainWindow.h"

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int showCmd) {
    // Only search System32 for DLLs loaded at run time (DLL planting defence).
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
    HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0);
    // The manifest asks for Per-Monitor V2; this is a fallback if it is stripped.
    using Fn = BOOL(WINAPI*)(HANDLE);
    if (auto fn = (Fn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"))
        fn((HANDLE)-4 /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */);

    std::wstring file;
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc > 1) file = argv[1];
    if (argv) LocalFree(argv);

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&icc);
    Gdiplus::GdiplusStartupInput gdiInput;
    ULONG_PTR gdiToken = 0;
    Gdiplus::GdiplusStartup(&gdiToken, &gdiInput, nullptr);

    int result = 0;
    {
        MainWindow window;
        if (!window.Create(inst, showCmd, file)) {
            MessageBoxW(nullptr, L"Feather CAD could not start (Direct2D is not available).", APP_NAME, MB_ICONERROR);
            result = 1;
        } else {
            MSG msg;
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                if (!TranslateAcceleratorW(window.Hwnd(), window.Accelerators(), &msg)) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            }
            result = (int)msg.wParam;
        }
    }
    Gdiplus::GdiplusShutdown(gdiToken);
    CoUninitialize();
    return result;
}
