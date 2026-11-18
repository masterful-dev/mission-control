"""Rejestr modeli (models.yaml) + layout (layout.yaml) + importerzy (llm.py, ikony z pulpitu).

Rodzaje modeli (SPEC §11a): `launcher` (start/stop skryptem launchera, stan w pliku stan-*.json)
i `cmd` (dowolna komenda). Stare wpisy `llmpy` sa automatycznie migrowane do `launcher`.
"""
from __future__ import annotations

import importlib.util
import os
import re
import threading
from pathlib import Path

import yaml

BASE_DIR = Path(__file__).resolve().parent.parent
MODELS_PATH = BASE_DIR / "models.yaml"
LAYOUT_PATH = BASE_DIR / "layout.yaml"
SERWER_DIR = Path.home() / "ai/serwer"
LLMPY_PATH = SERWER_DIR / "llm.py"
DESKTOP_DIR = Path.home() / "Pulpit"

DEFAULT_PORT = 8033
DEFAULT_HEALTH = "/v1/models"
RAM_GB_STRATA = 47
RAM_GB_DEFAULT = 2
DEFAULT_CATEGORY = "Inne"
# 02.11: zakładki TP1 / TP2 w panelu „Modele". Model trafia tam wg liczby KART w wariancie:
# 1 karta -> TP1, 2+ kart -> TP2. Kategorie niemodelowe (Grafika/Muzyka/Pomocnicze) zostaja.
KAT_TP1, KAT_TP2 = "TP1", "TP2"
KATEGORIE_TP = (KAT_TP1, KAT_TP2)
# §13: panel „Sieroty na GPU" usuniety z UI i z listy paneli layoutu (sieroty zostaja w backendzie).
PANELS = ["gpu", "llm", "system", "models", "energia"]
STARE_GPU = ("gpu0", "gpu1", "gpu2")   # do 15.10: osobny panel na kazda karte -> jeden panel 'gpu' + panel 'llm' 

# §12b (przyciaganie do siatki 12 kolumn) zastapione przez §13: swobodne okna w pikselach.
# `sizes` (w = kolumny 1..12, h = px) zostaja tolerowane w odczycie dla starego UI/layoutu.
MIN_W, MAX_W, MIN_H = 1, 12, 80
PANEL_SIZES = {
    "gpu": {"w": 6, "h": 430}, "llm": {"w": 6, "h": 430},
    "system": {"w": 6, "h": 340}, "models": {"w": 6, "h": 480}, "energia": {"w": 6, "h": 260},
}

# §13: okna = {id: {x, y, w, h}} w PIKSELACH (bez siatki, bez przyciagania).
OKNO_GAP = 12
MIN_OKNO_W, MIN_OKNO_H = 220, 80
OKNO_H = {"gpu": 400, "llm": 400, "system": 340, "models": 480, "energia": 260}
SZEROKOSC_DOMYSLNA = 1896   # 1920 - 2 x 12 px marginesu kontenera


def domyslne_okna(szerokosc: int = SZEROKOSC_DOMYSLNA) -> dict:
    """Uklad domyslny §13 (odstep 12 px): rzad 1 = GPU (wszystkie karty, 1/2) | Model LLM (1/2), h 400;
    rzad 2 = System (1/2, h 340) | Modele (1/2, h 480); pod Systemem Energia (1/2, h 260)."""
    S = max(660, int(szerokosc))
    pol = (S - OKNO_GAP) // 2
    y2 = OKNO_H["gpu"] + OKNO_GAP
    out = {"gpu": {"x": 0, "y": 0, "w": pol, "h": OKNO_H["gpu"]},
           "llm": {"x": pol + OKNO_GAP, "y": 0, "w": S - pol - OKNO_GAP, "h": OKNO_H["llm"]}}
    out["system"] = {"x": 0, "y": y2, "w": pol, "h": OKNO_H["system"]}
    out["models"] = {"x": pol + OKNO_GAP, "y": y2, "w": S - pol - OKNO_GAP, "h": OKNO_H["models"]}
    out["energia"] = {"x": 0, "y": y2 + OKNO_H["system"] + OKNO_GAP, "w": pol, "h": OKNO_H["energia"]}
    return out

_load_cache: tuple[float, list[dict]] | None = None
_io_lock = threading.Lock()   # rejestr/layout sa read-modify-write z wielu watkow (REST + producent stanu)


def _int(val, default=0):
    """Leniwa konwersja (models.yaml bywa edytowany recznie — '128k' nie moze wywalac calego ladowania)."""
    try:
        return int(val)
    except (TypeError, ValueError):
        return default


def _path(path: Path | None, default: Path) -> Path:
    return Path(path) if path is not None else default


# ---------- odczyt / zapis ----------

def load(path: Path | None = None) -> list[dict]:
    """Czyta models.yaml (z cache'em po mtime — wywoływane co 250 ms z /ws).

    Zwrota OBIEKTY Z CACHE — wolno je mutowac; do pracy uzywaj load_copy()."""
    global _load_cache
    p = _path(path, MODELS_PATH)
    if not p.is_file():
        return []
    mtime = p.stat().st_mtime
    with _io_lock:
        if _load_cache and _load_cache[0] == mtime and _load_cache[2] == p:
            return _load_cache[1]
        try:
            data = yaml.safe_load(p.read_text()) or []
        except (OSError, yaml.YAMLError):
            return []
        models = [normalize(m) for m in data if isinstance(m, dict) and m.get("id")]
        _load_cache = (mtime, models, p)
        return models


def load_copy(path: Path | None = None) -> list[dict]:
    """Kopie wpisow rejestru — bezpieczne do uzupelniania polami roboczymi (status, pids, plan...)."""
    return [dict(m) for m in load(path)]


def save(models: list[dict], path: Path | None = None) -> None:
    global _load_cache
    p = _path(path, MODELS_PATH)
    entries = [normalize(m) for m in models]
    with _io_lock:
        _load_cache = None
        tmp = p.with_suffix(".yaml.tmp")
        tmp.write_text(yaml.safe_dump(entries, allow_unicode=True, sort_keys=False))
        os.replace(tmp, p)  # atomowo: pol-zapisany YAML nie wejdzie w srodek ladowania


def slug(text: str) -> str:
    out = re.sub(r"[^a-z0-9]+", "-", str(text).lower()).strip("-")
    return out or "model"


def normalize(m: dict) -> dict:
    """Uzupełnia brakujące pola — rejestr zawsze ma ten sam kształt (teze migracja llmpy -> launcher)."""
    kind = m.get("kind") or "cmd"
    profile = m.get("profile")
    port = m.get("port")
    out = {
        "id": str(m["id"]),
        "name": m.get("name") or m["id"],
        "kind": kind,
        "category": m.get("category") or DEFAULT_CATEGORY,
        "order": _int(m.get("order"), 0),
        "profile": profile,
        "script": m.get("script"),
        "args": list(m.get("args") or []),
        "stan": m.get("stan"),
        "stan_match": dict(m.get("stan_match") or {}),
        "gpu_env": m.get("gpu_env"),
        "port_env": m.get("port_env"),
        "cmd": m.get("cmd"),
        "cwd": m.get("cwd"),
        "env": m.get("env") or {},
        "port": m.get("port"),
        "health": _clean_health(m.get("health")),
        "ctx": _int(m.get("ctx"), 0),
        "extra_args": m.get("extra_args") or [],
        "ram_gb": _num(m.get("ram_gb"), 0),
        "placements": _clean_placements(m.get("placements")),
        "enabled": bool(m.get("enabled", True)),
        # 02.11: 0 = nieprzypięty (sortuje się), 1 = PRZYPIĘTY na pozycji na liście (patrz apply_pin).
        "pin": 1 if _int(m.get("pin"), 0) else 0,
    }
    if kind == "llmpy":  # stary rodzaj: ten sam mechanizm co launcher (llm.py = launcher profilu)
        port = _int(port, DEFAULT_PORT) or DEFAULT_PORT
        out["kind"] = "launcher"
        out["script"] = out["script"] or str(LLMPY_PATH)
        out["args"] = out["args"] or ([profile] if profile else [])
        out["stan"] = out["stan"] or f"stan-{port}.json"
        out["stan_match"] = out["stan_match"] or ({"profil": profile} if profile else {})
        out["gpu_env"] = out["gpu_env"] or "LLM_GPUS"
        out["port_env"] = out["port_env"] or "LLM_PORT"
    return out


def _num(val, default):
    try:
        return float(val) if isinstance(val, str) else float(val or 0)
    except (TypeError, ValueError):
        return default


def _clean_health(val) -> str:
    """'health' to sciezka WZGLEDNA ('/v1/models') — pelny URL dalby 'http://127.0.0.1:8190http://...'."""
    health = str(val or DEFAULT_HEALTH).strip()
    if re.match(r"^https?://", health):
        health = "/" + health.split("//", 1)[1].split("/", 1)[1] if "/" in health.split("//", 1)[1] else DEFAULT_HEALTH
    return health if health.startswith("/") else "/" + health


def _clean_placements(val) -> list[dict]:
    """Warianty rozmieszczenia: gpus = unikalne numery kart, vram = liczby (duplikat karty podwojilby rezerwacje)."""
    out = []
    for pl in val or []:
        if not isinstance(pl, dict):
            continue
        gpus, seen = [], set()
        for g in pl.get("gpus") or []:
            g = _int(g, -1)
            if g >= 0 and g not in seen:
                seen.add(g)
                gpus.append(g)
        vram = [_int(v, 0) for v in pl.get("vram") or []]
        if not gpus or len(gpus) != len(vram):
            continue
        out.append({"gpus": gpus, "vram": vram})
    return out


# ---------- zakładki TP1 / TP2 + przypinanie (02.11) ----------

def tp_kategoria(m: dict) -> str | None:
    """Zakładka modelu wg liczby kart: 1 karta -> TP1, 2+ kart -> TP2.

    Model, który ma warianty jednokartowe WYŁĄCZNIE, jest TP1; jeśli ma choć jeden wariant
    na 2+ karty, jest TP2 (tam realnie startuje na dwóch kartach). Modele bez wariantów
    (np. Grafika/Muzyka/Pomocnicze) nie mają zakładki TP -> zostają w swojej kategorii."""
    plac = m.get("placements") or []
    if not plac:
        return None
    if any(len(p.get("gpus") or []) >= 2 for p in plac):
        return KAT_TP2
    return KAT_TP1


def kategoria_docelowa(m: dict) -> str:
    """Kategoria, do której model trafia po sortowaniu: TP1/TP2 dla modeli z wariantami,
    inaczej kategoria zapisana w wpisie."""
    return tp_kategoria(m) or str(m.get("category") or DEFAULT_CATEGORY)


def apply_pin(model_id: str, pin: bool, models_path: Path | None = None,
              layout_path: Path | None = None) -> dict:
    """PUT /api/models/{id}/pin — przypiąć lub odpiąć model na jego pozycji na liście.

    PRZYPIĘCIE: model trafia do zakładki TP1/TP2 (wg kart), dostaje pin=1 i `order` równe
    swojej TERAZNIEJSZEJ pozycji w tej zakładce — dzięki temu sort (order, id) nie przesuwa
    go względem sąsiadów. ODPIĘCIE: pin=0 — model znowu sortuje się normalnie.
    Zwraca {id, pin, category, order}.
    """
    models = load(models_path)
    if not any(m["id"] == model_id for m in models):
        raise ValueError(f"nieznany id modelu: {model_id!r}")
    kopie = [dict(m) for m in models]
    idx = next(i for i, m in enumerate(kopie) if m["id"] == model_id)
    m = kopie[idx]
    kat = tp_kategoria(m) or str(m.get("category") or DEFAULT_CATEGORY)
    if pin:
        pozycja = _int(m.get("order"), 0)
        m["category"], m["pin"], m["order"] = kat, 1, pozycja
    else:
        m["pin"] = 0
    save(kopie, models_path)
    layout = load_layout(layout_path)
    names = {c["name"] for c in layout["categories"]}
    if kat not in names:
        layout["categories"].append({"name": kat, "collapsed": False})
        save_layout(layout, layout_path)
    return {"id": model_id, "pin": m["pin"], "category": m["category"], "order": m["order"]}


# ---------- layout (§11b) ----------

def load_layout(path: Path | None = None) -> dict:
    p = _path(path, LAYOUT_PATH)
    if p.is_file():
        try:
            data = yaml.safe_load(p.read_text()) or {}
        except (OSError, yaml.YAMLError):
            data = {}
        if isinstance(data, dict):
            return normalize_layout(data)
    return normalize_layout({})


def normalize_sizes(val) -> dict:
    """Rozmiary paneli (sciezka ODCZYTU — tolerancyjna: zly wpis po prostu odpuszczamy, UI dostanie domyslny).
    §13: wpisy paneli USUNIETYCH (np. 'orphans') sa pomijane — layout sam sie czyści przy odczycie."""
    out = {}
    if isinstance(val, dict):
        for pid, s in val.items():
            if str(pid) not in PANELS or not isinstance(s, dict):
                continue
            w, h = _int(s.get("w"), 0), _int(s.get("h"), 0)
            if MIN_W <= w <= MAX_W and h >= MIN_H:
                out[str(pid)] = {"w": w, "h": h}
    return out


def normalize_okna(val) -> dict:
    """Okna (sciezka ODCZYTU — tolerancyjna: zly wpis odpuszczamy, UI dostanie uklad domyslny).
    §13: wpisy paneli USUNIETYCH (np. 'orphans') sa pomijane."""
    out = {}
    if isinstance(val, dict):
        for pid, o in val.items():
            if str(pid) not in PANELS or not isinstance(o, dict):
                continue
            x, y = _int(o.get("x"), -1), _int(o.get("y"), -1)
            w, h = _int(o.get("w"), 0), _int(o.get("h"), 0)
            if x >= 0 and y >= 0 and w >= MIN_OKNO_W and h >= MIN_OKNO_H:
                out[str(pid)] = {"x": x, "y": y, "w": w, "h": h}
    return out


def _migruj_stare_gpu(data: dict) -> dict:
    """Uklad sprzed 15.10 (panele gpu0/gpu1/gpu2): pas zajmowany przez trzy karty dzielony na pol —
    lewa polowa = panel 'gpu' (wszystkie karty na jednym wykresie), prawa = 'llm' (zapytania modelu).
    Reszta paneli zostaje na swoich miejscach."""
    panele = [str(x) for x in (data.get("panels") or [])]
    if "gpu" in panele or not any(p in STARE_GPU for p in panele):
        return data
    out = dict(data)
    nowe = []
    for p in panele:
        if p in STARE_GPU:
            p = "gpu"
        if p == "gpu" and "gpu" not in nowe:
            nowe += ["gpu", "llm"]
        elif p not in ("gpu", "llm"):
            nowe.append(p)
    out["panels"] = nowe
    okna = dict(data.get("okna") or {}) if isinstance(data.get("okna"), dict) else {}
    stare = [o for k, o in okna.items() if k in STARE_GPU and isinstance(o, dict)]
    try:
        x0 = min(int(o["x"]) for o in stare)
        y0 = min(int(o["y"]) for o in stare)
        x1 = max(int(o["x"]) + int(o["w"]) for o in stare)
        h = max(int(o["h"]) for o in stare)
    except (KeyError, TypeError, ValueError):   # brak/zle okna starych kart — UI dostanie uklad domyslny
        return out
    szer = x1 - x0
    if szer >= 2 * MIN_OKNO_W + OKNO_GAP:
        lewa = (szer - OKNO_GAP) // 2
        okna["gpu"] = {"x": x0, "y": y0, "w": lewa, "h": h}
        okna["llm"] = {"x": x0 + lewa + OKNO_GAP, "y": y0, "w": szer - lewa - OKNO_GAP, "h": h}
    else:                                       # karty ustawione w kolumnie — GPU nad LLM
        okna["gpu"] = {"x": x0, "y": y0, "w": max(szer, MIN_OKNO_W), "h": h}
        okna["llm"] = {"x": x0, "y": y0 + h + OKNO_GAP, "w": max(szer, MIN_OKNO_W), "h": h}
    out["okna"] = okna
    return out


def normalize_layout(data: dict) -> dict:
    data = _migruj_stare_gpu(data)
    panels, cats, seen = [], [], set()
    for c in data.get("categories") or []:
        if isinstance(c, str):
            c = {"name": c}
        if not isinstance(c, dict):
            continue
        name = str(c.get("name") or "")
        if not name or name in seen:
            continue
        seen.add(name)
        cats.append({"name": name, "collapsed": bool(c.get("collapsed"))})
    for x in data.get("panels") or PANELS:      # §13: nieznane id (np. usuniete 'orphans') odpuszczamy — migracja
        if str(x) in PANELS and str(x) not in panels:
            panels.append(str(x))
    return {"panels": panels or list(PANELS), "categories": cats,
            "sizes": normalize_sizes(data.get("sizes")), "okna": normalize_okna(data.get("okna"))}


def pelny_layout(layout: dict) -> dict:
    """Layout dla UI: nowe panele dopisane na KONIEC — układ użytkownika bez zmian.
    §13: `okna` (pozycje/rozmiary w pikselach) — brak wpisu = uklad domyslny (UI i tak przycina
    do biezacej szerokosci kontenera). `sizes` (§12b) zostaja dla starego UI."""
    out = dict(layout or {})
    panels = list(out.get("panels") or PANELS) + [p for p in PANELS if p not in (out.get("panels") or [])]
    out["panels"] = panels
    sizes = dict(out.get("sizes") or {})
    out["sizes"] = {p: dict(sizes.get(p) or PANEL_SIZES.get(p) or {"w": 6, "h": 300}) for p in panels}
    okna = dict(out.get("okna") or {})
    dom = domyslne_okna()
    out["okna"] = {p: dict(okna.get(p) or dom.get(p) or {"x": 0, "y": 0, "w": 400, "h": 300}) for p in panels}
    return out


def validate_layout(body) -> tuple[dict | None, str]:
    """PUT /api/layout — {panels: [znane id...], categories: [{name, collapsed}],
    sizes: {id: {w, h}}, okna: {id: {x, y, w, h}}} (§13: x,y >= 0, w >= 220, h >= 80, znane id);
    zle dane -> (None, powod) — API zwraca 400 i NIC nie zapisujemy."""
    if not isinstance(body, dict):
        return None, "layout musi byc obiektem {panels, categories, sizes, okna}"
    if isinstance(body.get("panels"), list):   # okno ze starym UI (gpu0/1/2) zapisuje dalej, juz w nowym ukladzie
        body = _migruj_stare_gpu(body)
        if isinstance(body.get("sizes"), dict):
            body["sizes"] = {k: v for k, v in body["sizes"].items() if k not in STARE_GPU}
        if isinstance(body.get("okna"), dict):
            body["okna"] = {k: v for k, v in body["okna"].items() if k not in STARE_GPU}
    panels = body.get("panels", PANELS)
    if not isinstance(panels, list) or not panels:
        return None, "panels musi byc niepusta lista identyfikatorow paneli"
    for x in panels:
        if not isinstance(x, str) or x not in PANELS:
            return None, f"nieznany panel: {x!r} (dopuszczalne: {', '.join(PANELS)})"
    cats = body.get("categories", [])
    if not isinstance(cats, list):
        return None, "categories musi byc lista {name, collapsed}"
    for c in cats:
        if not isinstance(c, dict) or not str(c.get("name") or ""):
            return None, f"zla kategoria: {c!r} (oczekiwano {{name: str, collapsed: bool}})"
        if "collapsed" in c and not isinstance(c["collapsed"], bool):
            return None, f"collapsed musi byc prawda/falszem: {c!r}"
    sizes_in = body.get("sizes", {})
    if not isinstance(sizes_in, dict):
        return None, "sizes musi byc obiektem {panel: {w, h}}"
    for pid, s in sizes_in.items():
        if pid not in PANELS:
            return None, f"nieznany panel w sizes: {pid!r} (dopuszczalne: {', '.join(PANELS)})"
        if not isinstance(s, dict):
            return None, f"zly rozmiar panelu {pid}: {s!r} (oczekiwano {{w: {MIN_W}..{MAX_W}, h: >= {MIN_H}}})"
        w, h = s.get("w"), s.get("h")
        if isinstance(w, bool) or not isinstance(w, int) or not MIN_W <= w <= MAX_W:
            return None, f"zla szerokosc panelu {pid}: {w!r} (oczekiwano liczby {MIN_W}..{MAX_W})"
        if isinstance(h, bool) or not isinstance(h, int) or h < MIN_H:
            return None, f"zla wysokosc panelu {pid}: {h!r} (oczekiwano liczby >= {MIN_H} px)"
    okna_in = body.get("okna", {})
    if not isinstance(okna_in, dict):
        return None, "okna musi byc obiektem {panel: {x, y, w, h}}"
    for pid, o in okna_in.items():
        if pid not in PANELS:
            return None, f"nieznany panel w okna: {pid!r} (dopuszczalne: {', '.join(PANELS)})"
        if not isinstance(o, dict):
            return None, (f"zle okno panelu {pid}: {o!r} "
                          f"(oczekiwano {{x: >=0, y: >=0, w: >={MIN_OKNO_W}, h: >={MIN_OKNO_H}}})")
        o = dict(o)
        for k in ("x", "y", "w", "h"):
            v = o.get(k)
            if isinstance(v, bool) or not isinstance(v, (int, float)) or v != v or v in (float("inf"), float("-inf")):
                return None, f"okna.{pid}.{k} musi byc liczba (bylo: {v!r})"
            o[k] = int(round(v))   # przegladarka przy powiekszeniu strony podaje ulamki pikseli (np. 790.4375)
        okna_in[pid] = o
        if o["x"] < 0 or o["y"] < 0:
            return None, f"okna.{pid}: x i y musza byc >= 0 (bylo: x={o['x']}, y={o['y']})"
        if o["w"] < MIN_OKNO_W:
            return None, f"okna.{pid}.w musi byc >= {MIN_OKNO_W} px (bylo: {o['w']})"
        if o["h"] < MIN_OKNO_H:
            return None, f"okna.{pid}.h musi byc >= {MIN_OKNO_H} px (bylo: {o['h']})"
    return (normalize_layout({"panels": panels, "categories": cats,
                              "sizes": sizes_in, "okna": okna_in}), "")


def save_layout(layout: dict, path: Path | None = None) -> dict:
    out = normalize_layout(layout or {})
    if not out.get("okna"):            # §13 migracja: brak okien = uklad domyslny, zapisany raz
        out["okna"] = domyslne_okna()
    p = _path(path, LAYOUT_PATH)
    with _io_lock:
        tmp = p.with_suffix(".yaml.tmp")
        tmp.write_text(yaml.safe_dump(out, allow_unicode=True, sort_keys=False))
        os.replace(tmp, p)
    return out


def apply_order(body: dict, models_path: Path | None = None, layout_path: Path | None = None) -> dict:
    """PUT /api/models/order — body {kategoria: [id, ...]}: ustawia kategorie i kolejnosc modeli.

    Nieznane id -> ValueError (API: 400, nic nie zapisujemy). Kategorie nieobecne w body zostaja
    w layoutie bez zmian. Modele przypisane w body trafiaja na poczatek rejestru w podanej kolejnosci."""
    if not isinstance(body, dict):
        raise ValueError("body musi byc obiektem {kategoria: [id, ...]}")
    models = load(models_path)          # obiekty z cache — NIE mutujemy ich (patrz: kopie ponizej)
    by_id = {m["id"]: m for m in models}
    kopie = {m["id"]: dict(m) for m in models}
    for cat, ids in body.items():
        if not isinstance(ids, list):
            raise ValueError(f"kategoria {cat!r}: oczekiwano listy id modelow")
        for mid in ids:
            if mid not in by_id:
                raise ValueError(f"nieznany id modelu w kolejnosci: {mid!r}")
    przypisane: list[dict] = []
    przypisane_ids: set[str] = set()
    for cat, ids in body.items():
        for i, mid in enumerate(ids):
            m = kopie[mid]
            m["category"], m["order"] = str(cat), i
            # 02.11: przeciągnięcie PRZYPIĘTEGO modelu go odpina — inaczej trzymałby stare
            # `order` i wracał na starą pozycję przy najbliższym renderze.
            m["pin"] = 0
            if mid not in przypisane_ids:
                przypisane_ids.add(mid)
                przypisane.append(m)
    porzadek = przypisane + [dict(m) for m in models if m["id"] not in przypisane_ids]
    save(porzadek, models_path)
    layout = load_layout(layout_path)   # kategorie spoza body zostaja
    names = {c["name"] for c in layout["categories"]}
    for cat in body:
        if cat not in names:
            layout["categories"].append({"name": str(cat), "collapsed": False})
    save_layout(layout, layout_path)
    return {"models": len(porzadek), "layout": layout}


def load_by_id(model_id: str, path: Path | None = None) -> dict | None:
    return next((m for m in load(path) if m["id"] == model_id), None)


# ---------- kategorie ----------

def categories(models: list[dict] | None = None) -> list[str]:
    out: list[str] = []
    for m in sorted(load(models) if models is None else models, key=lambda m: (m["order"], m["id"])):
        if m["category"] not in out:
            out.append(m["category"])
    return out


# ---------- importer profili z llm.py ----------

def load_llmpy_profiles(path: Path = LLMPY_PATH) -> dict:
    """Importuje moduł llm.py (ma `if __name__ == "__main__"`, więc nic nie startuje)."""
    spec = importlib.util.spec_from_file_location("llm_llmpy", str(path))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return dict(mod.PROFILES)


def placements_from_profile(profile: dict) -> list[dict]:
    gpus = list(profile.get("gpus") or [])
    vram = [int(v) for v in profile.get("vram_cel") or []]
    if not gpus or len(gpus) != len(vram):
        return []
    if len(gpus) == 1 and gpus[0] in (0, 1):
        # model jednokartowy: wariant na GPU0 i na GPU1
        return [{"gpus": [0], "vram": list(vram)}, {"gpus": [1], "vram": list(vram)}]
    return [{"gpus": gpus, "vram": vram}]


def entry_from_profile(key: str, profile: dict) -> dict:
    ram = RAM_GB_STRATA if key.startswith("strata") else RAM_GB_DEFAULT
    return normalize({
        "id": key,
        "name": profile.get("title") or key,
        "kind": "llmpy",
        "profile": key,
        "port": DEFAULT_PORT,
        "health": DEFAULT_HEALTH,
        "ctx": profile.get("n_ctx") or 0,
        "ram_gb": ram,
        "category": "LLM",
        "placements": placements_from_profile(profile),
    })


def import_llmpy(models_path: Path | None = None, llmpy_path: Path | None = None) -> list[dict]:
    """Dopisuje profile z llm.py do rejestru; istniejące wpisy zostają nietknięte."""
    models_path = Path(models_path or MODELS_PATH)
    models = load(models_path)
    known = {m["id"] for m in models}
    added = []
    for key, prof in load_llmpy_profiles(llmpy_path or LLMPY_PATH).items():
        if key == "test" or prof.get("wylaczony") or key in known:
            continue
        entry = entry_from_profile(key, prof)
        models.append(entry)
        added.append(entry)
    if added:
        save(models, models_path)
    return added


# ---------- importer ikon z pulpitu (§11a) ----------

PY_TOKEN = re.compile(r"python3?$")


def parse_desktop(path: Path) -> dict | None:
    """Wyciaga z ikony .desktop nazwe i (skrypt, argumenty) jesli Exec uruchamia python3 <skrypt>.py."""
    name, exec_line, slug_hint = None, None, None
    for line in path.read_text(errors="replace").splitlines():
        line = line.strip()
        if line.startswith("Name=") and name is None:
            name = line[5:].strip()
        elif line.startswith("Exec="):
            exec_line = line[5:].strip()
    if not exec_line or "python" not in exec_line:
        return None
    toks = exec_line.split()
    for i, tok in enumerate(toks):
        if tok.endswith(".py") and i and PY_TOKEN.search(toks[i - 1].split("/")[-1]):
            args = [a for a in toks[i + 1:] if not a.startswith("%")]
            # launcher-remember.sh <slug> "<nazwa>" -- python3 <skrypt>.py <args…> -> slug = id modelu
            hint = next((toks[j + 1] for j, t in enumerate(toks)
                         if t.endswith(".sh") and j + 1 < len(toks) and re.fullmatch(r"[\w.-]+", toks[j + 1])), None)
            return {"file": path.name, "name": name or path.stem, "script": tok, "args": args,
                    "id": hint or slug(name or path.stem), "category": DEFAULT_CATEGORY,
                    "port": _port_of(toks), "vram_mb": 0, "ram_gb": RAM_GB_DEFAULT}
    return None


def _port_of(toks: list[str]) -> int | None:
    for i, t in enumerate(toks):
        if t in ("--port", "--listen-port", "--http-port") and i + 1 < len(toks) and toks[i + 1].isdigit():
            return int(toks[i + 1])
    return None


def desktop_candidates(desktop_dir: Path | None = None, models: list[dict] | None = None) -> list[dict]:
    """Ikony z ~/Pulpit uruchamiajace python3 <skrypt>, ktorych NIE ma jeszcze w rejestrze."""
    models = load() if models is None else models
    known = {(str(m.get("script") or ""), tuple(m.get("args") or [])) for m in models}
    known_names = {m.get("name") for m in models}
    out = []
    for f in sorted(Path(desktop_dir or DESKTOP_DIR).glob("*.desktop")):
        c = parse_desktop(f)
        if not c:
            continue
        if (c["script"], tuple(c["args"])) in known or c["name"] in known_names:
            continue
        out.append(c)
    return out


def import_desktop(items: list[dict], models_path: Path | None = None) -> list[dict]:
    """Dodaje wybrane ikony jako modele kind=launcher (kategoria i VRAM podane przez uzytkownika)."""
    models = load(models_path)
    known = {m["id"] for m in models}
    added = []
    for it in items or []:
        vram = int(it.get("vram_mb") or 0)
        entry = normalize({
            "id": it.get("id") or slug(it.get("name") or ""),
            "name": it.get("name"),
            "kind": "launcher",
            "category": it.get("category") or DEFAULT_CATEGORY,
            "script": it.get("script"),
            "args": it.get("args") or [],
            "stan": it.get("stan"),
            "stan_match": it.get("stan_match") or {},
            "gpu_env": it.get("gpu_env"),
            "port_env": it.get("port_env"),
            "port": it.get("port"),
            "health": it.get("health") or DEFAULT_HEALTH,
            "ram_gb": it.get("ram_gb") or RAM_GB_DEFAULT,
            "placements": it.get("placements") or ([{"gpus": [0], "vram": [vram]},
                                                    {"gpus": [1], "vram": [vram]}] if vram else []),
        })
        if entry["id"] in known:
            continue
        models.append(entry)
        known.add(entry["id"])
        added.append(entry)
    if added:
        save(models, models_path)
    return added


# ---------- migracja §11a: 8 modeli w kategoriach, reszta enabled: false ----------

KATEGORIE = ["LLM", "Grafika", "Muzyka", "Pomocnicze"]

KANON = [
    {"id": "ninfer06", "category": "LLM", "name": "NINFER T0.6 Qwen3.8-27B NVFP4 (TP2)",
     "script": str(SERWER_DIR / "llm.py"), "args": ["ninfer06"], "profile": "ninfer06",
     "stan": "stan-8033.json", "stan_match": {"profil": "ninfer06"},
     "gpu_env": "LLM_GPUS", "port_env": "LLM_PORT", "port": 8033, "health": "/v1/models",
     "ram_gb": 2, "placements": [{"gpus": [0, 1], "vram": [20900, 20900]}]},
    {"id": "geoff-k3-tp1", "category": "LLM", "name": "NINFER GEOFFWATTS REFERENCE K3 TP1",
     "script": str(SERWER_DIR / "llm_geoff.py"), "args": ["geoffref"], "profile": "geoffref",
     "stan": "stan-geoff-8033.json", "stan_match": {"profil": "geoffref"},
     "gpu_env": "GEOFF_GPU", "port_env": "LLM_PORT", "port": 8033, "health": "/v1/models",
     "ram_gb": 2, "placements": [{"gpus": [1], "vram": [27000]}, {"gpus": [0], "vram": [27000]}]},
    {"id": "strata-iq3", "category": "LLM", "name": "STRATA IQ3_XXS Qwen3.8-Flash-Next 125B",
     "script": str(SERWER_DIR / "llm.py"), "args": ["strata-iq3"], "profile": "strata-iq3",
     "stan": "stan-8033.json", "stan_match": {"profil": "strata-iq3"},
     "gpu_env": "LLM_GPUS", "port_env": "LLM_PORT", "port": 8033, "health": "/v1/models",
     "ram_gb": RAM_GB_STRATA, "placements": [{"gpus": [0, 1], "vram": [25000, 31200]}]},
    {"id": "ornith", "category": "LLM", "name": "ORNITH Ornith-1.5-35B-A3B MTP",
     "script": str(SERWER_DIR / "llm.py"), "args": ["ornith"], "profile": "ornith",
     "stan": "stan-8033.json", "stan_match": {"profil": "ornith"},
     "gpu_env": "LLM_GPUS", "port_env": "LLM_PORT", "port": 8033, "health": "/v1/models",
     "ram_gb": 2, "placements": [{"gpus": [0], "vram": [25500]}, {"gpus": [1], "vram": [25500]}]},
    {"id": "obrazy-1gpu", "category": "Grafika", "name": "OBRAZY 1 GPU (GPU0) Qwen-Image 2.1",
     "script": str(SERWER_DIR / "obrazy.py"), "args": [],
     "stan": "stan-obrazy-8190.json", "stan_match": {"tryb": "qwen"},
     "gpu_env": "OBRAZY_GPU", "port_env": "OBRAZY_PORT", "port": 8190, "health": "/system_stats",
     "ram_gb": 22, "placements": [{"gpus": [0], "vram": [24000]}, {"gpus": [1], "vram": [24000]}]},
    {"id": "obrazy-2gpu", "category": "Grafika", "name": "OBRAZY 2 GPU (GPU1+GPU0) Qwen-Image 2.1 + Krea 2",
     "script": str(SERWER_DIR / "obrazy.py"), "args": ["--2gpu"],
     "stan": "stan-obrazy-8190.json", "stan_match": {"tryb": "2gpu"},
     "gpu_env": None, "port_env": "OBRAZY_PORT", "port": 8190, "health": "/system_stats",
     "ram_gb": 22, "placements": [{"gpus": [1, 0], "vram": [24000, 12000]}]},
    {"id": "yue2", "category": "Muzyka", "name": "YUE2 muzyka (GPU1) + ComfyUI",
     "script": str(SERWER_DIR / "yue2.py"), "args": [],
     "stan": "stan-yue2-8188.json", "stan_match": {},
     "gpu_env": None, "port_env": "YUE2_PORT", "port": 8188, "health": "/system_stats",
     "ram_gb": 3, "placements": [{"gpus": [1], "vram": [9000]}, {"gpus": [0], "vram": [9000]}]},
    {"id": "decyzja-nimble", "category": "Pomocnicze", "name": "DECYZJA model decyzyjny (Nimble)",
     "script": str(SERWER_DIR / "decyzja.py"), "args": ["nimble"], "profile": "nimble",
     "stan": "stan-decyzja-8036.json", "stan_match": {"profil": "nimble"},
     "gpu_env": "DECYZJA_GPU", "port_env": "DECYZJA_PORT", "port": 8036, "health": "/v1/models",
     "ram_gb": 2, "placements": [{"gpus": [0], "vram": [6500]}, {"gpus": [1], "vram": [6500]}]},
]
KANON_IDS = {k["id"] for k in KANON}
ZACHOWAJ_POLA = ("ctx", "extra_args", "env", "enabled")  # edycje uzytkownika maja pierwszenstwo


def desktop_name(script: str, args: list[str], desktop_dir: Path | None = None) -> str | None:
    """Nazwa wyswietlowa = pole Name ikony .desktop, ktorej Exec uruchamia ten skrypt (SPEC §11a)."""
    for c in desktop_candidates(desktop_dir, models=[]):
        if c["script"] == script and c["args"] == list(args):
            return c["name"]
    return None


def migrate(models_path: Path | None = None, layout_path: Path | None = None,
            desktop_dir: Path | None = None) -> dict:
    """Rejestr rundy 2: 8 modeli z tabeli (kind=launcher, kategorie), reszta enabled: false."""
    models = load(models_path)
    by_id = {m["id"]: m for m in models}
    porzadek: list[dict] = []
    for i, spec in enumerate(KANON):
        old = by_id.get(spec["id"]) or {}
        merged = {**spec, "kind": "launcher", "order": i}
        for key in ZACHOWAJ_POLA:
            if old.get(key) not in (None, "", [], {}):
                merged[key] = old[key]
        merged["name"] = desktop_name(spec["script"], spec["args"], desktop_dir) or old.get("name") or spec["name"]
        entry = normalize(merged)
        porzadek.append(entry)
    for m in models:  # stare wpisy zostaja, ale sa ukryte (enabled: false) — nie kasujemy
        if m["id"] in KANON_IDS:
            continue
        m = dict(m)
        m["enabled"] = False
        m["category"] = m["category"] or DEFAULT_CATEGORY
        porzadek.append(normalize(m))
    save(porzadek, models_path)
    layout = load_layout(layout_path)
    for cat in KATEGORIE + categories(porzadek):  # kategorie tabeli + kategorie starych wpisow
        layout["categories"].append({"name": cat, "collapsed": False})
    layout = save_layout(layout, layout_path)  # save_layout normalizuje (usuwa duplikaty)
    return {"models": porzadek, "layout": layout}
