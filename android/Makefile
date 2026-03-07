# DXX-Redux Android Build System
#
# Usage:
#   make                  Build D1X arm64 APK (for physical device)
#   make build-apk-x86   Build D1X x86_64 APK (for emulator)
#   make build-d2x       Build D2X arm64 APK
#   make build-d2x-x86   Build D2X x86_64 APK
#   make deploy           Install APK on connected device
#   make clean            Remove build container for current game
#   make clean-all        Remove build containers for both games
#
# One-time setup:
#   make docker-image     Build the Docker build image

.PHONY: build-apk build-apk-x86 build-d2x build-d2x-x86 \
        docker-image deploy deploy-permissions deploy-data \
        clean clean-all _build

# --- Paths ---
MAKEFILE_DIR    := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
DXX_ROOT        := $(abspath $(MAKEFILE_DIR)/..)
ANDROID_PROJECT := $(abspath $(MAKEFILE_DIR)/../../dxx-android)
ADB_KEYS        := $(HOME)/.android

# --- Game selection ---
DXX_GAME        ?= d1x

ifeq ($(DXX_GAME),d2x)
    DXX_PACKAGE := com.dxxredux.d2x
    GAME_DATA   := $(abspath $(MAKEFILE_DIR)/../../data/descent2/data)
else
    DXX_PACKAGE := com.dxxredux.d1x
    GAME_DATA   := $(abspath $(MAKEFILE_DIR)/../../data/descent/data)
endif

# --- Docker ---
DOCKER          := sg docker -c
BUILDER_IMAGE   := dxx-redux-android-builder
CONTAINER        = dxx-redux-builder-$(DXX_GAME)

# --- Build ---
TARGET_ABI      ?= arm64-v8a
APK             := $(ANDROID_PROJECT)/app/build/outputs/apk/debug/app-debug.apk

# ==== Build targets ====

build-apk: TARGET_ABI = arm64-v8a
build-apk: _build

build-apk-x86: TARGET_ABI = x86_64
build-apk-x86: _build

build-d2x: DXX_GAME = d2x
build-d2x: TARGET_ABI = arm64-v8a
build-d2x: _build

build-d2x-x86: DXX_GAME = d2x
build-d2x-x86: TARGET_ABI = x86_64
build-d2x-x86: _build

_build:
	-@$(DOCKER) "docker create --name $(CONTAINER) \
		-e TARGET_ABI=$(TARGET_ABI) \
		-e DXX_GAME=$(DXX_GAME) \
		-v $(DXX_ROOT):/build/dxx-redux \
		-v $(ANDROID_PROJECT):/build/dxx-android \
		-v $(GAME_DATA):/build/game-data:ro \
		$(BUILDER_IMAGE) \
		/build/dxx-redux/android/build-android.sh" 2>/dev/null
	$(DOCKER) "docker start -a $(CONTAINER)"

# ==== Docker image ====

docker-image:
	$(DOCKER) "docker build -t $(BUILDER_IMAGE) -f $(MAKEFILE_DIR)Dockerfile.android $(MAKEFILE_DIR)"

# ==== Deploy targets ====

deploy:
	$(DOCKER) "docker run --rm --privileged \
		-v /dev/bus/usb:/dev/bus/usb \
		-v $(ADB_KEYS):/root/.android \
		-v $(ANDROID_PROJECT):/build/dxx-android \
		$(BUILDER_IMAGE) \
		bash -c 'adb wait-for-device && adb install -r /build/dxx-android/app/build/outputs/apk/debug/app-debug.apk'"

deploy-permissions:
	$(DOCKER) "docker run --rm --privileged \
		-v /dev/bus/usb:/dev/bus/usb \
		-v $(ADB_KEYS):/root/.android \
		$(BUILDER_IMAGE) \
		adb shell appops set $(DXX_PACKAGE) MANAGE_EXTERNAL_STORAGE allow"

deploy-data:
ifeq ($(DXX_GAME),d2x)
	$(DOCKER) "docker run --rm --privileged \
		-v /dev/bus/usb:/dev/bus/usb \
		-v $(ADB_KEYS):/root/.android \
		-v $(GAME_DATA):/build/game-data \
		$(BUILDER_IMAGE) \
		bash -c 'adb push /build/game-data/DESCENT2.HOG /sdcard/dxx-redux/data/ && \
			adb push /build/game-data/DESCENT2.HAM /sdcard/dxx-redux/data/ && \
			adb push /build/game-data/DESCENT2.S11 /sdcard/dxx-redux/data/ && \
			adb push /build/game-data/DESCENT2.S22 /sdcard/dxx-redux/data/ && \
			for f in /build/game-data/*.PIG; do adb push \"\$$f\" /sdcard/dxx-redux/data/; done'"
else
	$(DOCKER) "docker run --rm --privileged \
		-v /dev/bus/usb:/dev/bus/usb \
		-v $(ADB_KEYS):/root/.android \
		-v $(GAME_DATA):/build/game-data \
		$(BUILDER_IMAGE) \
		bash -c 'adb push /build/game-data/descent.hog /sdcard/dxx-redux/data/ && \
			adb push /build/game-data/descent.pig /sdcard/dxx-redux/data/'"
endif

# ==== Cleanup ====

clean:
	-$(DOCKER) "docker rm -f $(CONTAINER)" 2>/dev/null

clean-all:
	-$(DOCKER) "docker rm -f dxx-redux-builder-d1x" 2>/dev/null
	-$(DOCKER) "docker rm -f dxx-redux-builder-d2x" 2>/dev/null
