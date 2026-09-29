// main.cpp - entry point: command line, DPI awareness, message loop.
//
// Command line:
//   FeatherPDF.exe [file.pdf] [/page N]   open a file (optionally at page N)
//   FeatherPDF.exe /register              register as a PDF handler (per user)
//   FeatherPDF.exe /unregister            remove that registration
#include "Common.h"

#include <algorithm>

#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include "FileAssoc.h"
#include "MainWindow.h"

namespace {
bool IsSwitch(const wchar_t* arg, const wchar_t* name) {
    return (arg[0] == L'/' || arg[0] == L'-') &&
           (lstrcmpiW(arg + 1, name) == 0 || (arg[1] == L'-' && lstrcmpiW(arg + 2, name) == 0));
}

void EnablePerMonitorDpi() {
    // The manifest already requests Per-Monitor V2; this is a fallback in
    // case the manifest is stripped (e.g. some repackagers).
    using Fn = BOOL(WINAPI*)(HANDLE);
    auto fn = (Fn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext");
    if (fn) fn((HANDLE)-4 /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */);
}
}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int showCmd) {
    // Only search System32 for DLLs loaded at run time (DLL planting defence).
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
    // Terminate on heap corruption instead of continuing in a bad state.
    HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0);
    EnablePerMonitorDpi();

    std::wstring file;
    int page = -1;
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i) {
        if (IsSwitch(argv[i], L"register")) {
            RegisterFileAssociation();
            LocalFree(argv);
            return 0;
        }
        if (IsSwitch(argv[i], L"unregister")) {
            UnregisterFileAssociation();
            LocalFree(argv);
            return 0;
        }
        if (IsSwitch(argv[i], L"page") && i + 1 < argc) {
            page = std::max(0, _wtoi(argv[++i]) - 1);
            continue;
        }
        if (file.empty()) file = argv[i];
    }
    if (argv) LocalFree(argv);

    // The Open dialog and ShellExecute expect COM on the UI thread.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);

    MainWindow window;
    if (!window.Create(inst, showCmd, file, page)) {
        MessageBoxW(nullptr, L"Feather PDF could not start.", APP_NAME, MB_ICONERROR);
        return 1;
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!TranslateAcceleratorW(window.Hwnd(), window.Accelerators(), &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    CoUninitialize();
    return (int)msg.wParam;
}
