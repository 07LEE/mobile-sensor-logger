plugins {
    id("com.android.application")
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
            }
        }

        ndk {
            // ARCore ships arm64 and armv7 only; there is no x86 emulator image
            // for it, so the device is the only build target that matters.
            abiFilters += listOf("arm64-v8a", "armeabi-v7a")
        }
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
    // Verify the current release before building; this pins a known-good line.
    implementation("com.google.ar:core:1.49.0")
    implementation("androidx.games:games-activity:3.0.5")
}
