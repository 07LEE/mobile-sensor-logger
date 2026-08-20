import java.net.URI

plugins {
    id("com.android.application")
}

// The ARCore AAR carries the native library but no headers and no Prefab
// package, so CMake cannot find it the way it finds game-activity. Both pieces
// are fetched here and their paths handed to CMake below.
val arcoreVersion = "1.49.0"
val arcoreNatives: Configuration by configurations.creating

val arcoreNativeDir = layout.buildDirectory.dir("arcore-native")
val arcoreIncludeDir = layout.buildDirectory.dir("arcore-include")

val extractArcoreNatives by tasks.registering(Copy::class) {
    from({ arcoreNatives.map { zipTree(it) } })
    include("jni/**")
    into(arcoreNativeDir)
}

// The C API header ships only in the SDK repository, not on Maven. It is
// downloaded rather than committed because it is Google's file under ARCore's
// terms, and this repository is public — redistributing it here is avoidable.
// Declaring the output keeps it from being fetched on every build.
val downloadArcoreHeader by tasks.registering {
    val header = arcoreIncludeDir.map { it.file("arcore_c_api.h") }
    outputs.file(header)

    doLast {
        val target = header.get().asFile
        target.parentFile.mkdirs()

        val url = "https://raw.githubusercontent.com/google-ar/" +
            "arcore-android-sdk/v$arcoreVersion/libraries/include/arcore_c_api.h"

        URI(url).toURL().openStream().use { source ->
            target.outputStream().use { source.copyTo(it) }
        }

        // A truncated or redirected download would otherwise surface as a
        // confusing compile error much later.
        if (target.length() < 1000) {
            throw GradleException("ARCore header download looks wrong: $url")
        }
    }
}

android {
    namespace = "com.sensor.logger"
    compileSdk = 35
    ndkVersion = "27.0.12077973"

    defaultConfig {
        applicationId = "com.sensor.logger"
        // ARCore requires API 24.
        minSdk = 24
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0"

        externalNativeBuild {
            cmake {
                cppFlags += "-std=c++17"
                arguments += "-DANDROID_STL=c++_shared"
                arguments += "-DARCORE_LIBPATH=" +
                    arcoreNativeDir.get().asFile.resolve("jni").path
                arguments += "-DARCORE_INCLUDE=" +
                    arcoreIncludeDir.get().asFile.path
            }
        }

        ndk {
            // ARCore ships arm64 and armv7 only; there is no x86 emulator image
            // for it, so the device is the only build target that matters.
            abiFilters += listOf("arm64-v8a", "armeabi-v7a")
        }
    }

    // Without this, the native packages inside AAR dependencies are not exposed
    // to CMake and find_package cannot see them.
    buildFeatures {
        prefab = true
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

dependencies {
    // games-activity still pulls kotlin-stdlib-jdk7/jdk8 1.6.21 while appcompat
    // pulls kotlin-stdlib 1.8.22. Those jdk artifacts were folded into the main
    // one in Kotlin 1.8, so both on the classpath means duplicate classes. The
    // BOM pins every stdlib artifact to one version and the folded-in ones
    // become empty shells.
    implementation(platform("org.jetbrains.kotlin:kotlin-bom:1.8.22"))

    implementation("com.google.ar:core:$arcoreVersion")
    implementation("androidx.games:games-activity:3.0.5")
    // GameActivity extends AppCompatActivity; without this its class fails to
    // load and the activity cannot be instantiated at all.
    implementation("androidx.appcompat:appcompat:1.7.0")

    // Same artifact again, resolved into its own configuration purely so the
    // AAR can be unpacked for its native library.
    arcoreNatives("com.google.ar:core:$arcoreVersion")
}

tasks.named("preBuild") {
    dependsOn(extractArcoreNatives, downloadArcoreHeader)
}
