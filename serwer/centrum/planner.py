"""Planner miejsca na GPU — czyste funkcje, bez I/O.

plan(models, running, gpu_state, ram_free) -> {id: {can_start, placement, reason}}
"""
from __future__ import annotations

MARGIN_MB = 1024


def _gb(mb: float) -> str:
    return f"{mb / 1024:.1f}".replace(".", ",") + " GB"


def _placement_of(model: dict, index: int = 0) -> dict | None:
    pl = model.get("placements") or []
    return pl[index] if 0 <= index < len(pl) else None


def free_vram(gpu_state: list[dict], running: list, margin: int = MARGIN_MB) -> dict[int, int]:
    """Wolne VRAM karty = total - max(zmierzone, rezerwacje modeli) - margines.

    02.11: margines z config.yaml (vram_margin_mb, margines_vram() w energia) — Flash-Next 125B TP2
    nie startował domyślnie (1 GiB marginesu zacinał ~36 MB niedoboru przy realnym miejscu na karcie)."""
    measured: dict[int, int] = {}
    total: dict[int, int] = {}
    for g in gpu_state:
        i = g.get("index")
        if i is None:
            continue
        measured[i] = int(g.get("vram_used_mb") or 0)
        total[i] = int(g.get("vram_total_mb") or 0)

    reserved = {i: 0 for i in total}
    for entry in running:
        model, placement = _resolve_running(entry)
        if not placement:
            continue
        for gpu, mb in zip(placement.get("gpus") or [], placement.get("vram") or []):
            if gpu in reserved:
                reserved[gpu] += int(mb)

    return {i: total[i] - max(measured.get(i, 0), reserved.get(i, 0)) - int(margin) for i in total}


def _resolve_running(entry):
    """running to id modelu albo {id, placement} — rezerwacja liczona z rejestru."""
    if isinstance(entry, dict):
        model = entry.get("model") or entry
        placement = entry.get("placement") or _placement_of(model)
        return model, placement
    return entry, None


def _score(free: dict[int, int], placement: dict) -> int:
    """Po starcie: ile zostaje na najbardziej wolnej karcie (im więcej, tym lepiej)."""
    f = dict(free)
    for gpu, mb in zip(placement["gpus"], placement["vram"]):
        f[gpu] = f.get(gpu, 0) - int(mb)
    return max(f.values()) if f else 0


def _check_placement(placement, free, gpu_state) -> str | None:
    gpus = placement.get("gpus") or []
    vram = placement.get("vram") or []
    if not gpus or len(gpus) != len(vram):
        return "model nie ma wariantu rozmieszczenia"
    for gpu, mb in zip(gpus, vram):
        if gpu not in free:
            return f"GPU{gpu}: karta niedostępna"
        if free[gpu] < int(mb):
            return f"GPU{gpu}: potrzeba {_gb(mb)}, wolne {_gb(max(free[gpu], 0))}"
    return None


def plan(models: list[dict], running: list, gpu_state: list[dict], ram_free: float,
         margin: int = MARGIN_MB) -> dict:
    free = free_vram(gpu_state, running, margin)
    running_ids = {e if isinstance(e, str) else e.get("id") for e in running}
    port_owner: dict[int, tuple[str, str]] = {}
    for entry in running:
        model, _ = _resolve_running(entry)
        if isinstance(model, dict) and model.get("port"):
            port_owner.setdefault(int(model["port"]),
                                  (model.get("id"), model.get("name") or model.get("id")))

    out: dict[str, dict] = {}
    for model in models:
        mid = model.get("id")
        if not model.get("enabled", True):
            out[mid] = {"can_start": False, "placement": None, "reason": "model wyłączony"}
            continue
        if mid in running_ids:
            out[mid] = {"can_start": True, "placement": _placement_of(model),
                        "reason": "model już działa"}
            continue

        port = model.get("port")
        ram_gb = float(model.get("ram_gb") or 0)
        if port and int(port) in port_owner:
            owner_id, owner = port_owner[int(port)]
            out[mid] = {"can_start": False, "placement": None,
                        "reason": f"port {port} zajęty przez {owner}",
                        "switch_from": owner_id}  # UI: „Przełącz" = zatrzymaj tamten model i postaw ten
            continue

        if ram_free < ram_gb:
            out[mid] = {"can_start": False, "placement": None,
                        "reason": f"RAM: potrzeba {ram_gb:.0f} GB, wolne {ram_free:.0f} GB"}
            continue

        best = None
        best_score = None
        first_reason = None
        for placement in model.get("placements") or []:
            why = _check_placement(placement, free, gpu_state)
            if why:
                first_reason = first_reason or why
                continue
            score = _score(free, placement)
            # remis: wyżej polożona karta (GPU1 przed GPU0) — druga karta zostaje cala wolna
            key = (score, sorted(placement["gpus"], reverse=True))
            if best is None or key > best_score:
                best, best_score = placement, key

        if best is None:
            out[mid] = {"can_start": False, "placement": None,
                        "reason": first_reason or "model nie ma wariantu rozmieszczenia"}
            continue
        out[mid] = {"can_start": True, "placement": best, "reason": ""}
    return out


def plan_przelaczenia(model: dict, running: list, gpu_state: list[dict], ram_free: float,
                      zmierzone: dict | None = None, wymus: tuple = (), zmierzony_ram: dict | None = None,
                      margin: int = MARGIN_MB) -> dict:
    """15.10: model nie mieści się, bo zajmują port/karty inne modele Centrum -> które zatrzymać, żeby wystartował.

    Najpierw tylko właściciel portu (+ `wymus`), potem także modele na kartach, których ten model potrzebuje.
    Zatrzymywane modele nie liczą się ani jako rezerwacja, ani jako zmierzony VRAM (`zmierzone` = {id: {gpu: MiB}}),
    więc decyzja nie czeka, aż NVML pokaże zwolnioną pamięć. Wynik: decyzja plan() + `zatrzymaj` (lista id).
    16.10: zwalniany RAM = zmierzony (`zmierzony_ram` = {id: GB}), nie wpis `ram_gb` — Ornith TP2 ma wpis 2 GB,
    a trzyma 20 GB, więc Strata (47 GB) wychodziła „za mało RAM” i PRZEŁĄCZ znikał."""
    mid = model.get("id")
    zmierzone = zmierzone or {}
    zmierzony_ram = zmierzony_ram or {}
    baza = plan([model], running, gpu_state, ram_free, margin).get(mid, {})
    if baza.get("can_start") and not wymus:
        return {**baza, "zatrzymaj": []}
    wpisy = {}
    for e in running:
        m, pl = _resolve_running(e)
        if isinstance(m, dict) and m.get("id") and m.get("id") != mid:
            wpisy[m["id"]] = (m, pl)
    port = model.get("port")
    na_porcie = [i for i, (m, _) in wpisy.items() if port and m.get("port") and int(m["port"]) == int(port)]
    potrzebne = {g for p in model.get("placements") or [] for g in p.get("gpus") or []}
    na_kartach = [i for i, (m, pl) in wpisy.items()
                  if set((pl or {}).get("gpus") or []) & potrzebne or set(zmierzone.get(i) or {}) & potrzebne]
    proby = []
    for zestaw in (list(wymus) + na_porcie, list(wymus) + na_porcie + na_kartach):
        zestaw = list(dict.fromkeys(z for z in zestaw if z in wpisy))
        if zestaw and zestaw not in proby:
            proby.append(zestaw)
    for zestaw in proby:
        reszta = [e for e in running if (_resolve_running(e)[0] or {}).get("id") not in zestaw]
        stan = []
        for g in gpu_state:
            g2 = dict(g)
            zwolnione = sum(int((zmierzone.get(z) or {}).get(g.get("index"), 0)) for z in zestaw)
            g2["vram_used_mb"] = max(0, int(g.get("vram_used_mb") or 0) - zwolnione)
            stan.append(g2)
        ram = ram_free + sum(max(float(zmierzony_ram.get(z) or 0), float(wpisy[z][0].get("ram_gb") or 0))
                             for z in zestaw)
        d = plan([model], reszta, stan, ram, margin).get(mid, {})
        if d.get("can_start"):
            nazwy = ", ".join(wpisy[z][0].get("name") or z for z in zestaw)
            return {"can_start": False, "placement": d.get("placement"), "zatrzymaj": zestaw,
                    "switch_from": zestaw[0], "reason": f"PRZEŁĄCZ zatrzyma: {nazwy}"}
    if proby:   # prawdziwy powód (np. RAM / obcy proces na karcie) zamiast mylącego „port zajęty”
        return {**baza, "reason": f"nawet po zatrzymaniu modeli: {d.get('reason') or baza.get('reason')}",
                "zatrzymaj": []}
    return {**baza, "zatrzymaj": []}
