#!/bin/bash
# b.sh [native-image flags...] - android-aarch64 native image of H, NDK link, run on the phone over adb
cd "$(dirname "$0")"; rm -rf tmp
NDK=~/Android/Sdk/ndk/26.3.11579264/toolchains/llvm/prebuilt/linux-x86_64/bin
timeout 500 ~/graalvm-25/x64/bin/native-image --target=android-aarch64 -H:+UnlockExperimentalVMOptions -H:-ForeignAPISupport "$@" \
  -H:+UseCAPCache -H:CAPCacheDir=$PWD/capc -H:CCompilerPath=$NDK/aarch64-linux-android31-clang -H:+ExitAfterRelocatableImageWrite \
  -H:TempDirectory=$PWD/tmp -o h H 2>&1 | grep -iE '^error'
O=$(find tmp -name 'h.o'); S=~/graalvm-25/x64/lib/static/linux-aarch64/glibc; C=~/graalvm-25/x64/lib/svm/clibraries/linux-aarch64/glibc
$NDK/aarch64-linux-android31-clang -O2 $CEXTRA -o h $O bionic_compat.c $C/liblibchelper.a $C/libjvm.a $C/libsvm_container.a \
  $S/libjava.a $S/libnio.a $S/libnet.a $S/libzip.a -lz -ldl -lm -llog $LDEXTRA 2>&1 | grep error
adb push h /data/local/tmp/h >/dev/null && adb shell /data/local/tmp/h; echo "exit=$?"
