// main.cpp - entry point: single instance, optional elevation, message loop.
//
//   ServicesManager.exe              start (or bring the running window forward)
//   ServicesManager.exe /elevated    used internally after "Restart as administrator"
#include "Common.h"

#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include "MainWindow.h"
#include "Util.h"

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int showCmd) {
    // Only search System32 for DLLs loaded at run time (DLL planting defence).
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0);

    bool relaunched = false;
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i)
        if (lstrcmpiW(argv[i], L"/elevated") == 0) relaunched = true;
    if (argv) LocalFree(argv);

    // One window is enough: bring the running one forward. (Not after an
    // elevation restart, while the old window is still closing.)
    if (!relaunched) {
        if (HWND existing = FindWindowW(APP_WINDOW_CLASS, nullptr)) {
            if (IsIconic(existing)) ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
            return 0;
        }
    }

    MainWindow window;
    window.GetSettings().Load();
    if (window.GetSettings().alwaysElevate && !relaunched && !IsElevated() &&
        RelaunchElevated(nullptr))
        return 0;  // the elevated copy takes over (if UAC was declined, carry on read-only)

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);

    if (!window.Create(inst, showCmd)) {
        MessageBoxW(nullptr, APP_NAME L" could not start.", APP_NAME, MB_ICONERROR);
        return 1;
    }
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!TranslateAcceleratorW(window.Hwnd(), window.Accelerators(), &msg) &&
            !IsDialogMessageW(window.Hwnd(), &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    CoUninitialize();
    return 0;
}
