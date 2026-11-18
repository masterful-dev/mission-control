// Uklad okna: gorny pasek, panele, scena, tryb waski.
// Panele: uklad domyslny liczy sie sam (kolumna lewa, prawa, dol). Po pierwszym przesunieciu albo rozciagnieciu kazdy panel
// ma wlasny prostokat zapisany jako ulamki okna: zmiana rozmiaru okna skaluje uklad, a zmiana od razu trafia do ustawien.
#include "aplikacja.h"
#include "sim.h"
#include "api.h"
#include "scena.h"
#include "ui.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <ctime>
using namespace ui;
std::string wskazowka_(const char* s);
namespace {
// figura twarzy wzgledem punktu zaczepienia, w jednostkach skali twarzy: od kresek nad czolem (-0,38) do konca szyi (+0,40); zmierzone na zrzutach
const float FIG_WYS = .80f, FIG_SRODEK = .01f;
bool otworzMenuKarty = false;
constexpr ImGuiWindowFlags FL = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing;
float wl, wr, navH = 0; bool waski = false; ImRect pole;   // pole: miejsce na panele pod gornym paskiem
const std::vector<std::string> LEWE = {"gpu", "cpu", "ram", "disks"}, PRAWE = {"llm", "models"}, DOLNE = {"energy"};
bool widac(const std::string& id) { return !A.ukryte[id]; }

// flaga NoBringToFrontOnFocus wstawia okno na spod stosu: tylko scena ma lezec pod reszta
void okno(const char* n, float x, float y, float w, float h, ImGuiWindowFlags f = FL & ~ImGuiWindowFlags_NoBringToFrontOnFocus) { ImGui::SetNextWindowPos({x, y}); ImGui::SetNextWindowSize({w, h}); ImGui::Begin(n, nullptr, f); }
ImRect prostPx(const std::string& id) { auto it = A.okna.find(id); Prost q = it == A.okna.end() ? Prost{.3f, .2f, .3f, .4f} : it->second; float pw = pole.GetWidth(), ph = pole.GetHeight(), w = std::max(q.w * pw, px(170)), h = std::max(q.h * ph, px(60)); ImVec2 a{pole.Min.x + q.x * pw, pole.Min.y + q.y * ph}; return {a, {a.x + w, a.y + h}}; }
void doUlamkow(const std::string& id, const ImRect& r, bool bezWys = false) { float pw = std::max(1.f, pole.GetWidth()), ph = std::max(1.f, pole.GetHeight()); Prost& q = A.okna[id]; q.x = (r.Min.x - pole.Min.x) / pw; q.y = (r.Min.y - pole.Min.y) / ph; q.w = r.GetWidth() / pw; if (!bezWys) q.h = r.GetHeight() / ph; }
// uklad liczony sam staje sie ukladem uzytkownika: kazdy panel dostaje prostokat, w ktorym wlasnie stoi
void zamroz() { for (auto& [id, p] : A.P) { if (!p.widac && A.okna.count(id)) continue; ImRect r = p.r; if (A.zwiniete[id]) r.Max.y = r.Min.y + std::max(p.natH, px(120)); if (id == "llm" && A.s.llm.empty() && !A.zwiniete[id]) r.Max.y += px(40); if (r.GetWidth() < 4) r = {{pole.Min.x + px(40), pole.Min.y + px(40)}, {pole.Min.x + px(380), pole.Min.y + px(300)}}; doUlamkow(id, r); } A.swobodny = true; }

void panelOkno(const std::string& id, float x, float y, float w, float h, bool duzy = false) {
  Panel& p = A.P[id]; bool zw = A.zwiniete[id] && !duzy; float R = px(M.r); bool acts = id == "models"; p.naglowekH = px(acts ? 30 : 27); if (zw) h = p.naglowekH;
  std::string nazwa = (duzy ? "##d_" : "##p_") + id; auto& io = ImGui::GetIO();
  if (duzy) ImGui::BeginChild(nazwa.c_str(), {w, h}, 0, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar); else okno(nazwa.c_str(), x, y, w, h);
  ImDrawList* d = ImGui::GetWindowDrawList(); ImVec2 a = ImGui::GetWindowPos(), b{a.x + w, a.y + h}; float hH = p.naglowekH; bool trzymany = A.chwyt.id == id && A.chwyt.aktywny, nadOknem = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
  if (!duzy) { p.r = {a, b}; p.widac = true; for (int i = 3; i > 0; i--) d->AddRect({a.x - px(i), a.y - px(i) + px(2)}, {b.x + px(i), b.y + px(i) + px(2)}, IM_COL32(0, 0, 0, trzymany ? 34 : 16), R + px(i), px(1.5f)); }
  d->AddRectFilled(a, b, duzy ? u(M.panel) : u(przezr(M.panel, trzymany ? std::min(1.f, M.pa + .12f) : M.pa)), R); if (!zw) d->AddRectFilled(a, {b.x, a.y + hH}, u(M.acc, .035f), R, ImDrawFlags_RoundTop);
  d->AddRect(a, b, u(trzymany ? M.acc : nadOknem && !duzy ? mix(M.acc, .35f, Kolor{M.line.r, M.line.g, M.line.b, 1}) : M.line, trzymany ? .9f : nadOknem && !duzy ? .5f : 1.f), R, px(1));
  float ax = 0; rysujAkcjePanelu(id, d, b.x - px(9), a.y + px(6), ax);
  std::string sub = podpisPanelu(id); ImFont* fs = id == "energy" ? fNum(W5) : fUi(W5); float sw = szer(fs, px(11), sub), xc = a.x + px(9), lh = px(14.85f), yc = a.y + (hH - lh) / 2;
  ImRect hr(a, {b.x - ax - (ax > 0 ? px(9) : 0), a.y + hH}), strz(a, {a.x + px(24), a.y + hH}); bool hh = hover(hr), hs = hover(strz);
  tekstL(d, xc, yc, lh, fUi(W5), px(10), u(hs ? M.acc : M.dim), zw ? "▸" : "▾"); xc += px(13); float dost = hr.Max.x - xc - px(9) - (sw > 0 ? sw + px(13) : 0); std::string tt = skroc(fUi(W7), px(11), wielkie(t(p.tytul)), std::max(px(20), dost), px(.88f)); tekstL(d, xc, yc, lh, fUi(W7), px(11), u(hh ? M.acc : M.txt), tt, px(.88f));
  if (sw > 0 && sw < hr.Max.x - xc - px(30)) tekstL(d, hr.Max.x - px(ax > 0 ? 0 : 9) - sw, yc, lh, fs, px(11), u(M.mut), sub);
  if (nadOknem) { A.pod.panel = id; if (hh) A.pod.el = "naglowek"; }
  if (!duzy) {
    if (hs) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); tipJuz(zw ? L("Unfold the panel", "Rozwiń panel") : L("Fold the panel", "Zwiń panel"), L("A folded panel keeps only its title bar.", "Zwinięty panel zostawia sam pasek tytułu.")); if (klik(strz)) A.zwin(id); }
    else if (hh) { tipJuz(t(p.tytul), std::string(sub.empty() ? "" : sub + "\n-\n") + (waski ? "" : std::string(L("Drag the title: move the panel", "Przeciągnij tytuł: przesuń panel")) + "\n" + L("Drag an edge or the corner: resize", "Przeciągnij krawędź albo róg: zmień rozmiar") + "\n") + L("Double-click: enlarge", "Dwuklik: powiększ") + "\n" + L("Right-click: panel menu", "Prawy przycisk: menu panelu") + (waski ? "" : std::string("\n-\n") + L("Position and size are saved at once.", "Położenie i rozmiar zapisują się od razu."))); if (ImGui::IsMouseDoubleClicked(0)) { A.chwyt = {}; A.pokazDialog(Dialog::Duzy, id); } }
    // chwyt: krawedzie i rogi zmieniaja rozmiar, tytul przesuwa
    if (!waski && A.chwyt.id.empty() && nadOknem && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) { ImVec2 m = io.MousePos; float e = px(5), c = px(13); int tr = 0;
      if (m.x >= a.x && m.x <= b.x && m.y >= a.y && m.y <= b.y) { if (m.x < a.x + e) tr |= CH_L; if (m.x > b.x - e) tr |= CH_P; if (m.y < a.y + px(3)) tr |= CH_G; if (m.y > b.y - e) tr |= CH_D; if (m.x > b.x - c && m.y > b.y - c) tr = CH_P | CH_D; if (m.x < a.x + c && m.y > b.y - c) tr = CH_L | CH_D; if (zw) tr &= ~(CH_G | CH_D); }
      if (tr) { bool l = tr & CH_L, r = tr & CH_P, g = tr & CH_G, dl = tr & CH_D; ImGui::SetMouseCursor((l && g) || (r && dl) ? ImGuiMouseCursor_ResizeNWSE : (r && g) || (l && dl) ? ImGuiMouseCursor_ResizeNESW : l || r ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS); if (ImGui::IsMouseClicked(0)) A.chwyt = {id, tr, true, m, {a, b}}; }
      else if (hh && !hs && ImGui::IsMouseClicked(0)) A.chwyt = {id, CH_RUCH, false, m, {a, b}}; }
    if (!waski && !zw) { bool rog = nadOknem && io.MousePos.x > b.x - px(13) && io.MousePos.y > b.y - px(13); ImU32 kc = u(rog || (trzymany && A.chwyt.tryb == (CH_P | CH_D)) ? M.acc : M.dim, rog ? 1.f : .7f); for (int i = 1; i <= 3; i++) d->AddLine({b.x - px(2.5f + i * 3.f), b.y - px(3)}, {b.x - px(3), b.y - px(2.5f + i * 3.f)}, kc, px(1)); }
  }
  float tresc = 0;
  if (!zw) { float bw = w - px(9), bh = std::max(px(1), h - hH - px(5)); ImGui::SetCursorScreenPos({a.x + px(4), a.y + hH}); ImGui::BeginChild("##b", {bw, bh}, 0, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav); ImGui::SetCursorPos({px(5), 0}); rysujCialoPanelu(id, w - px(21), (A.swobodny && !waski) || duzy ? bh - px(8) : 0); tresc = ImGui::GetCursorPosY(); ImGui::SetCursorPosX(0); ImGui::Dummy({1, px(8)}); ImGui::EndChild(); }
  if (!zw) p.natH = hH + tresc + px(8) + px(6);
  if (duzy) ImGui::EndChild(); else ImGui::End();
}
// uklad domyslny: kolumna paneli, wysokosci z tresci, panele "grow" biora reszte
void kolumna(const std::vector<std::string>& wsz, float x, float y0, float w, float hA) {
  std::vector<std::string> ids; for (auto& i : wsz) if (widac(i)) ids.push_back(i); int n = ids.size(); if (!n) return; float gap = px(7), wolne = hA - gap * (n - 1); std::vector<float> h(n); int G = 0;
  for (int i = 0; i < n; i++) { Panel& p = A.P[ids[i]]; bool zw = A.zwiniete[ids[i]], g = p.grow && !zw; if (zw) h[i] = std::max(p.naglowekH, px(27)); else if (g) { h[i] = -1; G++; } else h[i] = std::max(p.natH, px(27)); if (h[i] > 0) wolne -= h[i]; }
  if (G) { float each = wolne / G; for (int i = 0; i < n; i++) if (h[i] < 0) { float mn = std::min(A.P[ids[i]].natH, px(110)); h[i] = std::max(each, mn); wolne -= h[i]; } float nad = -wolne; if (nad > 0) { float sum = 0; for (int i = 0; i < n; i++) if (!A.zwiniete[ids[i]]) sum += h[i]; for (int i = 0; i < n; i++) if (!A.zwiniete[ids[i]]) h[i] = std::max(px(54), h[i] - nad * h[i] / std::max(sum, 1.f)); } }
  else if (wolne < 0) { float sum = 0; for (int i = 0; i < n; i++) if (!A.zwiniete[ids[i]]) sum += h[i]; for (int i = 0; i < n; i++) if (!A.zwiniete[ids[i]]) h[i] = std::max(px(54), h[i] + wolne * h[i] / std::max(sum, 1.f)); }
  float y = y0; for (int i = 0; i < n; i++) { panelOkno(ids[i], x, y, w, h[i]); y += h[i] + gap; }
}
void dol(float x, float w, float dolY) {
  std::vector<std::string> ids; for (auto& i : DOLNE) if (widac(i)) ids.push_back(i); int n = ids.size(); if (!n) return; float gap = px(7), y = dolY, mx = ui::H * S * .5f, sum = 0; for (auto& id : ids) sum += (A.zwiniete[id] ? px(27) : A.P[id].natH) + gap; float sk = sum > mx ? mx / sum : 1;
  for (int i = n - 1; i >= 0; i--) { auto& id = ids[i]; float h = (A.zwiniete[id] ? std::max(A.P[id].naglowekH, px(27)) : std::max(A.P[id].natH, px(27))) * (A.zwiniete[id] ? 1 : sk); y -= h; panelOkno(id, x, y, w, h); y -= gap; }
}
// przesuwanie i rozciaganie: liczone przed rysowaniem paneli, zeby panel szedl za kursorem bez opoznienia
void obslugaChwytu() {
  auto& c = A.chwyt; if (c.id.empty()) return; auto& io = ImGui::GetIO(); if (!io.MouseDown[0]) { if (c.aktywny) A.zapisz(); c = {}; return; }
  ImVec2 dm{io.MousePos.x - c.m0.x, io.MousePos.y - c.m0.y}; if (!c.aktywny) { if (std::hypot(dm.x, dm.y) < px(5)) return; c.aktywny = true; }
  if (!A.swobodny) zamroz(); bool zw = A.zwiniete[c.id]; ImRect r = c.r0; float minW = px(190), minH = px(72), gap = px(7), mar = px(8), thr = A.o.przyciaganie && !io.KeyAlt ? px(7) : 0; bool ruch = c.tryb & CH_RUCH;
  if (ruch) r.Translate(dm); else { if (c.tryb & CH_L) r.Min.x = std::min(r.Min.x + dm.x, r.Max.x - minW); if (c.tryb & CH_P) r.Max.x = std::max(r.Max.x + dm.x, r.Min.x + minW); if (c.tryb & CH_G) r.Min.y = std::min(r.Min.y + dm.y, r.Max.y - minH); if (c.tryb & CH_D) r.Max.y = std::max(r.Max.y + dm.y, r.Min.y + minH); }
  // przyciaganie do brzegow okna i do sasiednich paneli (Alt wylacza)
  ImDrawList* fg = ImGui::GetForegroundDrawList(); float bx = 0, by = 0, najx = thr, najy = thr, lx = -1, ly = -1;
  auto probaX = [&](float nasz, float cel_) { float q = cel_ - nasz; if (std::fabs(q) < najx) { najx = std::fabs(q); bx = q; lx = cel_; } }; auto probaY = [&](float nasz, float cel_) { float q = cel_ - nasz; if (std::fabs(q) < najy) { najy = std::fabs(q); by = q; ly = cel_; } };
  bool L_ = ruch || (c.tryb & CH_L), P_ = ruch || (c.tryb & CH_P), G_ = ruch || (c.tryb & CH_G), D_ = ruch || (c.tryb & CH_D);
  if (thr > 0) { if (L_) probaX(r.Min.x, pole.Min.x + mar); if (P_) probaX(r.Max.x, pole.Max.x - mar); if (G_) probaY(r.Min.y, pole.Min.y + mar); if (D_ && !zw) probaY(r.Max.y, pole.Max.y - mar);
    for (auto& [id, p] : A.P) { if (id == c.id || !p.widac) continue; const ImRect& o = p.r; bool blY = r.Min.y < o.Max.y + px(40) && r.Max.y > o.Min.y - px(40), blX = r.Min.x < o.Max.x + px(40) && r.Max.x > o.Min.x - px(40);
      if (blY) { if (L_) { probaX(r.Min.x, o.Max.x + gap); probaX(r.Min.x, o.Min.x); } if (P_) { probaX(r.Max.x, o.Min.x - gap); probaX(r.Max.x, o.Max.x); } } else { if (L_) probaX(r.Min.x, o.Min.x); if (P_) probaX(r.Max.x, o.Max.x); }
      if (blX) { if (G_) { probaY(r.Min.y, o.Max.y + gap); probaY(r.Min.y, o.Min.y); } if (D_ && !zw) { probaY(r.Max.y, o.Min.y - gap); probaY(r.Max.y, o.Max.y); } } else { if (G_) probaY(r.Min.y, o.Min.y); if (D_ && !zw) probaY(r.Max.y, o.Max.y); } } }
  if (ruch) { r.Translate({bx, by}); } else { if (bx != 0) { if ((c.tryb & CH_L) && std::fabs(lx - r.Min.x) < thr + .5f) r.Min.x = lx; else if (c.tryb & CH_P) r.Max.x = lx; } if (by != 0) { if ((c.tryb & CH_G) && std::fabs(ly - r.Min.y) < thr + .5f) r.Min.y = ly; else if (c.tryb & CH_D) r.Max.y = ly; } }
  if (lx >= 0) fg->AddLine({lx, pole.Min.y}, {lx, pole.Max.y}, u(M.acc, .35f), px(1)); if (ly >= 0) fg->AddLine({pole.Min.x, ly}, {pole.Max.x, ly}, u(M.acc, .35f), px(1));
  // panel nie wychodzi poza okno
  if (ruch) { float w = r.GetWidth(), h = r.GetHeight(); r.Min.x = std::clamp(r.Min.x, pole.Min.x - w + px(80), pole.Max.x - px(80)); r.Min.y = std::clamp(r.Min.y, pole.Min.y, pole.Max.y - px(27)); r.Max = {r.Min.x + w, r.Min.y + h}; }
  else { r.Min.x = std::max(r.Min.x, pole.Min.x); r.Max.x = std::min(r.Max.x, pole.Max.x); r.Min.y = std::max(r.Min.y, pole.Min.y); r.Max.y = std::min(r.Max.y, std::max(pole.Max.y, r.Min.y + minH)); if (r.GetWidth() < minW) { if (c.tryb & CH_L) r.Min.x = r.Max.x - minW; else r.Max.x = r.Min.x + minW; } }
  doUlamkow(c.id, r, zw);
  ImGui::SetMouseCursor(ruch ? ImGuiMouseCursor_ResizeAll : ((c.tryb & CH_L) && (c.tryb & CH_G)) || ((c.tryb & CH_P) && (c.tryb & CH_D)) ? ImGuiMouseCursor_ResizeNWSE : ((c.tryb & CH_P) && (c.tryb & CH_G)) || ((c.tryb & CH_L) && (c.tryb & CH_D)) ? ImGuiMouseCursor_ResizeNESW : (c.tryb & (CH_L | CH_P)) ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
  { char bb[48]; snprintf(bb, 48, "%d × %d", (int)std::lround(r.GetWidth() / S), (int)std::lround((zw ? A.P[c.id].naglowekH : r.GetHeight()) / S)); float w = szer(fNum(W6), px(10.5f), bb) + px(12); ImVec2 m = io.MousePos; fg->AddRectFilled({m.x + px(14), m.y + px(16)}, {m.x + px(14) + w, m.y + px(34)}, u(M.panel, .95f), px(4)); fg->AddRect({m.x + px(14), m.y + px(16)}, {m.x + px(14) + w, m.y + px(34)}, u(M.acc, .7f), px(4)); tekst(fg, m.x + px(20), m.y + px(19), fNum(W6), px(10.5f), u(M.txt), bb); }
}
void gora(float Wp) {
  float h = px(46); okno("##gora", 0, 0, Wp, h); auto d = ImGui::GetWindowDrawList(); d->AddRectFilled({0, 0}, {Wp, h}, u(przezr(M.panel, .86f))); d->AddLine({0, h - px(.5f)}, {Wp, h - px(.5f)}, u(M.line), px(1));
  if (ImGui::IsWindowHovered()) { A.pod.gora = true; A.pod.el = "gora"; } float W_ = ui::W, x = Wp - px(10), cy = h / 2, sp = W_ <= 1400 ? .1f : .2f; std::string br = t("brand"); float bw = szer(fNum(W7), px(13), br, px(13 * sp)); auto& e = A.s.energia; int ng = 0; for (auto& g : A.s.gpus) if (g.vram_total_mb >= 8000) ng++; ng = std::min(ng, 2);
  std::string zwTip = std::string("\n") + t("free_txt");
  Btn wf[2] = {{std::string(t("free_gpu")) + " 0", Ostrz, false, false, std::string(t("free_gpu")) + " 0" + zwTip}, {std::string(t("free_gpu")) + " 1", Ostrz, false, false, std::string(t("free_gpu")) + " 1" + zwTip}}, uk{"⟲ " + std::string(t("arrange")), 0, false, false, std::string(t("arrange")) + "\n" + L("Puts every panel back in the default layout and forgets your own positions and sizes.", "Ustawia wszystkie panele w układzie domyślnym i zapomina własne położenia i rozmiary.")}, ust{"⚙", 0, false, true, std::string(t("settings")) + "\n" + L("Appearance, language, scene, layout, startup.", "Wygląd, język, scena, układ, start z systemem.")};
  time_t tt = time(nullptr); tm lt = *localtime(&tt); char zeg[16]; strftime(zeg, 16, "%H:%M:%S", &lt); std::string moc = n0(e.teraz_w_gniazdko) + " W"; float cw = szer(fNum(W5), px(13), "00:00:00"), mw = szer(fNum(W6), px(14), moc), lw = szer(fUi(W5), px(12), t("power")), g = px(8);
  float pelne = px(10) + px(28) + bw + px(12) + px(140) + lw + px(6) + mw + g + btnRozm(wf[0]).x * ng + g * ng + btnRozm(uk).x + g + cw + g + px(9) + g + btnRozm(ust).x + px(10); int poz = 0; float brak = pelne - Wp; if (brak > 0) { poz = 1; uk.t = "⟲"; brak -= szer(fUi(W6), px(11), t("arrange")) + px(3); } if (brak > 0) { poz = 2; wf[0].t = "GPU 0"; wf[1].t = "GPU 1"; brak -= (btnRozm({std::string(t("free_gpu")) + " 0", Ostrz}).x - btnRozm(wf[0]).x) * ng; } if (brak > 0) { poz = 3; brak -= btnRozm(wf[0]).x * ng + g * ng; } if (brak > 0) { poz = 4; brak -= lw + px(6); } if (brak > 0) { poz = 5; brak -= bw + px(8); } if (brak > 0) poz = 6;
  auto zPrawej = [&](const Btn& b, const std::function<void()>& f, int gpu = -1) { ImVec2 z = btnRozm(b); x -= z.x; if (btn(d, x, cy - z.y / 2, b)) f(); if (hover({{x, 0}, {x + z.x, h}})) { if (gpu >= 0) { A.pod.gpu = gpu; A.pod.el = "gpu"; } else A.pod.el = "przycisk"; } x -= g; };
  zPrawej(ust, [] { A.pokazDialog(Dialog::Ustawienia); });
  { float kx = x - px(4.5f); bool zywe = A.prawdziwe && Api::polaczone(); kropka(d, kx, cy, zywe ? "on" : A.wymusSim || !A.prawdziwe ? "starting" : "failed"); ImRect r({kx - px(8), 0}, {kx + px(8), h}); if (hover(r)) { A.pod.el = "zrodlo"; A.pod.wartosc = A.prawdziwe ? Api::adres() : t("data_sim"); tipJuz(L("Data source", "Źródło danych"), std::string(zywe ? L("Live data from the Mission Center service.", "Prawdziwe dane z usługi Centrum Misji.") : A.prawdziwe ? L("The service stopped answering. Showing the last known state.", "Usługa przestała odpowiadać. Widać ostatni znany stan.") : L("Simulation: the numbers are made up. The service does not answer or simulation was chosen.", "Symulacja: liczby są zmyślone. Usługa nie odpowiada albo wybrano symulację.")) + "\n" + L("Address", "Adres") + "\t" + (A.wymusSim ? "—" : Api::adres()) + "\n" + wskazowka_(L("Right-click: switch the data source", "Prawy przycisk: zmień źródło danych"))); } x -= px(9) + g; }
  if (poz < 6) { x -= cw; tekstL(d, x, 0, h, fNum(W5), px(13), u(M.mut), zeg); ImRect r({x, 0}, {x + cw, h}); if (hover(r)) { char db[64]; const char* DE[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"}; const char* DP[] = {"niedziela", "poniedziałek", "wtorek", "środa", "czwartek", "piątek", "sobota"}; snprintf(db, 64, "%s, %02d.%02d.%04d", A.jezyk ? DP[lt.tm_wday] : DE[lt.tm_wday], lt.tm_mday, lt.tm_mon + 1, lt.tm_year + 1900); A.pod.el = "zegar"; A.pod.wartosc = std::string(db) + " " + zeg; tipJuz(zeg, std::string(db) + "\n" + L("Program running for", "Program działa od") + "\t" + czasPracy(A.teraz)); } x -= g; }
  zPrawej(uk, [] { if (A.swobodny) A.potwierdz(L("Restore the default layout? Your own positions and sizes of the panels will be forgotten.", "Przywrócić układ domyślny? Własne położenia i rozmiary paneli zostaną zapomniane."), [] { A.resetUkladu(); }); else A.resetUkladu(); });
  if (poz < 3) for (int i = ng - 1; i >= 0; i--) zPrawej(wf[i], [i] { A.pokazDialog(Dialog::Zwolnij, "", i); }, i);
  x -= mw; tekstL(d, x, 0, h, fNum(W6), px(14), u(M.txt), moc); { float x0 = poz < 4 ? x - px(6) - lw : x; ImRect r({x0, 0}, {x + mw, h}); if (hover(r)) { A.pod.panel = "energy"; A.pod.el = "moc"; A.pod.wartosc = moc; tipJuz(std::string(t("power")) + " · " + moc, std::string(L("Draw from the wall right now.", "Pobór z gniazdka w tej chwili.")) + "\n-\n" + t("e_dc") + "\t" + n0(e.teraz_w) + " W\n" + L("Main power supply", "Zasilacz główny") + "\t" + n0(e.psu_glowny_w) + " W\n" + L("Compute cards", "Karty obliczeniowe") + "\t" + n0(e.psu_gpu_w) + " W\n" + t("e_dev") + "\t" + n1(e.urzadzenia_w) + " W\n-\n" + t("e_today") + "\t" + n2(e.dzis_kwh) + " kWh\n" + t("e_month") + "\t" + n2(e.miesiac_kwh) + " kWh"); } } x -= px(6); if (poz < 4) { x -= lw; tekstL(d, x, 0, h, fUi(W5), px(12), u(M.mut), t("power")); x -= g; }
  float xl = px(10); ImVec2 c{xl + px(10), cy}; d->AddCircle(c, px(7.5f), u(M.acc), 32, px(1.25f)); d->AddCircle(c, px(3.75f), u(M.acc), 24, px(1.25f)); d->AddCircleFilled(c, px(1), u(M.acc)); float xb = xl; xl += px(28); if (poz < 5) { tekstL(d, xl, 0, h, fNum(W7), px(13), u(M.acc), br, px(13 * sp)); xl += bw + px(12); }
  { ImRect r({xb, 0}, {xl - px(6), h}); if (hover(r)) { A.pod.el = "marka"; tipJuz(t("brand"), std::string(L("Version", "Wersja")) + "\t2.1.0\n" + L("Interface size", "Rozmiar interfejsu") + "\t" + n0(A.zoom * 100) + " %\n-\n" + L("Ctrl + mouse wheel: interface size", "Ctrl + kółko myszy: rozmiar interfejsu") + "\nF: " + L("scene only", "sama scena") + "\n" + L("Right-click: program menu", "Prawy przycisk: menu programu")); } }
  const Model* m = nullptr; for (auto& q : A.s.models) if (q.status != "stopped") { m = &q; break; } if (m) { float mxw = x - xl - px(16); if (mxw > px(30)) { kropka(d, xl + px(4.5f), cy, m->status); std::string s = skroc(fUi(W7), px(12), A.o.pelneNazwy || m->krotka.empty() ? m->name : m->krotka, mxw); tekstL(d, xl + px(14), 0, h, fUi(W7), px(12), u(M.txt), s); if (hover({{xl, 0}, {xl + px(14) + szer(fUi(W7), px(12), s), h}})) { A.pod.model = m->id; A.pod.el = "model"; A.pod.wartosc = m->name; tipJuz(s, opisModelu(*m)); } } }
  ImGui::End();
}
bool zakryta = false;   // waskie okno: panel zaslania scene, napisy sceny znikaja
void pasekDemo(float cx, float maxW) {
  if (A.prawdziwe || A.demoOff || (!A.wymusSim && A.teraz < 1.5)) return; struct B_ { const char* k; const char* l; } T[] = {{"auto", "d_auto"}, {"load", "d_load"}, {"prompt", "d_prompt"}, {"hard", "d_hard"}, {"slow", "d_slow"}, {"heat", "d_heat"}, {"err", "d_err"}, {"unload", "d_unload"}, {"hide", "d_hide"}};
  maxW = std::max(maxW, px(120)); float gap = px(4), pad = px(6), lw = szer(fUi(W5), px(10), wielkie(t("demo")), px(.6f)) + px(4), bh = btnRozm({"x", 0, true}).y, x = pad + lw + gap, y = px(4), mx = 0; std::vector<ImVec2> pos; std::vector<size_t> wiersz{0}; std::vector<float> koniec; for (auto& q : T) { float w = btnRozm({t(q.l), 0, true}).x; if (x + w + pad > maxW) { koniec.push_back(x - gap); wiersz.push_back(pos.size()); x = pad; y += bh + gap; } pos.push_back({x, y}); x += w + gap; mx = std::max(mx, x); } koniec.push_back(x - gap); float w = std::min(maxW, mx - gap + pad), h = y + bh + px(4), x0 = cx - w / 2, y0 = px(54); float lbl = (w - pad - koniec[0]) / 2; for (size_t r = 0; r < wiersz.size(); r++) { float sz = r ? (w - pad - koniec[r] + pad) / 2 - pad / 2 : lbl; size_t e = r + 1 < wiersz.size() ? wiersz[r + 1] : pos.size(); for (size_t i = wiersz[r]; i < e; i++) pos[i].x += sz; }
  okno("##demo", x0, y0, w, h); auto d = ImGui::GetWindowDrawList(); ImVec2 a{x0, y0}, b{x0 + w, y0 + h}; d->AddRectFilled(a, b, u(przezr(M.panel, .88f)), px(M.r)); auto kres = [&](ImVec2 p, ImVec2 q) { float L_ = std::hypot(q.x - p.x, q.y - p.y), st = px(5); for (float s = 0; s < L_; s += st * 2) { float e = std::min(L_, s + st); d->AddLine({p.x + (q.x - p.x) * s / L_, p.y + (q.y - p.y) * s / L_}, {p.x + (q.x - p.x) * e / L_, p.y + (q.y - p.y) * e / L_}, u(przezr(M.warn, .5f)), px(1)); } }; kres(a, {b.x, a.y}); kres({b.x, a.y}, b); kres(b, {a.x, b.y}); kres({a.x, b.y}, a);
  tekst(d, x0 + pad + lbl, y0 + px(7), fUi(W5), px(10), u(M.warn), wielkie(t("demo")), px(.6f)); int i = 0; for (auto& q : T) { std::string k = q.k; Btn bt{t(q.l), k == "auto" && Sim::autoPlan ? Wlaczony : Zwykly, true, false, std::string(t(q.l)) + "\n" + L("Simulation control: no real model is touched.", "Sterowanie symulacją: żaden prawdziwy model nie jest ruszany.")}; if (btn(d, x0 + pos[i].x, y0 + pos[i].y, bt)) { if (k == "auto") Sim::autoPlan = !Sim::autoPlan; else if (k == "hide") { A.demoOff = true; A.toast("D"); } else Sim::demo(A.s, A.f, k); } i++; } if (ImGui::IsWindowHovered()) A.pod.el = "demo"; ImGui::End();
}
// ---- scena: karty graficzne na srodku ----
// kolor podstawy i podpisu: ten sam, ktorym swieci twarz (kolor jej biezacego zachowania); bez twarzy kolor stanu modelu
Kolor kolorStanu() { auto& f = A.f; if (Twarz::material() > .3f) { auto k = Zach::kolorAktywnego(); return {k[0], k[1], k[2], 1}; } if (f.down) return M.bad; if (f.status == "none") return mix(M.acc, .45f, M.dim); if (f.status == "loading" || f.status == "stopping") return M.warn; if (f.phase == "reading") return M.warn; if (f.phase == "thinking") return M.think; if (f.phase == "answering") return M.ok; return M.acc; }
float obrot[8] = {};
// maly znaczek karty w pasku na dole sceny: ikona z wentylatorami, pamiec, temperatura
float znaczekGpuSzer(float h, const Gpu& g) { float p = h * .5f; return h * .42f + ikonaGpuSzer(h * .56f, 1) + h * .3f + szer(fNum(W7), p, "GPU " + std::to_string(g.index)) + h * .3f + szer(fNum(W5), p, n1(g.vram_used_mb / 1024) + "/" + n0(g.vram_total_mb / 1024) + " GB · " + n0(g.temp_c) + "°") + h * .5f; }
void znaczekGpu(ImDrawList* d, const ImRect& r, const Gpu& g, int idx, const Model* uzywa, bool nad) {
  Kolor k = M.g[idx % 3]; float h = r.GetHeight(), R = h / 2, p = h * .5f; d->AddRectFilled(r.Min, r.Max, u(mix(M.panel, .9f, k), nad ? .96f : .78f), R); d->AddRect(r.Min, r.Max, u(k, nad ? .95f : uzywa ? .7f : .32f), R, px(1));
  float ih = h * .56f, x = r.Min.x + h * .42f; ikonaGpu(d, x, r.Min.y + (h - ih) / 2, ih, 1, k, obrot[idx % 8], nad || uzywa); x += ikonaGpuSzer(ih, 1) + h * .3f;
  std::string a = "GPU " + std::to_string(g.index), b = n1(g.vram_used_mb / 1024) + "/" + n0(g.vram_total_mb / 1024) + " GB · " + n0(g.temp_c) + "°"; tekstL(d, x, r.Min.y, h, fNum(W7), p, u(k), a); x += szer(fNum(W7), p, a) + h * .3f; tekstL(d, x, r.Min.y, h, fNum(W5), p, u(uzywa ? M.txt : M.mut), b);
}
struct UkladSceny { float cx, cy, gw, gh, dol; std::vector<ImRect> karty; std::vector<int> ktore; float bazaY, pol; float sc = 1; } US;
void policzScene(float cx, float cy, float gw, float gh, float dol_) {
  US = {cx, cy, gw, gh, dol_, {}, {}, 0, 0}; auto& gp = A.s.gpus; for (size_t i = 0; i < gp.size() && US.ktore.size() < 4; i++) if (gp[i].vram_total_mb >= 8000) US.ktore.push_back((int)i); if (US.ktore.empty()) for (size_t i = 0; i < gp.size() && i < 2; i++) US.ktore.push_back((int)i);
  int n = (int)US.ktore.size(); float pol = std::clamp(std::min(gw * .46f, gh * .8f), px(120), px(620)); US.pol = pol; float h = px(21), odst = px(22), razem = 0; std::vector<float> w; for (int i = 0; i < n; i++) { w.push_back(znaczekGpuSzer(h, gp[US.ktore[i]])); razem += w.back() + (i ? odst : 0); }
  float x = cx - razem / 2, y = dol_ - px(11) - h; if (A.o.karty && !zakryta && gw > razem + px(20) && gh > px(170)) for (int i = 0; i < n; i++) { US.karty.push_back({{x, y}, {x + w[i], y + h}}); x += w[i] + odst; }
  float Hp_ = ui::H * S; US.sc = std::min(gh / Hp_ * .96f, gw / Hp_ * 1.02f);
  US.bazaY = std::min(dol_ - px(US.karty.empty() ? 40 : 68), cy + .5f * US.sc * Hp_);          // tam, gdzie twarz ma swoja tafle; nad paskiem kart
}
void scena(float Wp, float Hp) {
  okno("##scena", 0, 0, Wp, Hp, FL); auto d = ImGui::GetWindowDrawList(); auto& io = ImGui::GetIO(); float cx = US.cx, cy = US.cy;
  ImGui::SetCursorScreenPos({0, 0}); ImGui::InvisibleButton("##s", {Wp, Hp}, ImGuiButtonFlags_MouseButtonLeft); bool hv = ImGui::IsItemHovered(), act = ImGui::IsItemActive(); static float moved = 0, spun = 0; static double ostKlik = -9; static bool dn = false, naKarcie = false;
  if (io.MousePos.x > -1e6f) Twarz::kursor(io.MousePos.x / Wp, io.MousePos.y / Hp, hv || act);
  if (io.MouseDelta.x || io.MouseDelta.y) Sim::mouseT = A.teraz; if (hv) { A.pod.scena = true; A.pod.el = "scena"; }
  const Model* dziala = nullptr; for (auto& m : A.s.models) if (m.status != "stopped") { dziala = &m; break; }
  // karty: klikniecie otwiera liste modeli, ktore mozna na niej uruchomic
  bool nadKarta = false; bool oba = dziala && dziala->ileGpu >= 2;
  for (size_t i = 0; i < US.karty.size(); i++) { int gi = US.ktore[i]; auto& g = A.s.gpus[gi]; const ImRect& r = US.karty[i]; bool nad = hv && r.Contains(io.MousePos); const Model* uz = nullptr; for (auto& m : A.s.models) if (m.status != "stopped" && std::count(m.gpus.begin(), m.gpus.end(), gi) && m.ileGpu > 0) uz = &m;
    obrot[gi % 8] += io.DeltaTime * (.5f + (float)g.util_gpu * .11f + (nad ? 1.5f : 0)) * A.o.ruch; if (obrot[gi % 8] > 6283) obrot[gi % 8] -= 6283;
    if (i + 1 < US.karty.size() && g.nvlink && A.s.gpus[US.ktore[i + 1]].nvlink) { const ImRect& r2 = US.karty[i + 1]; float y = (r.Min.y + r.Max.y) / 2, sw = r2.Min.x - r.Max.x; Kolor kk = oba ? M.think : M.dim; d->AddLine({r.Max.x + px(2), y}, {r2.Min.x - px(2), y}, u(kk, oba ? .9f : .6f), px(2)); if (oba) { float ph = std::fmod((float)A.teraz * .9f * A.o.ruch, 1.f); d->AddCircleFilled({r.Max.x + sw * ph, y}, px(2.5f), u(M.think, std::sin(ph * 3.14159f))); }
      if (hv && ImRect({r.Max.x, r.Min.y}, {r2.Min.x, r.Max.y}).Contains(io.MousePos)) { nadKarta = true; A.pod.el = "nvlink"; tipJuz("NVLink", L("The bridge that joins both cards so a model can use them as one (TP2). It lights up when such a model is running.", "Mostek łączący obie karty, dzięki któremu model może ich używać jak jednej (TP2). Świeci, gdy taki model działa.")); } }
    znaczekGpu(d, r, g, gi, uz, nad);
    if (nad) { nadKarta = true; A.pod.gpu = gi; A.pod.el = "karta"; A.pod.wartosc = "GPU " + std::to_string(gi) + " " + g.name; ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); tipJuz("GPU " + std::to_string(gi) + " · " + g.name, opisGpu(g) + "\n-\n" + L("Click: models you can start on this card", "Kliknięcie: modele, które można uruchomić na tej karcie") + "\n" + L("Right-click: free the card, details", "Prawy przycisk: zwolnij kartę, szczegóły")); if (ImGui::IsMouseReleased(0) && io.MouseDragMaxDistanceSqr[0] < 36 * S * S) { A.menu = A.pod; A.menu.el = "karta_start"; otworzMenuKarty = true; } } }
  // twarz: przeciaganie obraca, kolko przybliza, klikniecie ja zaczepia, dwuklik zostawia sama scene
  if (ImGui::IsMouseClicked(0)) naKarcie = nadKarta;
  if (act && !naKarcie) { if (!dn) { dn = true; moved = 0; } float dx = io.MouseDelta.x / S, dy = io.MouseDelta.y / S; moved += std::fabs(dx) + std::fabs(dy); spun += std::fabs(dx); if (moved > 4) { ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll); auto& U = Twarz::uzytkownik(); U.yaw -= dx * .006f; U.pitch = std::clamp(U.pitch + dy * .004f, -.5f, .5f); } }
  else if (dn) { dn = false; if (spun > 1800) Zach::graj("dizzy", 2.6f); spun = 0; if (moved < 5) { A.puls = A.teraz; if (A.o.react && Twarz::material() > .5f) { Twarz::kliknieto(); Zach::graj(A.teraz - ostKlik < .35 ? "wink" : "poked", 1.3f); ostKlik = A.teraz; } } }
  if (hv && !nadKarta && io.MouseWheel != 0 && !io.KeyCtrl) { auto& U = Twarz::uzytkownik(); U.zoom = U.zoom * (io.MouseWheel < 0 ? .94f : 1.06f); }
  if (hv && !nadKarta) { if (ImGui::IsMouseDoubleClicked(0)) A.skupienie = !A.skupienie;
    if (std::fabs(io.MousePos.y - US.bazaY) < px(26) && std::fabs(io.MousePos.x - cx) < US.pol * 1.2f) { A.pod.el = "widmo"; tipJuz(L("Activity trace", "Ślad aktywności"), std::string(L("The middle is now, the sides are the last seconds. Bars rise when the model reads or writes and when the cards work.", "Środek to teraz, boki to ostatnie sekundy. Słupki rosną, gdy model czyta albo pisze i gdy karty pracują.")) + "\n" + L("Level now", "Poziom teraz") + "\t" + n0(Scena::par().hist[0] * 100) + " %"); }
    else if (Twarz::material() > .5f && std::fabs(io.MousePos.x - cx) < US.pol * .5f && std::fabs(io.MousePos.y - cy) < US.gh * .36f) { auto& b = Zach::aktywne(); A.pod.el = "twarz"; tipJuz(L("Face", "Twarz"), b.n[A.jezyk] + (Zach::powod().empty() ? "" : " · " + Zach::powod()) + "\n-\n" + L("Drag: turn the head · Wheel: zoom", "Przeciągnij: obróć głowę · Kółko: przybliż") + "\n" + L("Click: poke · Double-click: scene only", "Kliknięcie: zaczep · Dwuklik: sama scena") + "\n" + L("Right-click: face and scene menu", "Prawy przycisk: menu twarzy i sceny")); } }
  // napisy
  if (!zakryta && Twarz::material() < .02f && A.f.status == "none") { const char* a = t("face_absent"); const char* b = L("Press START on the model list", "Naciśnij START na liście modeli"); float w1 = szer(fNum(W6), px(15), a, px(3.6f)); tekst(d, cx - w1 / 2, cy - px(34), fNum(W6), px(15), u(M.acc), a, px(3.6f)); float w2 = szer(fUi(W5), px(12), b); if (w2 < US.gw - px(20)) tekst(d, cx - w2 / 2, cy - px(12), fUi(W5), px(12), u(M.mut), b); }
  if (!zakryta && A.o.caption && (A.f.status != "none" || Zach::nadpisane())) { OpisStanu o = opisStanu(); std::string st = A.f.status == "none" ? "" : o.lab, be; auto& b = Zach::aktywne(); be = b.n[A.jezyk]; if (!Zach::powod().empty() && !Zach::nadpisane()) be += " · " + Zach::powod(); float w1 = szer(fNum(W6), px(15), st, px(1.5f)), w2 = szer(fUi(W5), px(12), be), w = std::max(w1, w2) + px(28), h = px(5 + 6 + 3) + px(12 * 1.35f) + (st.empty() ? 0 : px(20.25f)), y = US.bazaY - px(34) - h; Kolor c = kolorStanu(); d->AddRectFilled({cx - w / 2, y}, {cx + w / 2, y + h}, u(przezr(M.bg, .62f)), px(8)); float yy = y + px(5); if (!st.empty()) { tekstL(d, cx - w1 / 2, yy, px(20.25f), fNum(W6), px(15), u(c), st, px(1.5f)); yy += px(20.25f + 3); } tekstL(d, cx - w2 / 2, yy, px(16.2f), fUi(W5), px(12), u(M.txt, .85f), be); }
  ImGui::End();
}
void nawigacja(float Wp, float Hp) {
  float h = px(38); okno("##nav", 0, Hp - h, Wp, h); auto d = ImGui::GetWindowDrawList(); d->AddRectFilled({0, Hp - h}, {Wp, Hp}, u(przezr(M.panel, .92f))); d->AddLine({0, Hp - h}, {Wp, Hp - h}, u(M.line), px(1)); const char* T[4] = {"nav_face", "nav_hw", "nav_models", "p_energy"}; float cw = Wp / 4;
  for (int i = 0; i < 4; i++) { ImRect r({i * cw, Hp - h}, {(i + 1) * cw, Hp}); bool on = A.widok == i, hv = hover(r); if (on) d->AddRectFilled(r.Min, r.Max, u(przezr(M.acc, .18f))); std::string s = t(T[i]); tekstL(d, r.Min.x + (cw - szer(fUi(W6), px(12), s)) / 2, r.Min.y, h, fUi(W6), px(12), u(on || hv ? M.acc : M.mut), s); if (klik(r)) { A.widok = i; if (i == 3) A.zwiniete["energy"] = false; } } ImGui::End();
}
// najwiekszy wolny prostokat miedzy panelami: tam staje scena
ImRect wolnePole(ImRect r) {
  for (auto& [id, p] : A.P) { if (!p.widac) continue; ImRect o = p.r; if (!r.Overlaps(o)) continue; float w = r.GetWidth(), h = r.GetHeight(); float opc[4] = {std::max(0.f, o.Min.x - r.Min.x) * h, std::max(0.f, r.Max.x - o.Max.x) * h, std::max(0.f, o.Min.y - r.Min.y) * w, std::max(0.f, r.Max.y - o.Max.y) * w}; int b = 0; for (int i = 1; i < 4; i++) if (opc[i] > opc[b]) b = i; if (b == 0) r.Max.x = o.Min.x; else if (b == 1) r.Min.x = o.Max.x; else if (b == 2) r.Max.y = o.Min.y; else r.Min.y = o.Max.y; }
  return r;
}
void aktualizujScene(float Wp, float Hp, float dt) {
  auto& P = Scena::par(); auto& f = A.f; float cel_ = .08f + .03f * std::sin((float)A.teraz * .7f); double ut = 0; int nk = 0; for (auto& g : A.s.gpus) if (g.vram_total_mb >= 8000) { ut += g.util_gpu; nk++; } if (nk) ut /= nk * 100.;
  if (f.down) cel_ = .12f; else if (f.status == "loading") cel_ = .28f + .12f * std::sin((float)A.teraz * 3); else if (f.status == "ready") { if (f.phase == "reading") cel_ = .45f + .35f * std::min(1.f, f.pp / 3000); else if (f.phase == "thinking") cel_ = .55f; else if (f.phase == "answering") cel_ = .6f + .4f * std::min(1.f, f.tgAvg > 0 ? f.tg / f.tgAvg : .7f); else if (f.phase == "tool") cel_ = .3f; else if (f.phase == "compact") cel_ = .5f; else cel_ = .1f; }
  cel_ = std::max(cel_, (float)ut * .85f); static float poz = 0; poz += (cel_ - poz) * std::min(1.f, dt * 5); static double nast = 0; if (A.teraz >= nast) { nast = A.teraz + .045; for (int i = 63; i > 0; i--) P.hist[i] = P.hist[i - 1]; P.hist[0] = poz; }
  Kolor k = kolorStanu(); for (int i = 0; i < 3; i++) { float v = i == 0 ? k.r : i == 1 ? k.g : k.b; P.kolor[i] += (v - P.kolor[i]) * std::min(1.f, dt * 4); } P.akcent[0] = M.acc.r; P.akcent[1] = M.acc.g; P.akcent[2] = M.acc.b; P.tlo[0] = M.bg.r; P.tlo[1] = M.bg.g; P.tlo[2] = M.bg.b;
  static float sx = -1, sy, sp, sb; if (sx < 0) { sx = US.cx; sy = US.cy; sp = US.pol; sb = US.bazaY; } float kq = A.chwyt.aktywny ? 1 : std::min(1.f, dt * 9); sx += (US.cx - sx) * kq; sy += (US.cy - sy) * kq; sp += (US.pol - sp) * kq; sb += (US.bazaY - sb) * kq;
  auto sk = A.skorki(); auto it = sk.find(A.o.skorka); float blask = 1, mgla = .5f, siatka = .25f; if (it != sk.end()) { blask = it->second.glow; mgla = it->second.glass; siatka = it->second.scan; }
  P.cx = sx; P.cy = sy; P.pol = sp; P.bazaY = sb; P.poziom = poz; P.blask = std::clamp(blask, 0.f, 2.f); P.skala = S; P.mgla = mgla * 2; P.siatka = siatka * 4; P.puls = (float)(A.teraz - A.puls); P.ruch = A.o.ruch; P.widmo = A.o.widmo && !zakryta; (void)Wp; (void)Hp;
}
}
std::string wskazowka_(const char* s) { return std::string("-\n") + s; }
void budujInterfejs() {
  auto& io = ImGui::GetIO(); float Wp = io.DisplaySize.x, Hp = io.DisplaySize.y; ui::W = Wp / S; ui::H = Hp / S; float W_ = ui::W; A.pod = Cel{};
  wl = 296; wr = 506; if (W_ <= 1400) { wl = 262; wr = 470; } if (W_ <= 1180) { wl = std::clamp(W_ * .21f, 190.f, 262.f); wr = std::clamp(W_ * .32f, 250.f, 380.f); } waski = W_ < 860; navH = waski ? px(38) : 0;
  float top = px(46), mar = px(8); bool pokaz = !A.skupienie; pole = {{0, top}, {Wp, Hp - navH}};
  obslugaChwytu();
  // scena staje w wolnym miejscu z poprzedniej klatki
  ImRect wolne = pole; if (pokaz && !waski) wolne = wolnePole(pole); else if (waski && A.widok == 3 && A.P["energy"].widac) wolne.Max.y = A.P["energy"].r.Min.y; if (wolne.GetWidth() < px(200) || wolne.GetHeight() < px(160)) wolne = pole;
  zakryta = waski && (A.widok == 1 || A.widok == 2); policzScene((wolne.Min.x + wolne.Max.x) / 2, (wolne.Min.y + wolne.Max.y) / 2, wolne.GetWidth(), wolne.GetHeight(), wolne.Max.y); { // glowa z szyja i kreskami nad czolem zawsze miesci sie miedzy gora wolnego pola a podstawa i stoi w jego srodku
    auto& U = Twarz::uzytkownik(); float gora_ = wolne.Min.y + px(10), dol_ = US.bazaY - px(4), jedn = std::max(1.f, US.sc * Hp), zmax = std::clamp((dol_ - gora_) / (FIG_WYS * jedn), .3f, 3.f);
    static bool pierwszy = true; if (pierwszy && Twarz::material() > .5f) { pierwszy = false; U.zoom = zmax; } U.zoom = std::clamp(U.zoom, std::min(.45f, zmax), zmax); A.zoomTwarzyMax = zmax;
    if (getenv("CM_DIAG")) { static double nast = 0; if (A.teraz > nast) { nast = A.teraz + 2; fprintf(stderr, "diag t=%.0f zoom=%.3f zmax=%.3f sc=%.3f gora=%.0f dol=%.0f wolne=%.0f..%.0f x %.0f..%.0f skup=%d\n", A.teraz, U.zoom, zmax, US.sc, gora_, dol_, wolne.Min.y, wolne.Max.y, wolne.Min.x, wolne.Max.x, (int)A.skupienie); } }
    Twarz::widok(US.cx / Wp, ((gora_ + dol_) * .5f - FIG_SRODEK * jedn * U.zoom) / Hp, US.sc, std::max(0.f, (wolne.Min.x - px(14)) / Wp), std::min(1.f, (wolne.Max.x + px(14)) / Wp)); }
  Twarz::podstawa(!A.o.widmo); Twarz::otoczenie(false, A.o.rings); aktualizujScene(Wp, Hp, io.DeltaTime); scena(Wp, Hp);
  for (auto& [id, p] : A.P) p.widac = false;
  if (pokaz) {
    if (waski) { if (A.widok == 1) kolumna(LEWE, mar, top + mar, Wp - 2 * mar, Hp - top - 2 * mar - navH); else if (A.widok == 2) kolumna(PRAWE, mar, top + mar, Wp - 2 * mar, Hp - top - 2 * mar - navH); else if (A.widok == 3) dol(mar, Wp - 2 * mar, Hp - mar - navH); }
    else if (A.swobodny) { for (auto& p : definicjePaneli()) if (widac(p.id)) { ImRect r = prostPx(p.id); panelOkno(p.id, r.Min.x, r.Min.y, r.GetWidth(), r.GetHeight()); } }
    else { kolumna(LEWE, mar, top + mar, px(wl), Hp - top - 2 * mar); kolumna(PRAWE, Wp - mar - px(wr), top + mar, px(wr), Hp - top - 2 * mar); dol(px(wl + 16), Wp - px(wl + wr + 32), Hp - mar); }
  }
  if (!zakryta) pasekDemo(US.cx, US.gw - px(16)); gora(Wp); if (waski) nawigacja(Wp, Hp);
  if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) A.skupienie = false;
  if (!io.WantTextInput) { if (ImGui::IsKeyPressed(ImGuiKey_F, false)) A.skupienie = !A.skupienie; if (ImGui::IsKeyPressed(ImGuiKey_D, false)) A.demoOff = !A.demoOff; }
  if (io.KeyCtrl && io.MouseWheel != 0) { A.zoom = std::clamp(A.zoom + (io.MouseWheel > 0 ? .1f : -.1f), .7f, 2.f); A.zapisz(); }
  if (ImGui::IsMouseReleased(1) && !A.chwyt.aktywny && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) { A.menu = A.pod; ImGui::OpenPopup("##menu"); }
  if (!A.o.warunki && A.dialog == Dialog::Brak && !A.otworzDialog) A.pokazDialog(Dialog::Warunki);
  if (otworzMenuKarty) { otworzMenuKarty = false; ImGui::OpenPopup("##menu"); }
  rysujMenu(); rysujDialogi(); rysujTip(A.teraz);
  if (A.teraz < A.toastDo && !A.toastTxt.empty()) { auto* fg = ImGui::GetForegroundDrawList(); float w = szer(fUi(W5), px(12), A.toastTxt) + px(28), x = (Wp - w) / 2, y = Hp - px(64 + 31); fg->AddRectFilled({x, y}, {x + w, y + px(31)}, u(M.panel), px(M.r)); fg->AddRect({x, y}, {x + w, y + px(31)}, u(M.acc), px(M.r), px(1)); tekst(fg, x + px(14), y + px(8), fUi(W5), px(12), u(M.txt), A.toastTxt); }
}
void panelDuzy(const std::string& id, float w, float h) { panelOkno(id, 0, 0, w, h, true); }
