cmake_minimum_required(VERSION 3.20)
project(BatteryStatus VERSION 2.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(BatteryStatus WIN32
  src/main.cpp
  src/MainWindow.cpp
  src/Dialogs.cpp
  src/Battery.cpp
  src/History.cpp
  src/Alerts.cpp
  src/Summary.cpp
  src/TrayIcon.cpp
  src/Dashboard.cpp
  src/Settings.cpp
  src/Toolbar.cpp
  src/Theme.cpp
  src/Util.cpp
  res/app.rc
)

target_include_directories(BatteryStatus PRIVATE src res)
target_compile_definitions(BatteryStatus PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(BatteryStatus PRIVATE
  comctl32 comdlg32 shell32 shlwapi ole32 dwmapi uxtheme advapi32 gdi32 user32 gdiplus setupapi winmm)

if(MSVC)
  # Static CRT: no Visual C++ Redistributable needed.
  set_property(TARGET BatteryStatus PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_compile_options(BatteryStatus PRIVATE /W4 /permissive- /utf-8 /EHsc
    $<$<CONFIG:Release>:/O2 /GL /Gy>)
  target_link_options(BatteryStatus PRIVATE /MANIFEST:NO
    $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>)
else()
  target_compile_options(BatteryStatus PRIVATE -Wall -Wextra -Wno-missing-field-initializers
    -Wno-cast-function-type -municode $<$<CONFIG:Release>:-O2>)
  target_link_options(BatteryStatus PRIVATE -municode -static -static-libgcc -static-libstdc++
    $<$<CONFIG:Release>:-s>)
endif()

install(TARGETS BatteryStatus RUNTIME DESTINATION .)
