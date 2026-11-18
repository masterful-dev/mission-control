"""CENTRUM MISJI — backend FastAPI (port 8770, tylko 127.0.0.1).

/api/state + WebSocket /ws (pelny stan co 250 ms) + REST: rejestr modeli, start/stop/switch/log,
sieroty, zwalnianie GPU (SPEC §6).
"""
from __future__ import annotations

import asyncio
import json
import threading
import time
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, Response
from fastapi.staticfiles import StaticFiles

from . import energia, ladowanie, llm_live, planner, procs, raport_energii, registry, telemetry

BASE_DIR = Path(__file__).resolve().parent.parent
WEB_DIR = BASE_DIR / "web"
HOST, PORT = "127.0.0.1", 8770
WS_INTERVAL = 0.25  # s
EDITABLE_FIELDS = {"name", "ctx", "extra_args", "port", "placements", "env", "cmd", "cwd", "health",
                   "ram_gb", "enabled", "category", "order", "kind", "script", "args", "stan",
                   "stan_match", "gpu_env", "port_env", "profile", "pin"}

app = FastAPI(title="Centrum Misji", version="0.1.0")

_built = 0            # ile razy zbudowano pelny stan (test: <= 5x/s niezaleznie od liczby okien)
_built_lock = threading.Lock()


def state_builds() -> int:
    return _built


def current_state() -> dict:
    global _built
    with _built_lock:
        _built += 1
    # KOPIE wpisow z rejestru — pola robocze (status, pids, plan...) nie moga trafiac do cache registry
    models = [m for m in registry.load_copy() if m.get("enabled", True)]
    state = procs.refresh(models)
    running = procs.running_entries(models, state)
    gpus = telemetry.read_gpus()
    ram = telemetry.read_ram()
    margin = energia.margines_vram()
    decisions = planner.plan(models, running, gpus, ram["available_gb"], margin)
    zmierzone = procs.zmierzony_vram(state, gpus)
    ram_modeli = procs.zmierzony_ram(state)
    for m in models:
        d = decisions.get(m["id"], {})
        entry = state.get(m["id"]) or {}
        if not d.get("can_start") and not entry:   # 15.10: zajete przez inne modele -> PRZEŁĄCZ (kogo zatrzyma)
            d = planner.plan_przelaczenia(m, running, gpus, ram["available_gb"], zmierzone, zmierzony_ram=ram_modeli,
                                          margin=margin)
        m["zatrzymaj"] = d.get("zatrzymaj") or []
        m["status"] = entry.get("status", "stopped")
        m["can_start"] = d.get("can_start", False)
        m["plan_placement"] = d.get("placement")
        m["plan_reason"] = d.get("reason", "")
        m["switch_from"] = d.get("switch_from")
        m["running_placement"] = entry.get("placement")
        m["pids"] = sorted(procs._entry_all_pids(entry, procs.ppid_map())) if entry else []
        m["adopted"] = bool(entry.get("adopted"))
        m["started"] = entry.get("started")
        if m["status"] == "failed":
            m["log_tail"] = entry.get("log_tail") or []
        if m["status"] == "starting":
            m["ladowanie"] = ladowanie.postep(m, entry, gpus)
    snap = telemetry.snapshot(models=models, orphans=procs.orphans())
    snap["layout"] = registry.pelny_layout(registry.load_layout())
    snap["categories"] = registry.categories(models)
    snap["energia"] = miernik.podsumowanie() if miernik else energia.agregaty()
    snap["llm"] = llm_live.stan(models, state)
    _opisz_procesy_gpu(snap, models, state)
    return snap


def _opisz_procesy_gpu(snap: dict, models: list[dict], state: dict) -> None:
    """Podpis procesow na kartach: nazwa modelu Centrum (teze przejete drzewo procesu)
    albo znacznik system (GPU2 / lista chroniona) — UI nie pokazuje wtedy „Zwolnij"."""
    owner = procs.pid_owner(models, state)
    for g in snap["gpus"]:
        for p in g.get("processes") or []:
            pid = p.get("pid")
            if pid in owner:
                p["model"], p["system"] = owner[pid], False
            else:
                p["model"], p["system"] = None, procs.is_system(pid, g.get("index"))


@app.get("/api/state")
def api_state() -> Response:
    return Response(content=latest_state_json(), media_type="application/json")


@app.get("/api/models")
def api_models() -> list[dict]:
    return registry.load_copy()


@app.post("/api/models")
def api_add_model(model: dict) -> dict:
    models = registry.load_copy()   # KOPIE — nie mutujemy obiektow cache _load_cache
    mid = (model or {}).get("id")
    if not mid:
        raise HTTPException(400, "model musi miec id")
    if any(m["id"] == mid for m in models):
        raise HTTPException(409, f"id {mid} juz w rejestrze")
    entry = registry.normalize(model)
    _waliduj_model(entry)
    models.append(entry)
    registry.save(models)
    return entry


@app.put("/api/models/order")
def api_models_order(body: dict) -> dict:
    """UWAGA: musi byc zarejestrowany PRZED /api/models/{model_id} — inaczej 'order' jest id modelu."""
    try:
        return registry.apply_order(body or {})
    except ValueError as exc:
        raise HTTPException(400, str(exc)) from exc


@app.put("/api/models/{model_id}/pin")
def api_model_pin(model_id: str, body: dict) -> dict:
    """02.11: przypiąć/odpiąć model na pozycji na liście (UI: prawy przycisk myszy).
    Body {pin: 1|0|true|false}. 1 = model nie przesuwa się przy sortowaniu."""
    raw = (body or {}).get("pin", 1)
    if isinstance(raw, str) and raw.strip().lower() in ("0", "false", "off", "no"):
        raw = 0
    try:
        return registry.apply_pin(model_id, bool(raw))
    except ValueError as exc:
        raise HTTPException(400, str(exc)) from exc


@app.put("/api/models/{model_id}")
def api_edit_model(model_id: str, patch: dict) -> dict:
    models = registry.load_copy()   # KOPIE — nie mutujemy obiektow cache _load_cache
    for i, m in enumerate(models):
        if m["id"] != model_id:
            continue
        allowed = {k: v for k, v in (patch or {}).items() if k in EDITABLE_FIELDS}
        entry = registry.normalize({**m, **allowed})
        _waliduj_model(entry)
        if procs.load_state().get(model_id) and any(k in allowed for k in ("port", "placements", "script", "kind")):
            raise HTTPException(409, f"model {model_id} dziala — nie edytuj portu/wariantow/skryptu w trakcie pracy")
        models[i] = entry
        registry.save(models)
        return entry
    raise HTTPException(404, f"brak modelu {model_id}")


@app.delete("/api/models/{model_id}")
def api_remove_model(model_id: str) -> dict:
    if procs.load_state().get(model_id) or procs.adopted_entry(registry.load_by_id(model_id) or {}):
        raise HTTPException(409, f"model {model_id} dziala — najpierw go zatrzymaj")
    models = registry.load_copy()   # KOPIE — nie mutujemy obiektow cache _load_cache
    for i, m in enumerate(models):
        if m["id"] != model_id:
            continue
        m["enabled"] = False  # nie kasujemy wpisu — tylko wylaczamy
        registry.save(models)
        return m
    raise HTTPException(404, f"brak modelu {model_id}")


@app.post("/api/import-llmpy")
def api_import_llmpy() -> dict:
    added = registry.import_llmpy()
    return {"added": [m["id"] for m in added]}


@app.get("/api/import-desktop")
def api_import_desktop() -> list[dict]:
    """Ikony z ~/Pulpit uruchamiajace python3 <skrypt>, ktorych nie ma w rejestrze (§11a)."""
    return registry.desktop_candidates()


@app.post("/api/import-desktop")
def api_add_desktop(items: list[dict]) -> dict:
    added = registry.import_desktop(items or [])
    return {"added": [m["id"] for m in added]}


@app.post("/api/migrate")
def api_migrate() -> dict:
    out = registry.migrate()
    return {"models": [m["id"] for m in out["models"]]}


# ---------- layout i kolejnosc (§11b) ----------

@app.get("/api/layout")
def api_layout() -> dict:
    return registry.load_layout()


@app.put("/api/layout")
def api_save_layout(body: dict) -> dict:
    body = body or {}
    if not body.get("sizes"):   # klient bez rozmiarow (stary UI / first frame) — nie kasujemy zapisanych (§12b)
        body = {**body, "sizes": registry.load_layout().get("sizes", {})}
    if not body.get("okna"):   # §13: klient bez okien (drag kategorii/modelow) — NIE resetujemy ulozenia
        body = {**body, "okna": registry.load_layout().get("okna", {})}
    layout, blad = registry.validate_layout(body)
    if layout is None:
        raise HTTPException(400, blad)
    return registry.save_layout(layout)


def _model_or_404(model_id: str) -> dict:
    model = next((m for m in registry.load() if m["id"] == model_id), None)
    if model is None:
        raise HTTPException(404, f"brak modelu {model_id}")
    return model


def _waliduj_model(entry: dict) -> None:
    """Port musi byc liczba — planner/plan() licza rezerwacje i zajecie portu na int(port)."""
    port = entry.get("port")
    if port not in (None, ""):
        try:
            p = int(port)
        except (TypeError, ValueError) as exc:
            raise HTTPException(400, f"port musi byc liczba (bylo: {port!r})") from exc
        if not 1 <= p <= 65535:
            raise HTTPException(400, f"port poza zakresem: {p}")
    if entry.get("kind") == "cmd" and not (entry.get("cmd") or []):
        raise HTTPException(400, "kind=cmd wymaga niepustej komendy")
    if entry.get("kind") == "launcher" and not (entry.get("script") or entry.get("profile")):
        raise HTTPException(400, "kind=launcher wymaga skryptu lub profilu")


def _placement_index(body) -> int:
    raw = (body or {}).get("placement_index")
    try:
        idx = int(raw or 0)
    except (TypeError, ValueError) as exc:
        raise HTTPException(400, f"placement_index musi byc liczba (bylo: {raw!r})") from exc
    if idx < 0:
        raise HTTPException(400, "placement_index nie moze byc ujemny")
    return idx


def _proc(fn, *args) -> dict:
    try:
        return fn(*args)
    except procs.ProcError as exc:
        raise HTTPException(409, str(exc)) from exc


@app.post("/api/models/{model_id}/start")
def api_start(model_id: str, body: dict | None = None) -> dict:
    model = _model_or_404(model_id)
    idx = _placement_index(body)
    if idx >= len(model.get("placements") or []):
        raise HTTPException(400, f"brak wariantu rozmieszczenia nr {idx}")
    decision = _plan_startu(model)
    if decision.get("can_start"):
        return procs.zlec_start(model, idx)
    if decision.get("zatrzymaj"):   # 15.10: zajete przez inne modele — jednym kliknieciem zatrzymaj je i laduj ten
        return procs.zlec_start(model, _indeks_wariantu(model, decision.get("placement"), idx), decision["zatrzymaj"])
    raise HTTPException(409, decision.get("reason") or "model nie miesci sie na GPU")


def _plan_startu(model: dict, wymus: tuple = ()) -> dict:
    """Decyzja startu: czy sie miesci, a jesli nie — ktore modele Centrum zatrzymac (port / te same karty)."""
    models = [m for m in registry.load() if m.get("enabled", True)]
    state = procs.load_state()
    gpus = telemetry.read_gpus()
    return planner.plan_przelaczenia(model, procs.running_entries(models, state), gpus,
                                     telemetry.read_ram()["available_gb"], procs.zmierzony_vram(state, gpus), wymus,
                                     procs.zmierzony_ram(state), margin=energia.margines_vram())


def _indeks_wariantu(model: dict, placement: dict | None, domyslny: int = 0) -> int:
    pl = model.get("placements") or []
    return next((i for i, p in enumerate(pl) if p == placement), domyslny)


@app.post("/api/models/{model_id}/stop")
def api_stop(model_id: str, force: int = 0) -> dict:
    _model_or_404(model_id)
    return _proc(procs.stop, model_id, bool(force))


@app.post("/api/models/{model_id}/switch")
def api_switch(model_id: str, body: dict | None = None) -> dict:
    to = (body or {}).get("to")
    if not to:
        raise HTTPException(400, "body musi zawierac 'to'")
    if to == model_id:
        raise HTTPException(400, "nie mozna przełączyc modelu na niego samego")
    _model_or_404(model_id)
    target = registry.load_by_id(to)
    if not target:
        raise HTTPException(409, f"brak modelu {to} w rejestrze")
    decision = _plan_startu(target, wymus=(model_id,))
    if not decision.get("can_start") and not decision.get("zatrzymaj"):
        raise HTTPException(409, decision.get("reason") or "model nie miesci sie na GPU")
    zatrzymaj = list(dict.fromkeys([model_id] + list(decision.get("zatrzymaj") or [])))
    r = procs.zlec_start(target, _indeks_wariantu(target, decision.get("placement")), zatrzymaj)
    return {**r, "stopped": model_id, "started": to}


@app.get("/api/models/{model_id}/log")
def api_log(model_id: str, n: int = 200) -> dict:
    _model_or_404(model_id)
    return {"id": model_id, "lines": procs.log_lines(model_id, max(1, min(n, 2000)))}


@app.post("/api/orphans/{pid}/kill")
def api_kill_orphan(pid: int) -> dict:
    return _proc(procs.kill_orphan, pid)


@app.post("/api/gpus/{index}/free")
def api_free_gpu(index: int) -> dict:
    if not any(g.get("index") == index for g in telemetry.read_gpus()):
        raise HTTPException(404, f"karta GPU {index} nie istnieje")
    return _proc(procs.free_gpu, index)


# ---------- dziennik energii (§12a) ----------

@app.get("/api/energia")
def api_energia() -> dict:
    out = energia.agregaty()
    cfg = energia.load_config()
    out.update(miernik.podsumowanie() if miernik else {"teraz_w": None, "teraz_w_gniazdko": None,
                                                        "szacunek": None})
    out["psu_sprawnosc"] = cfg["psu_sprawnosc"]
    out.setdefault("urzadzenia", cfg["urzadzenia"])
    out.setdefault("urzadzenia_w", cfg["urzadzenia_w"])
    return out

@app.put("/api/energia/config")
def api_energia_config(body: dict | None = None) -> dict:
    try:
        cfg = energia.save_config(body or {})
    except ValueError as exc:
        raise HTTPException(400, str(exc)) from exc
    if miernik:
        miernik._sprawnosc = cfg["psu_sprawnosc"]
        miernik._cfg, miernik._spr_t = cfg, miernik.monotonic()   # nowa moc urządzeń od razu
    return cfg


@app.get("/api/energia.csv")
def api_energia_csv(od: str | None = None, do: str | None = None) -> Response:
    for nazwa, wart in (("od", od), ("do", do)):
        if wart not in (None, "") and energia._ts_dnia(wart) is None:
            raise HTTPException(400, f"{nazwa} musi byc datem YYYY-MM-DD (bylo: {wart!r})")
    csv = energia.csv_tekst(energia.DB_PATH, od, do)
    return Response(content=csv, media_type="text/csv; charset=utf-8",
                    headers={"Content-Disposition": 'attachment; filename="energia.csv"'})


def _raport_dane(od: str | None, do: str | None) -> dict:
    d_od, d_do = raport_energii.okres_domyslny()
    try:
        return raport_energii.dane(od or d_od, do or d_do)
    except ValueError as exc:
        raise HTTPException(400, str(exc))


@app.get("/api/energia/raport")
def api_energia_raport(od: str | None = None, do: str | None = None, format: str = "html") -> Response:
    d = _raport_dane(od, do)
    if format == "md":
        return Response(content=raport_energii.markdown(d), media_type="text/markdown; charset=utf-8")
    return Response(content=raport_energii.html_strona(d), media_type="text/html; charset=utf-8")


@app.post("/api/energia/raport/zapisz")
def api_energia_raport_zapisz(od: str | None = None, do: str | None = None) -> dict:
    d = _raport_dane(od, do)
    try:
        plik = raport_energii.zapisz(d)
    except OSError as exc:
        raise HTTPException(500, f"nie udalo sie zapisac raportu: {exc}")
    return {"plik": str(plik), "kwh": round(d["kwh"], 3)}


@app.get("/api/health")
def api_health() -> dict:
    return {"ok": True, "t": time.time()}


# ---------- producent stanu: JEDEN wattek liczy pelny stan co 250 ms ----------
# WS i /api/state tylko wysylaja gotowy JSON — petla zdarzen nie buduje stanu dla kazdego klienta.

_producer_thread: threading.Thread | None = None
_producer_payload = ""
_producer_lock = threading.Lock()
_producer_stop = threading.Event()
_miernik_stop = threading.Event()
_miernik_thread: threading.Thread | None = None
miernik: energia.Meter | None = None


def _producer_loop() -> None:
    while not _producer_stop.is_set():
        t0 = time.monotonic()
        try:
            payload = json.dumps(current_state(), ensure_ascii=False)
            with _producer_lock:
                global _producer_payload
                _producer_payload = payload
        except Exception as exc:  # blad telemetrii/NVML — utrzymaj ostatnia dobra ramke
            print(f"[CENTRUM] blad budowy stanu: {exc}", flush=True)
        _producer_stop.wait(max(0.0, WS_INTERVAL - (time.monotonic() - t0)))


def latest_state_json() -> str:
    with _producer_lock:
        if _producer_payload:
            return _producer_payload
    return json.dumps(current_state(), ensure_ascii=False)


@asynccontextmanager
async def lifespan(app: FastAPI):
    global _producer_thread, _producer_payload, _built
    _producer_stop.clear()
    with _producer_lock:
        _producer_payload = ""   # nowa aplikacja (teze testy z innym rejestrem) = nowy stan
    with _built_lock:
        _built = 0               # licznik budowy stanu liczony od startu tej aplikacji
    _producer_thread = threading.Thread(target=_producer_loop, name="centrum-producent", daemon=True)
    _producer_thread.start()
    global miernik, _miernik_thread
    _miernik_stop.clear()
    try:
        miernik = energia.Meter()
        _miernik_thread = threading.Thread(target=miernik.run, args=(_miernik_stop,),
                                          name="centrum-energia", daemon=True)
        _miernik_thread.start()
    except Exception as exc:      # baza energii niedostepna (np. tylko do odczytu) — UI bez panelu energii
        print(f"[CENTRUM] blad dziennika energii (start): {exc}", flush=True)
        miernik = None
    try:
        yield
    finally:
        _producer_stop.set()
        _miernik_stop.set()
        thread, _producer_thread = _producer_thread, None
        if thread is not None:
            thread.join(timeout=2)
        mt, _miernik_thread = _miernik_thread, None
        m, miernik = miernik, None
        if mt is not None:
            mt.join(timeout=3)
        if m is not None:
            m.zamknij()


app.router.lifespan_context = lifespan


@app.websocket("/ws")
async def ws(ws: WebSocket):
    await ws.accept()
    loop = asyncio.get_event_loop()
    last = loop.time()
    try:
        while True:
            now = loop.time()
            if now - last >= WS_INTERVAL:
                last = now
                await ws.send_text(latest_state_json())
            else:
                await asyncio.sleep(WS_INTERVAL - (now - last))
    except (WebSocketDisconnect, RuntimeError):
        return
    except Exception:
        try:
            await ws.close()
        except Exception:
            return


@app.middleware("http")
async def ui_bez_cache(request, call_next):
    """Pliki UI zawsze rewalidowane (ETag -> 304): po aktualizacji okno Centrum od razu bierze nowy JS/CSS."""
    resp = await call_next(request)
    if request.url.path == "/" or request.url.path.startswith("/web/"):
        resp.headers["Cache-Control"] = "no-cache"
    return resp


if WEB_DIR.is_dir():
    app.mount("/web", StaticFiles(directory=str(WEB_DIR)), name="web")

    @app.get("/")
    def index():
        return FileResponse(str(WEB_DIR / "index.html"))
