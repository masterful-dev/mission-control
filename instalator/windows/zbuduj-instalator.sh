#!/bin/bash
# Instalator Mission Control dla Windows (jeden plik .exe, bez uprawnien administratora).
# Wynik: wydanie/CentrumMisji-<wersja>-instalator.exe. Uzycie: instalator/windows/zbuduj-instalator.sh
set -euo pipefail
K="$(cd "$(dirname "$0")/../.." && pwd)"; T="$K/instalator/windows"; R="$T/robocze"; LM="$K/program/zewnetrzne/llvm-mingw/bin"
WERSJA=$(sed -n 's/^project(centrum VERSION \([0-9.]*\).*/\1/p' "$K/program/CMakeLists.txt")
(cd "$K/program" && ./buduj-windows.sh >/dev/null)
rm -rf "$R"; mkdir -p "$R/pliki" "$K/wydanie"
cp "$K/bin/windows/centrum.exe" "$R/pliki/MissionControl.exe"; "$LM/x86_64-w64-mingw32-strip" "$R/pliki/MissionControl.exe"; cp "$K/instalator/LICENCJA.txt" "$K/instalator/LICENSE.txt" "$K/instalator/THIRD-PARTY-NOTICES.txt" "$R/pliki/"; cp -r "$K/program/dane" "$R/pliki/dane"
FL="-O2 -municode -mwindows -static -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00"; LIBY="-lole32 -lshell32 -luuid -ladvapi32 -luser32"
printf '1 ICON "%s"\n' "$K/instalator/ikona/mission-control.ico" > "$R/od.rc"; "$LM/x86_64-w64-mingw32-windres" "$R/od.rc" -O coff -o "$R/od.res"
"$LM/x86_64-w64-mingw32-clang++" $FL -DODINSTALUJ "$T/instalator.cpp" "$R/od.res" -o "$R/pliki/odinstaluj.exe" $LIBY
python3 - "$R/pliki" "$R/pliki.bin" <<'PY'
import os, struct, sys
kat, wyj = sys.argv[1], sys.argv[2]; pliki = []
for d, _, fs in os.walk(kat):
    for f in fs: pliki.append(os.path.relpath(os.path.join(d, f), kat).replace(os.sep, '/'))
pliki.sort(key=lambda p: (-p.count('/'), p))          # najglebsze najpierw: tak odinstalowanie zwija katalogi
with open(wyj, 'wb') as o:
    o.write(struct.pack('<I', len(pliki)))
    for p in pliki:
        b = p.encode('utf-8'); dane = open(os.path.join(kat, p), 'rb').read()
        o.write(struct.pack('<I', len(b))); o.write(b); o.write(struct.pack('<I', len(dane))); o.write(dane)
print(len(pliki), 'plikow')
PY
printf '1 ICON "%s"\n100 RCDATA "%s"\n' "$K/instalator/ikona/mission-control.ico" "$R/pliki.bin" > "$R/inst.rc"; "$LM/x86_64-w64-mingw32-windres" "$R/inst.rc" -O coff -o "$R/inst.res"
W="$K/wydanie/MissionControl-$WERSJA-instalator.exe"
"$LM/x86_64-w64-mingw32-clang++" $FL "$T/instalator.cpp" "$R/inst.res" -o "$W" $LIBY
ls -la "$W"; file "$W" | cut -c1-140
