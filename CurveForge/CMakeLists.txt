cmake_minimum_required(VERSION 3.20)
project(CurveForge VERSION 1.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(CurveForge WIN32
  src/main.cpp
  src/MainWindow.cpp
  src/MainActions.cpp
  src/DataGrid.cpp
  src/Chart.cpp
  src/Engine.cpp
  src/Fit.cpp
  src/Expr.cpp
  src/Models.cpp
  src/Data.cpp
  src/NumFormat.cpp
  src/Dialogs.cpp
  src/Settings.cpp
  src/Toolbar.cpp
  src/Theme.cpp
  src/Util.cpp
  res/app.rc
)

target_include_directories(CurveForge PRIVATE src res)
target_compile_definitions(CurveForge PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(CurveForge PRIVATE
  comctl32 comdlg32 shell32 shlwapi ole32 dwmapi uxtheme advapi32 gdi32 user32 gdiplus)

if(MSVC)
  # Static CRT: no Visual C++ Redistributable needed.
  set_property(TARGET CurveForge PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_compile_options(CurveForge PRIVATE /W4 /permissive- /utf-8 /EHsc
    $<$<CONFIG:Release>:/O2 /GL /Gy>)
  target_link_options(CurveForge PRIVATE /MANIFEST:NO
    $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>)
else()
  target_compile_options(CurveForge PRIVATE -Wall -Wextra -Wno-missing-field-initializers
    -Wno-cast-function-type -municode $<$<CONFIG:Release>:-O2>)
  target_link_options(CurveForge PRIVATE -municode -static -static-libgcc -static-libstdc++
    $<$<CONFIG:Release>:-s>)
endif()

install(TARGETS CurveForge RUNTIME DESTINATION .)
