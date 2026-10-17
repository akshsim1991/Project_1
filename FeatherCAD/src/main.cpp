// main.cpp - entry point: command line, DPI awareness, message loop.
//
//   FeatherCAD.exe [drawing.fcad]
#include "Common.h"

#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include "GdiPlusInc.h"
#include "ImageFile.h"
#include "Plot.h"
#include "MainWindow.h"
#include "Util.h"

namespace {
// An unexpected error: say so (instead of vanishing) and leave a note in
// %LOCALAPPDATA%\FeatherCAD\crash.txt saying where it happened.
LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep) {
    static LONG entered = 0;
    if (InterlockedExchange(&entered, 1)) return EXCEPTION_EXECUTE_HANDLER;
    const void* address = ep->ExceptionRecord->ExceptionAddress;
    HMODULE mod = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)address, &mod);
    wchar_t modName[MAX_PATH] = L"?";
    if (mod) GetModuleFileNameW(mod, modName, MAX_PATH);
    wchar_t where[400];
    swprintf(where, 400, L"Error 0x%08lX in %ls at offset 0x%llX (Feather CAD %ls)",
             (unsigned long)ep->ExceptionRecord->ExceptionCode, FileNameFromPath(modName).c_str(),
             (unsigned long long)((const char*)address - (const char*)mod), APP_VERSION);
    WriteFileSafely(AppDataDir(nullptr) + L"\\crash.txt", WideToUtf8(std::wstring(where) + L"\r\n"));
    const std::wstring msg = std::wstring(L"Feather CAD has stopped because of an unexpected error.\n\n") + where +
                             L"\n\nAuto-saved copies of your drawings can be opened with File > Recover drawings.";
    MessageBoxW(nullptr, msg.c_str(), APP_NAME, MB_ICONERROR | MB_TOPMOST);
    return EXCEPTION_EXECUTE_HANDLER;
}
}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int showCmd) {
    // Only search System32 for DLLs loaded at run time (DLL planting defence).
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
    HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0);
    SetUnhandledExceptionFilter(CrashFilter);
    // The manifest asks for Per-Monitor V2; this is a fallback if it is stripped.
    using Fn = BOOL(WINAPI*)(HANDLE);
    if (auto fn = (Fn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"))
        fn((HANDLE)-4 /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */);

    std::vector<std::wstring> files;  // each opens in a tab of its own
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i) files.push_back(argv[i]);
    if (argv) LocalFree(argv);

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&icc);
    Gdiplus::GdiplusStartupInput gdiInput;
    ULONG_PTR gdiToken = 0;
    Gdiplus::GdiplusStartup(&gdiToken, &gdiInput, nullptr);
    SetImageReader(ReadImageRgb);  // pictures in prints and PDFs

    int result = 0;
    {
        MainWindow window;
        if (!window.Create(inst, showCmd, files)) {
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
