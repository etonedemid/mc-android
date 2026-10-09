#!/bin/bash
# patch-jar.sh - mc-android.jar: the Switch build's Minecraft + OptiFine jar (~/mc-switch/mc-optifine.jar, with its
# resource: URL patches) plus Android touches. Title screen (blr): the bottom-right line names the port's author.
set -e
cd "$(dirname "$0")"; M=~/mc-switch
rm -rf build/patch && mkdir -p build/patch && cp $M/mc-optifine.jar mc-android.jar
(cd build/patch && unzip -q ../../mc-android.jar blr.class)
CP=$PWD/mc-android.jar:$M/lwjgl3/pojav-noglfw.jar:$(find $M/game/libraries -name '*.jar' | tr '\n' ':')
~/graalvm-25/x64/bin/java $M/tools/LdcPatch.java build/patch/blr.class "Copyright Mojang AB. Do not distribute!" "etonedemid" "$CP"
(cd build/patch && zip -q ../../mc-android.jar blr.class)
