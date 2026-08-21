plugins {
    id("com.android.application")
}

android {
    namespace = "com.sensor.logger"
    compileSdk = 35
    ndkVersion = "27.0.12077973"

    defaultConfig {
        applicationId = "com.sensor.logger"
        // The NDK sensor API's package-scoped manager arrived in 26, and the
        // deprecated path it replaces is not worth carrying.
        minSdk = 26
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
            // The capture path only runs on a real camera, so the x86 emulator
            // images are of no use and the device ABIs are the only targets.
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

    implementation("androidx.games:games-activity:3.0.5")
    // GameActivity extends AppCompatActivity; without this its class fails to
    // load and the activity cannot be instantiated at all.
    implementation("androidx.appcompat:appcompat:1.7.0")
}
