cmake_minimum_required(VERSION 3.20)
project(ClipboardManager VERSION 1.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ClipboardManager WIN32
  src/main.cpp
  src/MainWindow.cpp
  src/ItemList.cpp
  src/Preview.cpp
  src/ClipboardIO.cpp
  src/Images.cpp
  src/Store.cpp
  src/Dialogs.cpp
  src/TrayIcon.cpp
  src/Settings.cpp
  src/Toolbar.cpp
  src/Theme.cpp
  src/Util.cpp
  res/app.rc
)

target_include_directories(ClipboardManager PRIVATE src res)
target_compile_definitions(ClipboardManager PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(ClipboardManager PRIVATE
  comctl32 comdlg32 shell32 shlwapi ole32 dwmapi uxtheme advapi32 gdi32 user32 gdiplus version)

if(MSVC)
  # Static CRT: no Visual C++ Redistributable needed.
  set_property(TARGET ClipboardManager PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_compile_options(ClipboardManager PRIVATE /W4 /permissive- /utf-8 /EHsc
    $<$<CONFIG:Release>:/O2 /GL /Gy>)
  target_link_options(ClipboardManager PRIVATE /MANIFEST:NO
    $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>)
else()
  target_compile_options(ClipboardManager PRIVATE -Wall -Wextra -Wno-missing-field-initializers
    -Wno-cast-function-type -municode $<$<CONFIG:Release>:-O2>)
  target_link_options(ClipboardManager PRIVATE -municode -static -static-libgcc -static-libstdc++
    $<$<CONFIG:Release>:-s>)
endif()

install(TARGETS ClipboardManager RUNTIME DESTINATION .)
