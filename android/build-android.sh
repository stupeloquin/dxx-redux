#!/bin/bash
set -e

echo "=== DXX-Redux Android Build Script ==="

export ANDROID_HOME=/opt/android-sdk
export ANDROID_SDK_ROOT=$ANDROID_HOME
export ANDROID_NDK_HOME=$ANDROID_HOME/ndk/26.1.10909125
export PATH="$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$ANDROID_NDK_HOME:$PATH"

# Install CMake 3.28.3 if not present
if [ ! -f $ANDROID_HOME/cmake/3.28.3/bin/cmake ]; then
    echo "=== Installing CMake 3.28.3 ==="
    yes | sdkmanager --install "cmake;3.28.3" 2>/dev/null || true
    if [ ! -f $ANDROID_HOME/cmake/3.28.3/bin/cmake ]; then
        CMAKE_URL="https://github.com/Kitware/CMake/releases/download/v3.28.3/cmake-3.28.3-linux-x86_64.tar.gz"
        wget -q "$CMAKE_URL" -O /tmp/cmake.tar.gz
        mkdir -p $ANDROID_HOME/cmake/3.28.3
        tar xzf /tmp/cmake.tar.gz -C $ANDROID_HOME/cmake/3.28.3 --strip-components=1
        rm /tmp/cmake.tar.gz
    fi
fi
export PATH="$ANDROID_HOME/cmake/3.28.3/bin:$PATH"

DXX_ROOT=/build/dxx-redux
ANDROID_PROJECT=/build/dxx-android
DEPS=/build/dxx-android/deps

# ============================================================
# Game selection: d1x (Descent 1) or d2x (Descent 2)
# ============================================================
DXX_GAME=${DXX_GAME:-d1x}
case "$DXX_GAME" in
    d1x) DXX_PACKAGE="com.dxxredux.d1x"
         DXX_LABEL="D1X-Redux"; DXX_SRCDIR="d1" ;;
    d2x) DXX_PACKAGE="com.dxxredux.d2x"
         DXX_LABEL="D2X-Redux"; DXX_SRCDIR="d2" ;;
    *)   echo "ERROR: DXX_GAME must be 'd1x' or 'd2x'"; exit 1 ;;
esac
DXX_PACKAGE_PATH=$(echo "$DXX_PACKAGE" | tr '.' '/')
echo "=== Building $DXX_LABEL (DXX_GAME=$DXX_GAME) ==="

# ============================================================
# Step 1: Download dependencies
# ============================================================
echo "=== Downloading dependencies ==="
mkdir -p $DEPS && cd $DEPS

if [ ! -d SDL2-2.30.10 ]; then
    echo "Downloading SDL2..."
    wget -q https://github.com/libsdl-org/SDL/releases/download/release-2.30.10/SDL2-2.30.10.tar.gz
    tar xzf SDL2-2.30.10.tar.gz
fi

if [ ! -d SDL2_mixer-2.8.0 ]; then
    echo "Downloading SDL2_mixer..."
    wget -q https://github.com/libsdl-org/SDL_mixer/releases/download/release-2.8.0/SDL2_mixer-2.8.0.tar.gz
    tar xzf SDL2_mixer-2.8.0.tar.gz
fi

if [ ! -d physfs-3.2.0 ]; then
    echo "Downloading PhysFS..."
    wget -q https://github.com/icculus/physfs/archive/refs/tags/release-3.2.0.tar.gz -O physfs-3.2.0.tar.gz
    tar xzf physfs-3.2.0.tar.gz
    mv physfs-release-3.2.0 physfs-3.2.0
fi

# ============================================================
# Step 2: Set up Android project from SDL2 template
# ============================================================
echo "=== Setting up Android project ==="
if [ ! -d $ANDROID_PROJECT/app ]; then
    cp -r $DEPS/SDL2-2.30.10/android-project/* $ANDROID_PROJECT/
fi

mkdir -p $ANDROID_PROJECT/app/jni

if [ ! -e $ANDROID_PROJECT/app/jni/SDL ]; then
    ln -sf ../../deps/SDL2-2.30.10 $ANDROID_PROJECT/app/jni/SDL
fi

if [ ! -e $ANDROID_PROJECT/app/jni/SDL2_mixer ]; then
    ln -sf ../../deps/SDL2_mixer-2.8.0 $ANDROID_PROJECT/app/jni/SDL2_mixer
fi

# ============================================================
# Step 3: Cross-compile PhysFS
# ============================================================
echo "=== Cross-compiling PhysFS ==="
PHYSFS_BUILD=/tmp/physfs-build-android
PHYSFS_INSTALL=$ANDROID_PROJECT/app/jni/physfs-install

if [ ! -f $PHYSFS_INSTALL/lib/libphysfs.a ]; then
    mkdir -p $PHYSFS_BUILD $PHYSFS_INSTALL
    cd $PHYSFS_BUILD
    cmake \
        -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
        -DANDROID_ABI=${TARGET_ABI:-x86_64} \
        -DANDROID_PLATFORM=android-24 \
        -DCMAKE_BUILD_TYPE=Release \
        -DPHYSFS_BUILD_SHARED=OFF \
        -DPHYSFS_BUILD_STATIC=ON \
        -DPHYSFS_BUILD_TEST=OFF \
        -DPHYSFS_BUILD_DOCS=OFF \
        -DCMAKE_INSTALL_PREFIX=$PHYSFS_INSTALL \
        $DEPS/physfs-3.2.0
    make -j$(nproc)
    make install
    echo "PhysFS installed to $PHYSFS_INSTALL"
else
    echo "PhysFS already built, skipping"
fi

# ============================================================
# Step 4: Configure Android project build files
# ============================================================
echo "=== Configuring Android project ==="

cat > $ANDROID_PROJECT/local.properties << 'LOCALEOF'
sdk.dir=/opt/android-sdk
LOCALEOF

cat > $ANDROID_PROJECT/gradle.properties << 'GRADLEPROPEOF'
org.gradle.jvmargs=-Xmx2048m
android.useAndroidX=true
GRADLEPROPEOF

# Write app/build.gradle
cat > $ANDROID_PROJECT/app/build.gradle << BUILDEOF
plugins {
    id 'com.android.application'
}

android {
    namespace "$DXX_PACKAGE"
    compileSdk 34
    ndkVersion "26.1.10909125"

    defaultConfig {
        applicationId "$DXX_PACKAGE"
        minSdk 24
        targetSdk 34
        versionCode 1
        versionName "1.1.0"

        ndk {
            abiFilters '${TARGET_ABI:-x86_64}'
        }

        externalNativeBuild {
            cmake {
                arguments "-DANDROID_STL=c++_shared",
                          "-DANDROID_PLATFORM=android-24",
                          "-DDXX_GAME=$DXX_GAME"
            }
        }
    }

    signingConfigs {
        debug {
            storeFile file("../debug.keystore")
            storePassword "android"
            keyAlias "androiddebugkey"
            keyPassword "android"
        }
    }

    buildTypes {
        debug {
            signingConfig signingConfigs.debug
        }
        release {
            minifyEnabled false
        }
    }

    externalNativeBuild {
        cmake {
            path "jni/src/CMakeLists.txt"
            version "3.28.3"
        }
    }

    sourceSets {
        main {
            java.srcDirs = ['src/main/java']
        }
    }

    lint {
        abortOnError false
    }
}

dependencies {
}
BUILDEOF

# Write top-level build.gradle
cat > $ANDROID_PROJECT/build.gradle << 'TOPBUILDEOF'
buildscript {
    repositories {
        google()
        mavenCentral()
    }
    dependencies {
        classpath 'com.android.tools.build:gradle:8.1.1'
    }
}

allprojects {
    repositories {
        google()
        mavenCentral()
    }
}
TOPBUILDEOF

cat > $ANDROID_PROJECT/settings.gradle << 'SETTINGSEOF'
include ':app'
SETTINGSEOF

# Gradle wrapper
mkdir -p $ANDROID_PROJECT/gradle/wrapper
cat > $ANDROID_PROJECT/gradle/wrapper/gradle-wrapper.properties << 'WRAPPEREOF'
distributionBase=GRADLE_USER_HOME
distributionPath=wrapper/dists
distributionUrl=https\://services.gradle.org/distributions/gradle-8.5-bin.zip
zipStoreBase=GRADLE_USER_HOME
zipStorePath=wrapper/dists
WRAPPEREOF

# AndroidManifest.xml
mkdir -p $ANDROID_PROJECT/app/src/main
cat > $ANDROID_PROJECT/app/src/main/AndroidManifest.xml << MANIFESTEOF
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    xmlns:tools="http://schemas.android.com/tools">

    <uses-permission android:name="android.permission.INTERNET" />
    <uses-permission android:name="android.permission.READ_EXTERNAL_STORAGE" />
    <uses-permission android:name="android.permission.WRITE_EXTERNAL_STORAGE" />
    <uses-permission android:name="android.permission.MANAGE_EXTERNAL_STORAGE"
        tools:ignore="ScopedStorage" />

    <uses-feature android:glEsVersion="0x00010001" android:required="true" />

    <application
        android:allowBackup="true"
        android:icon="@mipmap/ic_launcher"
        android:label="$DXX_LABEL"
        android:hasCode="true"
        android:hardwareAccelerated="true"
        android:requestLegacyExternalStorage="true">
        <activity
            android:name="$DXX_PACKAGE.DxxActivity"
            android:configChanges="keyboard|keyboardHidden|orientation|screenSize|screenLayout|uiMode"
            android:screenOrientation="sensorLandscape"
            android:exported="true">
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>
    </application>
</manifest>
MANIFESTEOF

# Copy SDL2 Java source files
mkdir -p $ANDROID_PROJECT/app/src/main/java/org/libsdl/app
cp $DEPS/SDL2-2.30.10/android-project/app/src/main/java/org/libsdl/app/*.java \
   $ANDROID_PROJECT/app/src/main/java/org/libsdl/app/

# Copy DxxActivity.java
mkdir -p $ANDROID_PROJECT/app/src/main/java/$DXX_PACKAGE_PATH
cp $DXX_ROOT/android/DxxActivity.java \
   $ANDROID_PROJECT/app/src/main/java/$DXX_PACKAGE_PATH/DxxActivity.java
sed -i "s/^package com\.dxxredux\.d1x;/package $DXX_PACKAGE;/" \
   $ANDROID_PROJECT/app/src/main/java/$DXX_PACKAGE_PATH/DxxActivity.java

# ============================================================
# Step 5: Create the CMake build file
# ============================================================
echo "=== Creating CMakeLists.txt ==="
mkdir -p $ANDROID_PROJECT/app/jni/src
cp $DXX_ROOT/android/CMakeLists.android.txt $ANDROID_PROJECT/app/jni/src/CMakeLists.txt

# ============================================================
# Step 6: Set up Gradle wrapper
# ============================================================
echo "=== Setting up Gradle ==="
cd $ANDROID_PROJECT

SDL2_TEMPLATE=$DEPS/SDL2-2.30.10/android-project
mkdir -p $ANDROID_PROJECT/gradle/wrapper
cp $SDL2_TEMPLATE/gradlew $ANDROID_PROJECT/gradlew
cp $SDL2_TEMPLATE/gradlew.bat $ANDROID_PROJECT/gradlew.bat 2>/dev/null || true
cp $SDL2_TEMPLATE/gradle/wrapper/gradle-wrapper.jar $ANDROID_PROJECT/gradle/wrapper/gradle-wrapper.jar
cp $SDL2_TEMPLATE/gradle/wrapper/gradle-wrapper.properties $ANDROID_PROJECT/gradle/wrapper/gradle-wrapper.properties
chmod +x $ANDROID_PROJECT/gradlew

# ============================================================
# Step 7: Build the APK
# ============================================================
# Generate debug keystore if missing
if [ ! -f $ANDROID_PROJECT/debug.keystore ]; then
    keytool -genkey -v -keystore $ANDROID_PROJECT/debug.keystore \
        -storepass android -alias androiddebugkey -keypass android \
        -keyalg RSA -keysize 2048 -validity 10000 \
        -dname "CN=Android Debug,O=Android,C=US"
fi

echo "=== Building APK ==="
cd $ANDROID_PROJECT
./gradlew assembleDebug --no-daemon --stacktrace 2>&1

echo "=== Build Complete ==="
ls -la $ANDROID_PROJECT/app/build/outputs/apk/debug/ 2>/dev/null || echo "APK not found - build may have failed"
