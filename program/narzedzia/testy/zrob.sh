#!/bin/bash
# Uruchamia program ze scenariuszem testowym i zamienia zrzuty na PNG. Uzycie: zrob.sh scenariusz.txt [argumenty programu]
D="$(cd "$(dirname "$0")/../.." && pwd)"; cd "$D" || exit 1
S="$1"; shift
timeout 120 nice -n 15 ../bin/centrum --test "$S" "$@" 2>&1 | tail -8
python3 - <<'PY'
import glob,os
from PIL import Image
for f in glob.glob('narzedzia/testy/zrzuty/*.ppm'):
    Image.open(f).save(f[:-3]+'png'); os.remove(f)
PY
