cmake_minimum_required(VERSION 3.20)
project(FeatherCAD VERSION 1.2.0 LANGUAGES CXX RC)

# Feather CAD: a lightweight 2D drafting program (Win32 + Direct2D, no
# third-party libraries).
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The drawing model and command engine have no Windows code; the unit tests
# build them on their own.
set(CORE_SOURCES
  src/Geometry.cpp
  src/Entity.cpp
  src/Spline.cpp
  src/Curve.cpp
  src/Modify.cpp
  src/Grips.cpp
  src/Select.cpp
  src/Document.cpp
  src/Snap.cpp
  src/Engine.cpp
  src/Commands.cpp
  src/CommandsModify.cpp
  src/CommandsDraw.cpp
)

add_executable(FeatherCAD WIN32
  ${CORE_SOURCES}
  src/main.cpp
  src/MainWindow.cpp
  src/Canvas.cpp
  src/CommandLine.cpp
  src/StatusBar.cpp
  src/Toolbar.cpp
  src/Icons.cpp
  src/CommandPalette.cpp
  src/Dialogs.cpp
  src/History.cpp
  src/Settings.cpp
  src/Theme.cpp
  src/Util.cpp
  res/app.rc
)

target_include_directories(FeatherCAD PRIVATE src res)
target_compile_definitions(FeatherCAD PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(FeatherCAD PRIVATE
  d2d1 dwrite comctl32 comdlg32 shell32 ole32 dwmapi uxtheme advapi32 gdi32 user32 gdiplus)

if(MSVC)
  # Static CRT: no Visual C++ Redistributable needed on the target PC.
  set_property(TARGET FeatherCAD PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_compile_options(FeatherCAD PRIVATE /W4 /permissive- /utf-8 /EHsc
    $<$<CONFIG:Release>:/O2 /GL /Gy>)
  target_link_options(FeatherCAD PRIVATE /MANIFEST:NO   # manifest comes from app.rc
    $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>)
else()
  target_compile_options(FeatherCAD PRIVATE -Wall -Wextra -Wno-missing-field-initializers -Wno-cast-function-type -municode
    $<$<CONFIG:Release>:-O2>)
  target_link_options(FeatherCAD PRIVATE -municode -static -static-libgcc -static-libstdc++
    $<$<CONFIG:Release>:-s>)
endif()

# Unit tests for geometry, undo, files, snaps and the command engine:
#   cmake -DFEATHERCAD_TESTS=ON ...   then run CoreTests(.exe)
option(FEATHERCAD_TESTS "Build the unit tests" OFF)
if(FEATHERCAD_TESTS)
  add_executable(CoreTests tests/CoreTests.cpp ${CORE_SOURCES})
  target_include_directories(CoreTests PRIVATE src)
  if(MSVC)
    set_property(TARGET CoreTests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(CoreTests PRIVATE /W4 /utf-8 /EHsc)
  else()
    target_compile_options(CoreTests PRIVATE -Wall -Wextra)
    if(WIN32)
      target_link_options(CoreTests PRIVATE -static)
    endif()
  endif()
endif()

# `cmake --install build --prefix dist` produces a ready-to-zip portable folder.
install(TARGETS FeatherCAD RUNTIME DESTINATION .)
install(FILES README.md DESTINATION .)
