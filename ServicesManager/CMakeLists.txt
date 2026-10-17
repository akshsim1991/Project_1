cmake_minimum_required(VERSION 3.20)
project(ServicesManager VERSION 2.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ServicesManager WIN32
  src/main.cpp
  src/MainWindow.cpp
  src/ServiceList.cpp
  src/Services.cpp
  src/Worker.cpp
  src/Dialogs.cpp
  src/Details.cpp
  src/Inspect.cpp
  src/Advice.cpp
  src/Profiles.cpp
  src/Settings.cpp
  src/Toolbar.cpp
  src/Theme.cpp
  src/Util.cpp
  res/app.rc
)

target_include_directories(ServicesManager PRIVATE src res)
target_compile_definitions(ServicesManager PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(ServicesManager PRIVATE
  comctl32 comdlg32 shell32 shlwapi ole32 dwmapi uxtheme advapi32 version gdi32 user32
  wintrust crypt32 bcrypt psapi wevtapi winhttp)

if(MSVC)
  # Static CRT: no Visual C++ Redistributable needed.
  set_property(TARGET ServicesManager PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_compile_options(ServicesManager PRIVATE /W4 /permissive- /utf-8 /EHsc
    $<$<CONFIG:Release>:/O2 /GL /Gy>)
  target_link_options(ServicesManager PRIVATE /MANIFEST:NO
    $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>)
else()
  target_compile_options(ServicesManager PRIVATE -Wall -Wextra -Wno-missing-field-initializers
    -Wno-cast-function-type -municode $<$<CONFIG:Release>:-O2>)
  target_link_options(ServicesManager PRIVATE -municode -static -static-libgcc -static-libstdc++
    $<$<CONFIG:Release>:-s>)
endif()

install(TARGETS ServicesManager RUNTIME DESTINATION .)
