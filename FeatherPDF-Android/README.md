# Feather PDF for Android

A fast, lightweight PDF reader for Android 7.0 and later. It uses the same
engine (PDFium) and the same design ideas as the Windows app in
[`../FeatherPDF`](../FeatherPDF): small tiles rendered on a background
thread, a memory-capped cache, and only a few pages parsed at a time.

© 2026 Akshaya Simha. Developed for faster experience.

## Features

| Area | What you get |
|---|---|
| Opening | Open PDF button, recent documents (long-press to remove), open from file managers, email, browsers and "Share" |
| Multiple documents | Each PDF opens as its own entry in Android's Recents screen, like tabs; re-opening a PDF switches to it |
| Reading | Continuous scroll, single page (tap the edges or swipe to turn), two pages side by side with an optional cover page |
| Zoom | Pinch, double-tap (2.5×), fit width, fit page |
| Navigation | Go to page, fast-scroll handle with page number, contents (bookmarks) panel, page thumbnails panel, clickable links |
| Search | Search as you type, match case, next/previous, highlights on every match |
| Text | Long-press to select, drag the handles, Copy / Select all / Share |
| View | Rotate left/right, page colours (normal, dark, dim), light/dark/system app theme |
| Output | Print (Android print dialog: page ranges, paper size, Save as PDF), share document, share the current page as an image, open with another app |
| Info | Document properties (title, author, dates, PDF version, size, encryption) |
| Security | Password-protected PDFs; web links ask before opening, and only http, https and mailto links are followed |

## Architecture

```
HomeActivity ── recent list, Open PDF (Storage Access Framework)
ReaderActivity ─ one per document (documentLaunchMode=intoExisting)
 ├─ PageView ─── layout, gestures, selection, drawing tiles from TileCache
 ├─ SidePanel ── Contents list + ThumbGridView
 └─ PdfWorker ── the single thread that talks to PDFium (JNI, pdf_jni.cpp)
```

* **One PDFium thread.** PDFium is not thread-safe, so every call goes
  through `PdfWorker`. Work is prioritised: commands (open, text, outline) →
  visible tiles → thumbnails → search. The tile queue is replaced on every
  repaint, so scrolling quickly never renders pages that have already gone
  off screen.
* **Tiles.** Pages are drawn in 512 × 512 tiles straight into Android
  bitmap memory (no copies). While pinching, the tiles from the previous zoom
  level are stretched until sharp ones arrive.
* **Memory.** The tile cache is capped at a quarter of the app's memory
  class (24–128 MB) and evicts least-recently-used tiles. Bitmaps are
  recycled through a pool. PDFium keeps at most 4 pages loaded, and the file
  is read on demand rather than loaded into memory. The app reacts to
  `onTrimMemory` by dropping caches.
* **No libraries.** The app uses only the Android framework (no AndroidX,
  no Kotlin coroutines), which keeps the APK small and start-up quick.

## Building

Requirements: JDK 17, the Android SDK (platform 35) and the NDK with CMake
(Android Studio installs these). PDFium is downloaded automatically on
the first build from
[bblanchon/pdfium-binaries](https://github.com/bblanchon/pdfium-binaries)
(release `chromium/8066`, the same as the Windows app).

```sh
cd FeatherPDF-Android
./gradlew assembleRelease          # APKs in app/build/outputs/apk/release/
./gradlew connectedDebugAndroidTest   # smoke tests on a device or emulator
```

The build produces one APK per CPU type and a universal APK:

| APK | For |
|---|---|
| `app-arm64-v8a-release.apk` | almost all phones and tablets from the last several years |
| `app-armeabi-v7a-release.apk` | older 32-bit devices |
| `app-x86_64-release.apk` | emulators and Chromebooks |
| `app-universal-release.apk` | any device (larger) |

GitHub Actions (`.github/workflows/android.yml`) builds the APKs on every push
and runs the smoke tests on Android 7.0 and Android 14 emulators. Download
the APKs from the run's **FeatherPDF-android-apks** artifact.

### Signing

Without configuration, release APKs are signed with the debug key. That is
fine for installing on your own devices, but updates will only install over
an APK signed with the same key. To use your own key, set these
environment variables (or the same names as repository secrets for CI,
with the keystore file base64-encoded in `FEATHER_KEYSTORE_B64`):

```
FEATHER_KEYSTORE           path to the .jks file (local builds)
FEATHER_KEYSTORE_PASSWORD
FEATHER_KEY_ALIAS
FEATHER_KEY_PASSWORD
```

Never commit the keystore to the repository.

## Installing

1. Copy the APK to the phone (or download the CI artifact on it).
2. Open it and allow "Install unknown apps" for the app you opened it from
   when Android asks.
3. To make Feather PDF the default, open any PDF, choose Feather PDF and tap
   **Always**.

Google Play Protect may warn about an app from an unknown developer. The
warning goes away when the app is published on Google Play (or another
store) with a permanent signing key.

## Limitations

* A malformed PDF that crashes PDFium closes the reader (native crashes can't
  be caught on Android as they can with Windows SEH). Other open documents
  and the home screen are not affected.
* Printing sends the original PDF to the print service. Password-protected
  PDFs may print blank or be refused, depending on the print service.
* Links inside the document jump to the target page; the exact position on
  the page is used only when the PDF includes one.
* No annotations, forms filling or editing in this version.

## Licences

Feather PDF uses PDFium (BSD-3-Clause and Apache-2.0), © The PDFium Authors.
See `../FeatherPDF/THIRD_PARTY_NOTICES.md`.
