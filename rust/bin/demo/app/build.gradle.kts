plugins {
    id("com.android.application")
}

android {
    ndkVersion = "26.0.10404224"
    compileSdk = 34
    defaultConfig {
        applicationId = "net.strayphotons.demo"
        minSdk = 29
        targetSdk = 34
        versionCode = 1
        versionName = "1.0"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }
    /*sourceSets.getByName("main") {
        // Vulkan validation layers
        val ndkHome = System.getenv("NDK_HOME")
        jniLibs.srcDir("${ndkHome}/sources/third_party/vulkan/src/build-android/jniLibs")
    }*/
    buildTypes {
        getByName("debug") {
            isDebuggable = true
            isJniDebuggable = true
            isMinifyEnabled = false
            packaging {
                //jniLibs.keepDebugSymbols.add("*/arm64-v8a/*.so")
                //jniLibs.keepDebugSymbols.add("*/armeabi-v7a/*.so")
                //jniLibs.keepDebugSymbols.add("*/x86/*.so")
                //jniLibs.keepDebugSymbols.add("*/x86_64/*.so")
            }
        }
        getByName("release") {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }
    flavorDimensions.add("abi")
    productFlavors {
        create("arm64") {
            dimension = "abi"
            ndk {
                abiFilters += listOf("arm64-v8a")
            }
        }
        create("arm") {
            dimension = "abi"
            ndk {
                abiFilters += listOf("armeabi-v7a")
            }
        }
        create("x86") {
            dimension = "abi"
            ndk {
                abiFilters += listOf("x86")
            }
        }
        create("x86_64") {
            dimension = "abi"
            ndk {
                abiFilters += listOf("x86_64")
            }
        }
    }

    assetPacks += mutableSetOf()

	compileOptions {
		sourceCompatibility = JavaVersion.VERSION_1_8
		targetCompatibility = JavaVersion.VERSION_1_8
	}
	namespace = "net.strayphotons.demo"
}

dependencies {
}

