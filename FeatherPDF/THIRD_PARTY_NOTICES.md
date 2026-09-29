# Third-party notices

Feather PDF uses a single third-party component at run time: **PDFium**,
distributed as `pdfium.dll` (prebuilt by the
[pdfium-binaries](https://github.com/bblanchon/pdfium-binaries) project,
release `chromium/8066`, no V8/XFA).

| Component | Used for | License |
|---|---|---|
| PDFium | PDF parsing, rendering, text extraction | BSD-3-Clause (some files Apache-2.0) |
| pdfium-binaries build scripts | Prebuilt DLL | MIT |
| FreeType | Font rendering (inside pdfium.dll) | FreeType License (FTL) |
| libjpeg-turbo | JPEG images | IJG + BSD-3-Clause + zlib |
| libpng | PNG images | libpng license |
| zlib | Flate streams | zlib license |
| OpenJPEG | JPEG 2000 images | BSD-2-Clause |
| Little CMS (lcms2) | Colour management | MIT |
| Anti-Grain Geometry 2.3 | Rasterisation | AGG license (BSD-style) |
| ICU | Unicode support | Unicode License v3 |
| Abseil | Utility library | Apache-2.0 |
| fast_float, simdutf | Number / UTF parsing | MIT / Apache-2.0 |
| LLVM libc | Support routines | Apache-2.0 with LLVM exception |

The full license texts are installed in the `licenses/` folder next to
`FeatherPDF.exe` (copied from the pdfium-binaries package by
`cmake --install`).

All of these are permissive licenses: Feather PDF can be distributed,
including commercially and in closed-source form, as long as these notices
are shipped with it.

The application itself uses only Windows system libraries (user32, gdi32,
comctl32, comdlg32, shell32, ole32, dwmapi, uxtheme, advapi32) and the C++
standard library, which is statically linked.
