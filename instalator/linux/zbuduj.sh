#!/bin/bash
# Pakiety Mission Control dla Linuksa (amd64):
#   wydanie/mission-control_<wersja>_amd64.deb        - Debian, Ubuntu, Mint (apt, dpkg, graficzny instalator pakietow)
#   wydanie/mission-control-<wersja>-linux-x86_64.run - kazdy Linux, instalacja dla uzytkownika bez hasla administratora
# Uzycie: instalator/linux/zbuduj.sh
set -euo pipefail
K="$(cd "$(dirname "$0")/../.." && pwd)"; I="$K/instalator"
WERSJA=$(sed -n 's/^project(centrum VERSION \([0-9.]*\).*/\1/p' "$K/program/CMakeLists.txt")
R="$I/linux/robocze"; T="$R/program"; rm -rf "$R" "$I/linux/pakiet"; mkdir -p "$T/bin" "$T/serwer/domyslne" "$K/wydanie"

# ---- 1. drzewo programu: wspolne dla obu pakietow ----
(cd "$K/program" && ./buduj.sh >/dev/null)
install -m 755 "$K/bin/centrum" "$T/bin/mission-control"; strip "$T/bin/mission-control"
cp -r "$K/program/dane" "$T/dane"
cp "$I/LICENCJA.txt" "$I/LICENSE.txt" "$I/THIRD-PARTY-NOTICES.txt" "$T/"
cp -r "$K/serwer/centrum" "$T/serwer/centrum"; cp -r "$K/serwer/web" "$T/serwer/web"; cp -r "$K/serwer/biblioteki-py312" "$T/serwer/lib"
find "$T" -name __pycache__ -type d -prune -exec rm -rf {} +
# dane uzytkownika poza katalogiem programu: CENTRUM_DANE (domyslnie ~/.local/share/mission-control)
DD='Path(os.environ.get("CENTRUM_DANE") or Path(__file__).resolve().parent.parent)'
for f in energia.py registry.py procs.py; do sed -i "s|^BASE_DIR = Path(__file__).resolve().parent.parent\$|BASE_DIR = $DD|" "$T/serwer/centrum/$f"; grep -q "CENTRUM_DANE" "$T/serwer/centrum/$f"; done
grep -q "^import os" "$T/serwer/centrum/energia.py" || sed -i '0,/^import shutil/s//import os\nimport shutil/' "$T/serwer/centrum/energia.py"
sed -i 's|^RAPORTY_DIR = Path("/mnt/H/!raporty")|RAPORTY_DIR = energia.BASE_DIR / "raporty"|' "$T/serwer/centrum/raport_energii.py"; grep -q 'BASE_DIR / "raporty"' "$T/serwer/centrum/raport_energii.py"
sed -i 's|^DISKS = \[.*\]|DISKS = ["/"]|' "$T/serwer/centrum/telemetry.py"
printf 'psu_sprawnosc: 0.92\nvram_margin_mb: 256\n# Urzadzenia stale wlaczone poza komputerem (monitory itp.): nazwa, sztuk, moc_w\nurzadzenia: []\n' > "$T/serwer/domyslne/config.yaml"
cp "$K/serwer/taryfa.yaml" "$T/serwer/domyslne/taryfa.yaml"; printf '[]\n' > "$T/serwer/domyslne/models.yaml"
cat > "$T/serwer/start.sh" <<'S'
#!/bin/bash
# Usluga danych Mission Control (tylko 127.0.0.1). Dane uzytkownika: ~/.local/share/mission-control.
T="$(cd "$(dirname "$0")" && pwd)"; PORT="${CENTRUM_PORT:-8770}"
export CENTRUM_DANE="${CENTRUM_DANE:-${XDG_DATA_HOME:-$HOME/.local/share}/mission-control}"
mkdir -p "$CENTRUM_DANE/logs"
for f in config.yaml taryfa.yaml models.yaml; do [ -e "$CENTRUM_DANE/$f" ] || cp "$T/domyslne/$f" "$CENTRUM_DANE/$f"; done
PY=$(command -v python3.12 || command -v python3 || true)
if [ -z "$PY" ] || [ "$("$PY" -c 'import sys; print("%d.%d" % sys.version_info[:2])')" != "3.12" ]; then echo "Mission Control: usluga danych wymaga Pythona 3.12 (polecenie python3.12). Okno dziala bez niej w trybie symulacji." >&2; exit 0; fi
# port zajety przez dzialajaca juz usluge: nic nie robimy
if "$PY" -c "import socket,sys; s=socket.socket(); s.settimeout(.5); sys.exit(0 if s.connect_ex(('127.0.0.1',$PORT))==0 else 1)"; then echo "port $PORT zajety: usluga juz dziala"; exit 0; fi
export CUDA_DEVICE_ORDER=PCI_BUS_ID PYTHONPATH="$T/lib:$T" PYTHONDONTWRITEBYTECODE=1 CENTRUM_STATE="$CENTRUM_DANE/state.json"
cd "$T" && exec "$PY" -m uvicorn centrum.app:app --host 127.0.0.1 --port "$PORT"
S
chmod 755 "$T/serwer/start.sh"
# polecenie startowe: pilnuje uslugi danych (systemd uzytkownika, a gdy go nie ma - zwykly proces w tle) i otwiera okno
cat > "$T/bin/uruchom.sh" <<'S'
#!/bin/sh
T="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
if ! systemctl --user start mission-control-serwer.service >/dev/null 2>&1; then (setsid "$T/serwer/start.sh" >/dev/null 2>&1 &) ; fi
exec "$T/bin/mission-control" "$@"
S
chmod 755 "$T/bin/uruchom.sh"
find "$T" -type d -exec chmod 755 {} +; find "$T" -type f ! -name mission-control ! -name '*.sh' -exec chmod 644 {} +
USLUGA='[Unit]
Description=Mission Control - usluga danych (sprzet, modele, energia; port 8770)

[Service]
Type=simple
ExecStart=@KAT@/serwer/start.sh
Restart=on-failure
RestartSec=3
TimeoutStopSec=15
KillMode=process

[Install]
WantedBy=default.target'
SKROT='[Desktop Entry]
Type=Application
Version=1.0
Name=Mission Control
Comment=Podgląd sprzętu, modele AI i energia
Comment[en]=Hardware monitor, AI models and energy
Exec=@EXEC@
Icon=mission-control
Terminal=false
Categories=System;Monitor;
StartupWMClass=centrum-misji-v2'

# ---- 2. pakiet .deb ----
P="$R/deb"; mkdir -p "$P/DEBIAN" "$P/opt" "$P/usr/bin" "$P/usr/lib/systemd/user" "$P/usr/share/applications" "$P/usr/share/doc/mission-control"
cp -a "$T" "$P/opt/mission-control"
ln -s /opt/mission-control/bin/uruchom.sh "$P/usr/bin/mission-control"
printf '%s\n' "${USLUGA//@KAT@//opt/mission-control}" > "$P/usr/lib/systemd/user/mission-control-serwer.service"
printf '%s\n' "${SKROT//@EXEC@/mission-control}" > "$P/usr/share/applications/mission-control.desktop"
for n in 16 24 32 48 64 128 256 512; do install -Dm 644 "$I/ikona/mission-control-$n.png" "$P/usr/share/icons/hicolor/${n}x${n}/apps/mission-control.png"; done
cat "$I/LICENSE.txt" > "$P/usr/share/doc/mission-control/copyright"; printf '\nThird-party components: /opt/mission-control/THIRD-PARTY-NOTICES.txt\n' >> "$P/usr/share/doc/mission-control/copyright"
cat > "$P/DEBIAN/control" <<S
Package: mission-control
Version: $WERSJA
Architecture: amd64
Maintainer: Masterful <kontakt@masterful.invalid>
Installed-Size: $(du -sk "$P" | cut -f1)
Depends: libc6 (>= 2.38), libstdc++6, python3 (>= 3.12), python3 (<< 3.13), libgl1, libx11-6, libxext6, libxcursor1, libxi6, libxrandr2, libxfixes3
Recommends: xdg-utils
Section: utils
Priority: optional
Description: Mission Control - hardware monitor, AI models and energy
 Window with graphics cards, processor, memory, disks and energy use, plus a list
 of AI models to start. Includes a background data service (127.0.0.1 only) and
 an animated face that follows the model's work.
 .
 Provided "as is", without any warranty; you install and use it at your own
 risk. Modifying the program or using its code requires the author's written
 consent. Terms: /opt/mission-control/LICENSE.txt
S
cat > "$P/DEBIAN/postinst" <<'S'
#!/bin/sh
set -e
systemctl --global enable mission-control-serwer.service >/dev/null 2>&1 || true
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q /usr/share/applications || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -q -f /usr/share/icons/hicolor || true
cat <<'W'

  MISSION CONTROL - terms of use / warunki korzystania
  The program is provided "as is", WITHOUT ANY WARRANTY. The author is not liable for
  any damage. You install and use it at your own risk. Modifying the program or using
  its code requires the author's written consent.
  Program jest bez zadnej gwarancji, autor nie odpowiada za zadne szkody, instalujesz
  i uzywasz go na wlasna odpowiedzialnosc. Modyfikowanie programu i wykorzystywanie jego
  kodu wymaga pisemnej zgody autora.
  Full text / pelny tekst: /opt/mission-control/LICENSE.txt, LICENCJA.txt
  The program asks you to accept these terms at first start.

W
exit 0
S
cat > "$P/DEBIAN/prerm" <<'S'
#!/bin/sh
set -e
if [ "$1" = remove ] || [ "$1" = purge ]; then systemctl --global disable mission-control-serwer.service >/dev/null 2>&1 || true; fi
exit 0
S
chmod 755 "$P/DEBIAN/postinst" "$P/DEBIAN/prerm"
DEB="$K/wydanie/mission-control_${WERSJA}_amd64.deb"
fakeroot dpkg-deb --build --root-owner-group "$P" "$DEB" >/dev/null

# ---- 3. instalator .run: kazdy Linux, bez administratora ----
U="$R/run"; mkdir -p "$U/ikony"; cp -a "$T" "$U/program"
for n in 16 24 32 48 64 128 256 512; do cp "$I/ikona/mission-control-$n.png" "$U/ikony/$n.png"; done
printf '%s\n' "$USLUGA" > "$U/usluga.wzor"; printf '%s\n' "$SKROT" > "$U/skrot.wzor"
cat > "$U/program/odinstaluj.sh" <<'S'
#!/bin/sh
# Usuwa Mission Control zainstalowane instalatorem .run. Dane uzytkownika (~/.local/share/mission-control) zostaja.
D="${XDG_DATA_HOME:-$HOME/.local/share}"; C="${XDG_CONFIG_HOME:-$HOME/.config}"; KAT="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
case "$KAT" in */mission-control) ;; *) echo "Nie rozpoznaje katalogu programu: $KAT" >&2; exit 1 ;; esac
systemctl --user disable --now mission-control-serwer.service >/dev/null 2>&1 || true
rm -f "$C/systemd/user/mission-control-serwer.service" "$D/applications/mission-control.desktop" "$HOME/.local/bin/mission-control" "$C/autostart/centrum-misji-v2.desktop"
for n in 16 24 32 48 64 128 256 512; do rm -f "$D/icons/hicolor/${n}x${n}/apps/mission-control.png"; done
rm -rf "$KAT"
echo "Mission Control odinstalowane / uninstalled."
S
chmod 755 "$U/program/odinstaluj.sh"
tar -C "$U" -czf "$R/dane.tar.gz" program ikony usluga.wzor skrot.wzor
RUN="$K/wydanie/mission-control-${WERSJA}-linux-x86_64.run"
cat > "$RUN" <<'S'
#!/bin/sh
# Instalator Mission Control dla Linuksa (x86_64). Instaluje dla biezacego uzytkownika, bez hasla administratora.
#   sh mission-control-*.run            instalacja z pytaniem o warunki
#   sh mission-control-*.run --accept   instalacja bez pytania (akceptujesz warunki)
set -e
D="${XDG_DATA_HOME:-$HOME/.local/share}"; C="${XDG_CONFIG_HOME:-$HOME/.config}"; KAT="$HOME/.local/opt/mission-control"
[ "$(uname -m)" = x86_64 ] || { echo "Ten pakiet jest dla x86_64 / this package is for x86_64." >&2; exit 1; }
WARUNKI='MISSION CONTROL - terms of use / warunki korzystania

EN: The program is provided "as is", WITHOUT ANY WARRANTY. The author is not liable for any
    damage, data loss or hardware failure. You install and use it entirely at your own risk.
    Without the author'"'"'s written consent you may not modify the program, use its code in
    whole or in part, reverse engineer it, or distribute modified versions.

PL: Program jest udostepniany w stanie, w jakim jest, BEZ ZADNEJ GWARANCJI. Autor nie odpowiada
    za zadne szkody, utrate danych ani uszkodzenie sprzetu. Instalujesz i uzywasz programu
    wylacznie na wlasna odpowiedzialnosc. Bez pisemnej zgody autora nie wolno modyfikowac
    programu, wykorzystywac jego kodu, odtwarzac go ani rozpowszechniac wersji zmienionych.'
if [ "${1:-}" != "--accept" ]; then
  if [ -t 0 ]; then printf '%s\n\nAccept / akceptujesz? [yes/tak/no]: ' "$WARUNKI"; read -r O; case "$O" in yes|y|tak|t|YES|TAK) ;; *) echo "Przerwano / aborted."; exit 1 ;; esac
  else echo "$WARUNKI"; echo; echo "Uruchom w terminalu albo z --accept / run in a terminal or with --accept." >&2; exit 1; fi
fi
POCZ=$(awk '/^__DANE__$/ {print NR + 1; exit}' "$0"); TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
tail -n +"$POCZ" "$0" | tar -xz -C "$TMP"
systemctl --user stop mission-control-serwer.service >/dev/null 2>&1 || true
rm -rf "$KAT"; mkdir -p "$HOME/.local/opt" "$HOME/.local/bin" "$D/applications" "$C/systemd/user"; mv "$TMP/program" "$KAT"
ln -sf "$KAT/bin/uruchom.sh" "$HOME/.local/bin/mission-control"
for n in 16 24 32 48 64 128 256 512; do mkdir -p "$D/icons/hicolor/${n}x${n}/apps"; cp "$TMP/ikony/$n.png" "$D/icons/hicolor/${n}x${n}/apps/mission-control.png"; done
sed "s|@KAT@|$KAT|" "$TMP/usluga.wzor" > "$C/systemd/user/mission-control-serwer.service"
sed "s|@EXEC@|\"$KAT/bin/uruchom.sh\"|" "$TMP/skrot.wzor" > "$D/applications/mission-control.desktop"
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q "$D/applications" 2>/dev/null || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -q -f -t "$D/icons/hicolor" 2>/dev/null || true
if [ -z "${MC_BEZ_USLUGI:-}" ]; then systemctl --user daemon-reload >/dev/null 2>&1 && systemctl --user enable mission-control-serwer.service >/dev/null 2>&1 || true; fi
echo "Mission Control zainstalowane / installed: $KAT"
echo "Start: menu programow albo polecenie mission-control (~/.local/bin). Odinstalowanie / uninstall: $KAT/odinstaluj.sh"
exit 0
__DANE__
S
cat "$R/dane.tar.gz" >> "$RUN"; chmod 755 "$RUN"
ls -la "$DEB" "$RUN"
