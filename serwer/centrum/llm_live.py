"""Panel „Model LLM — queries”: the same as what the launcher window shows (~/ai/serwer/llm.py).

Sources (without new traffic to the model):
- launcher proxy `/_live` on the model's public port — current query (stage, type, input/output,
  prefill, speed now/average), context, last queries, session sums;
- `historia-<port>.jsonl` (same dir as the state file, reset on proxy start) — since server start:
  generation (TG) and prompt reading (PP) weighted by tokens, results OK / error / interrupted.
"""
from __future__ import annotations

import json
import os
import time
from pathlib import Path

from . import procs

LIVE_TIMEOUT = 0.25      # s — state producer ticks every 250 ms, a hung proxy cannot stall it
PRZERWA_PO_BLEDZIE = 2.0  # s — proxy not responding: next attempt only after this pause
HIST_W_PANELU = 30

_blad_do: dict[int, float] = {}
_historia: dict[str, dict] = {}


def sciezka_historii(model: dict) -> Path:
    """stan-8033.json -> historia-8033.jsonl, stan-geoff-8033.json -> historia-geoff-8033.jsonl."""
    stan = procs.stan_path(model)
    nazwa = stan.name
    if nazwa.startswith("stan-"):
        nazwa = "historia-" + nazwa[len("stan-"):]
    return stan.with_name(nazwa[:-5] + ".jsonl" if nazwa.endswith(".json") else nazwa + ".jsonl")


def _puste() -> dict:
    return {"n": 0, "ok": 0, "bledy": 0, "przerwane": 0, "out": 0, "gen_s": 0.0,
            "pp_tok": 0, "pp_s": 0.0, "czas_s": 0.0, "rodzaje": {}}


def _dolicz(acc: dict, r: dict) -> None:
    acc["n"] += 1
    status = str(r.get("status") or "")
    if status == "OK":
        acc["ok"] += 1
    elif status == "PRZERWANE":
        acc["przerwane"] += 1
    else:
        acc["bledy"] += 1
    rodzaj = str(r.get("kind") or "?")
    acc["rodzaje"][rodzaj] = acc["rodzaje"].get(rodzaj, 0) + 1
    out, gtps = r.get("out") or 0, r.get("gtps") or 0
    if out and gtps:
        acc["out"] += out
        acc["gen_s"] += out / gtps
    pp = (r.get("in") or 0) - (r.get("cached") or 0)
    ptps = r.get("ptps") or 0
    if pp > 0 and ptps:
        acc["pp_tok"] += pp
        acc["pp_s"] += pp / ptps
    acc["czas_s"] += float(r.get("dur") or 0)


def statystyki_historii(path: Path) -> dict | None:
    """Session averages from historia-<port>.jsonl, read incrementally (file reset on proxy start)."""
    try:
        st = os.stat(path)
    except OSError:
        return None
    klucz = str(path)
    stan = _historia.get(klucz)
    if not stan or stan["ino"] != st.st_ino or st.st_size < stan["poz"]:
        stan = {"ino": st.st_ino, "poz": 0, "reszta": b"", "acc": _puste()}
        _historia[klucz] = stan
    if st.st_size > stan["poz"]:
        try:
            with open(path, "rb") as f:
                f.seek(stan["poz"])
                dane = f.read()
                stan["poz"] = f.tell()
        except OSError:
            dane = b""
        linie = (stan["reszta"] + dane).split(b"\n")
        stan["reszta"] = linie.pop()
        for ln in linie:
            try:
                r = json.loads(ln)
            except ValueError:
                continue
            if isinstance(r, dict):
                _dolicz(stan["acc"], r)
    return stan["acc"]


def _sredne(acc: dict) -> dict:
    return {"zapytan": acc["n"], "ok": acc["ok"], "bledy": acc["bledy"], "przerwane": acc["przerwane"],
            "tg_avg": acc["out"] / acc["gen_s"] if acc["gen_s"] else None,
            "pp_avg": acc["pp_tok"] / acc["pp_s"] if acc["pp_s"] else None,
            "czas_avg": acc["czas_s"] / acc["n"] if acc["n"] else None,
            "rodzaje": dict(acc["rodzaje"])}


def pobierz_live(port: int) -> dict | None:
    teraz = time.monotonic()
    if _blad_do.get(port, 0) > teraz:
        return None
    try:
        r = procs._http.get(f"http://127.0.0.1:{port}/_live", timeout=LIVE_TIMEOUT)
        d = r.json() if r.status_code == 200 else None
    except Exception:
        d = None
    if not isinstance(d, dict) or "tot" not in d:
        _blad_do[port] = teraz + PRZERWA_PO_BLEDZIE
        return None
    _blad_do.pop(port, None)
    return d


def stan_modelu(model: dict, live: dict | None) -> dict:
    out = {"id": model["id"], "name": model.get("name") or model["id"], "port": model.get("port"),
           "status": model.get("status"), "proxy": live is not None}
    acc = statystyki_historii(sciezka_historii(model))
    if live is None:
        out["sesja"] = _sredne(acc) if acc else None
        return out
    a = live.get("active")
    glowne = bool(a) and a.get("kind") in ("AGENT", "ROZMOWA")
    ctx = live.get("ctx") or 0
    ctx_main = live.get("ctx_main") or 0
    tot = live.get("tot") or {}
    if not acc:   # no history file — averages from recent queries in /_live
        acc = _puste()
        for r in live.get("hist") or []:
            _dolicz(acc, r)
    sesja = _sredne(acc)
    sesja["zapytan"] = max(sesja["zapytan"], tot.get("req") or 0)
    sesja["wejscie"] = tot.get("in") or 0
    sesja["wyjscie"] = tot.get("out") or 0
    if tot.get("gen_s"):
        sesja["tg_avg"] = (tot.get("out") or 0) / tot["gen_s"]
    out.update({
        "up_s": max(0.0, (live.get("t") or time.time()) - (live.get("start") or time.time())),
        "up_ok": bool(live.get("up_ok")),
        "n_ctx": live.get("n_ctx") or 0,
        "ctx": ctx if (glowne or not ctx_main) else ctx_main,
        "ctx_slot": ctx,
        "queued": live.get("queued") or 0,
        "active": a,
        "hist": list(reversed((live.get("hist") or [])[-HIST_W_PANELU:])),
        "sesja": sesja,
    })
    return out


def launcher_llm(model: dict) -> bool:
    """Launchery z proxy /_live: ~/ai/serwer/llm*.py (llm.py, llm_geoff.py, ...)."""
    return model.get("kind") == "launcher" and Path(str(model.get("script") or "")).name.startswith("llm")


# ---------------- WARP (own engine, kind=cmd): settlement from the server log ----------------
# The WARP server writes one JSON line per query to its log:
#   {"czas","id":"chatcmpl-warp-N","prompt","z_pamieci","prompt_ms","wyj","tps","koniec","przerwane","kolejka_ms"}
# From this we compute the same as what the panel gets from the /_live proxy (TG, PP, query count, tokens, last-queries table).
_warp_logi: dict[str, dict] = {}
_warp_w_toku: dict[str, float] = {}


def warp_llm(model: dict) -> bool:
    cmd = [str(c) for c in (model.get("cmd") or [])]
    return model.get("kind") == "cmd" and "serwer" in cmd and any(Path(c).name == "warp" for c in cmd)


def _rekord_warp(r: dict) -> dict | None:
    if "prompt_ms" not in r or "wyj" not in r:
        return None
    prompt, cache = int(r.get("prompt") or 0), int(r.get("z_pamieci") or 0)
    nowe_tok = max(0, prompt - cache)
    pp_s = float(r.get("prompt_ms") or 0) / 1000.0
    tps = float(r.get("tps") or 0)
    out = int(r.get("wyj") or 0)
    przerwane = bool(r.get("przerwane"))
    try:
        nr = int(str(r.get("id") or "").rsplit("-", 1)[-1])
    except ValueError:
        nr = 0
    return {"id": nr, "kind": "WARP", "in": prompt, "cached": cache, "out": out,
            "ptps": (nowe_tok / pp_s) if pp_s > 0 and nowe_tok else 0.0, "gtps": tps, "dur": pp_s + (out / tps if tps else 0.0),
            "status": "PRZERWANE" if przerwane else ("OK" if r.get("koniec") in ("stop", "tool_calls", "length") else str(r.get("koniec") or "?"))}


def _czytaj_log_warp(sciezka: str) -> dict | None:
    try:
        st = os.stat(sciezka)
    except OSError:
        return None
    stan = _warp_logi.get(sciezka)
    if not stan or st.st_size < stan["poz"]:
        stan = {"poz": 0, "reszta": b"", "acc": _puste(), "hist": [], "tot_in": 0, "tot_out": 0, "ostatni_ctx": 0}
        _warp_logi[sciezka] = stan
    if st.st_size > stan["poz"]:
        try:
            with open(sciezka, "rb") as f:
                f.seek(stan["poz"])
                dane = f.read()
                stan["poz"] = f.tell()
        except OSError:
            dane = b""
        linie = (stan["reszta"] + dane).split(b"\n")
        stan["reszta"] = linie.pop()
        for ln in linie:
            if not ln.startswith(b"{"):
                continue
            try:
                r = _rekord_warp(json.loads(ln))
            except ValueError:
                continue
            if r:
                _dolicz(stan["acc"], r)
                stan["tot_in"] += r["in"] - r["cached"]
                stan["tot_out"] += r["out"]
                stan["ostatni_ctx"] = r["in"] + r["out"]
                stan["hist"].append(r)
                del stan["hist"][:-HIST_W_PANELU]
    return stan


def stan_warp(model: dict, state: dict) -> dict:
    out = {"id": model["id"], "name": model.get("name") or model["id"], "port": model.get("port"), "status": model.get("status"), "proxy": True}
    wpis = (state or {}).get(model["id"]) or {}
    log = wpis.get("log")
    st = _czytaj_log_warp(log) if log else None
    acc = st["acc"] if st else _puste()
    sesja = _sredne(acc)
    sesja["wejscie"] = st["tot_in"] if st else 0
    sesja["wyjscie"] = st["tot_out"] if st else 0
    zdrowie = None
    try:
        zdrowie = procs._http.get(f"http://127.0.0.1:{int(model['port'])}/health", timeout=LIVE_TIMEOUT).json()
    except Exception:
        pass
    up_ok = isinstance(zdrowie, dict) and zdrowie.get("status") == "ok"
    teraz = time.time()
    w_toku = int((zdrowie or {}).get("w_toku") or 0)
    if w_toku:
        _warp_w_toku.setdefault(model["id"], teraz)
    else:
        _warp_w_toku.pop(model["id"], None)
    aktywne = None
    if up_ok and w_toku:
        aktywne = {"id": (st["hist"][-1]["id"] + 1) if st and st["hist"] else 1, "kind": "WARP", "phase": "generating", "in": None, "out": None,
                   "cache": 0, "proc": 0, "inst": 0, "avg": sesja.get("tg_avg"), "ptps": 0, "el": teraz - _warp_w_toku[model["id"]]}
    out.update({
        "up_s": max(0.0, teraz - (model.get("started") or teraz)), "up_ok": up_ok, "n_ctx": int(model.get("ctx") or 0),
        "ctx": st["ostatni_ctx"] if st else 0, "ctx_slot": st["ostatni_ctx"] if st else 0, "queued": int((zdrowie or {}).get("w_kolejce") or 0),
        "active": aktywne, "hist": list(reversed(st["hist"])) if st else [], "sesja": sesja,
    })
    return out


def stan(models: list[dict], state: dict | None = None) -> list[dict]:
    """Uruchomione modele LLM z launchera i WARP — jeden wpis na model (proxy bez odpowiedzi = wpis z proxy: False)."""
    wynik = []
    for m in models:
        if warp_llm(m) and m.get("status") in ("ready", "starting") and m.get("port"):
            wynik.append(stan_warp(m, state if state is not None else procs.load_state()))
            continue
        if not launcher_llm(m) or m.get("status") not in ("ready", "starting") or not m.get("port"):
            continue
        live = pobierz_live(int(m["port"]))
        if live is None and m.get("status") != "ready":
            continue
        wynik.append(stan_modelu(m, live))
    return wynik
