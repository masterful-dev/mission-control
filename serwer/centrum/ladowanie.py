"""Postęp ładowania modelu NA ŻYWO (15.10).

VRAM rośnie dopiero na końcu ładowania (np. Strata: ~6,5 min wczytuje 40 GB do RAM, potem w kilka sekund
17,7 GB na kartę), więc sam pasek VRAM skacze z 1% na 86%. Tu liczymy to, co proces modelu faktycznie
wczytał: max(odczyt przez read() [rchar], odczyt z dysku [read_bytes], pamięć RAM procesu [RSS]) z całego
drzewa procesu (bez proxy launchera) + jego VRAM wg NVML. Rozmiar plików modelu z profilu llm*.py (`pliki`).
"""
from __future__ import annotations

import os
import time
from pathlib import Path

from . import procs, registry

ROZSZERZENIA = (".gguf", ".ninfer", ".bin", ".safetensors")
MIN_PLIK = 64 * 2**20          # pomijamy male pliki (skrypty, klucze, binarki silnika)
STRONA = os.sysconf("SC_PAGE_SIZE")
_profile: dict[str, tuple[float, dict]] = {}
_rozmiary: dict[str, tuple[float, int | None]] = {}


def _profile_skryptu(skrypt: str) -> dict:
    t = time.monotonic()
    hit = _profile.get(skrypt)
    if hit and t - hit[0] < 300:
        return hit[1]
    try:
        prof = registry.load_llmpy_profiles(Path(skrypt))
    except Exception:
        prof = {}
    _profile[skrypt] = (t, prof)
    return prof


def _rozmiar_sciezki(p: str) -> int:
    try:
        if os.path.isdir(p):
            return sum(_rozmiar_sciezki(os.path.join(p, n)) for n in os.listdir(p))
        if p.lower().endswith(ROZSZERZENIA):
            r = os.stat(p).st_size
            return r if r >= MIN_PLIK else 0
    except OSError:
        pass
    return 0


def rozmiar_modelu(model: dict) -> int | None:
    """Suma plików wag (bajty) z profilu llm*.py modelu; None = nieznany (inny launcher)."""
    skrypt = str(model.get("script") or "")
    if not Path(skrypt).name.startswith("llm"):
        return None
    klucz = model.get("profile") or next(iter(model.get("args") or []), None)
    if not klucz:
        return None
    t = time.monotonic()
    hit = _rozmiary.get(f"{skrypt}:{klucz}")
    if hit and t - hit[0] < 300:
        return hit[1]
    prof = _profile_skryptu(skrypt).get(klucz) or {}
    suma = sum(_rozmiar_sciezki(str(f)) for f in prof.get("pliki") or []) or None
    _rozmiary[f"{skrypt}:{klucz}"] = (t, suma)
    return suma


def _liczniki(pid: int) -> tuple[int, int, int]:
    rchar = rbytes = rss = 0
    try:
        for ln in Path(f"/proc/{pid}/io").read_text().splitlines():
            k, _, v = ln.partition(":")
            if k == "rchar":
                rchar = int(v)
            elif k == "read_bytes":
                rbytes = int(v)
    except (OSError, ValueError):
        pass
    try:
        rss = int(Path(f"/proc/{pid}/statm").read_text().split()[1]) * STRONA
    except (OSError, ValueError, IndexError):
        pass
    return rchar, rbytes, rss


def postep(model: dict, entry: dict, gpus: list[dict]) -> dict:
    """{etap, wczytano_mb, rozmiar_mb, ram_mb, vram_mb, pct, czas_s} dla modelu w stanie 'starting'."""
    czas = max(0.0, time.time() - float(entry.get("started") or time.time()))
    if entry.get("pending"):
        return {"etap": "kolejka", "czas_s": round(czas, 1)}
    pids = procs._entry_all_pids(entry, procs.ppid_map()) - {entry.get("proxy_pid")}
    rchar = rbytes = rss = 0
    for pid in pids:
        a, b, c = _liczniki(int(pid))
        rchar, rbytes, rss = rchar + a, rbytes + b, rss + c
    vram = sum(int(pr.get("vram_mb") or 0) for g in gpus for pr in g.get("processes") or [] if pr.get("pid") in pids)
    rozmiar = rozmiar_modelu(model)
    wczytano = max(rchar, rbytes, rss)
    if rozmiar:
        wczytano = min(wczytano, rozmiar)
    return {"etap": "laduje", "wczytano_mb": wczytano // 2**20, "rozmiar_mb": rozmiar // 2**20 if rozmiar else None,
            "ram_mb": rss // 2**20, "vram_mb": vram,
            "pct": round(100 * wczytano / rozmiar, 1) if rozmiar else None, "czas_s": round(czas, 1)}
