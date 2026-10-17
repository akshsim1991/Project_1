cmake_minimum_required(VERSION 3.20)
project(StartupManager VERSION 1.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(StartupManager WIN32
  src/main.cpp
  src/MainWindow.cpp
  src/EntryView.cpp
  src/Entries.cpp
  src/FileInfo.cpp
  src/Worker.cpp
  src/Dialogs.cpp
  src/Settings.cpp
  src/Toolbar.cpp
  src/Theme.cpp
  src/Util.cpp
  res/app.rc
)

target_include_directories(StartupManager PRIVATE src res)
target_compile_definitions(StartupManager PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(StartupManager PRIVATE
  comctl32 comdlg32 shell32 shlwapi ole32 dwmapi uxtheme advapi32 version gdi32 user32
  wintrust crypt32 bcrypt oleaut32 uuid taskschd)

if(MSVC)
  # Static CRT: no Visual C++ Redistributable needed.
  set_property(TARGET StartupManager PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_compile_options(StartupManager PRIVATE /W4 /permissive- /utf-8 /EHsc
    $<$<CONFIG:Release>:/O2 /GL /Gy>)
  target_link_options(StartupManager PRIVATE /MANIFEST:NO
    $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>)
else()
  target_compile_options(StartupManager PRIVATE -Wall -Wextra -Wno-missing-field-initializers
    -Wno-cast-function-type -municode $<$<CONFIG:Release>:-O2>)
  target_link_options(StartupManager PRIVATE -municode -static -static-libgcc -static-libstdc++
    $<$<CONFIG:Release>:-s>)
endif()

install(TARGETS StartupManager RUNTIME DESTINATION .)
