# Mission Control install packages

Program icon: `ikona/mission-control-1024.png` (from it the 16–512 sizes and the `.ico` file are built). It is in the packages (menu, taskbar), in the program window and in the `.exe` file.

| Package | For | Build |
|---|---|---|
| `wydanie/mission-control_<wersja>_amd64.deb` | Debian, Ubuntu 24.04, Linux Mint 22 (apt, dpkg, graphical package installer) | `instalator/linux/zbuduj.sh` |
| `wydanie/mission-control-<wersja>-linux-x86_64.run` | any x86_64 Linux, user install without administrator password | `instalator/linux/zbuduj.sh` |
| `wydanie/MissionControl-<wersja>-setup.exe` | Windows 10 and 11, no administrator rights needed | `instalator/windows/zbuduj-instalator.sh` |

## Installation

    sudo apt install ./mission-control_2.1.1_amd64.deb          # removal: sudo apt remove mission-control
    sh mission-control-2.1.1-linux-x86_64.run                   # removal: ~/.local/opt/mission-control/uninstall.sh

| | `.deb` | `.run` |
|---|---|---|
| program | `/opt/mission-control` | `~/.local/opt/mission-control` |
| command | `/usr/bin/mission-control` | `~/.local/bin/mission-control` |
| menu and icons | `/usr/share/applications`, `/usr/share/icons/hicolor` | `~/.local/share/applications`, `~/.local/share/icons/hicolor` |
| data service | `mission-control-serwer.service` (user service) | same name in `~/.config/systemd/user`; without systemd the start command launches it itself |
| user data | `~/.local/share/mission-control/` | the same |

The data service needs Python 3.12 (the libraries in `serwer/biblioteki-py312` are built for this version). Without it the window works in simulation mode. When port 8770 is already taken by a running service, the second one does not start.

## Terms of use

`LICENCJA.txt` (Polish) and `LICENSE.txt` (English) go into the program folder together with `THIRD-PARTY-NOTICES.txt` (notes and full licences of third-party components; generated from sources).
- `.run` and the Windows installer show the terms and do not install without acceptance.
- `.deb` prints the terms during installation; the package does not block installation with a question, because graphical package installers cannot answer it.
- On first run the program shows the terms and closes itself without acceptance.

## Trials (19.11.2026)

- `.deb`: installation and removal with a real `dpkg` into a separate root folder with a Linux Mint 22.3 package database: dependencies satisfied, status `install ok installed`, removal without leftovers. No installation in the system itself (no administrator password).
- `.run`: refusal (nothing installed), installation into a separate home folder, start of the service and the window from installed files, uninstall.
- Windows: installer contents checked file by file; not run (no Windows and Wine).
