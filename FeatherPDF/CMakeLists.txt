cmake_minimum_required(VERSION 3.20)
project(FeatherPDF VERSION 2.1.0 LANGUAGES CXX RC)

# ---------------------------------------------------------------------------
# PDFium (prebuilt binaries from https://github.com/bblanchon/pdfium-binaries)
#
# Pass -DPDFIUM_DIR=<folder containing include/, lib/, bin/> to use a local
# copy; otherwise the pinned release below is downloaded at configure time.
# ---------------------------------------------------------------------------
set(PDFIUM_VERSION "8066" CACHE STRING "pdfium-binaries release (chromium/<N>)")
set(PDFIUM_DIR "" CACHE PATH "Folder with an extracted pdfium-binaries package")

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(_pdfium_arch "x86")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64" OR CMAKE_GENERATOR_PLATFORM STREQUAL "ARM64")
  set(_pdfium_arch "arm64")
else()
  set(_pdfium_arch "x64")
endif()

if(NOT PDFIUM_DIR)
  set(PDFIUM_DIR "${CMAKE_BINARY_DIR}/_deps/pdfium-${_pdfium_arch}-${PDFIUM_VERSION}")
  if(NOT EXISTS "${PDFIUM_DIR}/include/fpdfview.h")
    set(_url "https://github.com/bblanchon/pdfium-binaries/releases/download/chromium%2F${PDFIUM_VERSION}/pdfium-win-${_pdfium_arch}.tgz")
    message(STATUS "Downloading PDFium: ${_url}")
    file(DOWNLOAD "${_url}" "${CMAKE_BINARY_DIR}/_deps/pdfium.tgz" STATUS _st TLS_VERIFY ON)
    list(GET _st 0 _code)
    if(NOT _code EQUAL 0)
      message(FATAL_ERROR "PDFium download failed: ${_st}. Download it manually and set PDFIUM_DIR.")
    endif()
    file(ARCHIVE_EXTRACT INPUT "${CMAKE_BINARY_DIR}/_deps/pdfium.tgz" DESTINATION "${PDFIUM_DIR}")
  endif()
endif()

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(FeatherPDF WIN32
  src/main.cpp
  src/MainWindow.cpp
  src/PdfView.cpp
  src/PageCache.cpp
  src/RenderWorker.cpp
  src/PdfEngine.cpp
  src/DocEditor.cpp
  src/Search.cpp
  src/Toolbar.cpp
  src/TabBar.cpp
  src/Sidebar.cpp
  src/ThumbView.cpp
  src/Theme.cpp
  src/Settings.cpp
  src/FileAssoc.cpp
  src/Util.cpp
  src/Picture.cpp
  src/CommandPalette.cpp
  res/app.rc
)

target_include_directories(FeatherPDF PRIVATE src res "${PDFIUM_DIR}/include")
target_compile_definitions(FeatherPDF PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(FeatherPDF PRIVATE
  comctl32 comdlg32 shell32 ole32 dwmapi uxtheme advapi32 gdi32 user32 gdiplus)

if(MSVC)
  # Static CRT: no Visual C++ Redistributable needed on the target PC.
  set_property(TARGET FeatherPDF PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_compile_options(FeatherPDF PRIVATE /W4 /permissive- /utf-8 /EHsc
    $<$<CONFIG:Release>:/O2 /GL /Gy>)
  target_link_options(FeatherPDF PRIVATE /MANIFEST:NO   # manifest comes from app.rc
    $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>)
  target_link_libraries(FeatherPDF PRIVATE "${PDFIUM_DIR}/lib/pdfium.dll.lib")
else()
  # MinGW-w64: link directly against the DLL, static libstdc++/libgcc.
  target_compile_options(FeatherPDF PRIVATE -Wall -Wextra -Wno-missing-field-initializers -Wno-cast-function-type -municode
    $<$<CONFIG:Release>:-O2>)
  target_link_options(FeatherPDF PRIVATE -municode -static -static-libgcc -static-libstdc++
    $<$<CONFIG:Release>:-s>)
  target_link_libraries(FeatherPDF PRIVATE "${PDFIUM_DIR}/bin/pdfium.dll")
endif()

# pdfium.dll must sit next to the executable.
add_custom_command(TARGET FeatherPDF POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PDFIUM_DIR}/bin/pdfium.dll" "$<TARGET_FILE_DIR:FeatherPDF>")

# `cmake --install build --prefix dist` produces a ready-to-zip portable folder.
install(TARGETS FeatherPDF RUNTIME DESTINATION .)
install(FILES "${PDFIUM_DIR}/bin/pdfium.dll" DESTINATION .)
install(FILES "${PDFIUM_DIR}/LICENSE" DESTINATION licenses RENAME pdfium-binaries.txt)
install(DIRECTORY "${PDFIUM_DIR}/licenses/" DESTINATION licenses)
install(FILES THIRD_PARTY_NOTICES.md DESTINATION .)
