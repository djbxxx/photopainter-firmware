#!/bin/sh
# Build the firmware. Output: build/photopainter.uf2
set -e
cd "$(dirname "$0")"
ROOT="$(cd .. && pwd)"
export PICO_SDK_PATH="$ROOT/sdk/pico-sdk"
export PICO_TOOLCHAIN_PATH="$(echo "$ROOT"/toolchain/arm-gnu-toolchain-*)"
[ -f build/build.ninja ] || cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 >/dev/null
ninja -C build
