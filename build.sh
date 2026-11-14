#!/usr/bin/env bash
# Build MANTRA for MPC OS Gen1 / Force (armv7-a hard-float, glibc <= 2.31 so it also loads on MPC OS 2.x).
#   ./build.sh                     (MPC_VST=/path/to/mpc-vst-plugins to use another framework checkout)
# Needs: python3 + Pillow, a host gcc (for shadow_art) and Zig (pip install ziglang). No Docker.
# Output: build/package/L'Cronx - VST - MANTRA/  (skin + mantra.so) and build/pluginlist-entry.xml
set -euo pipefail
cd "$(dirname "$0")"
MPC_VST="${MPC_VST:-$PWD/third_party/mpc-vst-plugins}"
ZIG="${ZIG:-python3 -m ziglang}"
NAME="L'Cronx - VST - MANTRA"
python3 tools/gen_force.py
python3 tools/make_tile.py >/dev/null
mkdir -p build/host build/arm
rm -rf build/skin
# the skin is drawn by the browser renderer (vst.json "art": "html"; headless Chromium via tools/html_art.py: Playwright + chromium).
# GEN_VST_RELOCK=1 only for the first build after a deliberate parameter-order change (0.2.0 was one).
SHADOW_TITLE_FONT="$PWD/art/fonts/TitilliumWeb-SemiBold.ttf" SHADOW_ART="$MPC_VST/tools/html_art.py" python3 "$MPC_VST/tools/gen_vst.py" vst.json
TGT="-target arm-linux-gnueabihf.2.31 -mcpu=generic+v7a+vfp3d16-d32-neon+thumb2 -ffp-contract=off"
COMMON="-O2 -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections -DNDEBUG -w"
$ZIG c++ $TGT $COMMON -std=gnu++17 -fno-exceptions -fno-rtti -Isrc -Ibuild -I"$MPC_VST/wrapper" -c src/engine.cc -o build/arm/engine.o
$ZIG c++ $TGT $COMMON -std=gnu++17 -fno-exceptions -fno-rtti -Isrc -Ibuild -c src/step_touch.cc -o build/arm/step_touch.o
$ZIG cc $TGT $COMMON -std=gnu11 -Ibuild -c "$MPC_VST/wrapper/vst2_wrap.c" -o build/arm/vst2_wrap.o
printf '{\n  global: VSTPluginMain;\n  local: *;\n};\n' > build/arm/exports.map
$ZIG c++ $TGT -shared -fPIC -Wl,--no-undefined -Wl,--version-script=build/arm/exports.map -Wl,--gc-sections -Wl,-s \
  build/arm/engine.o build/arm/step_touch.o build/arm/vst2_wrap.o -lm -ldl -lpthread -o build/arm/mantra.so
PKG="build/package/$NAME"
rm -rf build/package && mkdir -p "$PKG"
cp -r "build/skin/$NAME/." "$PKG/"
cp build/arm/mantra.so "$PKG/"
echo "-> $PKG"
