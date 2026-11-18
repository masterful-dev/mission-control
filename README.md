# Mission Control

Program do podglądu sprzętu (karty graficzne, procesor, pamięć, dyski, energia) i uruchamiania modeli AI, z animowaną twarzą idącą za pracą modelu. Linux i Windows.

| Katalog | Zawartość |
|---|---|
| `program/` | okno: C++20, SDL3, OpenGL 3.3, Dear ImGui (`program/README.md`) |
| `serwer/` | usługa danych w Pythonie 3.12 (`centrum/`), jej biblioteki (`biblioteki-py312/`, lista w `requirements.txt`) |
| `instalator/` | pakiety instalacyjne, ikona, warunki korzystania (`instalator/README.md`) |
| `PLAN-WLASNE-SKLADNIKI.md` | jak zastąpić cudze składniki własnymi |

Budowa: `program/buduj.sh` (Linux), `program/buduj-windows.sh` (Windows z Linuksa), `instalator/linux/zbuduj.sh`, `instalator/windows/zbuduj-instalator.sh`.
Kompilator dla Windows (llvm-mingw, około 1 GB) nie jest w repozytorium: rozpakuj wydanie llvm-mingw dla Linuksa x86_64 do `program/zewnetrzne/llvm-mingw/`.

Warunki korzystania: `LICENSE.txt` (English), `LICENCJA.txt` (polski).
