# Pakiety instalacyjne Mission Control

Ikona programu: `ikona/mission-control-1024.png` (z niej powstają rozmiary 16–512 i plik `.ico`).

## Linux (.deb, amd64)

    instalator/linux/zbuduj-deb.sh        # wynik: wydanie/mission-control_<wersja>_amd64.deb
    sudo apt install ./wydanie/mission-control_2.1.0_amd64.deb

| Co | Gdzie po instalacji |
|---|---|
| okno (program C++) | `/opt/mission-control/bin/mission-control`, dane w `/opt/mission-control/dane` |
| usługa danych (Python) z bibliotekami | `/opt/mission-control/serwer`, usługa użytkownika `mission-control-serwer.service` |
| polecenie | `mission-control` (pilnuje usługi i otwiera okno) |
| menu i ikona | `/usr/share/applications/mission-control.desktop`, `/usr/share/icons/hicolor/*/apps/mission-control.png` |
| dane użytkownika (modele, energia, taryfa) | `~/.local/share/mission-control/` |
| ustawienia okna | `~/.local/share/Masterful/centrum-misji-v2/ustawienia.json` |

Wymaga Pythona 3.12 (Ubuntu 24.04, Linux Mint 22): biblioteki usługi są dołączone w wersjach, na których działa. Gdy port 8770 jest już zajęty przez działające Centrum, usługa z pakietu się nie uruchamia i okno korzysta z tamtej.
Źródła usługi skrypt bierze z `/mnt/RUNE/PROJEKTY/CENTRUM-MISJI/centrum` i w kopii przenosi dane użytkownika do `CENTRUM_DANE`.

Podczas instalacji oba pakiety pokazują warunki korzystania (`LICENCJA.txt`) i wymagają zgody; bez niej instalacja się nie wykona.
