import java.net.URI

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// ---------------------------------------------------------------------------
// PDFium (prebuilt, https://github.com/bblanchon/pdfium-binaries), pinned to
// the same release as the Windows app. Downloaded once per ABI into
// build/pdfium/<abi>/ and packaged from build/pdfium/jniLibs/<abi>/.
// ---------------------------------------------------------------------------
val pdfiumRelease = "8066"
val pdfiumArchByAbi = mapOf(
    "arm64-v8a" to "arm64",    // almost every phone and tablet
    "armeabi-v7a" to "arm",    // older 32-bit devices
    "x86_64" to "x64",         // emulators and Chromebooks
)
val pdfiumDir = layout.buildDirectory.dir("pdfium").get().asFile

val fetchPdfium by tasks.registering {
    description = "Downloads the prebuilt PDFium library for every ABI."
    outputs.dir(pdfiumDir)
    doLast {
        pdfiumArchByAbi.forEach { (abi, arch) ->
            val abiDir = File(pdfiumDir, abi)
            val so = File(abiDir, "lib/libpdfium.so")
            if (!so.exists()) {
                abiDir.mkdirs()
                val tgz = File(pdfiumDir, "pdfium-android-$arch.tgz")
                val url = "https://github.com/bblanchon/pdfium-binaries/releases/download/" +
                    "chromium%2F$pdfiumRelease/pdfium-android-$arch.tgz"
                URI(url).toURL().openStream().use { input ->
                    tgz.outputStream().use { input.copyTo(it) }
                }
                project.copy {
                    from(project.tarTree(project.resources.gzip(tgz)))
                    into(abiDir)
                }
            }
            val packaged = File(pdfiumDir, "jniLibs/$abi/libpdfium.so")
            packaged.parentFile.mkdirs()
            so.copyTo(packaged, overwrite = true)
        }
    }
}

android {
    namespace = "com.featherpdf.viewer"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.featherpdf.viewer"
        minSdk = 24          // Android 7.0 and later
        targetSdk = 35
        versionCode = 1
        versionName = "1.0.0"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        ndk { abiFilters += pdfiumArchByAbi.keys }
        externalNativeBuild {
            cmake { arguments += "-DPDFIUM_DIR=${pdfiumDir.absolutePath}" }
        }
    }

    externalNativeBuild {
        cmake { path = file("src/main/cpp/CMakeLists.txt") }
    }

    sourceSets["main"].jniLibs.srcDir(File(pdfiumDir, "jniLibs"))

    // One small APK per CPU type, plus a universal one.
    splits {
        abi {
            isEnable = true
            reset()
            include(*pdfiumArchByAbi.keys.toTypedArray())
            isUniversalApk = true
        }
    }

    // Release signing: set FEATHER_KEYSTORE (path), FEATHER_KEYSTORE_PASSWORD,
    // FEATHER_KEY_ALIAS and FEATHER_KEY_PASSWORD. Without them the release
    // build is signed with the debug key so it can still be installed.
    val keystore = System.getenv("FEATHER_KEYSTORE")?.let { file(it) }?.takeIf { it.exists() }
    signingConfigs {
        if (keystore != null) {
            create("release") {
                storeFile = keystore
                storePassword = System.getenv("FEATHER_KEYSTORE_PASSWORD")
                keyAlias = System.getenv("FEATHER_KEY_ALIAS")
                keyPassword = System.getenv("FEATHER_KEY_PASSWORD")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro",
            )
            signingConfig = if (keystore != null) signingConfigs.getByName("release")
                            else signingConfigs.getByName("debug")
        }
    }

    packaging {
        // Compressed native libraries: a smaller download (about half).
        jniLibs {
            useLegacyPackaging = true
            pickFirsts += "**/libpdfium.so"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }

    lint { abortOnError = false }
}

// PDFium must be present before CMake configures and before native
// libraries are merged into the APK.
tasks.configureEach {
    if (name.contains("CMake") || name.contains("JniLib") || name.contains("NativeLib") ||
        name == "preBuild"
    ) {
        dependsOn(fetchPdfium)
    }
}

dependencies {
    // The app itself has no library dependencies (framework only, for size
    // and start-up speed). These are used only by the emulator smoke test.
    androidTestImplementation("androidx.test:runner:1.6.2")
    androidTestImplementation("androidx.test:core:1.6.1")
    androidTestImplementation("androidx.test.ext:junit:1.2.1")
    androidTestImplementation("junit:junit:4.13.2")
}
