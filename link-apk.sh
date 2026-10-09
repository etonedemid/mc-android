#!/bin/bash
# link-apk.sh - libmc.so (native image + glue + JDK static libraries) and the APK around it: mc.apk, signed with a local
# debug key. Inputs: build/mc.o (build-image.sh), natives/ (build-natives.sh, build-openal.sh), gl4es.
set -e
cd "$(dirname "$0")"
SDK=~/Android/Sdk; NDK=$SDK/ndk/26.3.11579264/toolchains/llvm/prebuilt/linux-x86_64/bin; BT=$SDK/build-tools/34.0.0
CC=$NDK/aarch64-linux-android31-clang
S=~/graalvm-25/x64/lib/static/linux-aarch64/glibc; C=~/graalvm-25/x64/lib/svm/clibraries/linux-aarch64/glibc
mkdir -p out/apk/lib/arm64-v8a
$NDK/llvm-objcopy --redefine-sym main=mc_main build/mc.o out/mc.o
JDK=""; for l in java nio net zip prefs management_ext awt awt_headless mlib_image javajpeg lcms fdlibm extnet; do [ -f $S/lib$l.a ] && JDK="$JDK $S/lib$l.a"; done
$CC -shared -fPIC -O2 -o out/apk/lib/arm64-v8a/libmc.so out/mc.o app/glue.c app/bionic_compat.c \
  -Wl,--whole-archive $JDK -Wl,--no-whole-archive $C/liblibchelper.a $C/libjvm.a $C/libsvm_container.a \
  -Wl,--wrap=dlopen,--wrap=getaddrinfo,--wrap=freeaddrinfo -Wl,--no-undefined -lz -lm -ldl -llog -landroid -lEGL > out/link.log 2>&1 \
  || { grep -vE '^>>>' out/link.log | sed 's/.*error: //' | sort -u | head -40; exit 1; }
cp natives/*.so out/apk/lib/arm64-v8a/
cp deps/gl4es/lib/libGL.so.1 out/apk/lib/arm64-v8a/libGL.so
$NDK/llvm-strip --strip-debug out/apk/lib/arm64-v8a/*.so
[ -f debug.keystore ] || keytool -genkeypair -keystore debug.keystore -storepass android -keypass android -alias debug \
  -keyalg RSA -validity 10000 -dname "CN=etonedemid" > /dev/null 2>&1
rm -f out/mc.unaligned.apk out/mc.apk
# the activity (sign-in dialog). MSA_CLIENT_ID: an Azure application ID with public client flows on and Minecraft API
# access approved by Mojang; without one the dialog only offers offline play.
# (or in the git-ignored file msa-client-id)
[ -z "$MSA_CLIENT_ID" ] && [ -f msa-client-id ] && MSA_CLIENT_ID=$(cat msa-client-id)
J=$SDK/platforms/android-34/android.jar
rm -rf out/java out/dex && mkdir -p out/java/com/etonedemid/mc out/dex
printf 'package com.etonedemid.mc;\nfinal class Config { static final String MSA_CLIENT_ID = "%s"; }\n' "${MSA_CLIENT_ID:-}" > out/java/com/etonedemid/mc/Config.java
javac -source 8 -target 8 -Xlint:-options -cp $J -d out/java/classes app/java/com/etonedemid/mc/*.java out/java/com/etonedemid/mc/Config.java
$BT/d8 --release --min-api 31 --lib $J --output out/dex $(find out/java/classes -name '*.class')
rm -rf out/res && mkdir -p out/res && $BT/aapt2 compile --dir app/res -o out/res/
$BT/aapt2 link -o out/mc.unaligned.apk --manifest app/AndroidManifest.xml -I $SDK/platforms/android-34/android.jar out/res/*.flat
(cd out/apk && zip -qr -0 ../mc.unaligned.apk lib)
(cd out/dex && zip -q ../mc.unaligned.apk classes.dex)   # stored, so the libraries can be mapped straight from the APK
$BT/zipalign -p -f 4 out/mc.unaligned.apk out/mc.apk
$BT/apksigner sign --ks debug.keystore --ks-pass pass:android out/mc.apk
ls -la out/mc.apk
