#!/bin/bash
# push-data.sh - game assets (from ~/mc-switch/game, see its fetch.py) and settings to the phone, into the app's
# external data dir. Install the APK first. adb on a rooted phone writes as root, so ownership goes back to the app.
# Settings: the Switch tuning with OptiFine Render Regions off (gl4es has no glCopyBufferSubData: terrain vanishes).
set -e
cd "$(dirname "$0")"
PKG=com.etonedemid.mc; D=/sdcard/Android/data/$PKG/files
adb shell mkdir -p $D/minecraft
adb push ~/mc-switch/game/assets $D/ | tail -1
adb push opts/options.txt opts/optionsof.txt $D/minecraft/ | tail -1
U=$(adb shell stat -c %U /sdcard/Android/data/$PKG)
adb shell "chown -R $U:ext_data_rw $D && chmod -R ug+rwX $D" 2>/dev/null || true
echo "pushed to $D (owner $U)"
