#!/bin/sh
set -eu
# Arguments: real xxtea-iot-crypt 2.0.1 src and the ESPHome Python source root.
library=${1:?Usage: tests/run-encryption.sh /path/to/xxtea-iot-crypt/src /path/to/python/site-packages}
framework=${2:?Second argument: ESPHome source root containing the esphome directory}
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
g++ -std=c++17 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$repo/tests/stubs" -I"$repo/components/danfoss_eco" -I"$library" -I"$framework" \
    "$repo/tests/encryption.cpp" "$repo/components/danfoss_eco/helpers.cpp" \
    "$repo/components/danfoss_eco/xxtea.cpp" "$library/xxtea_core.c" \
    -o "$build/encryption"
"$build/encryption"
