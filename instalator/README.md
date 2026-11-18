# Pakiety instalacyjne Centrum Misji

Ikona programu: `ikona/centrum-misji-1024.png` (z niej powstają rozmiary 16–512 i plik `.ico`).

## Linux (.deb, amd64)

    instalator/linux/zbuduj-deb.sh        # wynik: wydanie/centrum-misji_<wersja>_amd64.deb
    sudo apt install ./wydanie/centrum-misji_2.1.0_amd64.deb

| Co | Gdzie po instalacji |
|---|---|
| okno (program C++) | `/opt/centrum-misji/bin/centrum`, dane w `/opt/centrum-misji/dane` |
| usługa danych (Python) z bibliotekami | `/opt/centrum-misji/serwer`, usługa użytkownika `centrum-misji-serwer.service` |
| polecenie | `centrum-misji` (pilnuje usługi i otwiera okno) |
| menu i ikona | `/usr/share/applications/centrum-misji.desktop`, `/usr/share/icons/hicolor/*/apps/centrum-misji.png` |
| dane użytkownika (modele, energia, taryfa) | `~/.local/share/centrum-misji/` |
| ustawienia okna | `~/.local/share/Masterful/centrum-misji-v2/ustawienia.json` |

Wymaga Pythona 3.12 (Ubuntu 24.04, Linux Mint 22): biblioteki usługi są dołączone w wersjach, na których działa. Gdy port 8770 jest już zajęty przez działające Centrum, usługa z pakietu się nie uruchamia i okno korzysta z tamtej.
Źródła usługi skrypt bierze z `/mnt/RUNE/PROJEKTY/CENTRUM-MISJI/centrum` i w kopii przenosi dane użytkownika do `CENTRUM_DANE`.
