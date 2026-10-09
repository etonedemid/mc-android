#!/bin/bash
# build-image.sh [flags...] - Minecraft 1.12.2 + OptiFine (jar and configs from ~/mc-switch) as an android-aarch64 native
# image: relocatable object build/mc.o for link-apk.sh. The CAP cache (bionic struct layouts and constants) is measured
# on the phone through ndk-cap-cc; pass CAP=use once cap/ is complete to skip that.
cd "$(dirname "$0")"; M=~/mc-switch
CP=$M/mc-optifine.jar:$M/lwjgl3/pojav-noglfw.jar:$M/lwjgl3/lwjgl-glfw-3.3.3.jar:$(find $M/game/libraries -name '*.jar' | sort | tr '\n' ':')
if [ "$CAP" = use ]; then CAPF="-H:+UseCAPCache"; else CAPF="-H:-UseCAPCache -H:+NewCAPCache"; mkdir -p cap; fi
mkdir -p build && cd build && rm -rf tmp mc.o
~/graalvm-25/x64/bin/native-image -cp "$CP" \
  -H:ConfigurationFileDirectories=$M/config,$M/config-gen --no-fallback -J-Xmx24g \
  --enable-native-access=ALL-UNNAMED --enable-url-protocols=http,https -H:+ReportExceptionStackTraces -H:+AddAllCharsets \
  "$@" --target=android-aarch64 -march=compatibility -H:+UnlockExperimentalVMOptions -H:-ForeignAPISupport \
  $CAPF -H:CAPCacheDir=$OLDPWD/cap -H:CCompilerPath=$OLDPWD/ndk-cap-cc \
  -H:+ExitAfterRelocatableImageWrite -H:TempDirectory=$PWD/tmp -o mc net.minecraft.client.main.Main
cp tmp/SVM-*/mc.o mc.o && ls -la mc.o
