#!/bin/sh
set -eu
library=${1:?xxtea-iot-crypt src required}
framework=${2:?ESPHome source root required}
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
g++ -std=c++17 -g -O1 -DUSE_ESP32 -fsanitize=address,undefined -fno-omit-frame-pointer \
 -I"$repo/tests/host" -I"$repo/tests/stubs" -I"$repo/components/danfoss_eco" -I"$library" -I"$framework" \
 "$repo/tests/control.cpp" "$repo/components/danfoss_eco/device.cpp" \
 "$repo/components/danfoss_eco/properties.cpp" "$repo/components/danfoss_eco/helpers.cpp" \
 "$repo/components/danfoss_eco/xxtea.cpp" "$library/xxtea_core.c" -o "$build/control"
result=0
for test in confirmed-mode short-settings one-request pending-target transient-pin request-deadline rejected-request verify-mode latest-target poll-coalescing disconnect-recovery newer-request-survives-retry bounded-retries unexpected-response mismatched-readback vacation-is-not-schedule poll-does-not-reset-retries discovery-deadline late-pin-response failed-poll-does-not-lose-control control-during-normal-close pairing-precedes-control; do
 "$build/control" "$test" || result=1
done
exit "$result"
