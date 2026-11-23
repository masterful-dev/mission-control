# Centrum Misji V2 (C++ program)

V2: rebuild from 11.11.2026. V1 stays unchanged in `/mnt/RUNE/PROJEKTY/CENTRUM-MISJI/program`. Both can run at the same time: V2 has its own settings (`~/.local/share/Masterful/centrum-misji-v2/ustawienia.json`) and its own single-instance lock.

One program for Linux and Windows: C++20, SDL3, OpenGL 3.3, Dear ImGui. All libraries are linked into the executable.

## Build

Linux, produces `../bin/centrum`:

    ./buduj.sh

Windows from Linux, produces `../bin/windows/centrum.exe`:

    ./buduj-windows.sh

The `dane/` folder must sit next to `centrum.exe`.

## Run

| Argument | Effect |
|---|---|
| (no arguments) | real data from `http://127.0.0.1:8770`; if the server does not answer: simulation with a control bar |
| `--sim` | always simulation |
| `--adres URL` | different Centrum server |
| `--dok` | DSH++ module mode: frameless window, line `DOK okno=<number>` on the output |
| `--rozmiar W H` | window size |
| `--test plik` | test scenario from `narzedzia/testy/` |

Shortcuts: `F` face only, `D` simulation bar, `Esc` back, `Ctrl` + wheel: interface size.

## Layout of files

| File | Contents |
|---|---|
| `zrodla/glowny.cpp` | window, loop, data from API or simulation, tray, test control |
| `zrodla/okno.cpp` | top bar, panel layout (default and custom: move, stretch, snap, save), scene, narrow mode |
| `zrodla/panele.cpp` | GPU, CPU, RAM, disks, model, model list, energy panels; tooltip of every element |
| `zrodla/menu.cpp` | right-click menu, separate for each element kind |
| `zrodla/dialogi.cpp` | settings, model properties, model form, import, report, confirmations |
| `zrodla/aplikacja.cpp` | settings, skins, model actions, model engines, 30-day energy |
| `zrodla/dyski.cpp` | this machine's disks detected from the system (Linux and Windows): name, description, usage, temperature |
| `zrodla/scena.cpp` | window background and live scene base (one shader for the whole screen) |
| `zrodla/api.cpp` | `/api/state` client and commands |
| `zrodla/sim.cpp` | simulation |
| `zrodla/ui.cpp` | fonts, buttons, colors, tooltips, card drawings |
| `narzedzia/eksport_makiety.mjs` | texts, skins and models from the makieta to `zrodla/gen_dane.inc` |
| `narzedzia/testy/` | scenarios, fake server `atrapa.py` |

## What V2 changes

- One model list instead of separate TP1 and TP2 panels: one or two cards drawn next to the model, bar color (1 card, 2 cards), filter, sort by clicking a column header.
- Model engine as a separate feature: detected from the launch command, shown as a badge, changed in the "Properties" window (right-click on a model). The list name has no engine and no TP1/TP2.
- Every panel can be moved by its title and stretched by an edge or a corner. The layout saves instantly as fractions of the window.
- Disks are detected by the program, not by the server configuration. Double-click opens the folder.
- Energy: tooltip over every bar, views for 48 hours, 30 days, the current month.
- Tooltip over every element, right-click menu depending on location.
- Face: hologram from the original Centrum Misji, always centered in the free area, no own background and no circles; live base behind it.

Final V2 version: 2.1.0 from 11.11.2026 (git tag `v2-final`). From this version system autostart (`~/.config/autostart/centrum-misji.desktop`) and the `CENTRUM MISJI V2` desktop shortcut launch `bin/centrum` from this folder.

## Tests

    SDL_VIDEO_DRIVER=offscreen ../bin/centrum --sim --test narzedzia/testy/v2-sim.txt --rozmiar 1680 962

The scenario drives the mouse and window size, screenshots land in `narzedzia/testy/zrzuty/`. The `czas N` command waits N seconds (the `offscreen` driver has no frame limit). Scenarios with clicks only run with `--sim`: on real data, a click in the model menu could start the model. Start and stop buttons are not tested on the real server: use `narzedzia/testy/atrapa.py <port>` with `--adres http://127.0.0.1:<port>`.

## Licenses

| Component | License |
|---|---|
| SDL3 | zlib |
| Dear ImGui | MIT |
| FreeType | FTL |
| nlohmann json | MIT |
| cpp-httplib | MIT |
| Inter, JetBrains Mono, Noto Sans Symbols2, Noto Sans Math | SIL OFL 1.1 |
| DejaVu Sans | Bitstream Vera license |

## Start and window

On Linux the program replaces the old window: V2 autostart is `~/.config/autostart/centrum-misji-v2.desktop` (the "Start with system" toggle); the menu and the system tray still point at V1. The "Start with system" toggle in Settings reads and changes the same file.

Only one copy runs: a second launch shows the existing window and exits. The lock does not apply to `--dok`, `--test` and `--sim`.

The window opens on the screen where it was last closed (`ekran` in `ustawienia.json`), and before the first close on the main screen.

Test without a window on the monitor: `SDL_VIDEO_DRIVER=offscreen ../bin/centrum --test scenariusz.txt --rozmiar 900 620`. On this driver there is no frame limit, so `czekaj` lasts much less than on a monitor.
