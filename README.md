# mc-android

Minecraft Java 1.12.2 with OptiFine, compiled ahead of time to a native Android app with GraalVM native-image. No JVM on the phone. The game runs on OpenGL ES through gl4es, with a GLFW-style window layer over EGL.

Private, for personal use. You need your own copy of the game: no Minecraft code or assets are in this repo or in the APK it builds, except the font tables in app/mcfont.h (generated from the game's own font).

Features:

- on-screen controls, with a layout you can move around and save
- a Minecraft-styled on-screen keyboard
- game controller support
- Microsoft account sign-in, or offline play
- render resolution setting, as a percentage of the screen
- pauses when the app goes to the background
- immersive full screen, either landscape orientation

Requirements: an arm64 phone, GraalVM 25 CE, Android NDK 26, adb. The jar and configs come from the Switch port in ~/mc-switch.

Build, in order:

    ./build-natives.sh      LWJGL natives for Android
    ./build-openal.sh       OpenAL Soft
    ./patch-jar.sh          game jar with the Android patches
    ./build-image.sh        native image (build/mc.o)
    ./link-apk.sh           libmc.so and out/mc.apk
    adb install -r out/mc.apk
    ./push-data.sh          assets and settings to the phone

build-image.sh measures C struct layouts on the phone (via ndk-cap-cc), so keep the phone connected the first time. Microsoft sign-in needs an Azure client ID in a file named msa-client-id (git-ignored). Without it, sign-in is off and you play offline.

Layout:

    app/        activity (Java), glue.c (window, input, EGL), touch.h (controls, keyboard), bionic_compat.c
    tools/      jar patchers, font generator
    cap/        native-image C struct layout cache for Android
    opts/       game settings pushed to the phone
