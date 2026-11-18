/* CENTRUM MISJI — swobodny układ paneli (SPEC §13): moduł ES z czystymi funkcjami geometrii
   + obsługa wskaźnika (pointer events, setPointerCapture — NIE HTML5 DnD). Zero bibliotek.
   Panel = {id, x, y, w, h} w PIKSELACH (bez siatki, bez przyciągania). */
export const MIN_OKNO_W = 220, MIN_OKNO_H = 80, OKNO_GAP = 12;
export const OKNO_H = { gpu: 400, llm: 400, system: 340, models: 480, energia: 260 };
export const PANELS = ["gpu", "llm", "system", "models", "energia"];

/* dwa okna przecinają się (wyłącznie krawędzie NIE są przecięciem) */
export function przecina(a, b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

/* przycinanie do kontenera: x >= 0, y >= 0, x + w <= szerokosc (prawa krawedz nie moze wystac) */
export function przycin(o, szerokosc) {
  const maxW = Math.max(MIN_OKNO_W, szerokosc);
  const w = Math.max(MIN_OKNO_W, Math.min(Math.round(o.w), maxW));
  return {
    x: Math.max(0, Math.min(Math.round(o.x), Math.max(0, szerokosc - w))),
    y: Math.max(0, Math.round(o.y)),
    w,
    h: Math.max(MIN_OKNO_H, Math.round(o.h)),
  };
}

/* rozsun(panele, ruszanyId) — CZYSTA funkcja: zwraca NOWE okna; panele przecinające ruszany
   przesuwane w DÓŁ o minimalną wartość (kaskadowo, aż nic się nie przecina).
   Pozostałych paneli nie ruszamy (żadnego dociągania w górę). RUSZANY panel jest „najwyżej”
   w kolejności (zawsze wygrywa remis y) — dlatego on sam nigdy nie zostaje przesunięty. */
export function rozsun(panele, ruszanyId) {
  // Ruszany panel zostaje DOKŁADNIE tam, gdzie go upuszczono. Każdy panel, który na niego nachodzi,
  // zjeżdża tuż pod niego; kaskadowo dalej (pod panele już przesunięte). Panele bez kolizji — nietknięte.
  const out = {};
  for (const id of Object.keys(panele || {})) out[id] = { ...panele[id] };
  if (!out[ruszanyId]) return out;
  const stale = [ruszanyId];                        // panele, których pozycja jest już ustalona
  for (let iter = 0; iter < 1000; iter++) {
    let ruszone = false;
    for (const id of Object.keys(out).sort((a, b) => out[a].y - out[b].y || (a < b ? -1 : 1))) {
      if (stale.includes(id)) continue;
      const p = out[id];
      for (let k = 0; k < stale.length; k++) {     // az p nie przecina ZADNEGO ustalonego panelu
        const o = out[stale[k]];
        if (przecina(p, o)) { p.y = o.y + o.h + OKNO_GAP; ruszone = true; k = -1; }
      }
      if (ruszone) { stale.push(id); break; }    // ustalony; od nowa — kolejne mogą nachodzić na niego
    }
    if (!ruszone) break;
  }
  return out;
}

/* naprawUklad(panele) — CZYSTA funkcja: usuwa WSZYSTkie przecięcia (każde okno przesuwane w DÓŁ,
   kaskadowo, aż nic się nie przecina). Używane po wczytaniu układu z serwera (dostosowanie do
   bieżącej szerokości okna) i po rozsunie — pozycje x/rozmiary nigdy się nie zmieniają. */
export function naprawUklad(panele) {
  const out = {};
  for (const id of Object.keys(panele || {})) out[id] = { ...panele[id] };
  const ids = Object.keys(out);
  for (let iter = 0; iter <= ids.length * ids.length; iter++) {
    let ruszone = false;
    for (const id of ids) {
      const p = out[id];
      let y = p.y;
      for (const other of ids) {
        if (other === id) continue;
        const o = out[other];
        const nad = o.y < p.y || (o.y === p.y && ids.indexOf(other) < ids.indexOf(id));
        if (nad && przecina(p, o)) y = Math.max(y, o.y + o.h);
      }
      if (y > p.y) { p.y = y; ruszone = true; }
    }
    if (!ruszone) break;
  }
  return out;
}

/* układ domyślny §13 (odstęp 12 px): rząd 1 GPU (wszystkie karty, 1/2) | Model LLM (1/2), h 400;
   rząd 2 System (1/2, h 340) | Modele (1/2, h 480); pod Systemem Energia (1/2, h 260). */
export function domyslneOkna(szerokosc, paneli = PANELS) {
  const S = Math.max(660, Math.round(szerokosc));
  const pol = Math.floor((S - OKNO_GAP) / 2);
  const y2 = OKNO_H.gpu + OKNO_GAP;
  const out = {};
  if (paneli.includes("gpu")) out.gpu = { x: 0, y: 0, w: pol, h: OKNO_H.gpu };
  if (paneli.includes("llm")) out.llm = { x: pol + OKNO_GAP, y: 0, w: S - pol - OKNO_GAP, h: OKNO_H.llm };
  if (paneli.includes("system")) out.system = { x: 0, y: y2, w: pol, h: OKNO_H.system };
  if (paneli.includes("models")) out.models = { x: pol + OKNO_GAP, y: y2, w: S - pol - OKNO_GAP, h: OKNO_H.models };
  if (paneli.includes("energia")) out.energia = { x: 0, y: y2 + OKNO_H.system + OKNO_GAP, w: pol, h: OKNO_H.energia };
  let dodatkowe = 0;
  for (const id of paneli) if (!out[id]) { out[id] = { x: 0, y: 0, w: Math.min(400, S), h: OKNO_H[id] || 300 }; dodatkowe++; }
  if (dodatkowe) naprawUklad(out);                     // nieznane panele — rozsuniete, zeby sie nie nakladaly
  return out;
}

/* wąskie okno (< 900 px): panele jeden pod drugim na pełną szerokość — zapisane pozycje nietknięte */
export function waskiUklad(panele, kolejnosc, szerokosc) {
  const out = {};
  let y = 0;
  for (const id of kolejnosc) {
    const o = (panele || {})[id] || { w: 400, h: 300 };
    const h = Math.max(240, Math.round(o.h) || 240);
    out[id] = { x: 0, y, w: Math.max(MIN_OKNO_W, Math.round(szerokosc)), h };
    y += h + OKNO_GAP;
  }
  return out;
}

/* wysokość kontenera = najniższa krawędź + 12 px (strona przewija się w pionie) */
export function wysokoscKontenera(panele) {
  let max = 0;
  for (const o of Object.values(panele || {})) max = Math.max(max, (o.y || 0) + (o.h || 0));
  return max + OKNO_GAP;
}

/* ---------- obsługa wskaźnika ----------
   ctx = { szerokosc(), okno(id), setOkno(id, okno), onDragStart(id), onEnd(id, typ), onDomylny(id) }
   Przenoszenie: chwyt za CAŁY nagłówek (poza przyciskami) — panel płynnie podąża za wskaźnikiem.
   Rozsun (bez nakładania) następuje dopiero po upuszczeniu / zmianie rozmiaru. */
export function podlaczPrzenoszenie(sec, ctx) {
  const head = sec.querySelector(".p-head");
  if (!head) return;
  let start = null;
  head.addEventListener("pointerdown", (e) => {
    if (e.button !== 0 || e.target.closest("button,select,input,a,.p-resize")) return;
    if (ctx.zablokowane && ctx.zablokowane()) return;   // wąskie okno (< 900 px): układ pionowy, drag zablokowany
    const id = sec.dataset.panel, o = ctx.okno(id);
    if (!o) return;
    e.preventDefault();
    try { head.setPointerCapture(e.pointerId); } catch (_) {}
    start = { px: e.clientX, py: e.clientY, x: o.x, y: o.y };
    sec.classList.add("moving");
    ctx.onDragStart(id);
  });
  head.addEventListener("pointermove", (e) => {
    if (!start) return;
    e.preventDefault();
    const id = sec.dataset.panel, o = ctx.okno(id);
    const x = Math.max(0, Math.min(start.x + e.clientX - start.px, Math.max(0, ctx.szerokosc() - o.w)));
    const y = Math.max(0, start.y + e.clientY - start.py);
    if (x === o.x && y === o.y) return;
    ctx.setOkno(id, { ...o, x, y });                 // tylko pozycja — bez rozsuwania w trakcie dragu
  });
  const koncz = (e) => {
    if (!start) return;
    start = null;
    sec.classList.remove("moving");
    try { head.releasePointerCapture(e.pointerId); } catch (_) {}
    ctx.onEnd(sec.dataset.panel, "move");
  };
  head.addEventListener("pointerup", koncz);
  head.addEventListener("pointercancel", koncz);
}

/* zmiana rozmiaru: uchwyt w prawym dolnym rogu, płynnie co do piksela; dwuklik = rozmiar domyślny */
export function podlaczRozmiar(sec, ctx) {
  const uchwyt = sec.querySelector(".p-resize");
  if (!uchwyt) return;
  uchwyt.title = "Przeciągnij, aby zmienić rozmiar panelu (dwuklik = rozmiar domyślny)";
  let start = null;
  uchwyt.addEventListener("pointerdown", (e) => {
    if (e.button !== 0) return;
    if (ctx.zablokowane && ctx.zablokowane()) return;   // wąskie okno (< 900 px): rozmiar zablokowany
    e.preventDefault(); e.stopPropagation();
    try { uchwyt.setPointerCapture(e.pointerId); } catch (_) {}
    const o = ctx.okno(sec.dataset.panel);
    if (!o) return;                                   // wąskie okno (< 900 px): rozmiar zablokowany
    start = { px: e.clientX, py: e.clientY, w: o.w, h: o.h };
    sec.classList.add("resizing");
    ctx.onDragStart(sec.dataset.panel);
  });
  uchwyt.addEventListener("pointermove", (e) => {
    if (!start) return;
    e.preventDefault();
    const id = sec.dataset.panel, o = ctx.okno(id);
    const w = Math.max(MIN_OKNO_W, Math.min(start.w + (e.clientX - start.px), Math.max(MIN_OKNO_W, ctx.szerokosc() - o.x)));
    const h = Math.max(MIN_OKNO_H, start.h + (e.clientY - start.py));
    if (w === o.w && h === o.h) return;
    ctx.setOkno(id, { ...o, w, h });
  });
  const koncz = (e) => {
    if (!start) return;
    start = null;
    sec.classList.remove("resizing");
    try { uchwyt.releasePointerCapture(e.pointerId); } catch (_) {}
    ctx.onEnd(sec.dataset.panel, "resize");
  };
  uchwyt.addEventListener("pointerup", koncz);
  uchwyt.addEventListener("pointercancel", koncz);
  uchwyt.addEventListener("dblclick", (e) => {
    e.preventDefault(); e.stopPropagation();
    if (ctx.zablokowane && ctx.zablokowane()) return;
    ctx.onDomylny(sec.dataset.panel);
  });
}
