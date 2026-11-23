<p align="left"><img src="assets/icon.png" width="96" alt="Mission Control icon"/></p>

# Mission Control

A desktop window that watches your hardware and runs your local models.
Your graphics cards, processor, RAM, disks and power draw in one place,
the model list with contexts and ports, and the current power draw
with cost.

It is written in C++20 (SDL3, OpenGL 3.3, Dear ImGui) with a Python data service.
One executable, no extra runtime to install for the window itself.

## What it does

- Hardware at a glance: load, memory and temperature of every GPU, plus CPU, RAM and disks.
- Models in one list: each model shows its GPUs, context size and port. Start and stop with one click.
- Energy: current power draw, the last 48 hours, the last 30 days and the current month, with tariff zones.
- An animated face that shows whether the model is reading, thinking or answering.
- Layout: drag panels by their title, stretch them by their edge. Saved on the spot.
- English and Polish interface.
- Simulation mode, so the window still works before the data service is connected.

## Editions

| Edition | Package | For | Status |
|---|---|---|---|
| Linux | `.deb` | Debian, Ubuntu 24.04, Linux Mint 22 | ready |
| Linux | `.run` | any x86_64 Linux, no administrator rights needed | ready |
| Windows | `setup.exe` | Windows 10 and 11, no administrator rights needed | preview |

## Install

**Debian, Ubuntu, Linux Mint**

    sudo apt install ./mission-control_2.1.1_amd64.deb

    sudo apt remove mission-control

**Any x86_64 Linux, no administrator rights**

    sh mission-control-2.1.1-linux-x86_64.run

Installs to `~/.local/opt/mission-control`. Remove with `~/.local/opt/mission-control/uninstall.sh`.

**Windows 10 and 11**

Run `MissionControl-2.1.1-setup.exe`. Installs for the current user, adds shortcuts to the Start menu and the desktop.
Remove from *Installed apps* in Windows settings.

The `.run` and Windows installers ask you to accept the terms of use before installing.
Your data (model list, settings, energy history) stays in your user profile when the program is removed.

## Requirements

- 64-bit (x86_64) system with OpenGL 3.3.
- Python 3.12 for the background data service. Without it the window runs in simulation mode.
- The data service listens on `127.0.0.1` only.

## Terms of use

Mission Control is provided "as is". The source code is not published.
Modifying the program or reusing its code requires the author's written consent.
Full terms: [LICENSE.txt](LICENSE.txt) (Polish: [LICENCJA.txt](LICENCJA.txt)).
Bundled third-party components keep their own licences: [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt).

## Repository layout

| Katalog | Zawartość |
|---|---|
| `program/` | okno: C++20, SDL3, OpenGL 3.3, Dear ImGui (`program/README.md`) |
| `serwer/` | usługa danych w Pythonie 3.12 (`centrum/`), jej biblioteki (`biblioteki-py312/`) |
| `instalator/` | pakiety instalacyjne, ikona, warunki korzystania (`instalator/README.md`) |
| `PLAN-WLASNE-SKLADNIKI.md` | jak zastąpić cudze składniki własnymi |

Build: `program/buduj.sh` (Linux), `program/buduj-windows.sh` (Windows from Linux),
`instalator/linux/zbuduj.sh`, `instalator/windows/zbuduj-instalator.sh`.
The Windows compiler (llvm-mingw, about 1 GB) is not in the repository:
unpack the llvm-mingw Linux x86_64 release into `program/zewnetrzne/llvm-mingw/`.
