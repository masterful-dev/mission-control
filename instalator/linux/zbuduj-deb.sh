#!/bin/bash
# Pakiet instalacyjny Mission Control dla Linuksa (.deb, amd64): okno C++, usluga danych, ikona, wpis w menu.
# Wynik: wydanie/mission-control_<wersja>_amd64.deb. Uzycie: instalator/linux/zbuduj-deb.sh
set -euo pipefail
K="$(cd "$(dirname "$0")/../.." && pwd)"            # katalog projektu V2
SERWER=/mnt/RUNE/PROJEKTY/CENTRUM-MISJI            # zrodla uslugi danych (Python)
WERSJA=$(sed -n 's/^project(centrum VERSION \([0-9.]*\).*/\1/p' "$K/program/CMakeLists.txt")
P="$K/instalator/linux/pakiet"; O="$P/opt/mission-control"
rm -rf "$P"; mkdir -p "$O/bin" "$O/serwer/domyslne" "$P/DEBIAN" "$P/usr/bin" "$P/usr/lib/systemd/user" "$P/usr/share/applications" "$P/usr/share/doc/mission-control"

# 1. okno
(cd "$K/program" && ./buduj.sh >/dev/null)
install -m 755 "$K/bin/centrum" "$O/bin/mission-control"; strip "$O/bin/mission-control"
cp -r "$K/program/dane" "$O/dane"

# 2. usluga danych: kod, strona zapasowa, biblioteki Pythona 3.12 ze sprawdzonego srodowiska
mkdir -p "$O/serwer/centrum" "$O/serwer/lib"
cp "$SERWER"/centrum/*.py "$O/serwer/centrum/"; cp -r "$SERWER/web" "$O/serwer/web"
SP=$(echo "$SERWER"/.venv/lib/python3.12/site-packages)
cp -r "$SP"/. "$O/serwer/lib/"       # cale srodowisko: FastAPI potrzebuje tez swoich zaleznosci posrednich
(cd "$O/serwer/lib" && rm -rf pip pip-* setuptools setuptools-* pkg_resources _distutils_hack distutils-precedence.pth pytest pytest-* _pytest pluggy pluggy-* iniconfig iniconfig-* pygments pygments-* Pygments-* py.py example.py _virtualenv.py _virtualenv.pth watchfiles watchfiles-* wheel wheel-*)
find "$O" -name __pycache__ -type d -prune -exec rm -rf {} +
# dane uzytkownika poza katalogiem programu: CENTRUM_DANE (domyslnie ~/.local/share/mission-control)
DD='Path(os.environ.get("CENTRUM_DANE") or Path(__file__).resolve().parent.parent)'
for f in energia.py registry.py procs.py; do sed -i "s|^BASE_DIR = Path(__file__).resolve().parent.parent\$|BASE_DIR = $DD|" "$O/serwer/centrum/$f"; grep -q "CENTRUM_DANE" "$O/serwer/centrum/$f"; done
grep -q "^import os" "$O/serwer/centrum/energia.py" || sed -i '0,/^import shutil/s//import os\nimport shutil/' "$O/serwer/centrum/energia.py"
sed -i 's|^RAPORTY_DIR = Path("/mnt/H/!raporty")|RAPORTY_DIR = energia.BASE_DIR / "raporty"|' "$O/serwer/centrum/raport_energii.py"; grep -q 'BASE_DIR / "raporty"' "$O/serwer/centrum/raport_energii.py"
sed -i 's|^DISKS = \[.*\]|DISKS = ["/"]|' "$O/serwer/centrum/telemetry.py"
printf 'psu_sprawnosc: 0.92\nvram_margin_mb: 256\n# Urzadzenia stale wlaczone poza komputerem (monitory itp.): nazwa, sztuk, moc_w\nurzadzenia: []\n' > "$O/serwer/domyslne/config.yaml"
cp "$SERWER/taryfa.yaml" "$O/serwer/domyslne/taryfa.yaml"; printf '[]\n' > "$O/serwer/domyslne/models.yaml"
cat > "$O/serwer/start.sh" <<'S'
#!/bin/bash
# Usluga danych Mission Control (tylko 127.0.0.1). Dane uzytkownika: ~/.local/share/mission-control.
T="$(cd "$(dirname "$0")" && pwd)"; PORT="${CENTRUM_PORT:-8770}"
export CENTRUM_DANE="${CENTRUM_DANE:-${XDG_DATA_HOME:-$HOME/.local/share}/mission-control}"
mkdir -p "$CENTRUM_DANE/logs"
for f in config.yaml taryfa.yaml models.yaml; do [ -e "$CENTRUM_DANE/$f" ] || cp "$T/domyslne/$f" "$CENTRUM_DANE/$f"; done
# port zajety przez dzialajace juz Centrum: nic nie robimy
if python3 -c "import socket,sys; s=socket.socket(); s.settimeout(.5); sys.exit(0 if s.connect_ex(('127.0.0.1',$PORT))==0 else 1)"; then echo "port $PORT zajety: usluga juz dziala"; exit 0; fi
export CUDA_DEVICE_ORDER=PCI_BUS_ID PYTHONPATH="$T/lib:$T" PYTHONDONTWRITEBYTECODE=1 CENTRUM_STATE="$CENTRUM_DANE/state.json"
cd "$T" && exec python3 -m uvicorn centrum.app:app --host 127.0.0.1 --port "$PORT"
S
chmod 755 "$O/serwer/start.sh"

# 3. system: polecenie, usluga uzytkownika, menu, ikony
cat > "$P/usr/bin/mission-control" <<'S'
#!/bin/sh
# Mission Control: pilnuje, zeby usluga danych dzialala, i otwiera okno.
systemctl --user start mission-control-serwer.service >/dev/null 2>&1 || true
exec /opt/mission-control/bin/mission-control "$@"
S
chmod 755 "$P/usr/bin/mission-control"
cat > "$P/usr/lib/systemd/user/mission-control-serwer.service" <<'S'
[Unit]
Description=Mission Control - usluga danych (sprzet, modele, energia; port 8770)

[Service]
Type=simple
ExecStart=/opt/mission-control/serwer/start.sh
Restart=on-failure
RestartSec=3
TimeoutStopSec=15
# modele uruchomione z Centrum nie gina przy restarcie uslugi
KillMode=process

[Install]
WantedBy=default.target
S
cat > "$P/usr/share/applications/mission-control.desktop" <<'S'
[Desktop Entry]
Type=Application
Version=1.0
Name=Mission Control
Comment=Podgląd sprzętu, modele AI i energia
Comment[en]=Hardware monitor, AI models and energy
Exec=mission-control
Icon=mission-control
Terminal=false
Categories=System;Monitor;
StartupWMClass=centrum-misji-v2
S
for n in 16 24 32 48 64 128 256 512; do install -Dm 644 "$K/instalator/ikona/mission-control-$n.png" "$P/usr/share/icons/hicolor/${n}x${n}/apps/mission-control.png"; done
cat > "$P/usr/share/doc/mission-control/copyright" <<'S'
Mission Control - Masterful.
Skladniki: SDL3 (zlib), Dear ImGui (MIT), FreeType (FTL), nlohmann json (MIT), cpp-httplib (MIT),
czcionki Inter, JetBrains Mono, Noto (SIL OFL 1.1), DejaVu Sans (Bitstream Vera),
siatka twarzy ICT FaceKit Light (USC ICT, MIT), FastAPI, Starlette, Uvicorn, Pydantic, httpx, PyYAML (MIT/BSD),
psutil (BSD), pynvml (BSD).
S

# 4. opis pakietu i skrypty
ROZM=$(du -sk "$P" | cut -f1)
cat > "$P/DEBIAN/control" <<S
Package: mission-control
Version: $WERSJA
Architecture: amd64
Maintainer: Masterful <kontakt@masterful.invalid>
Installed-Size: $ROZM
Pre-Depends: debconf (>= 0.5)
Depends: libc6 (>= 2.38), libstdc++6, python3 (>= 3.12), python3 (<< 3.13), libgl1, libx11-6, libxext6, libxcursor1, libxi6, libxrandr2, libxfixes3
Recommends: xdg-utils
Section: utils
Priority: optional
Description: Mission Control - podglad sprzetu, modele AI i energia
 Okno z kartami graficznymi, procesorem, pamiecia, dyskami i zuzyciem energii
 oraz lista modeli AI do uruchamiania. Zawiera usluge danych dzialajaca w tle
 (tylko 127.0.0.1) i animowana twarz, ktora idzie za praca modelu.
S
install -m 644 "$K/instalator/LICENCJA.txt" "$O/LICENCJA.txt"; cat "$K/instalator/LICENCJA.txt" >> "$P/usr/share/doc/mission-control/copyright"
# warunki korzystania: instalacja pyta o zgode (debconf), bez zgody sie nie wykona
cat > "$P/DEBIAN/templates" <<'S'
Template: mission-control/warunki
Type: boolean
Default: false
Description: Do you accept the Mission Control terms of use?
 The program is provided "as is", WITHOUT ANY WARRANTY. The author is not liable
 for any damage, data loss or hardware failure. You install and use it entirely
 at your own risk.
 .
 Without the author's written consent you may not modify the program, use its
 code in whole or in part, reverse engineer it, or distribute modified versions.
 .
 Full text: /opt/mission-control/LICENCJA.txt
Description-pl.UTF-8: Czy akceptujesz warunki korzystania z Mission Control?
 Program jest udostępniany w stanie, w jakim jest, BEZ ŻADNEJ GWARANCJI. Autor
 nie odpowiada za żadne szkody, utratę danych ani uszkodzenie sprzętu.
 Instalujesz i używasz programu wyłącznie na własną odpowiedzialność.
 .
 Bez pisemnej zgody autora nie wolno modyfikować programu, wykorzystywać jego
 kodu w całości ani we fragmentach, odtwarzać kodu ani rozpowszechniać wersji
 zmienionych.
 .
 Pełny tekst: /opt/mission-control/LICENCJA.txt
S
cat > "$P/DEBIAN/preinst" <<'S'
#!/bin/sh
set -e
. /usr/share/debconf/confmodule
db_fset mission-control/warunki seen false || true
db_input critical mission-control/warunki || true
db_go || true
db_get mission-control/warunki
if [ "$RET" != "true" ]; then echo "Mission Control: warunki korzystania nie zostaly zaakceptowane - instalacja przerwana." >&2; db_stop; exit 1; fi
db_stop
exit 0
S
cat > "$P/DEBIAN/postrm" <<'S'
#!/bin/sh
set -e
if [ "$1" = purge ] && [ -e /usr/share/debconf/confmodule ]; then . /usr/share/debconf/confmodule; db_purge; fi
exit 0
S
chmod 755 "$P/DEBIAN/preinst" "$P/DEBIAN/postrm"
cat > "$P/DEBIAN/postinst" <<'S'
#!/bin/sh
set -e
systemctl --global enable mission-control-serwer.service >/dev/null 2>&1 || true
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q /usr/share/applications || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -q -f /usr/share/icons/hicolor || true
exit 0
S
cat > "$P/DEBIAN/prerm" <<'S'
#!/bin/sh
set -e
if [ "$1" = remove ] || [ "$1" = purge ]; then systemctl --global disable mission-control-serwer.service >/dev/null 2>&1 || true; fi
exit 0
S
chmod 755 "$P/DEBIAN/postinst" "$P/DEBIAN/prerm"
find "$P" -type d -exec chmod 755 {} +; find "$P/opt" "$P/usr/share" -type f ! -name start.sh -exec chmod 644 {} +
mkdir -p "$K/wydanie"; W="$K/wydanie/mission-control_${WERSJA}_amd64.deb"
fakeroot dpkg-deb --build --root-owner-group "$P" "$W" >/dev/null
echo "$W"; ls -la "$W"
