# Centrum Misji V2 (program C++)

Wersja V2: przebudowa od 11.11.2026. Wersja V1 leży bez zmian w `/mnt/RUNE/PROJEKTY/CENTRUM-MISJI/program`. Obie mogą działać naraz: V2 ma własne ustawienia (`~/.local/share/Masterful/centrum-misji-v2/ustawienia.json`) i własną blokadę jednej kopii.

Jeden program na Linuksa i Windows: C++20, SDL3, OpenGL 3.3, Dear ImGui. Wszystkie biblioteki wchodzą do pliku wykonywalnego.

## Budowa

Linux, wynik `../bin/centrum`:

    ./buduj.sh

Windows z Linuksa, wynik `../bin/windows/centrum.exe`:

    ./buduj-windows.sh

Obok `centrum.exe` trzeba położyć katalog `dane/`.

## Uruchomienie

| Argument | Działanie |
|---|---|
| bez argumentów | prawdziwe dane z `http://127.0.0.1:8770`, gdy serwer nie odpowiada: symulacja z paskiem sterowania |
| `--sim` | zawsze symulacja |
| `--adres URL` | inny serwer Centrum |
| `--dok` | tryb modułu DSH++: okno bez ramki, wiersz `DOK okno=<numer>` na wyjściu |
| `--rozmiar W H` | rozmiar okna |
| `--test plik` | scenariusz testowy z `narzedzia/testy/` |

Skróty: `F` sama scena, `D` pasek symulacji, `Esc` powrót, `Ctrl` + kółko rozmiar interfejsu.

## Układ plików

| Plik | Zawartość |
|---|---|
| `zrodla/glowny.cpp` | okno, pętla, dane z API lub symulacji, zasobnik, sterowanie testowe |
| `zrodla/okno.cpp` | górny pasek, układ paneli (domyślny i własny: przesuwanie, rozciąganie, przyciąganie, zapis), scena, tryb wąski |
| `zrodla/panele.cpp` | panele GPU, procesor, RAM, dyski, model, lista modeli, energia; chmurki każdego elementu |
| `zrodla/menu.cpp` | menu prawego przycisku, osobne dla każdego rodzaju elementu |
| `zrodla/dialogi.cpp` | Ustawienia, właściwości modelu, formularz modelu, import, raport, potwierdzenia |
| `zrodla/aplikacja.cpp` | ustawienia, skórki, akcje na modelach, silniki modeli, energia z 30 dni |
| `zrodla/dyski.cpp` | dyski tego komputera wykrywane z systemu (Linux i Windows): nazwa, opis, zajętość, temperatura |
| `zrodla/scena.cpp` | tło okna i żywa podstawa sceny (jeden shader na cały ekran) |
| `zrodla/api.cpp` | klient `/api/state` i zleceń |
| `zrodla/sim.cpp` | symulacja |
| `zrodla/ui.cpp` | czcionki, przyciski, kolory, chmurki, rysunki kart |
| `narzedzia/eksport_makiety.mjs` | teksty, skórki i modele z makiety do `zrodla/gen_dane.inc` |
| `narzedzia/testy/` | scenariusze, atrapa serwera `atrapa.py` |

## Co zmienia V2

- Jedna lista modeli zamiast paneli TP1 i TP2: rysunek jednej albo dwóch kart przy modelu, kolor paska (1 karta, 2 karty), filtr, sortowanie po kliknięciu nagłówka kolumny.
- Silnik modelu jako osobna cecha: wykrywany z komendy startu, pokazany znaczkiem, zmieniany w oknie „Właściwości” (prawy przycisk na modelu). Nazwa na liście bez silnika i bez TP1/TP2.
- Każdy panel można przesunąć za tytuł i rozciągnąć za krawędź albo róg. Układ zapisuje się od razu jako ułamki okna.
- Dyski wykrywa program, nie konfiguracja serwera. Dwuklik otwiera katalog.
- Energia: chmurka nad każdym słupkiem, widoki 48 godzin, 30 dni, obecny miesiąc.
- Chmurka nad każdym elementem, menu prawego przycisku zależne od miejsca.
- Twarz: hologram z oryginalnego Centrum Misji, zawsze wyśrodkowana w wolnym polu, bez własnego tła i bez okręgów; pod nią żywa podstawa.

Wersja finalna V2: 2.1.0 z 11.11.2026 (znacznik git `v2-final`). Od tej wersji autostart systemu (`~/.config/autostart/centrum-misji.desktop`) i skrót `CENTRUM MISJI V2` na pulpicie uruchamiają `bin/centrum` z tego katalogu.

## Testy

    SDL_VIDEO_DRIVER=offscreen ../bin/centrum --sim --test narzedzia/testy/v2-sim.txt --rozmiar 1680 962

Scenariusz steruje myszą i rozmiarem okna, zrzuty trafiają do `narzedzia/testy/zrzuty/`. Polecenie `czas N` czeka N sekund (sterownik `offscreen` nie ma limitu klatek). Scenariusze z kliknięciami uruchamia się tylko z `--sim`: na prawdziwych danych kliknięcie w menu modelu mogłoby go uruchomić. Przycisków start i stop nie testuje się na prawdziwym serwerze: do tego służy `narzedzia/testy/atrapa.py <port>` z `--adres http://127.0.0.1:<port>`.

## Licencje

| Składnik | Licencja |
|---|---|
| SDL3 | zlib |
| Dear ImGui | MIT |
| FreeType | FTL |
| nlohmann json | MIT |
| cpp-httplib | MIT |
| Inter, JetBrains Mono, Noto Sans Symbols2, Noto Sans Math | SIL OFL 1.1 |
| DejaVu Sans | licencja Bitstream Vera |

## Start i okno

Na Linuksie program zastępuje stare okno: autostart V2 to `~/.config/autostart/centrum-misji-v2.desktop` (przełącznik „Start z systemem”); menu i zasobnik systemu wskazują nadal V1. Przełącznik „Start z systemem” w Ustawieniach czyta i zmienia ten sam plik.

Działa jedna kopia: kolejne uruchomienie pokazuje istniejące okno i kończy się. Blokada nie dotyczy `--dok`, `--test` i `--sim`.

Okno powstaje na ekranie, na którym je ostatnio zamknięto (`ekran` w `ustawienia.json`), a przed pierwszym zamknięciem na ekranie głównym.

Test bez okna na monitorze: `SDL_VIDEO_DRIVER=offscreen ../bin/centrum --test scenariusz.txt --rozmiar 900 620`. Na tym sterowniku nie ma limitu klatek, więc `czekaj` trwa dużo krócej niż na monitorze.
