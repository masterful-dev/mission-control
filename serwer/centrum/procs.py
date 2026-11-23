"""Start/stop/kill grup procesow modeli, logi, zdrowie, sieroty (SPEC §5).

Stan dzialajacych modeli trafia do state.json — restart backendu nie gubi modeli.
"""
from __future__ import annotations

import json
import os
import queue
import re
import signal
import subprocess
import sys
import threading
import time
from pathlib import Path

import httpx
import psutil

from . import energia, planner, registry, telemetry

BASE_DIR = Path(__file__).resolve().parent.parent
STATE_PATH = Path(os.environ.get("CENTRUM_STATE") or BASE_DIR / "state.json")
LOGS_DIR = BASE_DIR / "logs"
LLMPY_PATH = Path.home() / "ai/serwer/llm.py"
LLM_STAN_DIR = Path(os.environ.get("LLM_STAN_DIR") or Path.home() / "ai/serwer")

TERM_GRACE_S = 8.0     # SIGTERM -> SIGKILL
KILL_WAIT_S = 5.0
VRAM_WAIT_S = 30.0     # czekanie az NVML przestanie pokazywac procesy modelu
HEALTH_TTL = 1.0
LOG_TAIL_BYTES = 256_000
LOG_KEEP = 20          # logs/<id>-*.log — najwyzej tyle plikow na model (reszta kasowana przy starcie)
LAUNCHER_STAN_TIMEOUT_S = 180.0  # strata 125B laduje sie dlugo — nie porzucaj launchera po 20 s
CACHE_MAX = 512        # _children_cache/_protected_cache nie moga rosnac bez limitu

# Procesy, ktorych Centrum NIGDY nie zabija (pulpit, tlumacz PTT, agent Pi)
PROTECTED_NAMES = {"xorg", "x", "xwayland", "gnome-shell", "gnome-session-binary",
                   "gnome-terminal-server", "brave", "brave-browser", "pi", "pi-desktop", "node",
                   "pipewire", "pipewire-pulse", "wireplumber", "pulseaudio", "xdg-desktop-portal"}
PROTECTED_SUBSTRINGS = ("pi-coding-agent", "gnome", "brave", "xorg", "wayland", "pipewire",
                        "plasma", "xfce", "centrum/app", "uvicorn")

RUNNING_STATUSES = ("starting", "ready", "stopping")
STAN_TTL = 0.5        # pliki stanu llm.py — odczytywane co tick /ws
CHILDREN_TTL = 1.0
STAN_START_WINDOW_S = 120.0  # PID z pliku stanu musi startowac w ciagu 120 s od 'start' (inaczej to obcy proces po restarcie)
STAN_CLOCK_TOLERANCE_S = 60.0  # how much EARLIER than 'start' the process may have started (state-write delay)
BOOT_GRACE_S = 90.0   # wpis starszy niz uptime systemu = stan sprzed rebootu (PID-y sa juz inne)

_lock = threading.Lock()
_health_cache: dict[str, tuple[float, bool]] = {}
_stan_cache: dict[str, tuple[float, dict | None]] = {}
_children_cache: dict[int, tuple[float, set[int]]] = {}
_protected_cache: dict[int, tuple[float, bool]] = {}
_ppid_cache: tuple[float, dict[int, list[int]]] | None = None
PPID_MIN_REBUILD_S = 1.0   # minimalny odstep przed PRZYMUSENIM odswiezeniem drzewa /proc


def _cache_put(store: dict, key, value) -> None:
    """Write to the PID cache with an upper limit (otherwise the dict would grow for years)."""
    if len(store) >= CACHE_MAX:
        store.clear()
    store[key] = value


def ppid_map(force: bool = False) -> dict[int, list[int]]:
    """{pid: [dzieci]} — caly /proc czytany RAZ na tick (wczesniej: 4x na stan).

    Kazdy zywy PID jest kluczem (teze bez dzieci) — dzieki temu `pid not in mapa` oznacza
    przedawniona mape, a nie tylko 'proces bez dzieci'."""
    global _ppid_cache
    now = time.monotonic()
    if _ppid_cache:
        wiek = now - _ppid_cache[0]
        if wiek < CHILDREN_TTL and not (force and wiek >= PPID_MIN_REBUILD_S):
            return _ppid_cache[1]
    kids: dict[int, list[int]] = {}
    # surowy /proc/*/stat (bez psutil.Process — 470 procesow x psutil = ~40 ms, /proc = ~6 ms)
    try:
        entries = os.listdir("/proc")
    except OSError:
        entries = []
    for entry in entries:
        if not entry.isdigit():
            continue
        pid = int(entry)
        try:
            with open(f"/proc/{entry}/stat", "rb") as fh:
                data = fh.read()
        except OSError:
            continue
        try:
            ppid = int(data[data.rfind(b")") + 2:].split()[1])
        except (IndexError, ValueError):
            continue
        kids.setdefault(pid, [])
        kids.setdefault(ppid, []).append(pid)
    _ppid_cache = (now, kids)
    return kids


def _proc_start(pid: int) -> float | None:
    try:
        return psutil.Process(int(pid)).create_time()
    except psutil.Error:
        return None


class ProcError(Exception):
    pass


# ---------- state.json ----------

def load_state() -> dict:
    try:
        data = json.loads(STATE_PATH.read_text())
    except (OSError, ValueError):
        return {}
    return data if isinstance(data, dict) else {}


def save_state(state: dict) -> None:
    STATE_PATH.parent.mkdir(parents=True, exist_ok=True)
    tmp = STATE_PATH.with_suffix(".json.tmp")
    tmp.write_text(json.dumps(state, indent=1, ensure_ascii=False))
    os.replace(tmp, STATE_PATH)


# ---------- pomocnicze ----------

def _alive(pid) -> bool:
    """Zombie (zabity, ale nie odebrany przez rodzica) liczy sie jako NIE zywy."""
    if not pid:
        return False
    try:
        os.kill(int(pid), 0)
    except (OSError, ValueError):
        return False
    try:
        state = Path(f"/proc/{int(pid)}/stat").read_text().rsplit(")", 1)[-1].split()[0]
    except (OSError, IndexError):
        return False
    return state != "Z"


def _reap(pid) -> None:
    """Zbierz wlasne osierodzone dzieci (zeby nie zostawiac zombie w liscie procesow)."""
    try:
        os.waitpid(int(pid), os.WNOHANG)
    except (ChildProcessError, OSError, ValueError, TypeError):   # TypeError: wpis bez PID (16.10: STOP = 500)
        pass


def _entry_pids(entry: dict) -> set[int]:
    return {int(entry[k]) for k in ("pid", "proxy_pid") if entry.get(k)}


def entry_identity(entry: dict) -> dict:
    """Odcisk palca PID-ow wpisu: {pid: create_time} — zapisany przy starcie.

    Po restarcie komputera PID bywa przejety przez nowy proces (np. gnome-shell) — create_time
    sie rozni, wiec Centrum NIE wysyla SIGKILL do przypadkowego procesu."""
    out: dict[int, float] = {}
    for pid in _entry_pids(entry):
        start = _proc_start(pid)
        if start is not None:
            out[pid] = start
    return out


def entry_same_process(entry: dict) -> bool:
    """Czy PID-y z state.json to NADAL te same procesy (ten sam create_time co przy starcie)."""
    saved = entry.get("pid_start") or {}
    if not saved:
        return True  # stary wpis bez odcisku palca — nie mozna weryfikowac (zachowanie jak wczesniej)
    current = entry_identity(entry)
    for pid_str, start in saved.items():
        try:
            pid = int(pid_str)
        except (TypeError, ValueError):
            continue
        if pid not in current:
            return False  # proces umarl — wpis i tak zostanie usuniety
        if abs(current[pid] - float(start)) > 1.0:
            return False  # ten sam PID, INNY proces (przejety po restarcie)
    return True


def _proc_children(pid: int) -> list[int] | None:
    """Dzieci procesu z /proc/<pid>/task/<pid>/children (tanio, ~0,1 ms); None = kernel nie udomawia."""
    try:
        with open(f"/proc/{pid}/task/{pid}/children", "rb") as fh:
            return [int(t) for t in fh.read().split()]
    except OSError:
        return None


def _children(pid, kids: dict[int, list[int]] | None = None) -> set[int]:
    """Wszyscy potomkowie (drzewo procesu modelu) — z drzewa ppid liczonego raz na tick."""
    if not pid:
        return set()
    pid = int(pid)
    if kids is None:
        # bez mapy z ticka: mapa moze byc starsza niz proces (nowo postawiony model) —
        # drzewo bierzemy bezposrednio z psutil, nie z przedawnionej mapy
        try:
            return {c.pid for c in psutil.Process(pid).children(recursive=True)}
        except psutil.Error:
            return set()
    out, queue = set(), list(kids.get(pid, []))
    if not out:
        # mapa nie zna dzieci (liczona PRZED startem procesu — tezy caly PID nie jest w niej
        # kluczem) — dorzuca je tanio z /proc, inaczej nowo postawiony model traci rezerwacje,
        # jego dzieci staja sie sierotami a STOP nie zabija calej grupy (SPEC §5)
        direct = _proc_children(pid)
        if direct:
            kids[pid] = list(direct)
            queue = list(direct)
    while queue:
        child = queue.pop()
        if child in out:
            continue
        out.add(child)
        queue.extend(kids.get(child, []))
    return out


def _entry_all_pids(entry: dict, kids: dict[int, list[int]] | None = None) -> set[int]:
    pids = _entry_pids(entry)
    out = set(pids)
    for pid in pids:
        out |= _children(pid, kids)
    return out


def _protected(pid: int) -> bool:
    now = time.monotonic()
    hit = _protected_cache.get(pid)
    if hit and now - hit[0] < CHILDREN_TTL:
        return hit[1]
    try:
        p = psutil.Process(pid)
        name = p.name().lower()
        cmd = " ".join(p.cmdline()).lower()
    except psutil.Error:
        out = False
    else:
        out = name in PROTECTED_NAMES or any(s in cmd for s in PROTECTED_SUBSTRINGS)
    _cache_put(_protected_cache, pid, (now, out))
    return out


def is_system(pid: int, gpu_index) -> bool:
    """Proces pokazany jako „system" (szary, bez przycisku Zwolnij): GPU2 lub lista chroniona."""
    return gpu_index == 2 or _protected(pid)


# ---------- przejmowanie modeli uruchomionych poza Centrum (pliki stanu launcherow) ----------

def stan_path(model: dict) -> Path:
    """Sciezka pliku stanu launchera (SPEC §11a: pole `stan`, domyslnie stan-<port>.json)."""
    name = str(model.get("stan") or f"stan-{int(model.get('port') or 0)}.json")
    p = Path(name)
    return p if p.is_absolute() else LLM_STAN_DIR / name


def read_stan(model: dict) -> dict | None:
    key = str(stan_path(model))
    now = time.monotonic()
    hit = _stan_cache.get(key)
    if hit and now - hit[0] < STAN_TTL:
        return hit[1]
    try:
        data = json.loads(stan_path(model).read_text())
        data = data if isinstance(data, dict) else None
    except (OSError, ValueError):
        data = None
    _stan_cache[key] = (now, data)
    return data


def stan_match_keys(model: dict) -> dict:
    """Klucze pliku stanu, ktore musza sie zgadzac (SPEC §11a); dla profilu bez definicji — 'profil'."""
    match = model.get("stan_match")
    if match:
        return match
    return {"profil": model["profile"]} if model.get("profile") else {}


def stan_matches(model: dict, data: dict) -> bool:
    """On a shared port (8033, 8190) a model 'runs' only if the profile/mode in the state file matches."""
    for key, val in stan_match_keys(model).items():
        got = data.get(key)
        if str(got) != str(val) and not (isinstance(got, (int, float)) and str(val) == str(int(got))):
            return False
    return True


def launcher_names(model: dict) -> set[str]:
    """Nazwy skryptow launchera (yue2.py, llm.py, obrazy.py, decyzja.py, llm_geoff.py, ...) — do weryfikacji PID.

    Dodawana jest teze nazwa bez '.py' (argv0 wrappera `bash -c "$@" ...` to `llm`/`obrazy`/`yue2`)."""
    names = set()
    script = model.get("script") or (LLMPY_PATH if model.get("kind") == "llmpy" or model.get("profile") else None)
    if script:
        name = Path(str(script)).name
        names.add(name)
        if name.endswith(".py"):
            names.add(name[:-3])
    for arg in (model.get("args") or ([model["profile"]] if model.get("profile") else [])):
        name = Path(str(arg)).name
        if name.endswith(".py"):
            names.add(name)
            names.add(name[:-3])
    return names


def stan_pid_to_model(model: dict, data: dict) -> bool:
    """Is the PID in the state file really this launcher (not a foreign process after a computer restart).

    Launcher script name in cmdline (also argv0 of the wrapper `bash -c "$@" ... llm`) OR a process
    started NOT EARLIER than the saved 'start' (tolerance window only for clock delay).
    A process created BEFORE 'start' is at best a pre-reboot process — not this model."""
    names = launcher_names(model)
    pid = int(data.get("pid") or 0)
    try:
        proc = psutil.Process(pid)
        cmd_names = {Path(tok).name for tok in proc.cmdline()}
        create_time = proc.create_time()
    except (psutil.Error, OSError, ValueError, TypeError):
        return False
    try:
        start = float(data.get("start") or 0.0)
    except (TypeError, ValueError):
        start = 0.0
    if bool(start) and create_time > start + STAN_START_WINDOW_S:
        return False  # proces startowal znacznie pozniej niz zapisany 'start' -> obcy PID
    if names & cmd_names:
        return True
    if bool(start):
        # jednokierunkowe okno: proces utworzony PRZED 'start' (ponizej tolerancji) to proces
        # sprzed rebootu/obcy — nie ten model (abs() akceptowalo tez obce PID-y)
        return create_time >= start - STAN_CLOCK_TOLERANCE_S
    return False  # brak 'start' i brak nazwy launchera w cmdline — brak dowodu, ze to ten launcher


def adopted_entry(model: dict) -> dict | None:
    """Model z rejestru uruchomiony poza Centrum: plik stanu launchera i zywy PID -> wpis stanu."""
    if not model or model.get("kind") not in ("launcher", "llmpy"):
        return None
    data = read_stan(model)
    if not data or not _alive(data.get("pid")) or not stan_matches(model, data):
        return None
    if not stan_pid_to_model(model, data):  # PID z pliku stanu to inny proces -> stan nieaktualny
        return None
    return {"kind": "launcher", "profile": model.get("profile"), "pid": data["pid"],
            "proxy_pid": data.get("proxy_pid"), "pgid": None, "port": data.get("port") or model.get("port"),
            "log": data.get("log"), "started": data.get("start"), "status": "starting", "adopted": True}


def _measured_placement(pids: set[int]) -> dict | None:
    """Rozmieszczenie przejetego modelu — z tego, co NVML faktycznie pokazuje na kartach."""
    per: dict[int, int] = {}
    for g in telemetry.read_gpus():
        for pr in g.get("processes") or []:
            if pr.get("pid") in pids:
                per[g["index"]] = per.get(g["index"], 0) + int(pr.get("vram_mb") or 0)
    if not per:
        return None
    gpus = sorted(per)
    return {"gpus": gpus, "vram": [per[g] for g in gpus]}


def known_pids(models: list[dict] | None = None, state: dict | None = None) -> set[int]:
    """Wszystkie PID-y nalezace do modeli Centrum (takze przejetych) — nie sa sierotami."""
    kids = ppid_map()
    pids: set[int] = set()
    for entry in (state if state is not None else load_state()).values():
        pids |= _entry_all_pids(entry, kids)
    if state is None:
        for model in (models if models is not None else registry.load()):
            entry = adopted_entry(model)
            if entry:
                pids |= _entry_all_pids(entry, kids)
    return pids


def zmierzony_vram(state: dict, gpus: list[dict]) -> dict[str, dict[int, int]]:
    """{id modelu: {karta: MiB}} — VRAM procesow modelu wg NVML (do planu przelaczenia: co sie zwolni)."""
    kids = ppid_map()
    wlasciciel = {pid: mid for mid, entry in state.items() for pid in _entry_all_pids(entry, kids)}
    out: dict[str, dict[int, int]] = {}
    for g in gpus:
        for pr in g.get("processes") or []:
            mid = wlasciciel.get(pr.get("pid"))
            if mid:
                d = out.setdefault(mid, {})
                d[g["index"]] = d.get(g["index"], 0) + int(pr.get("vram_mb") or 0)
    return out


def zmierzony_ram(state: dict) -> dict[str, float]:
    """{id modelu: GB} — RAM procesow modelu (RssAnon + RssShmem; strony plikow system odda sam).
    Do planu przelaczenia: Ornith TP2 ma w rejestrze 2 GB, a trzyma ~20 GB (2 rangi po 1,6 anon + 8,7 shmem)."""
    kids = ppid_map()
    out: dict[str, float] = {}
    for mid, entry in state.items():
        if entry.get("pending"):
            continue
        kb = 0
        for pid in _entry_all_pids(entry, kids):
            try:
                with open(f"/proc/{int(pid)}/status") as f:
                    for line in f:
                        if line.startswith(("RssAnon:", "RssShmem:")):
                            kb += int(line.split()[1])
            except (OSError, ValueError, TypeError):
                pass
        if kb:
            out[mid] = kb / 1048576
    return out


def pid_owner(models: list[dict], state: dict) -> dict[int, str]:
    """{pid: nazwa modelu} — podpis procesow GPU (drzewo procesu wliczajac potomkow)."""
    by_id = {m["id"]: m for m in models}
    kids = ppid_map()
    owner: dict[int, str] = {}
    for mid, entry in state.items():
        name = (by_id.get(mid) or {}).get("name") or mid
        for pid in _entry_all_pids(entry, kids):
            owner[pid] = name
    return owner


def _read_log(path, n: int) -> list[str]:
    if not path:
        return []
    p = Path(path)
    try:
        with open(p, "rb") as f:
            f.seek(max(0, p.stat().st_size - LOG_TAIL_BYTES))
            data = f.read()
    except OSError:
        return []
    return [ln.rstrip() for ln in data.decode("utf-8", "replace").splitlines()[-n:]]


def _gpu_pids(pids: set[int]) -> dict[int, int]:
    """Ktora z PID-ow jest teraz widoczna na GPU (NVML) -> {pid: index karty}."""
    found: dict[int, int] = {}
    for g in telemetry.read_gpus():
        for pr in g.get("processes") or []:
            if pr.get("pid") in pids:
                found[pr["pid"]] = g.get("index")
    return found


def _sub(text: str, subs: dict[str, str]) -> str:
    for key, val in subs.items():
        text = text.replace("{" + key + "}", val)
    return text


def _base_env(model: dict, placement: dict) -> dict:
    env = dict(os.environ)
    # zmienne samej uslugi danych nie ida do modeli (jej biblioteki zepsulyby srodowisko Pythona modelu)
    for klucz in ("PYTHONPATH", "PYTHONDONTWRITEBYTECODE", "CENTRUM_DANE", "CENTRUM_STATE", "CENTRUM_PORT"):
        env.pop(klucz, None)
    env["CUDA_DEVICE_ORDER"] = "PCI_BUS_ID"
    gpus = placement.get("gpus") or []
    if gpus:
        env["CUDA_VISIBLE_DEVICES"] = ",".join(str(g) for g in gpus)
    for key, val in (model.get("env") or {}).items():
        env[str(key)] = str(val)
    return env


def _gpu_env(model: dict) -> str | None:
    """Zmienna, ktora launcher wykorzystuje do wyboru karty (SPEC §11a); None = launcher sam wybiera."""
    if model.get("gpu_env") is None:
        return None
    return model.get("gpu_env") or "CUDA_VISIBLE_DEVICES"


def _launcher_cmd(model: dict, gpus: list[int]) -> list[str]:
    script = model.get("script") or (LLMPY_PATH if model.get("profile") else None)
    if script and Path(script) == Path(registry.LLMPY_PATH):
        script = LLMPY_PATH  # sciezka llm.py zawsze biezaca (teze w testach z atrapa)
    if not script:
        raise ProcError(f"model {model.get('id')} nie ma skryptu launchera")
    args = model.get("args") or ([model["profile"]] if model.get("profile") else [])
    return [sys.executable or "python3", str(script)] + [str(a) for a in args]


def _log_path(model_id: str) -> str:
    LOGS_DIR.mkdir(parents=True, exist_ok=True)
    path = LOGS_DIR / f"{model_id}-{time.strftime('%Y%m%d-%H%M%S')}.log"
    # logs/<id>-*.log nie moga rosnac latami — zostawiamy ostatnie LOG_KEEP plikow danego modelu
    try:
        stare = sorted(LOGS_DIR.glob(f"{model_id}-*.log"), key=lambda p: p.stat().st_mtime)
        for stary in stare[:-LOG_KEEP]:
            try:
                stary.unlink()
            except OSError:
                pass
    except OSError:
        pass
    return str(path)


# ---------- start ----------

def start(model: dict, placement_index: int = 0) -> dict:
    mid = model["id"]
    with _lock:
        state = load_state()
        entry = state.get(mid)
        if entry and _alive(entry.get("pid")):
            return entry  # idempotentne — model juz dziala
        state.pop(mid, None)
        placements = model.get("placements") or []
        if not placements:
            raise ProcError("model nie ma wariantu rozmieszczenia")
        placement = placements[min(max(int(placement_index), 0), len(placements) - 1)]
        kind = model.get("kind") or "cmd"
        try:
            entry = _start_launcher(model, placement) if kind in ("launcher", "llmpy") else _start_cmd(model, placement)
        except ProcError:
            save_state(state)
            raise
        entry["placement"] = placement
        entry["started"] = time.time()
        entry["status"] = "starting"
        state[mid] = entry
        save_state(state)
        _health_cache.pop(mid, None)
        return entry


def _start_cmd(model: dict, placement: dict) -> dict:
    raw = [str(c) for c in (model.get("cmd") or [])]
    if not raw:
        raise ProcError("kind=cmd wymaga komendy")
    subs = {"port": str(model.get("port") or ""), "gpus": ",".join(str(g) for g in placement.get("gpus") or []),
            "ctx": str(model.get("ctx") or "")}
    cmd = [_sub(c, subs) for c in raw] + [str(a) for a in (model.get("extra_args") or [])]
    log = _log_path(model["id"])
    env = _base_env(model, placement)
    with open(log, "ab") as lf:
        proc = subprocess.Popen(cmd, env=env, cwd=model.get("cwd") or None, stdout=lf,
                                stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                                start_new_session=True)
    try:
        pgid = os.getpgid(proc.pid)
    except OSError:
        pgid = None
    entry = {"kind": "cmd", "pid": proc.pid, "pgid": pgid, "port": model.get("port"), "log": log}
    entry["pid_start"] = {str(k): v for k, v in entry_identity(entry).items()}  # odcisk palca
    return entry


def _start_launcher(model: dict, placement: dict) -> dict:
    """Start launchera (SPEC §11a): `python3 <skrypt> <args> --headless`; stan = plik stan-*.json."""
    port = int(model.get("port") or 0) or None
    gpus = [int(g) for g in (placement.get("gpus") or [])]
    env = _base_env(model, placement)
    gpu_env = _gpu_env(model)
    if gpu_env and gpus:
        # LLM_GPUS/CUDA_VISIBLE_DEVICES = lista kart; GEOFF_GPU/OBRAZY_GPU/DECYZJA_GPU = jedna karta
        env[gpu_env] = (",".join(str(g) for g in gpus)
                        if gpu_env in ("CUDA_VISIBLE_DEVICES", "LLM_GPUS") else str(gpus[0]))
    else:
        env.pop("CUDA_VISIBLE_DEVICES", None)  # launcher sam wybiera karte
    if model.get("port_env") and port:
        env[model["port_env"]] = str(port)
    if model.get("ctx"):
        env["LLM_CTX"] = str(model["ctx"])
    if model.get("extra_args"):
        env["LLM_EXTRA_ARGS"] = " ".join(str(a) for a in model["extra_args"])
    launcher_log = _log_path(model["id"])
    with open(launcher_log, "ab") as lf:
        launcher = subprocess.Popen(_launcher_cmd(model, gpus) + ["--headless"], env=env, stdout=lf,
                                    stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, start_new_session=True)
    # launcher zapisuje stan w pliku zaleznym od portu/trybu — dwa modele na tym samym porcie sie nie gryza
    # strata 125B laduje sie dlugo — dlugi deadline; po jego przekroczeniu zabijamy launchera,
    # zeby nie zostawic polowicznie uruchomionego procesu trzymajacego VRAM/pidfile
    deadline = time.monotonic() + LAUNCHER_STAN_TIMEOUT_S

    def wpis_ze_stanu() -> dict | None:
        data = read_stan(model)
        if data and _alive(data.get("pid")) and stan_matches(model, data) and stan_pid_to_model(model, data):
            entry = {"kind": "launcher", "profile": model.get("profile"), "pid": data["pid"],
                     "proxy_pid": data.get("proxy_pid"), "pgid": None, "port": data.get("port") or port,
                     "log": data.get("log"), "launcher_log": launcher_log}
            entry["pid_start"] = {str(k): v for k, v in entry_identity(entry).items()}
            return entry
        return None

    while time.monotonic() < deadline:
        entry = wpis_ze_stanu()
        if entry:
            return entry
        if launcher.poll() is not None:
            # 15.10: launcher zapisuje stan i od razu wychodzi (kod 0) — mogl to zrobic MIEDZY odczytem stanu
            # a poll(); bez ponownego odczytu udany start konczyl sie komunikatem „nie uruchomil modelu”
            _stan_cache.pop(str(stan_path(model)), None)
            entry = wpis_ze_stanu()
            if entry:
                return entry
            tail = " | ".join(_read_log(launcher_log, 3))
            profil = f" (profil {model['profile']})" if model.get("profile") else ""
            raise ProcError(f"launcher {Path(str(model.get('script') or LLMPY_PATH)).name} nie uruchomil "
                            f"modelu {model.get('id')}{profil} — kod {launcher.returncode}: {tail[:200]}")
        _stan_cache.pop(str(stan_path(model)), None)
        time.sleep(0.3)
    try:  # porzucamy launchera + jego drzewo (nie zostawiaj polowicznie startujacego modelu na GPU)
        pgid = os.getpgid(launcher.pid)
        os.killpg(pgid, signal.SIGKILL) if pgid == launcher.pid else launcher.kill()
    except (OSError, ProcessLookupError):
        launcher.kill()
    tail = " | ".join(_read_log(launcher_log, 3))
    raise ProcError(f"launcher nie zapisal stanu {stan_path(model).name} w ciagu "
                    f"{int(LAUNCHER_STAN_TIMEOUT_S)} s (model {model.get('id')}) — launcher zabity: {tail[:200]}")


# ---------- stop / kill ----------

def _kill_group(entry: dict, force: bool = False) -> None:
    """Zabij proces modelu — NIGDY cala grupa procesow, ktorej liderem nie jest ten proces.

    Adptowany launcher (llm.py) nie jest liderem grupy (pgid != pid); killpg(pgid) zabilby
    cala grupa procesow, ktora prowadzi do tego PID-a — np. caly terminal, z ktorego model
    zostal uruchomiony. Grupa = tylko procesy startowane przez Centrum (start_new_session)."""
    pid = int(entry.get("pid") or 0)
    if not pid:
        return
    saved_pgid = entry.get("pgid")
    sig = signal.SIGKILL if force else signal.SIGTERM
    for target in _kill_targets(entry):
        try:
            os.kill(target, sig)
        except OSError:
            pass
    if not force and not _wait_dead(pid, TERM_GRACE_S):
        for target in _kill_targets(entry):
            try:
                os.kill(target, signal.SIGKILL)
            except OSError:
                pass
    _wait_dead(pid, KILL_WAIT_S)


def _kill_targets(entry: dict) -> list[int]:
    """PID modelu + (jesli to lider grupy) cala jego grupa + potomkowie — bez grupy nadrzednej."""
    pid = int(entry.get("pid") or 0)
    targets = [pid] + [int(p) for p in (entry.get("proxy_pid"), entry.get("server_pid")) if p]
    pgid = entry.get("pgid")
    try:
        real_pgid = os.getpgid(pid)
    except OSError:
        real_pgid = None
    if pgid is not None and real_pgid is not None and pgid == pid == real_pgid:
        try:  # grupa stworzona przez Centrum (start_new_session) — liderem jest sam model
            os.killpg(pid, signal.SIGTERM)
        except OSError:
            pass
    for child in _children(pid, ppid_map()):
        if child not in targets:
            targets.append(child)
    return targets


def _wait_dead(pid: int, timeout: float) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if not _alive(pid):
            _reap(pid)
            return True
        time.sleep(0.2)
    _reap(pid)
    return not _alive(pid)


def _stop_launcher(model: dict, entry: dict, force: bool = False) -> None:
    """Stop = `python3 <skrypt> <args> --stop` (ten sam mechanizm, ktorego uzywaja launchery)."""
    env = _base_env(model, entry.get("placement") or {})
    gpu_env = _gpu_env(model)
    if not gpu_env:
        env.pop("CUDA_VISIBLE_DEVICES", None)
    if model.get("port_env") and entry.get("port"):
        env[model["port_env"]] = str(entry["port"])
    try:
        subprocess.run(_launcher_cmd(model, []) + ["--stop"], env=env,
                       timeout=30 if force else 150, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except (subprocess.TimeoutExpired, OSError) as exc:
        name = Path(str(model.get("script") or LLMPY_PATH)).name
        raise ProcError(f"{name} --stop nie zakonczyl sie: {exc}") from exc
    if force:
        for pid in _entry_pids(entry):
            try:
                os.kill(pid, signal.SIGKILL)
            except OSError:
                pass


def stop(model_id: str, force: bool = False) -> dict:
    with _lock:
        state = load_state()
        entry = state.get(model_id)
        if entry and entry.get("pending"):   # start jeszcze w kolejce — anulujemy, nic nie zostalo uruchomione
            _anulowane.add(model_id)
            state.pop(model_id, None)
            save_state(state)
            return {"id": model_id, "status": "stopped", "note": "start anulowany"}
        if not entry:
            model = registry.load_by_id(model_id)
            entry = adopted_entry(model) if model else None  # model przejety przez plik stanu — tez da sie zatrzymac
            if entry:
                state[model_id] = entry
        if not entry:
            return {"id": model_id, "status": "stopped", "note": "model nie dokonywal"}
        if _stale_entry(entry) or _pid_zajety(entry):
            # PID z state.json to juz INNY proces (przejety po restarcie komputera) — NIE zabijamy,
            # tylko usuwamy przedawniony wpis
            state.pop(model_id, None)
            save_state(state)
            _health_cache.pop(model_id, None)
            return {"id": model_id, "status": "stopped", "note": "stan przedawniony (PID przejety) — proces nietykalny"}
        pids = _entry_all_pids(entry, ppid_map())
        on_gpu = set(_gpu_pids(pids))
        model = registry.load_by_id(model_id)
        entry["status"] = "stopping"
        state[model_id] = entry
        save_state(state)
    # 16.10: the rest OUTSIDE the lock — launcher `--stop` takes up to 150 s, and during that time START/SWITCH
    # innego modelu ma przejsc od razu (zlecenie zatrzyma ten model SIGKILL-em i laduje nowy bez czekania)
    if (model or {}).get("kind") in ("launcher", "llmpy") or entry.get("kind") in ("launcher", "llmpy"):
        if model:
            try:
                _stop_launcher(model, entry, force)
            except ProcError:
                pass  # launcher nie dal rady — i tak porzadkujemy po PID-ach
        if _alive(entry.get("pid")):  # --stop nie zakonczyl procesu (zly stan / proces spoza launchera)
            _kill_group(entry, force)
    else:
        _kill_group(entry, force)
    _wait_dead(entry.get("pid"), KILL_WAIT_S)
    if on_gpu:
        _wait_vram(on_gpu)
    with _lock:
        state = load_state()
        biezacy = state.get(model_id)
        if biezacy and not biezacy.get("pending") and biezacy.get("pid") == entry.get("pid"):
            state.pop(model_id, None)   # nowy start tego samego modelu w miedzyczasie — jego wpisu nie ruszamy
            save_state(state)
    _health_cache.pop(model_id, None)
    return {"id": model_id, "status": "stopped"}


def _wait_vram(pids: set[int]) -> bool:
    """Czekaj (max VRAM_WAIT_S), az NVML przestanie pokazywac procesy modelu."""
    deadline = time.time() + VRAM_WAIT_S
    while time.time() < deadline:
        if not _gpu_pids(pids):
            return True
        time.sleep(0.5)
    return False


# ---------- stany / zdrowie ----------

# jeden klient HTTP na proces: httpx.get() tworzy nowego klienta i za kazdym razem laduje certyfikaty SSL (~19 ms)
_http = httpx.Client(timeout=1.0, verify=False, trust_env=False)


def health_ok(model: dict, entry: dict, use_cache: bool = True) -> bool:
    mid = model["id"]
    now = time.monotonic()
    hit = _health_cache.get(mid)
    if use_cache and hit and now - hit[0] < HEALTH_TTL:
        return hit[1]
    port = entry.get("port")
    ok = _alive(entry.get("pid"))
    if port:
        try:
            ok = _http.get(f"http://127.0.0.1:{int(port)}{model.get('health') or '/v1/models'}").status_code == 200
        except Exception:
            ok = False
    _health_cache[mid] = (now, ok)
    return ok


def refresh(models: list[dict]) -> dict:
    """Przeliczenie stanow (wywolywane co 250 ms przez producenta stanu). Nie blokuje, gdy trwa start/stop.

    Cale drzewo /proc czytamy RAZ na tick (ppid_map) — wczesniej kazdy model/czytnik przerzucal
    caly /proc osobno (4x /proc na stan)."""
    if not _lock.acquire(blocking=False):
        return load_state()
    try:
        kids = ppid_map(force=True)   # kazdy tick = NOWE drzewo /proc (nowe dzieci modelu widoczne od razu)
        state = load_state()
        by_id = {m["id"]: m for m in models}
        changed = False
        for mid, entry in list(state.items()):  # wpisy sprzed rebootu/awarii — PID-y sa juz inne
            if _stale_entry(entry) or not entry_same_process(entry):
                state.pop(mid)
                changed = True
        for model in models:  # adopcja: model postawiony poza Centrum (stan-<port>.json z llm.py)
            mid = model["id"]
            if mid in state or not model.get("enabled", True):
                continue
            entry = adopted_entry(model)
            if entry:
                entry["placement"] = _measured_placement(_entry_all_pids(entry, kids))
                entry["pid_start"] = {str(k): v for k, v in entry_identity(entry).items()}
                state[mid] = entry
                changed = True
        for mid, entry in list(state.items()):
            model = by_id.get(mid)
            status = entry.get("status", "starting")
            if entry.get("pending"):   # start zlecony, wykonuje go watek przelaczania (zaslepka bez PID-u)
                if mid not in _w_toku:     # zaslepka po restarcie Centrum — nikt jej juz nie wykona
                    state.pop(mid)
                    changed = True
                continue
            if entry.get("adopted") and not adopted_entry(model):
                state.pop(mid)  # model zatrzymany poza Centrum (stan-<port>.json zniknal / pid nie zyje)
                changed = True
                continue
            if status == "stopping":
                if not _alive(entry.get("pid")):
                    state.pop(mid)
                    changed = True
                continue
            if not _alive(entry.get("pid")) or (entry.get("proxy_pid") and not _alive(entry.get("proxy_pid"))):
                if status != "failed":
                    entry["status"] = "failed"
                    entry["failed_at"] = time.time()
                    entry["log_tail"] = _read_log(entry.get("log"), 20)
                    changed = True
                continue
            if model is None:
                continue
            if entry.get("adopted"):
                measured = _measured_placement(_entry_all_pids(entry, kids))
                if measured and entry.get("placement") != measured:
                    entry["placement"] = measured
                    changed = True
            if health_ok(model, entry):
                if status != "ready":
                    entry["status"] = "ready"
                    changed = True
            elif status != "starting":
                entry["status"] = "starting"
                changed = True
        if changed:
            save_state(state)
        return state
    finally:
        _lock.release()


def _stale_entry(entry: dict) -> bool:
    """Wpis zapisany wczesniej niz boot tego systemu = stan sprzed awarii/rebootu — PID-y sa juz inne."""
    try:
        started = float(entry.get("started") or 0.0)
    except (TypeError, ValueError):
        return False
    return bool(started) and started < psutil.boot_time() - 5


def _pid_zajety(entry: dict) -> bool:
    """PID z wpisu to juz INNY proces (przejety po restarcie) — Centrum nie wolno go zabijac."""
    return not entry_same_process(entry)


def running_entries(models: list[dict], state: dict) -> list[dict]:
    """Rezerwacje dla planera: modele, ktore trzymaja VRAM (przejete — z pomiaru NVML)."""
    by_id = {m["id"]: m for m in models}
    out = []
    for mid, entry in state.items():
        model = by_id.get(mid)
        if model and entry.get("status") in RUNNING_STATUSES:
            out.append({"id": mid, "model": model, "placement": entry.get("placement")})
    return out


def log_lines(model_id: str, n: int = 200) -> list[str]:
    entry = load_state().get(model_id)
    path = entry.get("log") if entry else None
    if not path or not Path(path).is_file():
        files = sorted(LOGS_DIR.glob(f"{model_id}-*.log"), key=lambda p: p.stat().st_mtime)
        path = str(files[-1]) if files else None
    return _read_log(path, n)


# ---------- switch ----------

def switch(from_id: str, to_id: str) -> dict:
    models = registry.load()
    target = next((m for m in models if m["id"] == to_id), None)
    if not target:
        raise ProcError(f"brak modelu {to_id} w rejestrze")
    stop(from_id)
    state = load_state()
    others = [e for e in running_entries(models, state) if e["id"] != to_id]
    decision = planner.plan([target], others, telemetry.read_gpus(),
                            telemetry.read_ram()["available_gb"], energia.margines_vram()).get(to_id, {})
    if not decision.get("can_start"):
        raise ProcError(decision.get("reason") or "model nie miesci sie na GPU")
    placement = decision.get("placement")
    index = next((i for i, p in enumerate(target.get("placements") or []) if p == placement), 0)
    entry = start(target, index)
    return {"stopped": from_id, "started": to_id, "placement": entry.get("placement")}


# ---------- przelaczanie bez czekania (15.10) ----------
# START / SWITCH click: state saved right away (old model "stopping", new "starting"), and ONE thread in the background
# wykonuje zlecenia po kolei: stary model SIGKILL (serwery inferencji nie maja czego zapisywac), czekanie tylko
# na zwolnienie jego VRAM (zwykle < 2 s), natychmiastowy start nowego. Blad startu = status „failed” z opisem.
SZYBKI_VRAM_S = 15.0
_kolejka: "queue.Queue[dict]" = queue.Queue()
_pracownik: threading.Thread | None = None
_prac_lock = threading.Lock()
_w_toku: set[str] = set()       # modele zlecone i jeszcze nie wystartowane (zaslepki „pending” w state.json)
_anulowane: set[str] = set()    # STOP klikniety, zanim watek zdazyl wystartowac model


def zlec_start(model: dict, placement_index: int = 0, zatrzymaj=()) -> dict:
    """Zlecenie startu (opcjonalnie po zatrzymaniu modeli `zatrzymaj`) — wraca natychmiast."""
    mid = model["id"]
    zatrzymaj = [z for z in zatrzymaj if z != mid]
    with _lock:
        state = load_state()
        biezacy = state.get(mid)
        if biezacy and (biezacy.get("pending") or _alive(biezacy.get("pid"))):
            return {"id": mid, "status": biezacy.get("status", "starting"), "zatrzymaj": [], "note": "juz startuje/dziala"}
        for z in zatrzymaj:
            if z in state:
                state[z]["status"] = "stopping"
        pl = model.get("placements") or []
        placement = pl[min(max(int(placement_index), 0), len(pl) - 1)] if pl else None
        state[mid] = {"kind": model.get("kind") or "cmd", "pending": True, "status": "starting",
                      "started": time.time(), "placement": placement, "port": model.get("port")}
        save_state(state)
        _anulowane.discard(mid)
        _w_toku.add(mid)
    _kolejka.put({"model": model, "idx": int(placement_index), "zatrzymaj": zatrzymaj})
    with _prac_lock:
        global _pracownik
        if _pracownik is None or not _pracownik.is_alive():
            _pracownik = threading.Thread(target=_petla_przelaczania, name="centrum-przelaczanie", daemon=True)
            _pracownik.start()
    return {"id": mid, "status": "starting", "zatrzymaj": zatrzymaj}


def _petla_przelaczania() -> None:
    while True:
        zadanie = _kolejka.get()
        try:
            _wykonaj_zlecenie(zadanie)
        except Exception as exc:   # watek nie moze umrzec — kolejne klikniecia musza dzialac
            print(f"[CENTRUM] blad przelaczania: {exc}", flush=True)
        finally:
            _w_toku.discard(zadanie["model"]["id"])
            _kolejka.task_done()


def _wykonaj_zlecenie(zadanie: dict) -> None:
    model = zadanie["model"]
    mid = model["id"]
    for z in zadanie["zatrzymaj"]:
        try:
            szybki_stop(z)
        except Exception as exc:
            print(f"[CENTRUM] szybki stop {z}: {exc}", flush=True)
    if mid in _anulowane:
        _anulowane.discard(mid)
        return
    try:
        start(model, zadanie["idx"])
    except Exception as exc:
        with _lock:
            state = load_state()
            state[mid] = {"kind": model.get("kind") or "cmd", "status": "failed", "failed_at": time.time(),
                          "started": time.time(), "port": model.get("port"),
                          "log_tail": [f"START NIEUDANY: {exc}"]}
            save_state(state)
        print(f"[CENTRUM] start {mid}: {exc}", flush=True)


def szybki_stop(model_id: str) -> None:
    """SIGKILL calego drzewa procesow modelu i czekanie (max SZYBKI_VRAM_S), az NVML przestanie je pokazywac."""
    with _lock:
        state = load_state()
        entry = state.get(model_id)
        if not entry:
            model = registry.load_by_id(model_id)
            entry = adopted_entry(model) if model else None
        if not entry or entry.get("pending") or _stale_entry(entry) or _pid_zajety(entry):
            if state.pop(model_id, None) is not None:
                save_state(state)
            return
        cele = {p for p in _entry_all_pids(entry, ppid_map(force=True)) if p and not _protected(p)}
        for p in cele:
            try:
                os.kill(p, signal.SIGKILL)
            except OSError:
                pass
        entry["status"] = "stopping"
        state[model_id] = entry
        save_state(state)
    koniec = time.monotonic() + SZYBKI_VRAM_S
    while time.monotonic() < koniec:
        for p in cele:
            _reap(p)
        if not any(_alive(p) for p in cele) and not _gpu_pids(cele):
            break
        time.sleep(0.1)
    with _lock:
        state = load_state()
        if state.pop(model_id, None) is not None:
            save_state(state)
    _health_cache.pop(model_id, None)


# ---------- sieroty ----------

SKRYPTY = {"main.py": "ComfyUI", "server.py": "serwer", "app.py": "app", "proxy.py": "proxy"}


def czytelna_nazwa(cmdline: str, pid_name: str) -> str:
    """ Czytelna nazwa procesu z cmdline, np. 'ComfyUI :8188 (YUE2)' dla main.py --port 8188 w YUE2-LINUX."""
    toks = cmdline.split()
    skrypt = next((Path(t).name for t in toks if t.endswith(".py")), "")
    label = SKRYPTY.get(skrypt) or skrypt or (pid_name if pid_name not in ("?", "") else "proces")
    port = ""
    for i, t in enumerate(toks):
        if i + 1 >= len(toks):
            break
        if t in ("--port", "--listen-port", "--http-port") and re.fullmatch(r"\d{2,6}", toks[i + 1]):
            port = ":" + toks[i + 1]
            break
    projekt = ""
    for t in toks:
        parts = Path(t).parts
        for j, part in enumerate(parts):
            if part.upper() in ("!PROJEKTY", "PROJEKTY") and j + 1 < len(parts):
                projekt = parts[j + 1].split("-")[0]
                break
        if projekt:
            break
    return f"{label}{(' ' + port) if port else ''}{(' (' + projekt + ')') if projekt else ''}"


def orphans() -> list[dict]:
    """Procesy na GPU0/GPU1 (NVML) nienalezece do zadnego modelu Centrum — grupowane po PID.

    GPU2 (T400: pulpit + tlumacz PTT) i procesy chronione to „system" — nie trafiaja tu w ogole.
    """
    models = registry.load()
    known = known_pids(models)
    per_pid: dict[int, dict] = {}
    for g in telemetry.read_gpus():
        index = g.get("index")
        if index not in (0, 1):  # GPU2 (T400: pulpit + tlumacz PTT) — nigdy nie ruszamy
            continue
        for proc in g.get("processes") or []:
            pid = proc.get("pid")
            if not pid or pid in known or _protected(pid):
                continue
            e = per_pid.setdefault(pid, {"pid": pid, "name": proc.get("name") or "?", "vram_mb": 0,
                                         "gpus": [], "cmdline": "", "uptime_s": 0})
            mb = int(proc.get("vram_mb") or 0)
            e["vram_mb"] += mb
            e["gpus"].append({"gpu": index, "vram_mb": mb})
    for e in per_pid.values():
        pid = e["pid"]
        try:
            p = psutil.Process(pid)
            cmdline = " ".join(p.cmdline()) or p.name()
            e["uptime_s"] = int(time.time() - p.create_time())
        except psutil.Error:
            cmdline = e["name"] or "?"
        e["cmdline"] = cmdline[:200]
        e["name"] = czytelna_nazwa(cmdline, e["name"])
        e["gpus"].sort(key=lambda x: x["gpu"])
    return sorted(per_pid.values(), key=lambda o: -o["vram_mb"])


def _kill_pid(pid: int) -> None:
    try:
        pgid = os.getpgid(pid)
    except OSError:
        pgid = None
    sig_target = (lambda sig: os.killpg(pgid, sig) if pgid == pid else os.kill(pid, sig))
    try:
        sig_target(signal.SIGTERM)
    except OSError:
        return
    if not _wait_dead(pid, TERM_GRACE_S):
        try:
            sig_target(signal.SIGKILL)
        except OSError:
            pass
    _wait_dead(pid, KILL_WAIT_S)


def kill_orphan(pid: int) -> dict:
    if pid not in {o["pid"] for o in orphans()}:  # przejete modele i „system" — nie do zabicia
        raise ProcError(f"PID {pid} nie jest sierota (albo jest chroniony)")
    _kill_pid(pid)
    return {"killed": pid}


def free_gpu(index: int) -> dict:
    if index == 2:
        raise ProcError("GPU2 (T400: pulpit + tlumacz PTT) — nie do zwalniania")
    known = known_pids()  # procesy modeli Centrum zatrzymuje sie przyciskiem Stop, nie „Zwolnij GPU"
    killed: list[int] = []
    for g in telemetry.read_gpus():
        if g.get("index") != index:
            continue
        for proc in g.get("processes") or []:
            pid = proc.get("pid")
            if not pid or pid in known or _protected(pid):
                continue
            _kill_pid(pid)
            killed.append(pid)
    return {"gpu": index, "killed": killed}
