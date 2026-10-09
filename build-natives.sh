#!/bin/bash
# build-natives.sh - LWJGL 3.3.3 natives (the version the Pojav LWJGL2 compat classes are built on) for Android with the
# NDK, one shared library per Linux name as LWJGL loads them: liblwjgl.so (+ libffi), liblwjgl_opengl.so, liblwjgl_stb.so.
set -e
cd "$(dirname "$0")"
L=~/mc-switch/deps/lwjgl3-333/modules/lwjgl; O=$PWD/natives; G=~/graalvm-25/x64; FFI=$PWD/deps/libffi/and
CC=~/Android/Sdk/ndk/26.3.11579264/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android31-clang
CFLAGS="-std=gnu11 -O2 -fPIC -DNDEBUG -DLWJGL_LINUX -DLWJGL_arm64 -D_GNU_SOURCE -I$G/include -I$G/include/linux \
  -I$L/core/src/main/c -I$L/core/src/main/c/linux -w"
mkdir -p $O && rm -rf $O/tmp && mkdir -p $O/tmp
build() {  # build <lib> <extra flags> <files...>
  local lib=$1 fl=$2; shift 2; local objs=()
  mkdir -p $O/tmp/$lib
  for f in "$@"; do o=$O/tmp/$lib/$(basename "$f" .c).o; $CC -c $CFLAGS $fl "$f" -o $o & objs+=($o); done
  wait
  $CC -shared -o $O/$lib.so "${objs[@]}" $EXTRA -ldl -lm -llog
  echo "$lib.so: ${#objs[@]} files"
}
EXTRA="$FFI/.libs/libffi.a" build liblwjgl "-I$L/core/src/main/c/libffi -I$L/core/src/main/c/libffi/aarch64 -I$FFI/include" \
  $L/core/src/main/c/*.c $(ls $L/core/src/generated/c/*.c) $(ls $L/core/src/generated/c/linux/*.c | grep -v liburing)
EXTRA= build liblwjgl_opengl "-I$L/opengl/src/main/c" $(ls $L/opengl/src/generated/c/*.c | grep -vE 'GLX|WGL')
EXTRA= build liblwjgl_stb "-include string.h -I$L/stb/src/main/c -isystem$L/stb/src/main/c" $L/stb/src/generated/c/*.c
