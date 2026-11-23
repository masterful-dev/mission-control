/* CENTRUM MISJI — free panel layout (SPEC §13): ES module with pure geometry functions
   + pointer handling (pointer events, setPointerCapture — NOT HTML5 DnD). Zero libraries.
   Panel = {id, x, y, w, h} in PIXELS (no grid, no snapping). */
export const MIN_OKNO_W = 220, MIN_OKNO_H = 80, OKNO_GAP = 12;
export const OKNO_H = { gpu: 400, llm: 400, system: 340, models: 480, energia: 260 };
export const PANELS = ["gpu", "llm", "system", "models", "energia"];

/* two windows intersect (edges alone do NOT intersect) */
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

/* rozsun(panele, ruszanyId) — PURE function: returns NEW windows; windows intersecting the moved
   one are pushed DOWN by the minimal value (cascading, until nothing intersects).
   The rest is untouched (no pull-up). The MOVED panel is "highest"
   in order (always wins the y tie) — that is why it never moves itself. */
export function rozsun(panele, ruszanyId) {
  // The moved panel stays EXACTLY where it was dropped. Every panel that overlaps it,
  // slides just under it; cascading further (under already-moved panels). Conflict-free panels — untouched.
  const out = {};
  for (const id of Object.keys(panele || {})) out[id] = { ...panele[id] };
  if (!out[ruszanyId]) return out;
  const stale = [ruszanyId];                        // panels whose position is already set
  for (let iter = 0; iter < 1000; iter++) {
    let ruszone = false;
    for (const id of Object.keys(out).sort((a, b) => out[a].y - out[b].y || (a < b ? -1 : 1))) {
      if (stale.includes(id)) continue;
      const p = out[id];
      for (let k = 0; k < stale.length; k++) {     // az p nie przecina ZADNEGO ustalonego panelu
        const o = out[stale[k]];
        if (przecina(p, o)) { p.y = o.y + o.h + OKNO_GAP; ruszone = true; k = -1; }
      }
      if (ruszone) { stale.push(id); break; }    // determined; from scratch — the next may overlap it
    }
    if (!ruszone) break;
  }
  return out;
}

/* naprawUklad(panele) — PURE function: removes ALL intersections (every window pushed DOWN,
   cascading, until nothing intersects). Used after loading the layout from the server (fit to
   the current window width) and after rozsun — x positions/sizes are never changed. */
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

/* default layout §13 (12 px gap): row 1 GPU (all cards, 1/2) | Model LLM (1/2), h 400;
   row 2 System (1/2, h 340) | Models (1/2, h 480); below System Energy (1/2, h 260). */
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

/* narrow window (< 900 px): panels one under another at full width — saved positions untouched */
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

/* container height = lowest edge + 12 px (page scrolls vertically) */
export function wysokoscKontenera(panele) {
  let max = 0;
  for (const o of Object.values(panele || {})) max = Math.max(max, (o.y || 0) + (o.h || 0));
  return max + OKNO_GAP;
}

/* ---------- pointer handling ----------
   ctx = { szerokosc(), okno(id), setOkno(id, okno), onDragStart(id), onEnd(id, typ), onDomylny(id) }
   Dragging: grip on the WHOLE header (outside buttons) — the panel fluidly follows the pointer.
   Spread (no overlap) happens only on drop / resize. */
export function podlaczPrzenoszenie(sec, ctx) {
  const head = sec.querySelector(".p-head");
  if (!head) return;
  let start = null;
  head.addEventListener("pointerdown", (e) => {
    if (e.button !== 0 || e.target.closest("button,select,input,a,.p-resize")) return;
    if (ctx.zablokowane && ctx.zablokowane()) return;   // narrow window (< 900 px): vertical layout, drag blocked
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

/* resize: handle in the bottom-right corner, fluidly per pixel; double-click = default size */
export function podlaczRozmiar(sec, ctx) {
  const uchwyt = sec.querySelector(".p-resize");
  if (!uchwyt) return;
  uchwyt.title = "Przeciągnij, aby zmienić rozmiar panelu (dwuklik = rozmiar domyślny)";
  let start = null;
  uchwyt.addEventListener("pointerdown", (e) => {
    if (e.button !== 0) return;
    if (ctx.zablokowane && ctx.zablokowane()) return;   // narrow window (< 900 px): resize blocked
    e.preventDefault(); e.stopPropagation();
    try { uchwyt.setPointerCapture(e.pointerId); } catch (_) {}
    const o = ctx.okno(sec.dataset.panel);
    if (!o) return;                                   // narrow window (< 900 px): resize blocked
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
