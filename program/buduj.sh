#!/bin/bash
# Budowa programu na Linuksie. Wynik: ../bin/centrum
set -e
D="$(cd "$(dirname "$0")" && pwd)"
B="$D/build"
[ -f "$B/build.ninja" ] || nice -n 15 cmake -S "$D" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release
exec gpu-zamek --budowa -n "CENTRUM: budowa programu" -- nice -n 15 cmake --build "$B" -j 8 "$@"
