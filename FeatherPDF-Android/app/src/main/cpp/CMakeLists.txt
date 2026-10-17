cmake_minimum_required(VERSION 3.22.1)
project(featherpdf CXX)

# PDFIUM_DIR is passed by app/build.gradle.kts (build/pdfium).
set(PDFIUM_ABI_DIR "${PDFIUM_DIR}/${ANDROID_ABI}")

add_library(pdfium SHARED IMPORTED)
set_target_properties(pdfium PROPERTIES IMPORTED_LOCATION "${PDFIUM_ABI_DIR}/lib/libpdfium.so")

add_library(featherpdf SHARED pdf_jni.cpp)
target_include_directories(featherpdf PRIVATE "${PDFIUM_ABI_DIR}/include")
target_compile_features(featherpdf PRIVATE cxx_std_17)
target_compile_options(featherpdf PRIVATE -O2 -Wall -Wextra -fvisibility=hidden)
target_link_options(featherpdf PRIVATE -Wl,--gc-sections -s)
target_link_libraries(featherpdf pdfium jnigraphics log)
