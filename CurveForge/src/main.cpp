// main.cpp - entry point: GDI+, COM, the window and the message loop.
//
//   CurveForge.exe [file]   opens a data file (CSV, TSV, TXT) or a .cforge project
#include "Common.h"

#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include "GdiPlusInc.h"
#include "MainWindow.h"

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int showCmd) {
    // Only search System32 for DLLs loaded at run time (DLL planting defence).
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0);

    std::wstring openPath;
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i)
        if (argv[i][0] != L'/' && argv[i][0] != L'-') openPath = argv[i];
    if (argv) LocalFree(argv);

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);
    Gdiplus::GdiplusStartupInput gdiInput;
    ULONG_PTR gdiToken = 0;
    Gdiplus::GdiplusStartup(&gdiToken, &gdiInput, nullptr);

    int code = 0;
    {
        MainWindow window;
        window.GetSettings().Load();
        if (!window.Create(inst, showCmd, openPath)) {
            MessageBoxW(nullptr, APP_NAME L" could not start.", APP_NAME, MB_ICONERROR);
            code = 1;
        } else {
            MSG msg;
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                if (!TranslateAcceleratorW(window.Hwnd(), window.Accelerators(), &msg) &&
                    !IsDialogMessageW(window.Hwnd(), &msg)) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            }
        }
    }
    Gdiplus::GdiplusShutdown(gdiToken);
    CoUninitialize();
    return code;
}
