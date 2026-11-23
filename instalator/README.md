# Pakiety instalacyjne Mission Control

Ikona programu: `ikona/mission-control-1024.png` (z niej powstają rozmiary 16–512 i plik `.ico`). Jest w pakietach (menu, pasek zadań), w oknie programu i w pliku `.exe`.

| Pakiet | Dla kogo | Budowa |
|---|---|---|
| `wydanie/mission-control_<wersja>_amd64.deb` | Debian, Ubuntu 24.04, Linux Mint 22 (apt, dpkg, graficzny instalator pakietów) | `instalator/linux/zbuduj.sh` |
| `wydanie/mission-control-<wersja>-linux-x86_64.run` | każdy Linux x86_64, instalacja dla użytkownika bez hasła administratora | `instalator/linux/zbuduj.sh` |
| `wydanie/MissionControl-<wersja>-setup.exe` | Windows 10 i 11, bez uprawnień administratora | `instalator/windows/zbuduj-instalator.sh` |

## Instalacja

    sudo apt install ./mission-control_2.1.1_amd64.deb          # usuwanie: sudo apt remove mission-control
    sh mission-control-2.1.1-linux-x86_64.run                   # usuwanie: ~/.local/opt/mission-control/uninstall.sh

| Co | `.deb` | `.run` |
|---|---|---|
| program | `/opt/mission-control` | `~/.local/opt/mission-control` |
| polecenie | `/usr/bin/mission-control` | `~/.local/bin/mission-control` |
| menu i ikony | `/usr/share/applications`, `/usr/share/icons/hicolor` | `~/.local/share/applications`, `~/.local/share/icons/hicolor` |
| usługa danych | `mission-control-serwer.service` (usługa użytkownika) | ta sama nazwa w `~/.config/systemd/user`; bez systemd polecenie startowe uruchamia ją samo |
| dane użytkownika | `~/.local/share/mission-control/` | to samo |

Usługa danych wymaga Pythona 3.12 (biblioteki w `serwer/biblioteki-py312` są zbudowane dla tej wersji). Bez niego okno działa w trybie symulacji. Gdy port 8770 jest zajęty przez działającą już usługę, druga się nie uruchamia.

## Warunki korzystania

`LICENCJA.txt` (polski) i `LICENSE.txt` (angielski) trafiają do katalogu programu razem z `THIRD-PARTY-NOTICES.txt` (noty i pełne licencje cudzych składników; generowane ze źródeł).
- `.run` i instalator Windows pokazują warunki i bez zgody nie instalują.
- `.deb` wypisuje warunki podczas instalacji; pakiet nie blokuje instalacji pytaniem, bo graficzne instalatory pakietów nie umieją na nie odpowiedzieć.
- Program przy pierwszym uruchomieniu pokazuje warunki i bez akceptacji się zamyka.

## Próby (19.11.2026)

- `.deb`: instalacja i usunięcie prawdziwym `dpkg` do osobnego katalogu-korzenia z bazą pakietów Linux Mint 22.3: zależności spełnione, stan `install ok installed`, usunięcie bez resztek. Instalacji w samym systemie nie było (brak hasła administratora).
- `.run`: odmowa (nic nie instaluje), instalacja w osobnym katalogu domowym, start usługi i okna z zainstalowanych plików, odinstalowanie.
- Windows: zawartość instalatora sprawdzona plik po pliku; nieuruchamiany (brak Windows i Wine).
