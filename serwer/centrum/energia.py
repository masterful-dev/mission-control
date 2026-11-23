"""Energy log (SPEC §12a): integrates power 1×/s into SQLite `energia.db`.

Power: PSU `corsairpsu` (DC side) + NVML per card; when the PSU is missing — GPU sum + RAPL (CPU)
and the "estimate" flag. Energy = Σ power × Δt, Δt from time.monotonic() (never assumed 1 s).
Separate thread (independent of WS and client count); write every minute, pause = missing rows.
"""
from __future__ import annotations

import shutil
import sqlite3
import threading
import time
from datetime import date, datetime
from pathlib import Path

import yaml

from . import telemetry
from .planner import MARGIN_MB as MARGIN_MB_DOMYSLNY

BASE_DIR = Path(__file__).resolve().parent.parent
DB_PATH = BASE_DIR / "energia.db"
CONFIG_PATH = BASE_DIR / "config.yaml"

SPRAWNOSC_DOMYSLNA = 0.90
# V100 (GPU0, GPU1) is powered by a SEPARATE PSU — Corsair does not see them; T400 (GPU2) draws from the slot = Corsair
GPU_OSOBNY_PSU_DOMYSLNIE = [0, 1]
SPRAWNOSC_MIN, SPRAWNOSC_MAX = 0.30, 1.00
MARGIN_MB = MARGIN_MB_DOMYSLNY   # planner VRAM reserve (planner.MARGIN_MB = 1024); config: vram_margin_mb
MAX_DT = 10.0        # s — longer sampling gap (thread stall) is not integrated
MAX_GPU = 8          # gpuN_wh columns in the minute table
URZADZENIA_MAKS_W = 5000.0   # sanity: total device power from the outlet

SCHEMA = """
CREATE TABLE IF NOT EXISTS minuty (
  ts_min INTEGER PRIMARY KEY,
  wh_dc REAL, wh_gniazdko REAL, w_sred REAL, w_max REAL,
  gpu0_wh REAL, gpu1_wh REAL, gpu2_wh REAL,
  sekund INTEGER,
  urzadzenia_wh REAL
);
"""


# ---------- config.yaml (PSU efficiency, editable in UI) ----------

def load_config(path: Path | None = None) -> dict:
    p = path or CONFIG_PATH
    try:
        data = yaml.safe_load(p.read_text()) or {}
    except (OSError, yaml.YAMLError):
        data = {}
    return _norm_config(data)


def _liczba(val, default: float = 0.0, lo: float = 0.0, hi: float = URZADZENIA_MAKS_W) -> float:
    try:
        v = float(val)
    except (TypeError, ValueError):
        return default
    return min(hi, max(lo, v))


def _norm_urzadzenia(val) -> list[dict]:
    """Always-on devices, powered from the outlet outside the computer PSUs.
    Every entry: {nazwa, sztuk, moc_w}; `razem_w` = sztuk × moc_w."""
    if isinstance(val, dict):
        val = [val]
    out = []
    for u in val or []:
        if not isinstance(u, dict):
            continue
        try:
            sztuk = max(0, min(50, int(u.get("sztuk", 1))))
        except (TypeError, ValueError):
            sztuk = 1
        moc = _liczba(u.get("moc_w"), 0.0)
        if sztuk <= 0 or moc <= 0:
            continue
        out.append({"nazwa": str(u.get("nazwa") or "urządzenie"), "sztuk": sztuk,
                    "moc_w": round(moc, 3), "razem_w": round(sztuk * moc, 3)})
    return out


def _norm_config(data: dict) -> dict:
    osobne = data.get("gpu_osobny_psu", GPU_OSOBNY_PSU_DOMYSLNIE)
    try:
        osobne = sorted({int(i) for i in osobne})
    except (TypeError, ValueError):
        osobne = list(GPU_OSOBNY_PSU_DOMYSLNIE)
    urzadzenia = _norm_urzadzenia(data.get("urzadzenia"))
    return {"psu_sprawnosc": _sprawnosc(data.get("psu_sprawnosc"), SPRAWNOSC_DOMYSLNA),
            "psu_gpu_sprawnosc": _sprawnosc(data.get("psu_gpu_sprawnosc"), SPRAWNOSC_DOMYSLNA),
            "vram_margin_mb": int(_liczba(data.get("vram_margin_mb"), MARGIN_MB, 0, 4096)),
            "gpu_osobny_psu": osobne,
            "urzadzenia": urzadzenia,
            "urzadzenia_w": round(sum(u["razem_w"] for u in urzadzenia), 3)}


def margines_vram() -> int:
    """VRAM reserve at startup planning (config.yaml: vram_margin_mb, in MiB; default 1024).

    02.11: lowered to 256 — Flash-Next 125B TP2 (warp-flash-tp2) needed 30.8 GB, the free space
    computed from a 1 GiB reserve gave "missing 36 MB" though the card had room."""
    return int(load_config().get("vram_margin_mb") or MARGIN_MB)


def save_config(cfg: dict, path: Path | None = None) -> dict:
    p = path or CONFIG_PATH
    out = _norm_config({**load_config(p), **(cfg or {})})
    p.write_text(yaml.safe_dump(out, sort_keys=False))
    return out


def _sprawnosc(val, default: float) -> float:
    try:
        v = float(val)
    except (TypeError, ValueError):
        return default
    return min(SPRAWNOSC_MAX, max(SPRAWNOSC_MIN, v))


# ---------- migracja schematu (stare bazy bez urzadzenia_wh) ----------

def _zapewnij_kolumne(con: sqlite3.Connection) -> bool:
    """Adds the urzadzenia_wh column to an existing DB. True = the column was already there."""
    kolumny = {r[1] for r in con.execute("PRAGMA table_info(minuty)")}
    if "urzadzenia_wh" in kolumny:
        return True
    con.execute("ALTER TABLE minuty ADD COLUMN urzadzenia_wh REAL")
    con.commit()
    return False


# ---------- meter (thread every 1 s) ----------

class Meter:
    """Integrates power and saves one row per full minute with samples."""

    def __init__(self, db_path: Path | None = None, czytaj=None,
                 monotonic=None, wall=None, sprawnosc: float | None = None,
                 config: dict | None = None):
        self.db_path = Path(db_path) if db_path else DB_PATH
        self.czytaj = czytaj or telemetry.read_energia
        self.monotonic = monotonic or time.monotonic
        self.wall = wall or time.time
        self._sprawnosc = sprawnosc          # None => czytaj z config.yaml (cache 30 s)
        # wstrzyknieta sprawnosc (testy) bez configu = jeden zasilacz (zadna karta na osobnym PSU)
        self._cfg_wstrz = _norm_config(config) if config is not None else (
            _norm_config({"gpu_osobny_psu": [], "psu_sprawnosc": sprawnosc}) if sprawnosc is not None else None)
        self._spr_t, self._spr_val = 0.0, SPRAWNOSC_DOMYSLNA
        self._agr: dict | None = None
        self._agr_t = 0.0
        self.lock = threading.Lock()
        self._last: float | None = None
        self._min: dict | None = None
        self.ostatni: dict | None = None     # last sample (for UI: "NOW … W")
        self._con = sqlite3.connect(str(self.db_path), timeout=5, check_same_thread=False)
        self._con.execute(SCHEMA)
        _zapewnij_kolumne(self._con)          # DB predating always-on devices
        self._con.commit()

    def sprawnosc(self) -> float:
        if self._sprawnosc is not None:
            return self._sprawnosc
        if self._cfg_wstrz is not None:      # konfiguracja podana w konstruktorze (testy) ma pierwszenstwo
            return self._cfg_wstrz["psu_sprawnosc"]
        now = self.monotonic()               # config.yaml read at most once per 30 s
        if now - self._spr_t >= 30.0:
            self._spr_t, self._cfg = now, load_config()
            self._spr_val = self._cfg["psu_sprawnosc"]
        return self._spr_val

    def config(self) -> dict:
        if self._cfg_wstrz is not None:
            return self._cfg_wstrz
        self.sprawnosc()                     # odswieza cache config.yaml
        return getattr(self, "_cfg", None) or _norm_config({})

    def krok(self, dt: float | None = None) -> dict:
        """One power sample; dt = actual gap (monotonic), computed when None."""
        try:
            src = self.czytaj() or {}
        except Exception as exc:      # telemetry/NVML in flight — skip the sample, not the whole log
            print(f"[CENTRUM] blad odczytu mocy: {exc}", flush=True)
            src = {}
        now = self.monotonic()
        if dt is None:
            dt = 0.0 if self._last is None else max(0.0, min(now - self._last, MAX_DT))
        self._last = now
        gpu = {int(i): float(w) for i, w in (src.get("gpu_w") or {}).items() if w is not None}
        psu = src.get("psu_w")
        rapl = src.get("rapl_w")
        cfg = self.config()
        osobne_w = sum(w for i, w in gpu.items() if i in cfg["gpu_osobny_psu"])   # drugi zasilacz (V100)
        if psu is not None:   # Corsair = CPU, RAM, dyski, wentylatory, T400; + karty z osobnego PSU
            glowny, szacunek = float(psu), False
        else:
            glowny, szacunek = sum(w for i, w in gpu.items() if i not in cfg["gpu_osobny_psu"]) + (rapl or 0.0), True
        dc = glowny + osobne_w
        # always-on devices (monitors, audio) draw current from the outlet directly — not through the PSU
        urzadzenia_w = cfg["urzadzenia_w"]
        gniazdko = (glowny / max(0.1, self.sprawnosc()) + osobne_w / max(0.1, cfg["psu_gpu_sprawnosc"])
                    + urzadzenia_w)
        probka = {"t": src.get("t") or self.wall(), "dt": dt, "dc_w": dc, "gpu": gpu,
                  "gniazdko_w": gniazdko, "szacunek": szacunek, "urzadzenia_w": urzadzenia_w,
                  "psu_glowny_w": glowny, "psu_gpu_w": osobne_w}
        with self.lock:
            self.ostatni = probka
            self._akumuluj(probka)
        return probka

    def _akumuluj(self, s: dict) -> None:
        # minute = bucket przedzialu [t-dt, t] (start przedzialu) — energia nie "przeskakuje" minuty
        ts_min = int(max(0.0, s["t"] - s["dt"]) // 60) * 60
        if self._min and self._min["ts_min"] != ts_min:
            self._flush()
        if self._min is None:
            self._min = {"ts_min": ts_min, "wh_dc": 0.0, "wh_gniazdko": 0.0, "w_max": 0.0,
                         "sekund": 0.0, "gpu": [0.0] * MAX_GPU, "urzadzenia": 0.0}
        m = self._min
        dt = s["dt"]
        m["wh_dc"] += s["dc_w"] * dt / 3600.0        # Wh = W x s / 3600
        m["wh_gniazdko"] += s["gniazdko_w"] * dt / 3600.0
        m["urzadzenia"] += s.get("urzadzenia_w", 0.0) * dt / 3600.0
        m["w_max"] = max(m["w_max"], s["dc_w"])
        m["sekund"] += dt
        for i, w in s["gpu"].items():
            if 0 <= i < MAX_GPU:
                m["gpu"][i] += w * dt / 3600.0

    def _flush(self) -> None:
        m, self._min = self._min, None
        if not m or m["sekund"] <= 0:
            return
        sekund = int(round(m["sekund"]))
        self._con.execute(
            "INSERT OR REPLACE INTO minuty (ts_min, wh_dc, wh_gniazdko, w_sred, w_max,"
            " gpu0_wh, gpu1_wh, gpu2_wh, sekund, urzadzenia_wh) VALUES (?,?,?,?,?,?,?,?,?,?)",
            (m["ts_min"], round(m["wh_dc"], 4), round(m["wh_gniazdko"], 4),
             round(m["wh_dc"] * 3600.0 / m["sekund"], 3), round(m["w_max"], 3),
             *[round(x, 4) for x in m["gpu"][:3]], sekund, round(m.get("urzadzenia", 0.0), 4)))
        self._con.commit()

    def run(self, stop: threading.Event | None = None, interval: float = 1.0) -> None:
        stop = stop or threading.Event()
        while not stop.is_set():
            t0 = self.monotonic()
            try:
                self.krok()
            except Exception as exc:
                print(f"[CENTRUM] blad dziennika energii: {exc}", flush=True)
            stop.wait(max(0.0, interval - (self.monotonic() - t0)))
        with self.lock:      # close: the current (incomplete) minute does not vanish
            self._flush()

    def podsumowanie(self, teraz: float | None = None) -> dict:
        """To the WS state: current power + aggregates (aggregates at most once per 30 s — SQL not on every tick)."""
        with self.lock:
            s = self.ostatni
        cfg = self.config()
        out = dict(self._agregaty_cache(teraz))     # agregaty (cache 30 s)
        out["psu_sprawnosc"] = self.sprawnosc()
        out["psu_gpu_sprawnosc"] = cfg["psu_gpu_sprawnosc"]
        out["urzadzenia"] = cfg["urzadzenia"]
        out["urzadzenia_w"] = cfg["urzadzenia_w"]
        if s:                                       # biezaca moc > wartosci z agregatow (agregaty: None)
            out["teraz_w"] = round(s["dc_w"], 1)
            out["teraz_w_gniazdko"] = round(s["gniazdko_w"], 1)
            out["szacunek"] = bool(s["szacunek"])
            out["psu_glowny_w"] = round(s.get("psu_glowny_w", 0.0), 1)
            out["psu_gpu_w"] = round(s.get("psu_gpu_w", 0.0), 1)
            osobne = self.config()["gpu_osobny_psu"]     # kazda karta z wlasnego odczytu NVML
            out["psu_gpu_karty"] = {str(i): round(w, 1) for i, w in sorted((s.get("gpu") or {}).items()) if i in osobne}
        return out

    def _agregaty_cache(self, teraz: float | None = None) -> dict:
        t = time.time() if teraz is None else float(teraz)
        if self._agr is None or t - self._agr_t >= 30.0:
            self._agr_t, self._agr = t, agregaty(self.db_path, teraz=t)
        return self._agr

    def zamknij(self) -> None:
        with self.lock:
            self._flush()
        try:
            self._con.close()
        except sqlite3.Error:
            pass


# ---------- agregaty (API /api/energia) ----------

def _polacz(db_path):
    con = sqlite3.connect(str(db_path), timeout=5)
    con.row_factory = sqlite3.Row
    return con


def agregaty(db_path: Path | None = None, teraz: float | None = None, godziny: int = 48) -> dict:
    p = Path(db_path) if db_path else DB_PATH
    t = time.time() if teraz is None else float(teraz)
    out = {"teraz_w": None, "teraz_w_gniazdko": None, "szacunek": None,
           "ostatnia_godzina_wh": 0.0, "dzis_kwh": 0.0, "miesiac_kwh": 0.0,
           "dzis_urzadzenia_kwh": 0.0, "miesiac_urzadzenia_kwh": 0.0,
           "miesiac_sred_w": 0.0, "miesiac_godzin_pracy": 0, "ostatnia_godzina_sred_w": 0.0, "godziny": [], "dni": []}
    if not p.is_file():
        return out
    con = _polacz(p)
    try:
        _zapewnij_kolumne(con)

        def suma(kolumna: str, where: str, *args) -> float:
            try:
                return float(con.execute(
                    f"SELECT COALESCE(SUM({kolumna}),0) FROM minuty WHERE {where}", args).fetchone()[0])
            except sqlite3.Error:
                return 0.0

        def sumuj(where: str, *args) -> float:
            return suma("wh_gniazdko", where, *args)

        dzis_where = "date(ts_min,'unixepoch','localtime') = date(?,'unixepoch','localtime')"
        mies_where = ("strftime('%Y-%m',ts_min,'unixepoch','localtime')"
                      " = strftime('%Y-%m',?,'unixepoch','localtime')")
        out["ostatnia_godzina_wh"] = round(sumuj("ts_min > ? AND ts_min <= ?", t - 3600, t), 3)
        out["dzis_kwh"] = round(sumuj(dzis_where, t) / 1000, 4)
        out["miesiac_kwh"] = round(sumuj(mies_where, t) / 1000, 4)
        out["dzis_urzadzenia_kwh"] = round(suma("urzadzenia_wh", dzis_where, t) / 1000, 4)
        out["miesiac_urzadzenia_kwh"] = round(suma("urzadzenia_wh", mies_where, t) / 1000, 4)
        def sekundy(where: str, *args) -> float:
            try:
                return float(con.execute(
                    f"SELECT COALESCE(SUM(sekund),0) FROM minuty WHERE {where}", args).fetchone()[0])
            except sqlite3.Error:
                return 0.0

        # srednia na godzine = energia / FAKTYCZNIE zmierzony czas (Wh/h = W); przerwa = brak wierszy, nie zera
        s_godz = sekundy("ts_min > ? AND ts_min <= ?", t - 3600, t)
        out["ostatnia_godzina_sred_w"] = round(out["ostatnia_godzina_wh"] * 3600 / s_godz, 1) if s_godz else 0.0
        miesiac = "strftime('%Y-%m',ts_min,'unixepoch','localtime') = strftime('%Y-%m',?,'unixepoch','localtime')"
        s_mies = sekundy(miesiac, t)
        out["miesiac_godzin_pracy"] = round(s_mies / 3600, 2)
        out["miesiac_sred_w"] = round(out["miesiac_kwh"] * 1000 * 3600 / s_mies, 1) if s_mies else 0.0

        h0 = int(t // 3600) * 3600
        start = h0 - (godziny - 1) * 3600
        wh_godziny = {h: 0.0 for h in range(start, h0 + 3600, 3600)}
        try:
            for r in con.execute("SELECT ts_min, wh_gniazdko FROM minuty WHERE ts_min >= ?", (start,)):
                h = int(r["ts_min"] // 3600) * 3600
                if h in wh_godziny:
                    wh_godziny[h] += r["wh_gniazdko"]
        except sqlite3.Error:
            pass
        out["godziny"] = [{"ts": h, "wh": round(v, 3)} for h, v in sorted(wh_godziny.items())]

        dni: dict[str, float] = {}
        try:
            for r in con.execute(
                    "SELECT date(ts_min,'unixepoch','localtime') AS d, wh_gniazdko FROM minuty"
                    " WHERE strftime('%Y-%m',ts_min,'unixepoch','localtime')"
                    " = strftime('%Y-%m',?,'unixepoch','localtime')", (t,)):
                if r["d"]:
                    dni[r["d"]] = dni.get(r["d"], 0.0) + r["wh_gniazdko"]
        except sqlite3.Error:
            pass
        out["dni"] = [{"data": d, "kwh": round(v / 1000, 4)} for d, v in sorted(dni.items())]
    finally:
        con.close()
    return out


# ---------- history fill-in: always-on devices ----------

def uzupelnij_historie(db_path: Path | None = None, urzadzenia_w: float | None = None,
                       kopia: bool = True) -> dict:
    """Adds always-on devices (monitors, audio) to ALL saved minutes.

    Rows without `urzadzenia_wh` (NULL) get this value computed from actually
    measured seconds; `wh_gniazdko` grows by the same energy. The function is
    idempotent — a second call changes nothing. Before changing, it makes a copy
    of the DB (`energia.db.przed-urzadzenia-RRRRMMDD-GGMMSS`).
    """
    p = Path(db_path) if db_path else DB_PATH
    w = _liczba(urzadzenia_w) if urzadzenia_w is not None else load_config()["urzadzenia_w"]
    wynik = {"plik": str(p), "urzadzenia_w": round(w, 3), "wiersze": 0, "wh": 0.0, "kwh": 0.0, "kopia": None}
    if not p.is_file() or w <= 0:
        return wynik
    con = sqlite3.connect(str(p), timeout=10)
    try:
        _zapewnij_kolumne(con)
        n = int(con.execute(
            "SELECT COUNT(*) FROM minuty WHERE urzadzenia_wh IS NULL").fetchone()[0])
        if not n:
            return wynik
        if kopia:
            cel = p.with_name(f"{p.name}.przed-urzadzenia-{datetime.now():%Y%m%d-%H%M%S}")
            shutil.copy2(p, cel)
            wynik["kopia"] = str(cel)
        con.execute(
            "UPDATE minuty SET"
            " urzadzenia_wh = ROUND(? * COALESCE(sekund, 0) / 3600.0, 4),"
            " wh_gniazdko = ROUND(COALESCE(wh_gniazdko, 0) + ? * COALESCE(sekund, 0) / 3600.0, 4)"
            " WHERE urzadzenia_wh IS NULL", (w, w))
        con.commit()
        wynik["wiersze"] = n
        wynik["wh"] = round(float(con.execute(
            "SELECT COALESCE(SUM(urzadzenia_wh), 0) FROM minuty").fetchone()[0]), 3)
        wynik["kwh"] = round(wynik["wh"] / 1000, 6)
    finally:
        con.close()
    return wynik


# ---------- eksport CSV (minuty) ----------

def _ts_dnia(s: str, koniec: bool = False) -> int | None:
    try:
        d = datetime.strptime(str(s).strip(), "%Y-%m-%d")
    except (TypeError, ValueError):
        return None
    ts = int(d.timestamp())
    return ts + 86399 if koniec else ts


def csv_rows(db_path: Path | None = None, od=None, do=None) -> list[list[str]]:
    p = Path(db_path) if db_path else DB_PATH
    rows = [["ts", "ts_min", "wh_dc", "wh_gniazdko", "w_sred", "w_max",
             "gpu0_wh", "gpu1_wh", "gpu2_wh", "sekund", "urzadzenia_wh"]]
    if not p.is_file():
        return rows
    where, args = "", []
    if od not in (None, ""):
        ts = _ts_dnia(od)
        if ts is not None:
            where, _ = "WHERE ts_min >= ?", [ts]
    if do not in (None, ""):
        ts = _ts_dnia(do, koniec=True)
        if ts is not None:
            where += " AND ts_min <= ?" if where else "WHERE ts_min <= ?"
            args.append(ts)
    con = _polacz(p)
    try:
        _zapewnij_kolumne(con)
        for r in con.execute(f"SELECT * FROM minuty {where} ORDER BY ts_min", args):
            rows.append([
                datetime.fromtimestamp(r["ts_min"]).strftime("%Y-%m-%d %H:%M"),
                str(r["ts_min"]), *[f"{r[k] if r[k] is not None else 0:.4f}" for k in
                                    ("wh_dc", "wh_gniazdko", "w_sred", "w_max",
                                     "gpu0_wh", "gpu1_wh", "gpu2_wh")],
                str(r["sekund"]),
                f"{r['urzadzenia_wh'] if r['urzadzenia_wh'] is not None else 0:.4f}"])
    except sqlite3.Error as exc:
        print(f"[CENTRUM] blad eksportu CSV: {exc}", flush=True)
    finally:
        con.close()
    return rows


def csv_tekst(db_path: Path | None = None, od=None, do=None) -> str:
    import csv
    import io
    buf = io.StringIO()
    csv.writer(buf).writerows(csv_rows(db_path, od, do))
    return buf.getvalue()


def dzis() -> str:
    return date.today().isoformat()


if __name__ == "__main__":      # python -m centrum.energia --uzupelnij [--moc W]
    import argparse
    import json

    ap = argparse.ArgumentParser(description="Dziennik energii Centrum Misji")
    ap.add_argument("--uzupelnij", action="store_true",
                    help="dolicz urządzenia stale do zapisanej historii (idempotentnie)")
    ap.add_argument("--moc", type=float, default=None,
                    help="łączna moc urządzeń [W] (domyślnie z config.yaml)")
    ap.add_argument("--bez-kopii", action="store_true", help="nie rób kopii bazy przed zmianą")
    args = ap.parse_args()
    if args.uzupelnij:
        print(json.dumps(uzupelnij_historie(urzadzenia_w=args.moc, kopia=not args.bez_kopii),
                         ensure_ascii=False, indent=1))
    else:
        ap.print_help()
