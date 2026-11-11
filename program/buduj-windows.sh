#!/bin/bash
# Budowa wersji na Windows z Linuksa. Wynik: ../bin/windows/centrum.exe
set -e
D="$(cd "$(dirname "$0")" && pwd)"
B="$D/build-win"
[ -f "$B/build.ninja" ] || nice -n 15 cmake -S "$D" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$D/narzedzia/windows.cmake" "$@"
gpu-zamek --budowa -n "CENTRUM: budowa Windows" -- nice -n 15 cmake --build "$B" -j 8 --target centrum
rm -rf "$D/../bin/windows/dane"
cp -r "$D/dane" "$D/../bin/windows/dane"
