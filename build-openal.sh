#!/bin/bash
# build-openal.sh - openal-soft 1.18.2 (the last C release) for Android: natives/libopenal.so on the OpenSL ES backend
set -e
cd "$(dirname "$0")"
S=~/mc-switch/deps/openal-soft-1.18.2; B=$PWD/deps/openal-build; NDK=~/Android/Sdk/ndk/26.3.11579264
rm -rf $B && cmake -S $S -B $B -G Ninja -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-31 -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DLIBTYPE=SHARED \
  -DALSOFT_UTILS=OFF -DALSOFT_EXAMPLES=OFF -DALSOFT_TESTS=OFF -DALSOFT_CONFIG=OFF -DALSOFT_HRTF_DEFS=OFF -DALSOFT_AMBDEC_PRESETS=OFF \
  -DALSOFT_NO_CONFIG_UTIL=ON -DALSOFT_REQUIRE_OPENSL=ON -DALSOFT_BACKEND_WAVE=OFF > $B.log 2>&1 || { tail -20 $B.log; exit 1; }
ninja -C $B OpenAL 2>&1 | grep -E "error|FAILED" | head || true
cp $B/libopenal.so natives/ && ls -la natives/libopenal.so
