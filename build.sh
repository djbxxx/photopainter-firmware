#!/bin/sh
# Build the firmware. Output: build/photopainter.uf2
# Uses $PICO_SDK_PATH / $PICO_TOOLCHAIN_PATH if set, else ../sdk/pico-sdk and ../toolchain/arm-gnu-toolchain-*.
set -e
cd "$(dirname "$0")"
ROOT="$(cd .. && pwd)"
: "${PICO_SDK_PATH:=$ROOT/sdk/pico-sdk}"
export PICO_SDK_PATH
if [ -z "$PICO_TOOLCHAIN_PATH" ]; then
    for d in "$ROOT"/toolchain/arm-gnu-toolchain-*; do [ -d "$d" ] && export PICO_TOOLCHAIN_PATH="$d"; done
fi
[ -f build/build.ninja ] || cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 >/dev/null
ninja -C build
