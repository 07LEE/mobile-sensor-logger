plugins {
    id("com.android.application")
}

// The ARCore AAR carries the native library but no headers and no Prefab
// package, so CMake cannot find it the way it finds game-activity. The library
// is unpacked here and its path handed to CMake below; the header is vendored
// under src/main/cpp/include, since it is published only in the SDK repository.
val arcoreVersion = "1.49.0"
val arcoreNatives: Configuration by configurations.creating

val arcoreNativeDir = layout.buildDirectory.dir("arcore-native")

val extractArcoreNatives by tasks.registering(Copy::class) {
    from({ arcoreNatives.map { zipTree(it) } })
    include("jni/**")
    into(arcoreNativeDir)
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
    implementation("com.google.ar:core:$arcoreVersion")
    implementation("androidx.games:games-activity:3.0.5")

    // Same artifact again, resolved into its own configuration purely so the
    // AAR can be unpacked for its native library.
    arcoreNatives("com.google.ar:core:$arcoreVersion")
}

tasks.named("preBuild") {
    dependsOn(extractArcoreNatives)
}
