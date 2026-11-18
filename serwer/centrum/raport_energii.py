"""Raport zużycia energii za wybrany okres (strona HTML do druku/PDF + Markdown do H:\\!raporty).

Dane z energia.db (wiersz = minuta): wh_gniazdko (oba zasilacze, przez swoje sprawności), wh_dc,
gpu0_wh/gpu1_wh (V100 na osobnym zasilaczu), sekund (faktycznie zmierzony czas). Przerwa = brak wierszy.
Raport podaje dokładne okresy pracy (od–do co do minuty), zużycie w każdej godzinie i podział na strefy
taryfowe z taryfa.yaml (np. G12 dzień/noc) z kosztem, gdy wpisano ceny.
"""
from __future__ import annotations

import html
from datetime import date, datetime
from pathlib import Path

import yaml

from . import energia

RAPORTY_DIR = Path("/mnt/H/!raporty")
TARYFA_PATH = energia.BASE_DIR / "taryfa.yaml"
PRZERWA_S = 120          # dłuższa luka między minutami = nowy okres pracy


def okres_domyslny(dzis: date | None = None) -> tuple[str, str]:
    d = dzis or date.today()
    return d.replace(day=1).isoformat(), d.isoformat()


# ---------- taryfa ----------

def _minuta_dnia(hhmm: str) -> int:
    h, m = str(hhmm).strip().split(":")
    return int(h) * 60 + int(m)


def wczytaj_taryfe(path: Path | None = None) -> dict:
    try:
        t = yaml.safe_load(Path(path or TARYFA_PATH).read_text(encoding="utf-8")) or {}
    except (OSError, yaml.YAMLError):
        t = {}
    strefy = []
    for s in t.get("strefy") or []:
        przedzialy = None
        if s.get("godziny") != "reszta":
            przedzialy = []
            for p in s.get("godziny") or []:
                a, b = str(p).split("-")
                przedzialy.append((_minuta_dnia(a), _minuta_dnia(b)))
        cena = s.get("cena")
        strefy.append({"nazwa": str(s.get("nazwa") or "strefa"), "przedzialy": przedzialy,
                       "cena": float(cena) if cena not in (None, "") else None})
    if not strefy:
        strefy = [{"nazwa": "całodobowa", "przedzialy": None, "cena": None}]
    return {"nazwa": str(t.get("nazwa") or "—"), "strefy": strefy, "weekend": t.get("weekend_cala_doba")}


def strefa(ts: int, taryfa: dict) -> str:
    dt = datetime.fromtimestamp(ts)
    if taryfa.get("weekend") and dt.weekday() >= 5:
        return str(taryfa["weekend"])
    m = dt.hour * 60 + dt.minute
    reszta = None
    for s in taryfa["strefy"]:
        if s["przedzialy"] is None:
            reszta = reszta or s["nazwa"]
            continue
        for a, b in s["przedzialy"]:
            if (a <= m < b) if a < b else (m >= a or m < b):     # przedział przez północ, np. 22:00-06:00
                return s["nazwa"]
    return reszta or taryfa["strefy"][-1]["nazwa"]


# ---------- dane ----------

def dane(od: str, do: str, db_path: Path | None = None, taryfa: dict | None = None) -> dict:
    t0, t1 = energia._ts_dnia(od), energia._ts_dnia(do, koniec=True)
    if t0 is None or t1 is None or t1 < t0:
        raise ValueError(f"zły okres: od={od!r} do={do!r} (format YYYY-MM-DD, od ≤ do)")
    tar = taryfa or wczytaj_taryfe()
    out = {"od": od, "do": do, "wygenerowano": datetime.now().strftime("%Y-%m-%d %H:%M"), "taryfa": tar,
           "dni": [], "sesje": [], "godziny": [], "strefy": [], "kwh": 0.0, "kwh_dc": 0.0, "kwh_gpu_dc": 0.0,
           "kwh_urzadzenia": 0.0, "urzadzenia_w": energia.load_config()["urzadzenia_w"],
           "godzin": 0.0, "w_max": 0.0, "w_sred": 0.0, "koszt": None,
           "dni_okresu": (datetime.strptime(do, "%Y-%m-%d") - datetime.strptime(od, "%Y-%m-%d")).days + 1,
           "dni_z_pomiarem": 0}
    p = Path(db_path) if db_path else energia.DB_PATH
    rows = []
    if p.exists():
        con = energia._polacz(p)
        try:
            energia._zapewnij_kolumne(con)
            rows = con.execute("SELECT ts_min, wh_gniazdko, wh_dc, gpu0_wh + gpu1_wh AS wh_gpu, sekund, w_max,"
                               " urzadzenia_wh FROM minuty WHERE ts_min BETWEEN ? AND ?"
                               " ORDER BY ts_min", (t0, t1)).fetchall()
        finally:
            con.close()
    dni: dict[str, dict] = {}
    godz: dict[int, dict] = {}
    strefy = {s["nazwa"]: 0.0 for s in tar["strefy"]}
    sesja = None
    for r in rows:
        ts, wh, s = int(r["ts_min"]), float(r["wh_gniazdko"] or 0), int(r["sekund"] or 0)
        dzien = datetime.fromtimestamp(ts).strftime("%Y-%m-%d")
        d = dni.setdefault(dzien, {"dzien": dzien, "kwh": 0.0, "s": 0, "w_max": 0.0})
        d["kwh"] += wh / 1000; d["s"] += s; d["w_max"] = max(d["w_max"], float(r["w_max"] or 0))
        h0 = ts - ts % 3600
        g = godz.setdefault(h0, {"ts": h0, "kwh": 0.0, "s": 0, "strefa": strefa(ts, tar)})
        g["kwh"] += wh / 1000; g["s"] += s
        nazwa = strefa(ts, tar)
        strefy[nazwa] = strefy.get(nazwa, 0.0) + wh / 1000
        if sesja and ts - sesja["koniec"] <= PRZERWA_S:
            sesja["koniec"] = ts + 60; sesja["kwh"] += wh / 1000; sesja["s"] += s
        else:
            sesja = {"start": ts, "koniec": ts + 60, "kwh": wh / 1000, "s": s}
            out["sesje"].append(sesja)
        out["kwh_dc"] += float(r["wh_dc"] or 0) / 1000
        out["kwh_gpu_dc"] += float(r["wh_gpu"] or 0) / 1000
        out["kwh_urzadzenia"] += float(r["urzadzenia_wh"] or 0) / 1000
    for d in dni.values():
        d["godzin"] = d["s"] / 3600
        d["w_sred"] = d["kwh"] * 1000 / d["godzin"] if d["godzin"] else 0.0
    out["dni"] = list(dni.values())
    out["godziny"] = [godz[k] for k in sorted(godz)]
    out["kwh"] = sum(d["kwh"] for d in out["dni"])
    out["godzin"] = sum(d["godzin"] for d in out["dni"])
    out["w_max"] = max((d["w_max"] for d in out["dni"]), default=0.0)
    out["w_sred"] = out["kwh"] * 1000 / out["godzin"] if out["godzin"] else 0.0
    out["dni_z_pomiarem"] = len(out["dni"])
    ceny = {s["nazwa"]: s["cena"] for s in tar["strefy"]}
    koszt_znany = True
    for nazwa, kwh in strefy.items():
        cena = ceny.get(nazwa)
        out["strefy"].append({"nazwa": nazwa, "kwh": kwh, "cena": cena,
                              "koszt": kwh * cena if cena is not None else None})
        if cena is None and kwh > 0:
            koszt_znany = False
    if koszt_znany and any(x["koszt"] is not None for x in out["strefy"]):
        out["koszt"] = sum(x["koszt"] or 0.0 for x in out["strefy"])
    return out


# ---------- formaty ----------

def _l(x: float, n: int = 2) -> str:
    return f"{x:.{n}f}".replace(".", ",")


def _hm(ts: int) -> str:
    return datetime.fromtimestamp(ts).strftime("%Y-%m-%d %H:%M")


def _zl(x) -> str:
    return "—" if x is None else f"{_l(x, 2)} zł"


def markdown(d: dict) -> str:
    gpu_udzial = d["kwh_gpu_dc"] / d["kwh_dc"] * 100 if d["kwh_dc"] else 0.0
    urz_udzial = d.get("kwh_urzadzenia", 0.0) / d["kwh"] * 100 if d["kwh"] else 0.0
    w = [f"# Zużycie energii — {d['od']} … {d['do']}", "",
         "| Pozycja | Wartość |", "|---|---|",
         f"| Zużycie z gniazdka (zasilacze + urządzenia stałe) | **{_l(d['kwh'], 3)} kWh** |",
         f"| w tym urządzenia stale włączone (monitory, audio) | {_l(d.get('kwh_urzadzenia', 0.0), 3)} kWh "
         f"({_l(urz_udzial, 0)}%) |",
         f"| Koszt (taryfa {d['taryfa']['nazwa']}) | {_zl(d['koszt'])} |",
         f"| Czas pracy komputera (zmierzony) | {_l(d['godzin'], 1)} h w {d['dni_z_pomiarem']} z {d['dni_okresu']} dni |",
         f"| Średnio na godzinę pracy | {_l(d['w_sred'], 0)} Wh |",
         f"| Najwyższy pobór (DC, minuta) | {_l(d['w_max'], 0)} W |",
         f"| Karty V100 (osobny zasilacz) | {_l(gpu_udzial, 0)}% energii DC |", "",
         f"## Strefy taryfowe ({d['taryfa']['nazwa']})", "", "| Strefa | kWh | Cena | Koszt |", "|---|---:|---:|---:|"]
    for s in d["strefy"]:
        w.append(f"| {s['nazwa']} | {_l(s['kwh'], 3)} | {'—' if s['cena'] is None else _l(s['cena'], 4) + ' zł/kWh'} | {_zl(s['koszt'])} |")
    w += ["", "## Okresy pracy (od – do)", "", "| Od | Do | Czas | kWh | Średnio W |", "|---|---|---:|---:|---:|"]
    for s in d["sesje"]:
        h = s["s"] / 3600
        w.append(f"| {_hm(s['start'])} | {_hm(s['koniec'])} | {_l(h, 2)} h | {_l(s['kwh'], 3)} | {_l(s['kwh'] * 1000 / h if h else 0, 0)} |")
    w += ["", "## Dni", "", "| Dzień | kWh | Godzin pracy | Średnio Wh/h | Max W |", "|---|---:|---:|---:|---:|"]
    for x in d["dni"]:
        w.append(f"| {x['dzien']} | {_l(x['kwh'], 3)} | {_l(x['godzin'], 1)} | {_l(x['w_sred'], 0)} | {_l(x['w_max'], 0)} |")
    w += ["", "## Godziny", "", "| Godzina | Strefa | kWh | Minut pomiaru |", "|---|---|---:|---:|"]
    for g in d["godziny"]:
        w.append(f"| {_hm(g['ts'])}–{datetime.fromtimestamp(g['ts'] + 3600).strftime('%H:%M')} | {g['strefa']} "
                 f"| {_l(g['kwh'], 4)} | {round(g['s'] / 60)} |")
    w += ["", f"Wygenerowano {d['wygenerowano']} w Centrum Misji. Pomiar co 1 s: zasilacz Corsair (CPU, RAM, dyski, "
          "wentylatory, T400) + moc kart V100 z osobnego zasilacza; z gniazdka = każdy zasilacz przez swoją sprawność "
          f"+ urządzenia stale włączone ({_l(d.get('urzadzenia_w', 0.0), 1)} W: monitory, interfejs audio). "
          "Gdy komputer jest wyłączony, nic nie jest liczone. Strefy i ceny: plik taryfa.yaml w katalogu Centrum Misji. "
          "Dane minutowe do własnych wyliczeń: przycisk POBIERZ CSV.", ""]
    return "\n".join(w)


def html_strona(d: dict) -> str:
    e = html.escape
    maks = max((x["kwh"] for x in d["dni"]), default=0.0) or 1.0
    slupki = "".join(
        f'<div class="b" title="{e(x["dzien"])}: {_l(x["kwh"], 3)} kWh"><i style="height:{x["kwh"] / maks * 100:.1f}%"></i>'
        f'<span>{e(x["dzien"][8:])}</span></div>' for x in d["dni"])

    def tabela(naglowki, wiersze, pusto="Brak pomiarów w tym okresie."):
        th = "".join(f"<th>{e(h)}</th>" for h in naglowki)
        tr = "".join("<tr>" + "".join(f"<td>{e(str(c))}</td>" for c in w) + "</tr>" for w in wiersze) or \
            f'<tr><td colspan="{len(naglowki)}">{pusto}</td></tr>'
        return f"<table><thead><tr>{th}</tr></thead><tbody>{tr}</tbody></table>"

    t_strefy = tabela(["Strefa", "kWh", "Cena", "Koszt"], [
        (s["nazwa"], _l(s["kwh"], 3), "—" if s["cena"] is None else _l(s["cena"], 4) + " zł/kWh", _zl(s["koszt"]))
        for s in d["strefy"]])
    t_sesje = tabela(["Od", "Do", "Czas", "kWh", "Średnio W"], [
        (_hm(s["start"]), _hm(s["koniec"]), _l(s["s"] / 3600, 2) + " h", _l(s["kwh"], 3),
         _l(s["kwh"] * 3600000 / s["s"] if s["s"] else 0, 0)) for s in d["sesje"]])
    t_dni = tabela(["Dzień", "kWh", "Godzin pracy", "Średnio Wh/h", "Max W"], [
        (x["dzien"], _l(x["kwh"], 3), _l(x["godzin"], 1), _l(x["w_sred"], 0), _l(x["w_max"], 0)) for x in d["dni"]])
    t_godz = tabela(["Godzina", "Strefa", "kWh", "Minut pomiaru"], [
        (_hm(g["ts"]) + "–" + datetime.fromtimestamp(g["ts"] + 3600).strftime("%H:%M"), g["strefa"],
         _l(g["kwh"], 4), round(g["s"] / 60)) for g in d["godziny"]])
    gpu_udzial = d["kwh_gpu_dc"] / d["kwh_dc"] * 100 if d["kwh_dc"] else 0.0
    urz_udzial = d.get("kwh_urzadzenia", 0.0) / d["kwh"] * 100 if d["kwh"] else 0.0
    q = f"od={e(d['od'])}&do={e(d['do'])}"
    return f"""<!doctype html><html lang="pl"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Energia {e(d['od'])} … {e(d['do'])}</title>
<style>
body{{margin:0;background:#020812;color:#dbe7f5;font:14px/1.5 Inter,system-ui,sans-serif}}
main{{max-width:960px;margin:0 auto;padding:24px 16px}}
h1{{font-size:20px;letter-spacing:.08em;text-transform:uppercase;color:#ff8a1f;margin:0 0 4px}}
h2{{font-size:14px;letter-spacing:.1em;text-transform:uppercase;color:#ff8a1f;margin:26px 0 4px}}
.mut{{color:#7f93ab}}
.kafle{{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px;margin:18px 0}}
.k{{border:1px solid #1a4d7a;border-radius:8px;padding:12px;background:rgba(8,24,44,.82)}}
.k b{{display:block;font:700 24px "JetBrains Mono",monospace;color:#38d6ff}} .k span{{font-size:12px;color:#7f93ab;text-transform:uppercase;letter-spacing:.1em}}
.wyk{{display:flex;align-items:flex-end;gap:4px;height:150px;border:1px solid #1a4d7a;border-radius:8px;padding:10px;margin:12px 0}}
.b{{flex:1;height:100%;display:flex;flex-direction:column;justify-content:flex-end;align-items:center}}
.b i{{display:block;width:100%;background:linear-gradient(#38d6ff,#0b6fa0);border-radius:3px 3px 0 0;min-height:1px}}
.b span{{font-size:10px;color:#7f93ab}}
.tw{{overflow-x:auto}} table{{width:100%;border-collapse:collapse;margin-top:8px}}
th,td{{padding:5px 8px;border-bottom:1px solid #12314f;text-align:right;white-space:nowrap}}
th:first-child,td:first-child{{text-align:left}} th{{color:#ff8a1f;font-size:12px;text-transform:uppercase;letter-spacing:.08em}}
td{{font-family:"JetBrains Mono",monospace;font-size:13px}}
.przyc{{display:flex;flex-wrap:wrap;gap:8px;margin-top:16px}} .przyc a,.przyc button{{background:transparent;color:#ff8a1f;border:1px solid #ff8a1f;border-radius:6px;padding:8px 14px;font:700 13px Inter,sans-serif;cursor:pointer;text-decoration:none}}
#info{{color:#22c55e;align-self:center}}
@media print{{body{{background:#fff;color:#000}} .k,.wyk{{background:#fff;border-color:#999}} .k b{{color:#000}} .przyc{{display:none}}
 .b i{{background:#555}} th,h1,h2{{color:#000}} td,th{{border-color:#ccc}}}}
</style></head><body><main>
<h1>Zużycie energii</h1><div class="mut">{e(d['od'])} … {e(d['do'])} · taryfa {e(d['taryfa']['nazwa'])} · wygenerowano {e(d['wygenerowano'])}</div>
<div class="kafle">
<div class="k"><span>Z gniazdka</span><b>{_l(d['kwh'], 3)} kWh</b></div>
<div class="k"><span>w tym urządzenia stałe</span><b>{_l(d.get('kwh_urzadzenia', 0.0), 3)} kWh</b><span>{_l(urz_udzial, 0)}% · monitory, audio</span></div>
<div class="k"><span>Koszt</span><b>{_zl(d['koszt'])}</b><span>{'ceny w taryfa.yaml' if d['koszt'] is None else ''}</span></div>
<div class="k"><span>Średnio na godzinę pracy</span><b>{_l(d['w_sred'], 0)} Wh</b></div>
<div class="k"><span>Czas pracy</span><b>{_l(d['godzin'], 1)} h</b><span>{d['dni_z_pomiarem']} z {d['dni_okresu']} dni</span></div>
<div class="k"><span>Max pobór (DC)</span><b>{_l(d['w_max'], 0)} W</b></div>
<div class="k"><span>Karty V100 (osobny PSU)</span><b>{_l(gpu_udzial, 0)}%</b><span>energii DC</span></div>
</div>
<h2>Strefy taryfowe</h2><div class="tw">{t_strefy}</div>
<h2>Okresy pracy (od – do)</h2><div class="tw">{t_sesje}</div>
<h2>Dni</h2><div class="wyk">{slupki or '<span class="mut">Brak pomiarów</span>'}</div><div class="tw">{t_dni}</div>
<h2>Godziny</h2><div class="tw">{t_godz}</div>
<p class="mut">Pomiar co 1 s: zasilacz Corsair (CPU, RAM, dyski, wentylatory, T400) + karty V100 z osobnego zasilacza;
z gniazdka = każdy zasilacz przez swoją sprawność + urządzenia stale włączone ({_l(d.get('urzadzenia_w', 0.0), 1)} W:
monitory, interfejs audio). Gdy komputer jest wyłączony, nic nie jest liczone.
Strefy i ceny: plik <code>taryfa.yaml</code> w katalogu Centrum Misji.</p>
<div class="przyc"><button onclick="window.print()">DRUKUJ / PDF</button>
<button onclick="zapisz()">ZAPISZ W H:\\!RAPORTY</button>
<a href="/api/energia.csv?{q}">MINUTY (CSV)</a><span id="info"></span></div>
</main><script>
async function zapisz(){{const r=await fetch('/api/energia/raport/zapisz?{q}',{{method:'POST'}});
const j=await r.json();document.getElementById('info').textContent=r.ok?('Zapisano: '+j.plik):('Błąd: '+(j.detail||r.status));}}
</script></body></html>"""


def zapisz(d: dict, katalog: Path | None = None) -> Path:
    kat = Path(katalog) if katalog else RAPORTY_DIR
    kat.mkdir(parents=True, exist_ok=True)
    plik = kat / f"ENERGIA--raport-{d['od']}_{d['do']}.md"
    plik.write_text(markdown(d), encoding="utf-8")
    return plik
