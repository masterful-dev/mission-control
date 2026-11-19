<p align="left"><img src="assets/icon.png" width="96" alt="Mission Control icon"/></p>

# Mission Control

**One window that shows what your workstation is doing, and lets you decide what it does next.**

Mission Control is a native desktop application for people who run AI models on their own hardware.
It watches the machine (graphics cards, processor, memory, disks, power draw) and starts and stops
your local models from the same window.

I built it because I kept missing it: a hardware monitor that also knows which model is loaded,
on which card, and what it costs to keep it running.

## What it does

- **Hardware at a glance.** Load, memory and temperature of every graphics card, plus processor, RAM and disks.
- **Your models in one list.** Each model shows the cards it needs, its context size and its port. Start or stop it with one click.
- **Energy you can read.** Power draw now, the last 48 hours, the last 30 days and the current month, with tariff zones for cost.
- **A face that follows the work.** An animated face shows whether the model is reading, thinking or answering.
- **A layout that is yours.** Drag any panel by its title, stretch it by its edge, and the layout is saved on the spot.
- **English and Polish interface.**
- **A simulation mode.** Without the data service the window runs on simulated data, so you can look around before connecting anything.

## Editions

Mission Control is one program built from one codebase. It ships in two editions, and both live in this repository:

| Edition | Package | For | Status |
|---|---|---|---|
| **Linux** | `.deb` | Debian, Ubuntu 24.04, Linux Mint 22 | ready |
| **Linux** | `.run` | any x86_64 Linux, installs without administrator rights | ready |
| **Windows** | `setup.exe` | Windows 10 and 11, installs without administrator rights | preview: built and packaged, not yet verified on a Windows machine |

Downloads are published under **Releases**, one release per version, with the Linux and Windows packages side by side.

## Install

**Debian, Ubuntu, Linux Mint**

    sudo apt install ./mission-control_2.1.1_amd64.deb

Remove it with `sudo apt remove mission-control`.

**Any x86_64 Linux, without administrator rights**

    sh mission-control-2.1.1-linux-x86_64.run

The program goes to `~/.local/opt/mission-control` and appears in the applications menu.
Remove it with `~/.local/opt/mission-control/uninstall.sh`.

**Windows 10 and 11**

Run `MissionControl-2.1.1-setup.exe`. It installs for the current user and adds shortcuts to the Start menu and the desktop.
Remove it from *Installed apps* in Windows settings.

The `.run` and Windows installers ask you to accept the terms of use before they install anything; the program itself asks again on first start.
Your data (model list, settings, energy history) stays in your user profile when the program is removed.
Each release comes with `SHA256SUMS.txt`, so you can check a download with `sha256sum -c SHA256SUMS.txt`.

## Requirements

- A 64-bit (x86_64) system with OpenGL 3.3.
- Python 3.12 for the background data service. Without it the window still runs, in simulation mode.
- The data service listens on `127.0.0.1` only.

## Built with

C++20, SDL3, OpenGL 3.3 and Dear ImGui for the window; Python for the data service.
Every library is compiled into the executable, so there is nothing extra to install for the window itself.

## Terms of use

Mission Control is provided "as is". The source code is not published, and modifying
the program or reusing its code requires the author's written consent. Full terms: [LICENSE.txt](LICENSE.txt)
(Polish version: [LICENCJA.txt](LICENCJA.txt)).

Bundled third-party components keep their own licences: [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt).

---

<sub>Mission Control by <a href="https://github.com/masterful-dev">Masterful</a>.</sub>
