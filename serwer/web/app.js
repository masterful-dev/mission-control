/* CENTRUM MISJI — HUD frontend (§7 + §11b/§11c/§11d + §13).
   Listens on WebSocket /ws (full state every 250 ms), only updates changed DOM values.
   §13: panels = free windows in PIXELS (web/okna.js, pointer events — NOT HTML5 DnD),
   categories and models draggable (HTML5 drag&drop). Zero libraries, zero CDN. */
import {
  MIN_OKNO_W, PANELS as PANELE_LAYOUT,
  rozsun, przycin, naprawUklad, domyslneOkna, waskiUklad, wysokoscKontenera,
  podlaczPrzenoszenie, podlaczRozmiar,
} from "./okna.js";

const $ = (id) => document.getElementById(id);
const GB = (mb) => (mb / 1024).toFixed(1).replace(".", ",");
const fmtTime = (d) => d.toTimeString().slice(0, 8);
const CYAN = "#38d6ff", ORANGE = "#ff8a1f", BAD = "#ff4d4d", OK = "#22c55e", AMBER = "#f59e0b";

/* ---------------- WebSocket ---------------- */
let ws = null, ostatniStan = null;

function polacz() {
  const prot = location.protocol === "https:" ? "wss" : "ws";
  ws = new WebSocket(`${prot}://${location.host}/ws`);
  ws.onopen = () => setKropka(true);
  ws.onmessage = (e) => { try { render(JSON.parse(e.data)); } catch (err) { console.error(err); } };
  ws.onclose = () => { setKropka(false); setTimeout(polacz, 1000); };
  ws.onerror = () => { try { ws.close(); } catch (_) {} };
}
function setKropka(on) {
  const el2 = $("wsStatus");
  el2.className = "kropka " + (on ? "on" : "off");
  el2.title = on ? "WebSocket: połączony" : "WebSocket: rozłączony — ponawiam co 1 s";
}

/* ---------------- REST ---------------- */
async function api(method, path, body) {
  const res = await fetch(path, body !== undefined
    ? { method, headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) }
    : { method });
  const data = await res.json().catch(() => ({}));
  if (!res.ok) throw new Error(data.detail || `HTTP ${res.status}`);
  return data;
}

let toastTimer = null;
function toast(text, err) {
  const t = $("toast");
  t.textContent = text;
  t.className = "toast" + (err ? " err" : "");
  t.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { t.hidden = true; }, 4000);
}
function akcja(fn, errPrefiks) {
  fn().catch((e) => toast(`${errPrefiks}: ${e.message}`, true));
}

/* ---------------- pomocnicy DOM ---------------- */
function setText(el2, txt) { if (el2 && el2.textContent !== txt) el2.textContent = txt; }
function setBar(el2, pct, klasa) {
  if (!el2) return;
  const w = Math.max(0, Math.min(100, pct)) + "%";
  if (el2.style.width !== w) el2.style.width = w;
  if (el2.dataset.k !== klasa) { el2.dataset.k = klasa; el2.className = klasa || ""; }
}
function klasaPct(p) { return p >= 90 ? "bad" : p >= 70 ? "warn" : ""; }
function klasaTemp(t) { return t === null || t === undefined ? "" : t >= 85 ? "bad" : t >= 75 ? "warn" : ""; }
function tempTxt(t) { return t === null || t === undefined ? "—" : String(Math.round(t)); }
function el(tag, klasa, html) {
  const n = document.createElement(tag);
  if (klasa) n.className = klasa;
  if (html !== undefined) n.innerHTML = html;
  return n;
}
/* esc() — EVERY value from outside the app (process cmdline, model name, .desktop path, hwmon label)
   goes into innerHTML only through esc(): otherwise a foreign process with `<img onerror=...>` in cmdline
   executes code in the Centrum UI with Centrum's permissions (AUDIT: XSS in orphans/imports/names). */
function esc(v) {
  return String(v === null || v === undefined ? "" : v)
    .replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;").replace(/'/g, "&#39;");
}
function ikona(id) { return `<svg class="ic"><use href="#${id}"/></svg>`; }
function uptimeTxt(s) {
  if (!s) return "—";
  if (s < 90) return `${s} s`;
  if (s < 5400) return `${Math.round(s / 60)} min`;
  return `${(s / 3600).toFixed(1).replace(".", ",")} h`;
}

/* ---------------- modale ---------------- */
function otworz(id) { $(id).hidden = false; }
function zamknij(id) { $(id).hidden = true; }
document.addEventListener("click", (e) => {
  const c = e.target.closest("[data-close]");
  if (c) zamknij(c.dataset.close);
  if (e.target.classList.contains("modal")) e.target.hidden = true;
});
document.addEventListener("keydown", (e) => {
  if (e.key === "Escape") document.querySelectorAll(".modal, .sidepanel").forEach((n) => { n.hidden = true; });
});

let potwierdzenie = null;
function potwierdz(text, onGo) {
  $("confirmText").textContent = text;
  $("confirmErr").textContent = "";
  potwierdzenie = async () => {
    try { await onGo(); zamknij("confirmModal"); }
    catch (e) { $("confirmErr").textContent = e.message; }
  };
  otworz("confirmModal");
}
$("confirmGo").onclick = () => potwierdzenie && potwierdzenie();

/* ---------------- panel layout (§13: free windows in pixels, web/okna.js) ---------------- */
let uklad = { panels: PANELE_LAYOUT.slice(), cats: [] };
let czekamLayout = false, czekamOrder = false, drag = null;

/* §13: `okna` = LIVE geometry (clipped to current width, without intersections) — we draw this
   and it moves with the pointer. `oknaZapisane` = the last geometry saved/loaded from the server —
   the layout is restored from it after widening the window (user positions are NOT overwritten by clipping). */
let okna = {}, oknaZapisane = {}, oknoAktywne = null;
let serwerKlucz = "", poZapisie = 0;   // §13: starsza klatka /ws (sprzed naszego PUT) nie moze cofnac ulozenia
const PAD = 12;                      // margines kontenera (CSS .grid padding)
let zapisTimer = null;

const PANELE = {
  gpu: { title: "GPU", buduj: (b, h) => budujGPU(b, h) },
  llm: { title: "Model LLM — zapytania", buduj: (b, h) => budujLLM(b, h) },
  system: { title: "System", buduj: (b, h) => budujSystem(b, h) },
  models: { title: "Modele", buduj: (b, h) => budujModele(b, h) },
  energia: { title: "Energia", buduj: (b, h) => budujEnergia(b, h) },
};
const panelEls = new Map();

function panelEl(id) {
  let sec = panelEls.get(id);
  if (sec) return sec;
  const def = PANELE[id] || { title: id, buduj: () => {} };
  sec = el("section", "panel");
  sec.dataset.panel = id;
  const head = el("div", "p-head");
  head.innerHTML = `<span class="grip" title="Przeciągnij panel">⠿</span><h3>${def.title}</h3><span class="sub"></span>`;
  const body = el("div", "p-body");
  const uchwyt = el("div", "p-resize");
  sec.append(head, body, uchwyt);
  def.buduj(body, head);
  /* §13: dragging by the whole header + resize handle — pointer events (web/okna.js) */
  podlaczPrzenoszenie(sec, OKNA_CTX);
  podlaczRozmiar(sec, OKNA_CTX);
  panelEls.set(id, sec);
  return sec;
}

/* ---------- geometria okien (§13) ---------- */
function szerokoscKontenera() {
  const grid = $("panels");
  const w = grid.hidden ? 0 : (grid.clientWidth || 0);      // ukryty (przed wczytaniem ulozenia) -> okno
  return Math.max(0, (w || (window.innerWidth || 1920)) - 2 * PAD);
}
function waskieOkno() { return (window.innerWidth || 1920) < 900; }

/* kolejnosc paneli: z serwera + brakujace (np. 'energia' dopisana przez pelny_layout) */
function aktywnePanele() {
  const ids = uklad.panels.filter((p) => PANELE[p]);
  for (const k of Object.keys(PANELE)) if (!ids.includes(k)) ids.push(k);
  return ids;
}

/* context for web/okna.js: oknaZYWE is the only source geometry drawn on screen */
const OKNA_CTX = {
  szerokosc: () => Math.max(szerokoscKontenera(), zasiegUkladu()),
  zablokowane: waskieOkno,                     // < 900 px: uklad pionowy — drag/rozmiar paneli zablokowany
  okno: (id) => okna[id],
  setOkno: (id, o) => {
    okna[id] = { x: Math.round(o.x), y: Math.round(o.y), w: Math.round(o.w), h: Math.round(o.h) };
    narysujOkno(id);
  },
  onDragStart: (id) => { oknoAktywne = id; const s = panelEls.get(id); if (s) s.classList.add("moving"); },
  onEnd: (id, typ) => {
    oknoAktywne = null;
    const s = panelEls.get(id); if (s) s.classList.remove("moving");
    if (waskieOkno()) { zastosujUklad(); return; }   // vertical layout of narrow window — view only, no save
    okna = naprawUklad(rozsun(okna, id));            // no overlap: intersecting windows go DOWN
    for (const pid of Object.keys(okna)) narysujOkno(pid);
    ustawWysokoscKontenera();
    oknaZapisane = { ...okna };                       // to trafia na serwer (pozycje pikselowe 1:1)
    zapiszLayoutDebounce();
  },
  onDomylny: (id) => {                                // double-click on the handle = default size
    if (waskieOkno()) return;
    const o = okna[id]; if (!o) return;
    const dom = domyslneOkna(szerokoscKontenera())[id] || { w: 400, h: 300 };
    okna[id] = przycin({ ...o, w: dom.w, h: dom.h }, szerokoscKontenera());
    okna = naprawUklad(okna);
    for (const pid of Object.keys(okna)) narysujOkno(pid);
    ustawWysokoscKontenera();
    oknaZapisane = { ...okna };
    zapiszLayoutDebounce();
  },
};

function narysujOkno(id) {
  const sec = panelEls.get(id), o = okna[id];
  if (!sec || !o) return;
  const lewo = (o.x + PAD) + "px", gora = (o.y + PAD) + "px", sz = o.w + "px", wyso = o.h + "px";
  if (sec.style.left !== lewo) sec.style.left = lewo;
  if (sec.style.top !== gora) sec.style.top = gora;
  if (sec.style.width !== sz) sec.style.width = sz;
  if (sec.style.height !== wyso) sec.style.height = wyso;
}
function ustawWysokoscKontenera() {
  const grid = $("panels");
  if (waskieOkno()) { grid.style.height = ""; grid.style.minWidth = ""; return; }
  grid.style.minWidth = (zasiegUkladu() + 2 * PAD) + "px";   // layout wider than window = horizontal scrolling
  const h = wysokoscKontenera(okna) + 2 * PAD;
  grid.style.height = Math.max(h, window.innerHeight - 52) + "px";
}

/* narrow window (< 900 px): panels one under another at full width — SAVED positions untouched */
function zastosujUklad() {
  const S = szerokoscKontenera();
  if (waskieOkno()) {
    okna = waskiUklad(oknaZapisane, aktywnePanele(), Math.max(S, MIN_OKNO_W));
  } else {
    // SAVED positions 1:1 — without clipping to window width (narrower window = horizontal scrollbar);
    // window-fit may NEVER hit the server (14.10: narrow preview overwrote the full-screen layout)
    okna = {};
    for (const [id, o] of Object.entries(oknaZapisane)) okna[id] = { ...o };
  }
  for (const id of Object.keys(okna)) narysujOkno(id);
  ustawWysokoscKontenera();
}
function zasiegUkladu() {
  let m = 0;
  for (const o of Object.values(oknaZapisane || {})) m = Math.max(m, (o.x || 0) + (o.w || 0));
  return m;
}
function przycinWszystkie(p, S) {
  const out = {};
  for (const [id, o] of Object.entries(p || {})) out[id] = przycin(o, S);
  return out;
}

/* we take windows from the server only when nothing is being saved and nothing is held (§13: layout memory) */
function oknaZSerweraFn(stan) {
  const serwer = stan.layout && stan.layout.okna;
  if (!serwer || czekamLayout || oknoAktywne || drag) return;
  const kluczS = Object.entries(serwer).map(([k, v]) => `${k}:${v.x},${v.y},${v.w},${v.h}`).join("|");
  const kluczL = Object.entries(oknaZapisane).map(([k, v]) => `${k}:${v.x},${v.y},${v.w},${v.h}`).join("|");
  if (kluczS === kluczL) { serwerKlucz = kluczS; return; }
  /* /ws broadcasts every 250 ms — a frame built BEFORE our PUT must not undo a freshly saved layout;
     we only accept a NEW server version (external change: another card / LAYOUT) */
  if (kluczS === serwerKlucz && Date.now() - poZapisie < 4000) return;
  serwerKlucz = kluczS;
  const dom = domyslneOkna(szerokoscKontenera());
  oknaZapisane = {};
  for (const [id, o] of Object.entries(serwer)) {
    if (!o || !PANELE[id]) continue;
    oknaZapisane[id] = { x: o.x, y: o.y, w: o.w, h: o.h };
  }
  for (const id of aktywnePanele()) if (!oknaZapisane[id]) oknaZapisane[id] = dom[id];   // §13 migracja
  zastosujUklad();
}

function renderPanele(stan) {
  if (!layoutWczytany) return;                      // §13: layout from the server first, then panels
  const serwer = (stan.layout && stan.layout.panels) || uklad.panels;
  if (!drag && !czekamLayout && serwer.join("|") !== uklad.panels.join("|"))
    uklad.panels = serwer.slice();
  oknaZSerweraFn(stan);
  const ids = aktywnePanele();
  const klucz = ids.join("|");
  const grid = $("panels");
  if (grid.dataset.k !== klucz) {
    grid.dataset.k = klucz;
    for (const id of ids) grid.appendChild(panelEl(id));   // appendChild moves existing — order = ids
    for (const [id, sec] of panelEls) if (!ids.includes(id)) { sec.remove(); panelEls.delete(id); }
    for (const id of ids) if (!oknaZapisane[id]) oknaZapisane[id] = domyslneOkna(szerokoscKontenera())[id];
    zastosujUklad();
  }
}

function gdzieUpasc(e, target, pionowo) {
  const r = target.getBoundingClientRect(), s = drag.el.getBoundingClientRect();
  const tenSamWiersz = !pionowo && Math.abs(r.top - s.top) < 24;
  return tenSamWiersz ? e.clientX < r.left + r.width / 2 : e.clientY < r.top + r.height / 2;
}
function oznaczWstawienie(target, przed) {
  target.classList.toggle("drop-before", przed);
  target.classList.toggle("drop-after", !przed);
}
function zoznaczWstawienie(target) { target.classList.remove("drop-before", "drop-after"); }

/* ---------- layout save to the server (§13: 300 ms debounce, after EVERY move/resize) ---------- */
function zapiszLayoutDebounce() {
  czekamLayout = true;   // od razu: klatka /ws w oknie debounce nie moze cofnac swiezo przesunietego panelu
  clearTimeout(zapisTimer);
  zapisTimer = setTimeout(zapiszLayout, 300);
}

async function zapiszLayout() {
  czekamLayout = true;
  try {
    const okr = {};   // calkowite piksele (przy powiekszeniu strony wskaznik daje ulamki)
    for (const [id, o] of Object.entries(oknaZapisane))
      okr[id] = { x: Math.round(o.x), y: Math.round(o.y), w: Math.round(o.w), h: Math.round(o.h) };
    await api("PUT", "/api/layout", { panels: uklad.panels, categories: uklad.cats, okna: okr });
    poZapisie = Date.now();
  } catch (e) { toast(`Zapis układu: ${e.message}`, true); }
  finally { czekamLayout = false; }   // we wait for the server's response, not a fixed timeout
}

/* ---------- “⟲ LAYOUT” (top bar): default layout at current width, with confirmation ---------- */
function ulozDomyslnie() {
  potwierdz("Ułożyć panele domyślnie? Bieżące rozmieszczenie zostanie nadpisane.", async () => {
    oknaZapisane = domyslneOkna(szerokoscKontenera());
    zastosujUklad();
    await zapiszLayout();
    toast("Układ domyślny przywrócony");
  });
}

/* ---------- buttons “FREE GPU 0 / 1” (top bar, §13) + process list modal ---------- */
function zwolnijGpu(i) {
  const orph = (ostatniStan?.orphans || []).filter((o) => (o.gpus || []).some((g) => g.gpu === i));
  const list = $("freeList");
  $("freeModalTitle").textContent = `ZWOLNIJ GPU ${i} — PROCESY NA KARCIE`;
  list.innerHTML = orph.length
    ? orph.map((o) => `<div class="proc sierota"><span class="pid">PID ${esc(o.pid)}</span>
        <span class="pname" title="${esc(o.cmdline || o.name)}">${esc(o.name)}</span>
        <span class="vram">${(o.vram_mb || 0).toLocaleString("pl-PL")} MiB</span></div>`).join("")
    : `<div class="mut" style="font-size:12px">Brak obcych procesów na GPU ${i} — nic do zwolnienia.</div>`;
  $("freeErr").textContent = "";
  $("freeGo").onclick = () => akcja(
    () => api("POST", `/api/gpus/${i}/free`).then((r) => { toast(`GPU ${i}: zwolniono ${r.killed.length} procesów`); zamknij("freeModal"); }),
    "Zwolnij GPU");
  otworz("freeModal");
}

/* ---------------- panel GPU: wszystkie karty w jednej tabeli + jeden wykres (15.10) ---------------- */
/* color = card (stable, independent of order and state); palette checked with a CVD validator on the panel background */
const KOLOR_GPU = ["#3987e5", "#d95926", "#199e70"];
const kolorGpu = (i) => KOLOR_GPU[i] || "#8a94a6";
const MIARY = {
  vram: { lbl: "VRAM", jedn: "%", wart: (p) => p.vram / p.total * 100 },
  temp: { lbl: "TEMP.", jedn: "°C", wart: (p) => p.temp },
  moc: { lbl: "MOC", jedn: "W", wart: (p) => p.moc },
  util: { lbl: "UŻYCIE", jedn: "%", wart: (p) => p.util },
};
let miara = "vram";
try { const m = localStorage.getItem("cm-miara-gpu"); if (MIARY[m]) miara = m; } catch (_) {}

const gpuWiersze = new Map();   // card index -> table row cells
let gpuPanel = null;            // {tab, canvas, tip, procs, procRows, head}
let kursorX = null;             // pointer position above the chart (px) — read line + card values
let fontWykresu = "";

const RING_R0 = 84, RING_KROK = 21, RING_W = 15;   // rings: GPU0 outermost, each next card one step inward
let szczegolyGpu = false;
try { szczegolyGpu = localStorage.getItem("cm-gpu-szczegoly") === "1"; } catch (_) {}

function budujGPU(body, head) {
  const root = el("div", "gpus");
  root.innerHTML = `
    <div class="gk">
      <svg class="gk-svg" viewBox="0 0 200 200" aria-label="VRAM kart: pierścień na kartę">
        <g class="gk-ringi"></g>
        <text class="gk-suma" x="100" y="97" text-anchor="middle"></text>
        <text class="gk-z" x="100" y="115" text-anchor="middle"></text>
      </svg>
      <div class="gk-leg"></div>
    </div>
    <button class="btn ghost gk-toggle"></button>
    <div class="gk-szcz">
    <div class="gt">
      <span class="gt-h">KARTA</span><span class="gt-h">VRAM</span><span class="gt-h r">RDZEŃ</span>
      <span class="gt-h r">HBM</span><span class="gt-h r">MOC</span><span class="gt-h r">UŻYCIE</span>
      <span class="gt-h r">ZEGAR</span><span class="gt-h r">WENT.</span>
    </div>
    <div class="gw-head"><span class="lbl">ostatnie 120 s</span><div class="en-tryb gw-miary"></div></div>
    <div class="gw"><canvas class="wykres-gpu"></canvas></div>
    <div class="gpu-procs"></div>
    </div>`;
  const szcz = root.querySelector(".gk-szcz"), tog = root.querySelector(".gk-toggle");
  const pokaz = () => {
    szcz.hidden = !szczegolyGpu;
    tog.textContent = szczegolyGpu ? "▾ SZCZEGÓŁY KART" : "▸ SZCZEGÓŁY KART (tabela, wykres 120 s, procesy)";
  };
  tog.onclick = () => {
    szczegolyGpu = !szczegolyGpu;
    try { localStorage.setItem("cm-gpu-szczegoly", szczegolyGpu ? "1" : "0"); } catch (_) {}
    pokaz();
  };
  pokaz();
  const miary = root.querySelector(".gw-miary");
  for (const [k, m] of Object.entries(MIARY)) {
    const b = el("button", "btn tryb" + (k === miara ? " active" : ""), m.lbl);
    b.dataset.m = k;
    b.onclick = () => {
      miara = k;
      try { localStorage.setItem("cm-miara-gpu", k); } catch (_) {}
      for (const x of miary.children) x.classList.toggle("active", x.dataset.m === k);
    };
    miary.appendChild(b);
  }
  const canvas = root.querySelector("canvas");
  canvas.addEventListener("pointermove", (e) => { kursorX = e.offsetX; });
  canvas.addEventListener("pointerleave", () => { kursorX = null; });
  body.appendChild(root);
  gpuPanel = { tab: root.querySelector(".gt"), canvas, procs: root.querySelector(".gpu-procs"),
    procRows: new Map(), head: head.querySelector(".sub"), ringi: root.querySelector(".gk-ringi"),
    leg: root.querySelector(".gk-leg"), suma: root.querySelector(".gk-suma"), z: root.querySelector(".gk-z") };
  gpuWiersze.clear();
}

function wierszGPU(i) {
  let w = gpuWiersze.get(i);
  if (w) return w;
  const kom = (k) => { const s = el("span", "gt-c " + k); gpuPanel.tab.appendChild(s); return s; };
  const karta = kom("gt-karta");
  karta.innerHTML = `<i class="kol"></i><b></b><span class="gt-nazwa"></span>`;
  karta.querySelector(".kol").style.background = kolorGpu(i);
  const vram = kom("gt-vram");
  vram.innerHTML = `<div class="bar"><i></i></div><span class="num"></span>`;
  const r = Math.max(10, RING_R0 - i * RING_KROK), C = 2 * Math.PI * r;
  const NS = "http://www.w3.org/2000/svg";
  const tlo = document.createElementNS(NS, "circle"), luk = document.createElementNS(NS, "circle");
  for (const c of [tlo, luk]) {
    c.setAttribute("cx", "100"); c.setAttribute("cy", "100"); c.setAttribute("r", String(r));
    c.setAttribute("fill", "none"); c.setAttribute("stroke-width", String(RING_W));
  }
  tlo.setAttribute("class", "gk-tlo");
  luk.setAttribute("class", "gk-luk");
  luk.setAttribute("stroke", kolorGpu(i));
  luk.setAttribute("stroke-dasharray", `0 ${C}`);
  gpuPanel.ringi.append(tlo, luk);
  const leg = el("div", "gk-wiersz");
  leg.innerHTML = `<i class="kol"></i><b></b><span class="gk-nazwa"></span>
    <span class="gk-temp"><span class="gk-tl">RDZEŃ</span><b class="num gk-t"></b><span class="gk-tl gk-hl">HBM</span><b class="num gk-h"></b></span>
    <span class="num gk-gb"></span><b class="num gk-pct"></b><span class="num gk-w"></span>`;
  leg.querySelector(".kol").style.background = kolorGpu(i);
  gpuPanel.leg.appendChild(leg);
  w = { tlo, luk, C, leg, legB: leg.querySelector("b"), legNazwa: leg.querySelector(".gk-nazwa"),
    legGb: leg.querySelector(".gk-gb"), legPct: leg.querySelector(".gk-pct"), legT: leg.querySelector(".gk-t"),
    legH: leg.querySelector(".gk-h"), legHl: leg.querySelector(".gk-hl"),
    legW: leg.querySelector(".gk-w"),
    karta, b: karta.querySelector("b"), nazwa: karta.querySelector(".gt-nazwa"),
    vram, bar: vram.querySelector("i"), vramTxt: vram.querySelector(".num"),
    temp: kom("num r"), hbm: kom("num r"), moc: kom("num r"), util: kom("num r"), zegar: kom("num r"), went: kom("num r") };
  gpuWiersze.set(i, w);
  return w;
}

function krotkaNazwa(n) { return String(n || "").replace(/^(NVIDIA|Tesla)\s+/i, "").replace(/^Tesla\s+/i, ""); }

function renderGPU(gpus) {
  if (!gpuPanel) return;
  let uzyte = 0, razem = 0, moc = 0;
  const widoczne = new Set();
  for (const g of gpus) {
    widoczne.add(g.index);
    const w = wierszGPU(g.index);
    const used = g.vram_used_mb || 0, total = g.vram_total_mb || 1, pct = used / total * 100;
    uzyte += used; razem += total; moc += g.power_w || 0;
    setText(w.b, `GPU${g.index}`);
    w.luk.setAttribute("stroke-dasharray", `${(Math.min(100, pct) / 100) * w.C} ${w.C}`);
    setText(w.legB, `GPU${g.index}`);
    setText(w.legNazwa, krotkaNazwa(g.name).replace(/-SXM2/i, ""));
    setText(w.legGb, `${GB(used)} / ${GB(total)} GB`);
    setText(w.legPct, `${Math.round(pct)}%`);
    setText(w.legT, g.temp_c == null ? "—" : `${Math.round(g.temp_c)}°C`);
    w.legT.className = "num gk-t " + klasaTemp(g.temp_c);
    w.legH.hidden = w.legHl.hidden = g.hbm_temp_c == null;      // T400 nie ma HBM
    setText(w.legH, g.hbm_temp_c == null ? "" : `${Math.round(g.hbm_temp_c)}°C`);
    w.legH.className = "num gk-h " + klasaTemp(g.hbm_temp_c);
    setText(w.legW, g.power_w == null ? "—" : `${Math.round(g.power_w)} W`);
    setText(w.nazwa, krotkaNazwa(g.name) + (g.nvlink ? " · NVLink" : "") + (g.index === 2 ? " · chroniona" : ""));
    setBar(w.bar, pct, klasaPct(pct));
    setText(w.vramTxt, `${GB(used)}/${GB(total)} GB`);
    setText(w.temp, g.temp_c == null ? "—" : `${Math.round(g.temp_c)}°C`);
    w.temp.className = "gt-c num r " + klasaTemp(g.temp_c);
    setText(w.hbm, g.hbm_temp_c == null ? "—" : `${Math.round(g.hbm_temp_c)}°C`);
    w.hbm.className = "gt-c num r " + klasaTemp(g.hbm_temp_c);
    setText(w.moc, g.power_w == null ? "—" : `${Math.round(g.power_w)}/${g.power_limit_w ?? "-"} W`);
    setText(w.util, g.util_gpu == null ? "—" : `${g.util_gpu}%`);
    setText(w.zegar, g.sm_clock_mhz == null ? "—" : `${g.sm_clock_mhz} MHz`);
    setText(w.went, g.fan_pct == null ? "—" : `${g.fan_pct}%`);
  }
  for (const [i, w] of gpuWiersze) if (!widoczne.has(i)) {
    for (const k of ["karta", "vram", "temp", "hbm", "moc", "util", "zegar", "went", "tlo", "luk", "leg"]) w[k].remove();
    gpuWiersze.delete(i);
  }
  setText(gpuPanel.head, `${gpus.length} karty · VRAM ${GB(uzyte)}/${GB(razem)} GB · ${Math.round(moc)} W`);
  setText(gpuPanel.suma, `${GB(uzyte)} GB`);
  setText(gpuPanel.z, `z ${GB(razem)} GB VRAM`);

  const widoczneP = new Set();
  for (const g of gpus) for (const p of g.processes || []) {
    const klucz = `${g.index}:${p.pid}`;
    widoczneP.add(klucz);
    const system = p.system === true, model = p.model || null, sierota = !system && !model;
    let row = gpuPanel.procRows.get(klucz);
    if (!row) {
      row = el("div", "proc");
      row.innerHTML = `<i class="kol"></i><span class="gpu-id"></span><span class="pid"></span><span class="pname"></span><span class="vram"></span>`;
      row.querySelector(".kol").style.background = kolorGpu(g.index);
      const btn = el("button", "btn danger ghost btn-kill", "ZWOLNIJ");
      btn.onclick = () => potwierdz(`Zabić proces ${p.pid} (${p.name}) na GPU ${g.index}?`,
        () => api("POST", `/api/orphans/${p.pid}/kill`).then(() => toast(`PID ${p.pid} zabity`)));
      row.appendChild(btn);
      gpuPanel.procRows.set(klucz, row);
      gpuPanel.procs.appendChild(row);
    }
    row.classList.toggle("sierota", sierota);
    row.classList.toggle("system", system);
    row.querySelector(".btn-kill").hidden = !sierota;
    setText(row.querySelector(".gpu-id"), `GPU${g.index}`);
    setText(row.querySelector(".pid"), String(p.pid));
    setText(row.querySelector(".pname"), model || (system ? `⚙ system · ${p.name}` : `⚠ SIEROTA ${p.name}`));
    setText(row.querySelector(".vram"), `${(p.vram_mb || 0).toLocaleString("pl-PL")} MiB`);
  }
  for (const [k, row] of gpuPanel.procRows) if (!widoczneP.has(k)) { row.remove(); gpuPanel.procRows.delete(k); }
}

/* ---------------- 120 s chart: all cards, one measure, one axis (canvas, rAF) ---------------- */
const HIST_S = 120, HIST_MAX = 600;
const historia = new Map();

function zbierzHistorie(stan) {
  const now = Date.now() / 1000;
  for (const g of stan.gpus) {
    let h = historia.get(g.index);
    if (!h) { h = []; historia.set(g.index, h); }
    const last = h[h.length - 1];
    if (!last || now - last.t >= 0.25) {
      h.push({ t: now, temp: g.temp_c ?? null, vram: g.vram_used_mb || 0, total: g.vram_total_mb || 1,
        moc: g.power_w ?? null, limit: g.power_limit_w ?? null, util: g.util_gpu ?? null });
      while (h.length && now - h[0].t > HIST_S) h.shift();
      if (h.length > HIST_MAX) h.shift();
    }
  }
}

function skalaMax() {
  if (miara !== "moc") return 100;
  let m = 50;
  for (const h of historia.values()) for (const p of h) m = Math.max(m, p.limit || 0, p.moc || 0);
  return Math.ceil(m / 50) * 50;
}

function rysujWykresy() {
  requestAnimationFrame(rysujWykresy);
  if (!gpuPanel) return;
  const c = gpuPanel.canvas;
  const dpr = window.devicePixelRatio || 1;
  const w = c.clientWidth, h = c.clientHeight;
  if (!w || !h) return;
  if (c.width !== Math.round(w * dpr) || c.height !== Math.round(h * dpr)) { c.width = Math.round(w * dpr); c.height = Math.round(h * dpr); }
  const ctx = c.getContext("2d");
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, w, h);
  const now = Date.now() / 1000, M = MIARY[miara], max = skalaMax();
  const L = 34, P = 64, G = 6, D = 6;                    // margins: axis, line-end labels, top, bottom
  const pw = Math.max(10, w - L - P), ph = Math.max(10, h - G - D);
  const x = (t) => L + ((t - (now - HIST_S)) / HIST_S) * pw;
  const y = (v) => G + ph - (Math.max(0, Math.min(max, v)) / max) * ph;
  ctx.font = fontWykresu || (fontWykresu = "10px " + getComputedStyle(document.body).getPropertyValue("--mono"));
  ctx.textBaseline = "middle";
  for (let i = 0; i <= 4; i++) {                         // grid + axis labels (one axis, one unit)
    const v = (max / 4) * i, yy = Math.round(y(v)) + 0.5;
    ctx.strokeStyle = "rgba(26,77,122,.55)"; ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo(L, yy); ctx.lineTo(L + pw, yy); ctx.stroke();
    ctx.fillStyle = "#7fa3c4"; ctx.textAlign = "right";
    ctx.fillText(i === 4 ? `${Math.round(v)}${M.jedn}` : String(Math.round(v)), L - 4, yy);
  }
  const konce = [];
  for (const [idx, hist] of historia) {
    const pkt = hist.filter((p) => M.wart(p) != null);
    if (pkt.length < 2) continue;
    ctx.beginPath();
    pkt.forEach((p, i) => { const px = x(p.t), py = y(M.wart(p)); i ? ctx.lineTo(px, py) : ctx.moveTo(px, py); });
    ctx.strokeStyle = kolorGpu(idx); ctx.lineWidth = 2; ctx.lineJoin = "round"; ctx.stroke();
    const ost = pkt[pkt.length - 1];
    konce.push({ idx, y: y(M.wart(ost)), v: M.wart(ost), x: x(ost.t) });
  }
  konce.sort((a, b) => a.y - b.y);                       // line-end labels without overlap
  for (let i = 1; i < konce.length; i++) konce[i].ly = Math.max(konce[i].y, (konce[i - 1].ly ?? konce[i - 1].y) + 12);
  for (const k of konce) {
    const ly = Math.min(h - 6, k.ly ?? k.y);
    ctx.fillStyle = kolorGpu(k.idx);
    ctx.beginPath(); ctx.arc(k.x, k.y, 3, 0, 2 * Math.PI); ctx.fill();
    ctx.fillRect(L + pw + 6, ly - 3, 6, 6);
    ctx.fillStyle = "#d7e9f8"; ctx.textAlign = "left";
    ctx.fillText(`GPU${k.idx} ${Math.round(k.v)}${M.jedn}`, L + pw + 15, ly);
  }
  if (kursorX !== null && kursorX >= L && kursorX <= L + pw) {   // read line: card values at this moment
    const t = now - HIST_S + ((kursorX - L) / pw) * HIST_S;
    ctx.strokeStyle = "rgba(215,233,248,.45)"; ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo(Math.round(kursorX) + 0.5, G); ctx.lineTo(Math.round(kursorX) + 0.5, G + ph); ctx.stroke();
    const wiersze = [];
    for (const [idx, hist] of historia) {
      let best = null;
      for (const p of hist) if (!best || Math.abs(p.t - t) < Math.abs(best.t - t)) best = p;
      if (best && M.wart(best) != null) {
        wiersze.push({ idx, v: M.wart(best) });
        ctx.fillStyle = kolorGpu(idx);
        ctx.beginPath(); ctx.arc(x(best.t), y(M.wart(best)), 4, 0, 2 * Math.PI); ctx.fill();
      }
    }
    const tekst = [`−${Math.max(0, Math.round(now - t))} s`, ...wiersze.map((r) => `GPU${r.idx} ${Math.round(r.v)}${M.jedn}`)];
    const bw = 92, bh = 14 * tekst.length + 6;
    const bx = kursorX + 10 + bw > L + pw ? kursorX - 10 - bw : kursorX + 10;
    ctx.fillStyle = "rgba(2,8,18,.92)"; ctx.strokeStyle = "rgba(56,214,255,.45)";
    ctx.fillRect(bx, G, bw, bh); ctx.strokeRect(bx + 0.5, G + 0.5, bw - 1, bh - 1);
    tekst.forEach((s, i) => {
      if (i) { ctx.fillStyle = kolorGpu(wiersze[i - 1].idx); ctx.fillRect(bx + 6, G + 10 + 14 * i - 3, 6, 6); }
      ctx.fillStyle = i ? "#d7e9f8" : "#7fa3c4"; ctx.textAlign = "left";
      ctx.fillText(s, bx + (i ? 16 : 6), G + 10 + 14 * i);
    });
  }
}
requestAnimationFrame(rysujWykresy);

/* ---------------- panel Model LLM: to, co pokazuje okno launchera (proxy /_live, 15.10) ---------------- */
const ETAP = {
  reading: ["◔ CZYTA PROMPT", "warn"], thinking: ["✎ MYŚLI", "think"], answering: ["✍ ODPOWIADA", "ok"],
  tool: ["⚙ WYWOŁUJE NARZĘDZIE", "cyan"], generating: ["▶ GENERUJE", "ok"],
};
const RODZAJ = { AGENT: "agent", ROZMOWA: "czat", POMOCNICZE: "pomocnicze", KOMPAKTOWANIE: "kompaktowanie", "TYTUŁ": "tytuł" };
const n0 = (v) => (v == null ? "—" : Math.round(v).toLocaleString("pl-PL"));
const n1 = (v) => (v == null || !isFinite(v) ? "—" : v.toFixed(1).replace(".", ","));
const nK = (v) => (v == null ? "—" : v >= 1e6 ? `${(v / 1e6).toFixed(2).replace(".", ",")} mln`
  : v >= 1e4 ? `${(v / 1e3).toFixed(1).replace(".", ",")} tys.` : n0(v));
let llmBody = null, llmHead = null;
const llmKarty = new Map();

function budujLLM(body, head) {
  llmBody = body; llmHead = head.querySelector(".sub");
  body.innerHTML = `<div class="llm-pusty mut">Żaden model LLM z launchera nie działa.</div>`;
  llmKarty.clear();
}

function kartaLLM(id) {
  let k = llmKarty.get(id);
  if (k) return k;
  const root = el("div", "llm");
  root.innerHTML = `
    <div class="llm-tytul"><b class="llm-nazwa"></b><span class="mut llm-meta"></span></div>
    <div class="llm-kafle">
      <div class="kafel"><span class="m-lbl">GENERACJA (TG)</span><b class="num k-tg"></b><span class="u">t/s</span><span class="u k-tgu"></span></div>
      <div class="kafel"><span class="m-lbl">CZYTANIE PROMPTU (PP)</span><b class="num k-pp"></b><span class="u">t/s</span><span class="u k-ppu"></span></div>
      <div class="kafel"><span class="m-lbl">ZAPYTANIA</span><b class="num k-n"></b><span class="u k-wyn"></span></div>
      <div class="kafel"><span class="m-lbl">TOKENY WEJŚCIE</span><b class="num k-tok"></b><span class="u k-wyj"></span></div>
    </div>
    <div class="llm-teraz">
      <div class="llm-etap"><span class="etap"></span><span class="mut etap-info"></span></div>
      <div class="llm-licz num mut"></div>
    </div>
    <div class="llm-ctx"><span class="m-lbl">KONTEKST</span><div class="bar"><i></i></div><span class="num ctx-txt"></span></div>
    <div class="llm-tab">
      <div class="lt lt-h"><span class="r">#</span><span>TYP</span><span class="r">WEJŚCIE</span><span class="r">Z CACHE</span>
        <span class="r">WYJŚCIE</span><span class="r">PP t/s</span><span class="r">TG t/s</span><span class="r">AKCEPT.</span><span class="r">CZAS</span><span>WYNIK</span></div>
      <div class="lt-wiersze"></div>
    </div>`;
  const q = (s) => root.querySelector(s);
  k = { root, nazwa: q(".llm-nazwa"), meta: q(".llm-meta"), tg: q(".k-tg"), pp: q(".k-pp"), n: q(".k-n"), wyn: q(".k-wyn"),
    tgu: q(".k-tgu"), ppu: q(".k-ppu"),
    tok: q(".k-tok"), wyj: q(".k-wyj"), etap: q(".etap"), etapInfo: q(".etap-info"), licz: q(".llm-licz"),
    ctxBar: q(".llm-ctx i"), ctxTxt: q(".ctx-txt"), wiersze: q(".lt-wiersze"), histKlucz: null };
  llmKarty.set(id, k);
  return k;
}

function renderLLM(stan) {
  if (!llmBody) return;
  const lista = stan.llm || [];
  const pusty = llmBody.querySelector(".llm-pusty");
  if (pusty) pusty.hidden = lista.length > 0;
  setText(llmHead, lista.length ? lista.map((m) => `:${m.port}`).join(" · ") : "brak");
  const ids = new Set(lista.map((m) => m.id));
  for (const [id, k] of llmKarty) if (!ids.has(id)) { k.root.remove(); llmKarty.delete(id); }
  for (const m of lista) {
    const k = kartaLLM(m.id);
    if (!k.root.parentNode) llmBody.appendChild(k.root);
    setText(k.nazwa, m.name);
    const sr = m.sesja && m.sesja.czas_avg != null ? ` · śr. czas zapytania ${n1(m.sesja.czas_avg)} s` : "";
    setText(k.meta, ` · :${m.port}` + (m.proxy ? ` · działa ${uptimeTxt(Math.round(m.up_s || 0))}` : "") + sr);
    const s = m.sesja || {};
    const a = m.active;
    /* live tiles: during a query the big number = speed NOW (per /ws frame), session average below;
       tokens and query count include the in-flight query */
    const generuje = a && a.phase !== "reading" && a.inst > 0, czyta = a && a.phase === "reading" && a.ptps > 0;
    setText(k.tg, generuje ? n1(a.inst) : n1(s.tg_avg));
    setText(k.tgu, generuje ? `teraz · śr. sesji ${n1(s.tg_avg)}` : "średnio od startu serwera");
    k.tg.classList.toggle("zywo", !!generuje);
    setText(k.pp, czyta ? n0(a.ptps) : s.pp_avg == null ? "—" : n0(s.pp_avg));
    setText(k.ppu, czyta ? `teraz · śr. sesji ${s.pp_avg == null ? "—" : n0(s.pp_avg)}` : "średnio od startu serwera");
    k.pp.classList.toggle("zywo", !!czyta);
    setText(k.n, n0((s.zapytan || 0) + (a ? 1 : 0)));
    const wyn = [`OK ${s.ok ?? 0}`];
    if (s.bledy) wyn.push(`błędy ${s.bledy}`);
    if (s.przerwane) wyn.push(`przerw. ${s.przerwane}`);
    if (a) wyn.push("1 w toku");
    setText(k.wyn, wyn.join(" · "));
    k.wyn.classList.toggle("bad", !!s.bledy);
    const wej = (s.wejscie || 0) + (a ? a.in || 0 : 0), wyj = (s.wyjscie || 0) + (a ? a.out || 0 : 0);
    setText(k.tok, nK(wej));
    setText(k.wyj, `wyjście ${nK(wyj)}`);
    k.tok.title = `wejście ${n0(wej)} tok · wyjście ${n0(wyj)} tok`;

    if (!m.proxy) {
      setText(k.etap, "✕ BRAK DANYCH"); k.etap.className = "etap bad"; setText(k.etapInfo, "  proxy launchera nie odpowiada"); setText(k.licz, "");
    } else if (!m.up_ok) {
      const laduje = m.status === "starting";
      setText(k.etap, laduje ? "⏳ ŁADUJE MODEL" : "✕ SERWER NIE ODPOWIADA"); k.etap.className = "etap " + (laduje ? "warn" : "bad");
      const ld = laduje ? ((stan.models || []).find((x) => x.id === m.id) || {}).ladowanie : null;
      setText(k.etapInfo, ld && ld.etap === "laduje"
        ? `  wczytano ${GB(ld.wczytano_mb)}` + (ld.rozmiar_mb ? ` / ${GB(ld.rozmiar_mb)} GB (${Math.round(ld.pct)}%)` : " GB") + ` · VRAM ${GB(ld.vram_mb)} GB`
        : laduje ? "  zapytania czekają, aż model się załaduje" : "");
      setText(k.licz, "");
    } else if (a) {
      let [lab, kl] = ETAP[a.phase] || [String(a.phase || "").toUpperCase(), ""];
      if (a.kind === "KOMPAKTOWANIE") { lab = "⧉ KOMPAKTUJE KONTEKST"; kl = "cyan"; }
      setText(k.etap, lab); k.etap.className = "etap " + kl;
      setText(k.etapInfo, `  zapytanie #${a.id} · ${RODZAJ[a.kind] || a.kind}` + (m.queued ? ` · w kolejce ${m.queued}` : "")
        + (a.eff ? ` · myślenie: ${a.eff}` : "") + (a.tool_names && a.tool_names.length ? ` · ${a.tool_names[0]}` : ""));
      const wej = `wejście ${n0(a.in)} tok` + (a.cache ? ` (cache ${n0(a.cache)})` : "") + ` · wyjście ${n0(a.out)} tok`;
      const pred = a.phase === "reading"
        ? ` · prefill ${n0(a.proc)} tok` + (a.ptps ? ` · PP ${n0(a.ptps)} t/s` : "")
        : ` · TG teraz ${a.inst ? n1(a.inst) : "…"} · śr. ${a.avg ? n1(a.avg) : "…"} t/s`;
      setText(k.licz, wej + pred + ` · ${n1(a.el)} s`);
    } else {
      setText(k.etap, "○ BEZCZYNNY"); k.etap.className = "etap idle";
      setText(k.etapInfo, "  czeka na zapytanie"); setText(k.licz, "");
    }
    const nctx = m.n_ctx || 0, ctx = m.ctx || 0, fr = nctx ? ctx / nctx * 100 : 0;
    setBar(k.ctxBar, fr, fr >= 85 ? "bad" : fr >= 60 ? "warn" : "");
    setText(k.ctxTxt, `${n0(ctx)} / ${n0(nctx)} tok (${n1(fr)}%)`);

    const hist = m.hist || [];
    const klucz = hist.map((r) => r.id).join(",");
    if (klucz !== k.histKlucz) {
      k.histKlucz = klucz;
      k.wiersze.innerHTML = hist.length ? hist.map((r) => {
        const ok = r.status === "OK", przerw = r.status === "PRZERWANE";
        return `<div class="lt"><span class="r num mut">${esc(r.id)}</span><span>${esc(RODZAJ[r.kind] || r.kind)}</span>
          <span class="r num">${n0(r.in)}</span><span class="r num mut">${r.cached ? n0(r.cached) : "-"}</span>
          <span class="r num">${n0(r.out)}</span><span class="r num">${r.ptps ? n0(r.ptps) : "-"}</span>
          <span class="r num b">${r.gtps ? n1(r.gtps) : "-"}</span><span class="r num mut">${r.acc != null ? r.acc.toFixed(2).replace(".", ",") : "-"}</span>
          <span class="r num">${n1(r.dur)} s</span><span class="${ok ? "w-ok" : przerw ? "w-warn" : "w-bad"}">${ok ? "✓ OK" : esc(r.status)}</span></div>`;
      }).join("") : `<div class="mut lt-pusto">jeszcze żadnego zapytania od startu serwera</div>`;
    }
  }
}

/* ---------------- kafel systemu ---------------- */
function budujSystem(body) {
  body.innerHTML = `
    <div class="sys-grid">
      <div class="sys-block">
        <h4>CPU</h4>
        <div class="big"><b class="num" id="cpuPct">–</b><span class="unit">%</span></div>
        <div class="bar"><i id="cpuBar"></i></div>
        <div class="kv"><span>temp. pakietu</span><b class="num" id="cpuTempPkg">–</b></div>
        <div class="kv"><span>najgorętszy rdzeń</span><b class="num" id="cpuTempCore">–</b></div>
        <div class="kv"><span>load 1′</span><b class="num" id="cpuLoad">–</b></div>
      </div>
      <div class="sys-block">
        <h4>RAM</h4>
        <div class="big"><b class="num" id="ramPct">–</b><span class="unit">%</span></div>
        <div class="bar"><i id="ramBar"></i></div>
        <div class="kv"><span>wolne</span><b class="num" id="ramFree">–</b></div>
        <div class="kv"><span>wymiana (swap)</span><b class="num" id="swapTxt">–</b></div>
      </div>
      <div class="sys-block">
        <h4>Dyski</h4>
        <div id="diskList"></div>
      </div>
      <div class="sys-block">
        <h4>NVMe · zasilacz</h4>
        <div class="big"><b class="num" id="psuW">–</b><span class="unit">W PSU</span></div>
        <div id="nvmeList"></div>
        <div id="psuTemps"></div>
      </div>
    </div>`;
}

function renderSystem(stan) {
  const c = stan.cpu, r = stan.ram;
  setText($("cpuPct"), String(Math.round(c.percent ?? 0)));
  setBar($("cpuBar"), c.percent ?? 0, klasaPct(c.percent ?? 0));
  setText($("cpuTempPkg"), tempTxt(c.temp_package_c)); $("cpuTempPkg").className = "num " + klasaTemp(c.temp_package_c);
  setText($("cpuTempCore"), tempTxt(c.temp_hottest_c)); $("cpuTempCore").className = "num " + klasaTemp(c.temp_hottest_c);
  setText($("cpuLoad"), String(c.load1 ?? "–"));
  setText($("ramPct"), String(Math.round(r.percent ?? 0)));
  setBar($("ramBar"), r.percent ?? 0, klasaPct(r.percent ?? 0));
  setText($("ramFree"), `${r.available_gb} / ${r.total_gb} GB`);
  setText($("swapTxt"), `${r.swap_used_gb} / ${r.swap_total_gb} GB`);

  const dl = $("diskList"), dWidoczne = new Set();
  for (const d of stan.disks) {
    dWidoczne.add(d.path);
    let row = dl.querySelector(`[data-dysk="${CSS.escape(d.path)}"]`);
    if (!row) {
      row = el("div", "", `<div class="kv"><span>${d.path}</span><b class="num"></b></div><div class="bar"><i></i></div>`);
      row.dataset.dysk = d.path;
      dl.appendChild(row);
    }
    setText(row.querySelector("b"), `${d.used_gb} / ${d.total_gb} GB`);
    setBar(row.querySelector(".bar i"), d.percent, klasaPct(d.percent));
  }
  dl.querySelectorAll("[data-dysk]").forEach((n) => { if (!dWidoczne.has(n.dataset.dysk)) n.remove(); });

  const nl = $("nvmeList"), nWidoczne = new Set();
  for (const d of stan.nvme) {
    nWidoczne.add(d.name);
    let row = nl.querySelector(`[data-nvme="${CSS.escape(d.name)}"]`);
    if (!row) { row = el("div", "kv", `<span></span><b class="num"></b>`); row.dataset.nvme = d.name; nl.appendChild(row); }
    setText(row.querySelector("span"), `NVMe ${d.name}`);
    const b = row.querySelector("b");
    setText(b, `${tempTxt(d.temp_c)} °C`); b.className = "num " + klasaTemp(d.temp_c);
  }
  nl.querySelectorAll("[data-nvme]").forEach((n) => { if (!nWidoczne.has(n.dataset.nvme)) n.remove(); });

  const psu = stan.psu || {};
  setText($("psuW"), psu.power_w === null || psu.power_w === undefined ? "–" : String(Math.round(psu.power_w)));
  const pt = $("psuTemps");
  const temps = Object.entries(psu.temps || {});
  const klucz = temps.map(([k, v]) => `${k}:${v}`).join("|");
  if (pt.dataset.k !== klucz) {
    pt.dataset.k = klucz;
    pt.innerHTML = temps.map(([k, v]) =>
      `<div class="kv"><span>${k}</span><b class="num ${klasaTemp(v)}">${tempTxt(v)} °C</b></div>`).join("");
  }

  // the same number as the Energy panel and the gauge: power from the outlet (T400 sits in the Corsair read, not separately)
  const en = stan.energia || {};
  setText($("mocTotal"), en.teraz_w_gniazdko == null ? "–" : `${Math.round(en.teraz_w_gniazdko)} W`);
  $("mocBox").title = `Z gniazdka ${Math.round(en.teraz_w_gniazdko || 0)} W · DC ${Math.round(en.teraz_w || 0)} W `
    + `(Corsair z T400 ${Math.round(en.psu_glowny_w || 0)} W + V100 ${Math.round(en.psu_gpu_w || 0)} W)`;
}

/* ---------------- models: categories + dragging (§11b) + glowing tile (§11c) ---------------- */
const modelWiersze = new Map();
const catEls = new Map();
const STAN_TXT = { stopped: "zatrzymany", starting: "startuje…", ready: "gotowy", stopping: "zatrzymuje…", failed: "padł" };
const PLAKIETKA = { ready: "● PRACUJE", starting: "⏳ ŁADUJE", stopping: "⏳ STOP", failed: "✖ BŁĄD" };

function budujModele(body, head) {
  const btnCat = el("button", "btn ghost", "+ KATEGORIA");
  btnCat.title = "Dodaj kategorię modeli";
  btnCat.onclick = () => otworzKategorie(null);
  const btnImp = el("button", "btn warn", ikona("i-import") + "IMPORT Z PULPITU");
  btnImp.title = "Dodaj modele z ikon ~/Pulpit (.desktop)";
  btnImp.onclick = otworzImport;
  const btnAdd = el("button", "btn accent", ikona("i-plus") + "DODAJ MODEL");
  btnAdd.onclick = () => otworzFormularz(null);
  head.append(btnCat, btnImp, btnAdd);
  const list = el("div", "model-list");
  list.id = "modelList";
  body.appendChild(list);
}

function wariantLabel(p) { return `GPU ${(p.gpus || []).join("+")} · ${Math.max(...(p.vram || [0]))} MiB`; }

const KAT_TP = ["TP1", "TP2"];
/* Model tab by number of CARDS in placements (1 card -> TP1, 2+ -> TP2); models without
   placements (Graphics/Music/Helper) stay in their category. The same condition as
   registry.tp_kategoria() on the server side. */
function tpKategoria(m) {
  const pl = m.placements || [];
  if (!pl.length) return null;
  return pl.some((p) => (p.gpus || []).length >= 2) ? "TP2" : "TP1";
}

function kategorieZUkladu(stan) {
  const serwer = (stan.layout && stan.layout.categories) || [];
  if (!drag && !czekamLayout && !czekamOrder) {
    const kluczS = serwer.map((c) => `${c.name}:${c.collapsed ? 1 : 0}`).join("|");
    const kluczL = uklad.cats.map((c) => `${c.name}:${c.collapsed ? 1 : 0}`).join("|");
    if (kluczS !== kluczL) uklad.cats = serwer.map((c) => ({ name: c.name, collapsed: !!c.collapsed }));
  }
  const cats = uklad.cats.slice();
  const modele = stan.models || [];
  /* 02.11: TP1 / TP2 tabs — models with placements land there automatically by card count.
     TP1 and TP2 are always present (even when empty), categories without models vanish from the panel. */
  for (const n of KAT_TP) if (!cats.some((c) => c.name === n)) cats.push({ name: n, collapsed: false });
  for (const m of modele) {
    const kat = tpKategoria(m) || m.category;
    if (!cats.some((c) => c.name === kat)) cats.push({ name: kat, collapsed: false });
  }
  uklad.cats = cats.filter((c) =>
    KAT_TP.includes(c.name) || modele.some((m) => !tpKategoria(m) && m.category === c.name));
  return uklad.cats;
}

function porzadekZSerwera(stan) {
  const out = {};
  for (const m of stan.models || []) {
    const kat = tpKategoria(m) || m.category;
    (out[kat] = out[kat] || []).push(m.id);
  }
  return out;
}
/* Sort in the tab: PINNED models (pin=1) hold their place (by `order`),
   unpinned sort next to them by (order, id) — they do not jump on state refresh. */
function sortujWKategorii(ids, modele) {
  const po = (a, b) => (modele.get(a).order - modele.get(b).order) || (a < b ? -1 : a > b ? 1 : 0);
  const przypiete = ids.filter((id) => modele.get(id).pin).sort(po);
  const wolne = ids.filter((id) => !modele.get(id).pin).sort(po);
  return [...przypiete, ...wolne];
}
function porzadekModeli(stan) {
  const modele = new Map((stan.models || []).map((m) => [m.id, m]));
  const serwer = {};
  for (const [kat, ids] of Object.entries(porzadekZSerwera(stan))) serwer[kat] = sortujWKategorii(ids, modele);
  if (!drag) {
    // 02.11: the server wins when the ORDER of pinned models differs (the server is the source of positions).
    // When only unpinned differ — we keep the local order until the order write completes.
    const sporne = Object.keys(serwer).some((kat) => {
      const po = (ids) => (ids || []).filter((id) => modele.get(id)?.pin).join(",");
      return po(porzadekLokalny?.[kat]) !== po(serwer[kat]);
    });
    if (!czekamOrder || !porzadekLokalny || sporne) porzadekLokalny = serwer;
  }
  if (!porzadekLokalny) porzadekLokalny = serwer;
  return porzadekLokalny;
}
let porzadekLokalny = null;

function wierszModelu(m) {
  // Always a new row: the old instance may sit in another container (after
  // a reorder/pin) or in an already-deleted category — the move
  // (appendChild) would leave the category without tiles.
  const wStary = modelWiersze.get(m.id);
  if (wStary) wStary.root.remove();
  const root = el("div", "model");
  root.dataset.id = m.id;
  root.draggable = true;
  root.innerHTML = `
    <div class="model-row">
      <span class="grip" title="Przeciągnij model">⠿</span>
      <span class="m-pin" title="Przypięty — model nie zmienia miejsca na liście" hidden>📌</span>
      <span class="kropka status-dot"></span>
      <span class="m-name"></span>
      <span class="plakietka" hidden></span>
      <span class="m-meta"></span>
      <span class="m-actions"></span>
    </div>
    <div class="m-postep" hidden><div class="bar"><i></i></div><span class="num"></span></div>
    <div class="m-reason" hidden></div>
    <pre class="m-logtail" hidden></pre>`;
  const act = root.querySelector(".m-actions");

  const select = el("select", "m-variant");
  select.title = "Wariant rozmieszczenia";
  const start = el("button", "btn accent", ikona("i-play") + "START");
  start.onclick = () => akcja(() => {
    const idx = parseInt(select.value || "0", 10);
    return api("POST", `/api/models/${m.id}/start`, { placement_index: idx });
  }, "Start");
  const stop = el("button", "btn", ikona("i-stop") + "STOP");
  stop.onclick = () => akcja(() => api("POST", `/api/models/${m.id}/stop`), "Stop");
  const force = el("button", "btn danger", "WYMUŚ");
  force.title = "Natychmiast SIGKILL";
  force.onclick = () => potwierdz(`Wymusić stop (SIGKILL) modelu ${m.name}?`,
    () => api("POST", `/api/models/${m.id}/stop?force=1`));
  const restart = el("button", "btn", ikona("i-restart"));
  restart.title = "Restart";
  restart.onclick = () => akcja(async () => {
    await api("POST", `/api/models/${m.id}/stop`);
    await api("POST", `/api/models/${m.id}/start`, {});
  }, "Restart");
  const switchBtn = el("button", "btn warn", ikona("i-switch") + "PRZEŁĄCZ");
  switchBtn.onclick = () => otworzSwitch(m.id);
  const gear = el("button", "btn ghost", ikona("i-gear"));
  gear.title = "Ustawienia";
  gear.onclick = () => otworzFormularz(m.id);
  const log = el("button", "btn ghost", ikona("i-log"));
  log.title = "Log";
  log.onclick = () => otworzLog(m.id);
  const del = el("button", "btn ghost danger", ikona("i-trash"));
  del.title = "Usuń z listy (wyłącza wpis)";
  del.onclick = () => potwierdz(`Usunąć model ${m.name} z listy? Wpis pozostanie w models.yaml (enabled: false).`,
    () => api("DELETE", `/api/models/${m.id}`));
  act.append(select, start, stop, force, restart, switchBtn, gear, log, del);

  const w = { root, dot: root.querySelector(".status-dot"), name: root.querySelector(".m-name"),
        pin: root.querySelector(".m-pin"),
        plak: root.querySelector(".plakietka"), meta: root.querySelector(".m-meta"),
        reason: root.querySelector(".m-reason"), tail: root.querySelector(".m-logtail"),
        postep: root.querySelector(".m-postep"), postepBar: root.querySelector(".m-postep i"),
        postepTxt: root.querySelector(".m-postep .num"),
        select, start, stop, force, restart, switchBtn, del };
  modelWiersze.set(m.id, w);
  podlaczDragModelu(root, m.id);
  root.addEventListener("contextmenu", (e) => { e.preventDefault(); otworzMenuModelu(e, m.id); });
  return w;
}

function catEl(nazwa) {
  let c = catEls.get(nazwa);
  if (c) return c;
  const root = el("div", "cat");
  root.dataset.cat = nazwa;
  root.draggable = true;
  root.innerHTML = `
    <div class="cat-head">
      <span class="grip" title="Przeciągnij kategorię">⠿</span>
      <span class="cat-name" title="Dwuklik = zmień nazwę"></span>
      <span class="cat-count"></span><span class="cat-run"></span>
      <button class="btn ghost icon collapse" title="Zwiń / rozwiń">▾</button>
    </div>
    <div class="cat-models"></div>`;
  root.querySelector(".cat-name").textContent = nazwa;
  root.querySelector(".cat-name").addEventListener("dblclick", () => otworzKategorie(nazwa));
  root.querySelector(".collapse").onclick = () => {
    const c2 = uklad.cats.find((x) => x.name === nazwa) || { name: nazwa, collapsed: false };
    c2.collapsed = !c2.collapsed;
    if (!uklad.cats.includes(c2)) uklad.cats.push(c2);
    root.classList.toggle("collapsed", c2.collapsed);
    root.querySelector(".collapse").textContent = c2.collapsed ? "▸" : "▾";
    zapiszLayout();
  };
  podlaczDragKategorii(root, nazwa);
  catEls.set(nazwa, root);
  return root;
}

function renderModele(stan) {
  const cats = kategorieZUkladu(stan);
  const porz = porzadekModeli(stan);
  const modele = new Map((stan.models || []).map((m) => [m.id, m]));
  const ukladListy = cats.map((c) => `${c.name}>${(porz[c.name] || []).join(",")}`).join("|");
  const strukturalnie = ukladListy !== $("modelList").dataset.k;

  for (const c of cats) {
    const ids = (porz[c.name] || []).filter((id) => modele.has(id));
    const box = catEl(c.name);
    box.classList.toggle("collapsed", !!c.collapsed);
    box.querySelector(".collapse").textContent = c.collapsed ? "▸" : "▾";
    setText(box.querySelector(".cat-name"), c.name);
    const ile = ids.filter((id) => ["ready", "starting", "stopping"].includes(modele.get(id).status)).length;
    setText(box.querySelector(".cat-count"), `${ids.length} modeli`);
    setText(box.querySelector(".cat-run"), ile ? `· ${ile} pracuje` : "");
    if (strukturalnie && !drag) {
      for (const id of ids) box.querySelector(".cat-models").appendChild(wierszModelu(modele.get(id)).root);
    }
  }
  if (strukturalnie && !drag) {
    $("modelList").dataset.k = ukladListy;
    for (const c of cats) $("modelList").appendChild(catEl(c.name));
    for (const [nazwa, box] of catEls) {
      if (!cats.some((c) => c.name === nazwa)) { box.remove(); catEls.delete(nazwa); }
    }
    for (const [id, w] of modelWiersze) {
      if (!modele.has(id)) { w.root.remove(); modelWiersze.delete(id); }
    }
  }
  for (const m of stan.models || []) renderModelValues(m);
}

function renderModelValues(m) {
  const w = modelWiersze.get(m.id);
  if (!w) return;
  const status = m.status || "stopped";
  setText(w.name, m.name);
  w.pin.hidden = !m.pin;
  w.root.classList.toggle("pinned", !!m.pin);
  w.dot.className = "kropka status-dot " + status;
  w.dot.title = STAN_TXT[status] || status;
  const trzymane = ["drop-before", "drop-after", "drop-in", "drag-src"].filter((c) => w.root.classList.contains(c));
  w.root.className = "model" + (status === "stopped" ? "" : " " + status) + (m.pin ? " pinned" : "");
  for (const c of trzymane) w.root.classList.add(c);
  const plak = PLAKIETKA[status];
  w.plak.hidden = !plak;
  setText(w.plak, plak || "");

  const pl = m.running_placement || m.plan_placement;
  const meta = [`port ${m.port}`, pl ? wariantLabel(pl) : "brak wariantu",
    m.kind === "launcher" ? (m.profile || (m.script || "").split("/").pop() + (m.args || []).join(" ")) : "cmd",
    m.ctx ? `ctx ${m.ctx.toLocaleString("pl-PL")}` : "ctx –",
    m.ram_gb ? `RAM ${m.ram_gb} GB` : null,
    m.adopted ? "uruchomiony poza Centrum" : null].filter(Boolean);
  setText(w.meta, meta.join(" · "));

  const placements = m.placements || [];
  if (placements.length > 1) {
    const klucz = placements.map(wariantLabel).join("|");
    if (w.select.dataset.k !== klucz) {
      w.select.dataset.k = klucz;
      w.select.innerHTML = placements.map((p, i) => `<option value="${i}">${esc(wariantLabel(p))}</option>`).join("");
      const best = m.plan_placement;
      const idx = best ? placements.findIndex((p) => JSON.stringify(p) === JSON.stringify(best)) : 0;
      w.select.value = String(idx >= 0 ? idx : 0);
    }
    w.select.hidden = status !== "stopped";
  } else w.select.hidden = true;

  const ld = status === "starting" ? m.ladowanie : null;
  w.postep.hidden = !ld;
  if (ld) {
    const czas = `${Math.floor(ld.czas_s / 60)}:${String(Math.floor(ld.czas_s % 60)).padStart(2, "0")}`;
    if (ld.etap === "kolejka") {
      setBar(w.postepBar, 0, "");
      setText(w.postepTxt, `zatrzymuję poprzedni model i uruchamiam ten · ${czas}`);
    } else {
      setBar(w.postepBar, ld.pct ?? 0, "");
      setText(w.postepTxt, `wczytano ${GB(ld.wczytano_mb)}` + (ld.rozmiar_mb ? ` / ${GB(ld.rozmiar_mb)} GB (${Math.round(ld.pct)}%)` : " GB")
        + ` · RAM ${GB(ld.ram_mb)} GB · VRAM ${GB(ld.vram_mb)} GB · ${czas}`);
    }
  }
  const canStart = m.can_start && status === "stopped";
  w.start.disabled = !canStart;
  w.start.title = canStart ? (pl ? `Start: ${wariantLabel(pl)}` : "Start") : (m.plan_reason || "brak miejsca");
  const czynny = status === "ready" || status === "starting" || status === "stopping";
  w.stop.disabled = !czynny;
  w.force.hidden = !czynny;
  w.restart.hidden = !(status === "ready" || status === "failed");

  const zatrzyma = (m.zatrzymaj || []).length ? m.zatrzymaj : null;
  w.switchBtn.hidden = !(status === "ready" || (zatrzyma && status === "stopped"));
  if (zatrzyma && status === "stopped") {
    const nazwy = zatrzyma.map((id) => (ostatniStan?.models || []).find((x) => x.id === id)?.name || id).join(", ");
    w.switchBtn.title = `Jednym kliknięciem: zatrzyma ${nazwy} i od razu ładuje ten model`;
    w.switchBtn.onclick = () => akcja(() => api("POST", `/api/models/${m.id}/start`, {}), "Przełączanie");
  } else {
    w.switchBtn.title = "Przełącz na inny model";
    w.switchBtn.onclick = () => otworzSwitch(m.id);
  }
  w.del.hidden = czynny;

  const reasonShown = status === "stopped" && !m.can_start && !zatrzyma && m.plan_reason;
  w.reason.hidden = !reasonShown;
  if (reasonShown) setText(w.reason, m.plan_reason);
  const tail = status === "failed" ? (m.log_tail || []).join("\n") : "";
  w.tail.hidden = !tail;
  if (tail) setText(w.tail, tail);
}

/* ---------- dragging models and categories ---------- */
function podlaczDragModelu(root, id) {
  root.addEventListener("dragstart", (e) => {
    e.stopPropagation();
    drag = { typ: "model", id, el: root };
    root.classList.add("drag-src");
    e.dataTransfer.effectAllowed = "move";
    e.dataTransfer.setData("text/plain", id);
  });
  root.addEventListener("dragend", () => { root.classList.remove("drag-src"); zoznaczWstawienie(root); drag = null; });
  root.addEventListener("dragover", (e) => {
    if (!drag || drag.typ !== "model" || drag.el === root) return;
    e.preventDefault(); e.stopPropagation();
    oznaczWstawienie(root, gdzieUpasc(e, root, true));
  });
  root.addEventListener("dragleave", () => zoznaczWstawienie(root));
  root.addEventListener("drop", (e) => {
    if (!drag || drag.typ !== "model" || drag.el === root) return;
    e.preventDefault(); e.stopPropagation();
    const przed = gdzieUpasc(e, root, true);
    zoznaczWstawienie(root);
    const docelowaKategoria = root.closest(".cat").dataset.cat;
    wstawModel(drag.id, docelowaKategoria, root.dataset.id, przed);
  });
}

function podlaczDragKategorii(root, nazwa) {
  root.addEventListener("dragstart", (e) => {
    if (!e.target.closest(".cat-head") || e.target.closest(".collapse")) { e.preventDefault(); return; }
    e.stopPropagation();
    drag = { typ: "cat", id: nazwa, el: root };
    root.classList.add("drag-src");
    e.dataTransfer.effectAllowed = "move";
    e.dataTransfer.setData("text/plain", nazwa);
  });
  root.addEventListener("dragend", () => { root.classList.remove("drag-src"); zoznaczWstawienie(root); drag = null; });
  root.addEventListener("dragover", (e) => {
    if (!drag || (drag.typ !== "cat" && drag.typ !== "model") || drag.el === root) return;
    e.preventDefault();
    if (drag.typ === "cat") oznaczWstawienie(root, gdzieUpasc(e, root, true));
    else root.classList.add("drop-in");
  });
  root.addEventListener("dragleave", () => { zoznaczWstawienie(root); root.classList.remove("drop-in"); });
  root.addEventListener("drop", (e) => {
    if (!drag || drag.el === root) return;
    e.preventDefault();
    zoznaczWstawienie(root); root.classList.remove("drop-in");
    if (drag.typ === "cat") {
      const przed = gdzieUpasc(e, root, true);
      const c = uklad.cats.filter((x) => x.name !== drag.id);
      let j = c.findIndex((x) => x.name === nazwa);
      if (j < 0) j = c.length; else if (!przed) j += 1;
      const przenoszony = uklad.cats.find((x) => x.name === drag.id) || { name: drag.id, collapsed: false };
      c.splice(j, 0, przenoszony);
      uklad.cats = c;
      $("modelList").dataset.k = "";
      zapiszLayout();
    } else {
      wstawModel(drag.id, nazwa, null, false);
    }
  });
}

function wstawModel(id, kategoria, przedId, przed) {
  const p = {};
  for (const [kat, ids] of Object.entries(porzadekLokalny || {})) p[kat] = ids.filter((x) => x !== id);
  const docel = p[kategoria] || (p[kategoria] = []);
  let j = przedId ? docel.indexOf(przedId) : -1;
  if (j < 0) j = przed ? 0 : docel.length; else if (!przed) j += 1;
  docel.splice(j, 0, id);
  if (!uklad.cats.some((c) => c.name === kategoria)) uklad.cats.push({ name: kategoria, collapsed: false });
  porzadekLokalny = p;
  $("modelList").dataset.k = "";
  // 02.11: TP1/TP2 to kategorie wyliczone z LAYOUTU (serwer) — bez nich PUT /api/models/order
  // would move the model back to the old category on the next refresh.
  for (const [kat, ids] of Object.entries(porzadekZSerwera(ostatniStan || { models: [] }))) {
    if (KAT_TP.includes(kat) && !(kat in p)) p[kat] = ids;
  }
  zapiszKolejnosc(p);
}

async function zapiszKolejnosc(p) {
  czekamOrder = true;
  const body = {};
  for (const c of uklad.cats) if (p[c.name] && p[c.name].length) body[c.name] = p[c.name];
  for (const k of Object.keys(p)) if (!(k in body)) body[k] = p[k];
  try { await api("PUT", "/api/models/order", body); }
  catch (e) { toast(`Zapis kolejności: ${e.message}`, true); }
  setTimeout(() => { czekamOrder = false; }, 600);
}

/* ---------- menu modelu (prawy przycisk myszy): PIN / UNPIN ---------- */
let menuModelu = null;
function zamknijMenuModelu() {
  if (menuModelu) { menuModelu.remove(); menuModelu = null; }
}
async function przypnijModel(id, pin) {
  try {
    await api("PUT", `/api/models/${id}/pin`, { pin: pin ? 1 : 0 });
    if (ostatniStan) {
      const m = (ostatniStan.models || []).find((x) => x.id === id);
      if (m) m.pin = pin ? 1 : 0;
    }
    porzadekLokalny = null;            // list rebuilt from the new order (server)
    $("modelList").dataset.k = "";
    if (ostatniStan) renderModele(ostatniStan);
    toast(pin ? "Przypięty — nie zmieni miejsca na liście" : "Odpięty — sortuje się normalnie");
  } catch (e) { toast(`Pin: ${e.message}`, true); }
}
function otworzMenuModelu(e, id) {
  zamknijMenuModelu();
  const m = (ostatniStan?.models || []).find((x) => x.id === id);
  if (!m) return;
  const menu = el("div", "ctx-menu");
  menu.setAttribute("role", "menu");
  const pozycja = (porzadekLokalny && porzadekLokalny[tpKategoria(m) || m.category] || []).indexOf(id);
  const wiersze = [
    { txt: m.pin ? "UNPIN — odpiąć z miejsca" : "PIN — przypiąć na miejscu", akcja: () => przypnijModel(id, !m.pin) },
    { txt: "START", akcja: () => akcja(() => api("POST", `/api/models/${id}/start`, {}), "Start"), ukryj: m.status !== "stopped" },
    { txt: "STOP", akcja: () => akcja(() => api("POST", `/api/models/${id}/stop`), "Stop"), ukryj: m.status === "stopped" },
    { txt: "PRZEŁĄCZ model", akcja: () => otworzSwitch(id) },
    { txt: "USTAWIENIA", akcja: () => otworzFormularz(id) },
    { txt: "LOG", akcja: () => otworzLog(id) },
  ];
  const info = el("div", "ctx-info");
  info.textContent = `${m.name} · ${tpKategoria(m) || m.category}${pozycja >= 0 ? ` · miejsce ${pozycja + 1}` : ""}${m.pin ? " · 📌 przypięty" : ""}`;
  menu.appendChild(info);
  for (const w of wiersze) {
    if (w.ukryj) continue;
    const b = el("button", "ctx-item", esc(w.txt));
    b.setAttribute("role", "menuitem");
    b.onclick = () => { zamknijMenuModelu(); w.akcja(); };
    menu.appendChild(b);
  }
  document.body.appendChild(menu);
  menuModelu = menu;
  const r = menu.getBoundingClientRect();
  menu.style.left = `${Math.max(4, Math.min(e.clientX, window.innerWidth - r.width - 8))}px`;
  menu.style.top = `${Math.max(4, Math.min(e.clientY, window.innerHeight - r.height - 8))}px`;
}
document.addEventListener("click", (e) => { if (menuModelu && !menuModelu.contains(e.target)) zamknijMenuModelu(); });
document.addEventListener("contextmenu", (e) => { if (menuModelu && !e.target.closest(".model")) zamknijMenuModelu(); });
document.addEventListener("keydown", (e) => { if (e.key === "Escape") zamknijMenuModelu(); });
window.addEventListener("scroll", () => zamknijMenuModelu(), true);

/* ---------- categories: add / rename ---------- */
let kategoriaEdytowana = null;
function otworzKategorie(nazwa) {
  kategoriaEdytowana = nazwa;
  $("catModalTitle").textContent = nazwa ? "ZMIEŃ NAZWĘ KATEGORII" : "NOWA KATEGORIA";
  $("catName").value = nazwa || "";
  $("catErr").textContent = "";
  otworz("catModal");
  $("catName").focus();
}
$("catGo").onclick = async () => {
  const nowa = $("catName").value.trim();
  if (!nowa) { $("catErr").textContent = "podaj nazwę"; return; }
  if (nowa !== kategoriaEdytowana && uklad.cats.some((c) => c.name === nowa)) {
    $("catErr").textContent = "kategoria już istnieje"; return;
  }
  if (kategoriaEdytowana === null) {
    uklad.cats.push({ name: nowa, collapsed: false });
    $("modelList").dataset.k = "";
    await zapiszLayout();
  } else {
    const ids = (porzadekLokalny && porzadekLokalny[kategoriaEdytowana]) || [];
    for (const c of uklad.cats) if (c.name === kategoriaEdytowana) c.name = nowa;
    if (ids.length) {
      porzadekLokalny[nowa] = ids;
      delete porzadekLokalny[kategoriaEdytowana];
      $("modelList").dataset.k = "";
      await zapiszKolejnosc(porzadekLokalny);
    } else {
      $("modelList").dataset.k = "";
      await zapiszLayout();
    }
  }
  zamknij("catModal");
};

/* ---------------- panel ENERGIA (§12a) ---------------- */
let energiaDane = null, energiaTryb = "godziny", energiaSprawnosc = null;

function budujEnergia(body, head) {
  const btnCsv = el("button", "btn ghost", "POBIERZ CSV");
  btnCsv.title = "Eksport minutowy (CSV)";
  btnCsv.onclick = () => { window.location.href = "/api/energia.csv"; };
  const btnRap = el("button", "btn ghost", "RAPORT");
  btnRap.title = "Raport zużycia za okres (strona do druku/PDF, zapis w H:\\!raporty)";
  btnRap.onclick = () => {
    const dzis = new Date(), pad = (n) => String(n).padStart(2, "0");
    const iso = (d) => `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`;
    const od0 = iso(new Date(dzis.getFullYear(), dzis.getMonth(), 1));
    const v = prompt("Okres raportu: OD DO (RRRR-MM-DD RRRR-MM-DD)", `${od0} ${iso(dzis)}`);
    if (!v) return;
    const [od, d2] = v.trim().split(/\s+/);
    window.open(`/api/energia/raport?od=${encodeURIComponent(od)}&do=${encodeURIComponent(d2 || od)}`, "_blank");
  };
  const btnCfg = el("button", "btn ghost", "SPRAWNOŚĆ PSU");
  btnCfg.title = "Sprawność zasilacza (DC → gniazdko)";
  btnCfg.onclick = () => {
    const v = parseFloat(prompt("Sprawność PSU (0.30–1.00, np. 0.90):", String(energiaSprawnosc ?? 0.9)));
    if (!isFinite(v)) return;
    akcja(() => api("PUT", "/api/energia/config", { psu_sprawnosc: v }).then((r) => {
      energiaSprawnosc = r.psu_sprawnosc;
      toast(`Sprawność PSU: ${r.psu_sprawnosc}`);
    }), "Sprawność PSU");
  };
  head.append(btnRap, btnCfg, btnCsv);
  body.innerHTML = `
    <div class="en-top">
      <div class="big"><b class="num" id="enNow">–</b><span class="unit">W Z GNIAZDKA</span>
        <span class="en-flag" id="enFlag" hidden>SZACUNEK</span></div>
      <div class="kv"><span>za zasilaczami (DC)</span><b class="num" id="enGniazdko">–</b></div>
      <div class="kv"><span>zasilacz Corsair: CPU, płyta, RAM, dyski, wentylatory, T400</span><b class="num" id="enPsuGlowny">–</b></div>
      <div class="kv"><span id="enPsuGpuOpis">zasilacz V100 (osobny)</span><b class="num" id="enPsuGpu">–</b></div>
      <div class="kv"><span id="enUrzOpis">urządzenia stałe (monitory, audio)</span><b class="num" id="enUrzadzenia">–</b></div>
    </div>
    <div class="en-kv">
      <div class="kv"><span>śr./h teraz (60 min)</span><b class="num" id="enGodzina">–</b></div>
      <div class="kv"><span>śr./h w tym miesiącu</span><b class="num" id="enSred">–</b></div>
      <div class="kv"><span>dziś</span><b class="num" id="enDzis">–</b></div>
      <div class="kv"><span>miesiąc</span><b class="num" id="enMiesiac">–</b></div>
      <div class="kv"><span>godzin pracy w miesiącu</span><b class="num" id="enGodziny">–</b></div>
      <div class="kv"><span>w tym urządzenia stałe (miesiąc)</span><b class="num" id="enUrzMiesiac">–</b></div>
    </div>
    <div class="en-tryb">
      <button class="btn ghost" id="enBiezacyMiesiac" title="Raport od pierwszego dnia miesiąca do dziś">BIEŻĄCY MIESIĄC — RAPORT</button>
    </div>
    <div class="en-wykres">
      <div class="en-tryb">
        <button class="btn ghost tryb active" data-tryb="godziny">48 GODZIN (Wh)</button>
        <button class="btn ghost tryb" data-tryb="dni">DNI MIESIĄCA (kWh)</button>
      </div>
      <canvas id="enCanvas"></canvas>
    </div>`;
  body.querySelector("#enBiezacyMiesiac").onclick = () => {
    window.open("/api/energia/raport", "_blank");
  };
  body.querySelectorAll(".tryb").forEach((b) => {
    b.onclick = () => {
      energiaTryb = b.dataset.tryb;
      body.querySelectorAll(".tryb").forEach((x) => x.classList.toggle("active", x === b));
      rysujEnergie();
    };
  });
}

function renderEnergia(stan) {
  const e = stan.energia || {};
  energiaDane = e;
  if (e.psu_sprawnosc !== undefined && e.psu_sprawnosc !== null) energiaSprawnosc = e.psu_sprawnosc;
  /* one power figure everywhere (panel, MOC bar, gauge, Wh/kWh): power from the OUTLET;
     DC = Corsair read (with T400) + V100 from NVML — each PSU divided by its own efficiency */
  setText($("enNow"), e.teraz_w_gniazdko == null ? "–" : String(Math.round(e.teraz_w_gniazdko)));
  setText($("enGniazdko"), e.teraz_w == null ? "–" : `${Math.round(e.teraz_w)} W`);
  const spr = (v) => (v == null ? "" : ` ÷ ${String(v).replace(".", ",")}`);
  setText($("enPsuGlowny"), e.psu_glowny_w == null ? "–" : `${Math.round(e.psu_glowny_w)} W${spr(e.psu_sprawnosc)}`);
  setText($("enPsuGpu"), e.psu_gpu_w == null ? "–" : `${Math.round(e.psu_gpu_w)} W${spr(e.psu_gpu_sprawnosc)}`);
  const karty = Object.entries(e.psu_gpu_karty || {});   // each V100 from its own NVML measurement (not ×2)
  setText($("enPsuGpuOpis"), karty.length
    ? `zasilacz V100: ${karty.map(([i, w]) => `GPU${i} ${Math.round(w)} W`).join(" + ")} =` : "zasilacz V100 (osobny)");
  const urz = e.urzadzenia || [];                       // always-on, powered from the outlet (not through the PSU)
  const pl = (v) => String(v).replace(".", ",");
  setText($("enUrzadzenia"), e.urzadzenia_w == null ? "–" : `${pl(e.urzadzenia_w)} W`);
  setText($("enUrzOpis"), urz.length
    ? `urządzenia stałe: ${urz.map((u) => `${u.sztuk}× ${pl(u.moc_w)} W`).join(" + ")} =`
    : "urządzenia stałe (monitory, audio)");
  const flag = $("enFlag");
  if (flag) flag.hidden = !e.szacunek;
  if (e.ostatnia_godzina_wh === undefined) return;      // brak agregatow (backend bez bazy)
  const w = (id, txt) => setText($(id), txt);
  w("enGodzina", `${(e.ostatnia_godzina_sred_w ?? 0).toFixed(1).replace(".", ",")} Wh`);
  w("enSred", `${e.miesiac_sred_w.toFixed(1).replace(".", ",")} Wh`);
  w("enDzis", `${e.dzis_kwh.toFixed(2).replace(".", ",")} kWh`);
  w("enMiesiac", `${e.miesiac_kwh.toFixed(2).replace(".", ",")} kWh`);
  w("enGodziny", String(e.miesiac_godzin_pracy));
  w("enUrzMiesiac", `${(e.miesiac_urzadzenia_kwh ?? 0).toFixed(2).replace(".", ",")} kWh`);
  rysujEnergie();
}

function rysujEnergie() {
  const c = $("enCanvas");
  if (!c || !energiaDane) return;
  const dpr = window.devicePixelRatio || 1;
  const w = c.clientWidth, h = c.clientHeight;
  if (!w || !h) return;
  if (c.width !== w * dpr || c.height !== h * dpr) { c.width = w * dpr; c.height = h * dpr; }
  const ctx = c.getContext("2d");
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, w, h);
  const dane = energiaTryb === "dni" ? energiaDane.dni.map((x) => ({ v: x.kwh, t: x.data }))
                                      : energiaDane.godziny.map((x) => ({ v: x.wh, t: x.ts }));
  const max = Math.max(...dane.map((x) => x.v), 0);
  ctx.strokeStyle = "rgba(26,77,122,.55)"; ctx.lineWidth = 1;
  for (let i = 1; i < 4; i++) {
    const y = Math.round((h / 4) * i) + 0.5;
    ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke();
  }
  if (!max) return;
  const n = dane.length || 1, slot = w / n, bw = Math.max(1, slot * 0.62);
  for (let i = 0; i < dane.length; i++) {
    const v = dane[i].v;
    const bh = Math.max(1, (v / max) * (h - 6));
    const x = i * slot + (slot - bw) / 2;
    ctx.fillStyle = i === dane.length - 1 ? ORANGE : CYAN;
    ctx.shadowColor = i === dane.length - 1 ? "rgba(255,138,31,.5)" : "rgba(56,214,255,.4)";
    ctx.shadowBlur = 6;
    ctx.fillRect(x, h - bh - 2, bw, bh);
  }
  ctx.shadowBlur = 0;
}

/* ---------------- orphans (§13: panel removed from UI — backend stays, handling of FREE GPU 0/1 buttons) ---------------- */
function renderSieroty(stan) {
  const wszystkie = stan.orphans || [];
  for (const i of [0, 1]) {                      // counters of foreign processes at the top bar buttons
    const guz = document.querySelector(`.btn-gpu-free[data-gpu="${i}"]`);
    if (guz) setText(guz.querySelector(".cnt"),
      `(${wszystkie.filter((o) => (o.gpus || []).some((g) => g.gpu === i)).length})`);
  }
  const modal = $("freeModal");
  if (modal.hidden) return;                      // lista w modalu = procesy na karcie wskazanej przyciskiem
  const gi = Number(modal.dataset.gpu || 0);
  const list = $("freeList");
  const orph = wszystkie.filter((o) => (o.gpus || []).some((g) => g.gpu === gi));
  const klucz = orph.map((o) => `${o.pid}:${o.vram_mb}:${(o.gpus || []).map((g) => `${g.gpu}:${g.vram_mb}`).join(",")}`).join("|");
  if (list.dataset.k === klucz) return;
  list.dataset.k = klucz;
  list.innerHTML = "";
  if (!orph.length) {
    list.innerHTML = `<div class="mut" style="font-size:12px">Brak obcych procesów na GPU ${gi} — nic do zwolnienia.</div>`;
    return;
  }
  for (const o of orph) {
    const row = el("div", "proc sierota");
    const gpus = o.gpus || [];
    const karty = gpus.map((g) => `GPU ${g.gpu}`).join(" + ");
    const perKarta = gpus.map((g) => `GPU ${g.gpu}: ${(g.vram_mb || 0).toLocaleString("pl-PL")} MiB`).join(" · ");
    row.innerHTML = `<span class="pid">PID ${esc(o.pid)}</span><span class="pname" title="${esc(o.cmdline || o.name)}">${esc(o.name)}</span>
      <span class="mut">${karty} · ${uptimeTxt(o.uptime_s)}</span>
      <span class="vram" title="${esc(perKarta)}">${(o.vram_mb || 0).toLocaleString("pl-PL")} MiB</span>`;
    const btn = el("button", "btn danger ghost", "ZWOLNIJ");
    btn.onclick = () => potwierdz(`Zabić sierotę PID ${o.pid} (${o.name}, ${o.cmdline})?`,
      () => api("POST", `/api/orphans/${o.pid}/kill`).then(() => toast(`PID ${o.pid} zabity`)));
    row.appendChild(btn);
    list.appendChild(row);
  }
}

/* ---------------- badges of running models in the top bar (§11c) ---------------- */
function renderRunBadges(stan) {
  const box = $("runBadges");
  const lista = (stan.models || [])
    .filter((m) => ["ready", "starting", "failed"].includes(m.status))
    .map((m) => `${m.status}:${m.name}`).join("|");
  if (box.dataset.k === lista) return;
  box.dataset.k = lista;
  box.innerHTML = (stan.models || [])
    .filter((m) => ["ready", "starting", "failed"].includes(m.status))
    .map((m) => `<span class="run-badge ${esc(m.status)}">${m.status === "ready" ? "●" : m.status === "starting" ? "⏳" : "✖"} ${esc(m.name)}</span>`)
    .join("");
}

/* ---------------- formularz modelu (dodaj / edytuj) ---------------- */
let edytowanyId = null;
let wariantyEdytowanego = [];   // original placements — asymmetric placements can NOT be flattened
let wariantEdytowany = 0;       // ktory wariant edytujemy (karty/VRAM dotycza tylko niego)

/* zbudujWarianty — czysta funkcja (testowalna w node): zachowuje ISTNIEJACE warianty modelu.
   Zmiana karty/VRAM w UI dotyczy tylko WSKAZANEGO wariantu; pozostale (np. strata-iq3 25000+31200,
   obrazy-2gpu 24000+12000) zostaja bez zmian — wczesniej zapis formularza plaskol wszystko do
   jednego VRAM, przez co planer niedoszacowywal rezerwacji (AUDYT: OOM na przepelnionej karcie).
   Asymetryczny wariant zostaje nienaruszony, dopoki uzytkownik NIE wpisal innej wartosci VRAM. */
function zbudujWarianty(istniejace, gpus, vram, index) {
  const v = Math.max(128, Math.round(vram || 0));
  if (!istniejace || !istniejace.length) {
    return gpus.length === 1 && gpus[0] !== 2
      ? [{ gpus: [0], vram: [v] }, { gpus: [1], vram: [v] }]   // nowy model jednokartowy: GPU0 i GPU1
      : [{ gpus: [...gpus], vram: gpus.map(() => v) }];
  }
  const i = Math.min(Math.max(index || 0, 0), istniejace.length - 1);
  return istniejace.map((p, j) => {
    if (j !== i) return { gpus: [...p.gpus], vram: [...p.vram] };   // inne warianty — bez zmian
    const asymetryczny = new Set(p.vram).size > 1;
    const vramNowe = asymetryczny && v === (p.vram || [0])[0]
      ? [...p.vram]                       // VRAM nie zostalo zmienione — zachowaj podzial na karty
      : gpus.map(() => v);                // swiadoma zmiana — nowa wartosc na kazda karte wariantu
    return { gpus: [...gpus], vram: vramNowe };
  });
}

function otworzFormularz(id) {
  const m = id ? (ostatniStan?.models || []).find((x) => x.id === id) : null;
  if (id && !m) { toast(`Brak modelu ${id} w stanie — odswiez strone`, true); return; }
  edytowanyId = id || null;
  wariantyEdytowanego = (m?.placements || []).map((p) => ({ gpus: [...(p.gpus || [])], vram: [...(p.vram || [])] }));
  $("modelModalTitle").textContent = id ? `USTAWIENIA: ${m.name}` : "DODAJ MODEL";
  const f = $("modelForm");
  const ustaw = (n, v) => { if (f.elements[n]) f.elements[n].value = v ?? ""; };
  ustaw("id", m?.id || "");
  f.elements.id.disabled = !!id;
  ustaw("name", m?.name || "");
  ustaw("category", m?.category || "Inne");
  f.elements.kind.value = m?.kind || "launcher";
  ustaw("script", m?.script || "");
  ustaw("args", (m?.args || []).join(" "));
  ustaw("stan", m?.stan || "");
  const sm = m?.stan_match || {};
  const klucz = Object.keys(sm)[0] || "profil";
  ustaw("stan_key", klucz);
  ustaw("stan_val", sm[klucz] || m?.profile || "");
  ustaw("gpu_env", m?.gpu_env || "");
  ustaw("port_env", m?.port_env || "");
  ustaw("cmd", (m?.cmd || []).join(" "));
  ustaw("port", m?.port || 8033);
  ustaw("health", m?.health || "/v1/models");
  ustaw("ctx", m?.ctx || 0);
  const pl = (m?.placements || [])[0] || { gpus: [0], vram: [20000] };
  pokazWarianty(f, pl);
  ustaw("ram_gb", m?.ram_gb ?? 2);
  ustaw("extra_args", (m?.extra_args || []).join(" "));
  ustaw("env", Object.entries(m?.env || {}).map(([k, v]) => `${k}=${v}`).join(", "));
  $("modelFormErr").textContent = "";
  pokazRodzaj();
  otworz("modelModal");
}
/* pokaza warianty modelu w formularzu (select „Wariant') — przy edycji ISTNIEJACEGO modelu
   formularz NIE moze pokazywac tylko pierwszego wariantu (zapis plaszczol
   strata-iq3 25000+31200 do 25000+25000). Karty/VRAM edytujemy tylko wskazany wariant. */
function pokazWarianty(f, domyślny) {
  const select = f.elements.wariant;
  if (!select) return;
  const lista = wariantyEdytowanego.length ? wariantyEdytowanego : [domyślny];
  select.innerHTML = lista.map((p, i) => `<option value="${i}">${esc(wariantLabel(p))}${lista.length > 1 ? "" : " (nowy)"}</option>`).join("");
  select.hidden = lista.length <= 1;
  wariantEdytowany = 0;
  select.onchange = () => {
    wariantEdytowany = parseInt(select.value, 10) || 0;
    const p = lista[wariantEdytowany] || lista[0];
    f.elements.gpusel.value = (p.gpus || [0]).join(",");
    f.elements.vram.value = (p.vram || [20000])[0];
  };
  const p = lista[0];
  f.elements.gpusel.value = (p.gpus || [0]).join(",");
  f.elements.vram.value = (p.vram || [20000])[0];
}
function pokazRodzaj() {
  const kind = $("modelForm").elements.kind.value;
  document.querySelectorAll(".launcher-only").forEach((n) => { n.style.display = kind === "launcher" ? "" : "none"; });
  document.querySelectorAll(".cmd-only").forEach((n) => { n.style.display = kind === "cmd" ? "" : "none"; });
}
$("modelForm").elements.kind.addEventListener("change", pokazRodzaj);

$("modelForm").addEventListener("submit", async (e) => {
  e.preventDefault();
  const f = e.target, g = (n) => f.elements[n].value.trim();
  const gpus = f.elements.gpusel.value.split(",").map(Number);
  const vram = parseInt(f.elements.vram.value, 10);
  const placements = zbudujWarianty(edytowanyId ? wariantyEdytowanego : null, gpus, vram, wariantEdytowany);
  const env = {};
  for (const cz of g("env").split(",").map((s) => s.trim()).filter(Boolean)) {
    const [k, ...v] = cz.split("=");
    if (k) env[k] = v.join("=");
  }
  const kind = f.elements.kind.value;
  const stanMatch = g("stan_val") ? { [g("stan_key") || "profil"]: g("stan_val") } : {};
  const body = {
    id: g("id"), name: g("name"), kind, category: g("category") || "Inne",
    script: kind === "launcher" ? g("script") : null,
    args: kind === "launcher" ? g("args").split(/\s+/).filter(Boolean) : [],
    stan: kind === "launcher" ? g("stan") : null,
    stan_match: kind === "launcher" ? stanMatch : {},
    profile: kind === "launcher" ? (g("stan_val") || null) : null,
    gpu_env: kind === "launcher" ? (g("gpu_env") || null) : null,
    port_env: kind === "launcher" ? (g("port_env") || null) : null,
    cmd: kind === "cmd" ? g("cmd").split(/\s+/).filter(Boolean) : null,
    port: parseInt(f.elements.port.value, 10),
    health: g("health") || "/v1/models",
    ctx: parseInt(f.elements.ctx.value, 10) || 0,
    ram_gb: parseFloat(f.elements.ram_gb.value) || 0,
    extra_args: g("extra_args").split(/\s+/).filter(Boolean),
    env, placements,
  };
  try {
    if (edytowanyId) await api("PUT", `/api/models/${edytowanyId}`, body);
    else await api("POST", "/api/models", body);
    zamknij("modelModal");
    toast(edytowanyId ? "Model zaktualizowany" : "Model dodany");
  } catch (err) { $("modelFormErr").textContent = err.message; }
});

/* ---------------- import z pulpitu (§11a) ---------------- */
async function otworzImport() {
  $("importErr").textContent = "";
  $("importList").innerHTML = `<div class="mut">czytam ikony z ~/Pulpit…</div>`;
  otworz("importModal");
  let kandydaci = [];
  try { kandydaci = await api("GET", "/api/import-desktop"); }
  catch (e) { $("importErr").textContent = e.message; return; }
  const datalist = $("catList");
  datalist.innerHTML = uklad.cats.map((c) => `<option value="${esc(c.name)}">`).join("");
  if (!kandydaci.length) { $("importList").innerHTML = `<div class="mut">Brak nowych ikon — wszystko już w rejestrze.</div>`; return; }
  $("importList").innerHTML = kandydaci.map((c, i) => `
    <div class="imp-row" data-i="${i}">
      <input type="checkbox" checked>
      <input class="nm" value="${esc(c.name)}">
      <input class="kat" list="catList" value="${esc(c.category)}">
      <input class="vram" type="number" min="128" step="100" value="${esc(c.vram_mb || 20000)}">
      <input class="ram" type="number" min="0" step="1" value="${esc(c.ram_gb || 2)}">
      <span class="skrypt" title="${esc(c.script)} ${esc((c.args || []).join(" "))}">${esc(c.script)}${(c.args || []).length ? " " + esc(c.args.join(" ")) : ""}</span>
    </div>`).join("");
  $("importList").dataset.kandydaci = JSON.stringify(kandydaci);
}
$("importGo").onclick = async () => {
  const kandydaci = JSON.parse($("importList").dataset.kandydaci || "[]");
  const items = [];
  $("importList").querySelectorAll(".imp-row").forEach((row) => {
    const c = kandydaci[parseInt(row.dataset.i, 10)];
    if (!c || !row.querySelector("input[type=checkbox]").checked) return;
    items.push({ id: c.id, name: row.querySelector(".nm").value.trim() || c.name,
      category: row.querySelector(".kat").value.trim() || "Inne",
      script: c.script, args: c.args || [], port: c.port || null,
      vram_mb: parseInt(row.querySelector(".vram").value, 10) || 0,
      ram_gb: parseFloat(row.querySelector(".ram").value) || 0 });
  });
  if (!items.length) { $("importErr").textContent = "nic nie zaznaczono"; return; }
  try {
    const r = await api("POST", "/api/import-desktop", items);
    zamknij("importModal");
    toast(r.added.length ? `Zaimportowano: ${r.added.join(", ")}` : "Bez zmian");
  } catch (e) { $("importErr").textContent = e.message; }
};

/* ---------------- switching ---------------- */
let switchZ = null;
function otworzSwitch(id) {
  const m = (ostatniStan?.models || []).find((x) => x.id === id);
  if (!m) return;
  switchZ = id;
  $("switchInfo").textContent = `${m.name} zostanie natychmiast zatrzymany, a wybrany model od razu zacznie się ładować.`;
  const mozna = (x) => x.can_start || (x.zatrzymaj || []).length > 0;
  $("switchTarget").innerHTML = (ostatniStan.models || [])
    .filter((x) => x.id !== id && x.status === "stopped")
    .map((x) => `<option value="${esc(x.id)}" ${mozna(x) ? "" : "disabled"}>${esc(x.name)}${mozna(x) ? "" : " — " + esc(x.plan_reason || "nie mieści się")}</option>`)
    .join("");
  $("switchErr").textContent = "";
  otworz("switchModal");
}
$("switchGo").onclick = () => {
  const to = $("switchTarget").value;
  if (!switchZ || !to) return;
  akcja(() => api("POST", `/api/models/${switchZ}/switch`, { to }).then(() => zamknij("switchModal")), "Przełączanie");
};

/* ---------------- panel logu ---------------- */
let logId = null, logTimer = null, logOstatni = "";
function otworzLog(id) {
  logId = id;
  const m = (ostatniStan?.models || []).find((x) => x.id === id);
  $("logTitle").textContent = m ? m.name : id;
  $("logBody").textContent = "";
  logOstatni = "";
  $("logPanel").hidden = false;
  odswiezLog();
  clearInterval(logTimer);
  logTimer = setInterval(odswiezLog, 1000);
}
async function odswiezLog() {
  if (!logId || $("logPanel").hidden) { clearInterval(logTimer); return; }
  try {
    const r = await api("GET", `/api/models/${logId}/log?n=200`);
    const txt = r.lines.join("\n");
    if (txt !== logOstatni) {
      logOstatni = txt;
      const pre = $("logBody");
      pre.textContent = txt || "(pusto)";
      pre.scrollTop = pre.scrollHeight;
    }
  } catch (e) { $("logBody").textContent = e.message; }
}

/* ---------------- main render ---------------- */
function render(stan) {
  ostatniStan = stan;
  zbierzHistorie(stan);
  if (!layoutWczytany) return;      // §13: najpierw uklad z serwera — panele (i ich elementy) jeszcze nie istnieja
  renderPanele(stan);
  renderGPU(stan.gpus || []);
  renderLLM(stan);
  renderSystem(stan);
  renderModele(stan);
  renderSieroty(stan);
  renderEnergia(stan);
  renderRunBadges(stan);
}

/* ---------------- start: layout 1:1 from the server BEFORE panels appear (§13: no blink) ---------------- */
let layoutWczytany = false;
let brakujace = [];

async function pobierzLayout() {
  try {
    const layout = await api("GET", "/api/layout");
    if (Array.isArray(layout.panels)) uklad.panels = layout.panels.filter((p) => PANELE[p]);
    if (Array.isArray(layout.categories)) uklad.cats = layout.categories.map((c) => ({ name: c.name, collapsed: !!c.collapsed }));
    oknaZapisane = {};
    for (const [id, o] of Object.entries(layout.okna || {}))
      if (PANELE[id]) oknaZapisane[id] = { x: o.x, y: o.y, w: o.w, h: o.h };
    brakujace = aktywnePanele().filter((id) => !oknaZapisane[id]);   // §13 migracja: liczona PO pokazaniu kontenera
  } catch (e) {
    toast(`Układ paneli: ${e.message} — używam układu domyślnego`, true);
  } finally {
    layoutWczytany = true;
    $("panels").hidden = false;                     // panels appear already at the right places
    if (brakujace.length) {                         // szerokosc kontenera znana dopiero po pokazaniu
      const dom = domyslneOkna(szerokoscKontenera());
      for (const id of brakujace) oknaZapisane[id] = dom[id];
      zapiszLayout();                               // uklad domyslny od razu zapamietany na serwerze
    }
    zastosujUklad();
    if (ostatniStan) renderPanele(ostatniStan);
  }
}

function podlaczPasek() {
  $("ulozBtn").onclick = ulozDomyslnie;
  for (const btn of document.querySelectorAll(".btn-gpu-free"))
    btn.onclick = () => {
      const i = Number(btn.dataset.gpu);
      $("freeModal").dataset.gpu = String(i);
      zwolnijGpu(i);
    };
}

window.addEventListener("resize", () => {
  if (!layoutWczytany) return;
  zastosujUklad();                                  // clip to the new width — SAVED positions untouched
});

setInterval(() => setText($("zegar"), fmtTime(new Date())), 1000);
setText($("zegar"), fmtTime(new Date()));
podlaczPasek();
pobierzLayout();
polacz();
