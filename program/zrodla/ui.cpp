#include "ui.h"
#include "aplikacja.h"
#include <SDL3/SDL.h>
#include <misc/cpp/imgui_stdlib.h>
#include <misc/freetype/imgui_freetype.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace ui {
Motyw M; float S = 1, W = 1680, H = 962;
static ImFont* F[2][3];
Kolor hex(const std::string& h) { unsigned v = 0; if (h.size() >= 7) sscanf(h.c_str() + 1, "%x", &v); return {((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f, 1}; }
std::string hex(const Kolor& k) { char b[8]; snprintf(b, 8, "#%02x%02x%02x", (int)std::lround(k.r * 255), (int)std::lround(k.g * 255), (int)std::lround(k.b * 255)); return b; }
ImU32 u(const Kolor& k, float a) { return ImGui::ColorConvertFloat4ToU32({k.r, k.g, k.b, k.a * a}); }
Kolor mix(const Kolor& a, float p, const Kolor& b) { float q = 1 - p, al = a.a * p + b.a * q; if (al <= 0) return {0, 0, 0, 0}; auto m = [&](float x, float y) { return (x * a.a * p + y * b.a * q) / al; }; return {m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), al}; }
Kolor przezr(const Kolor& a, float p) { return {a.r, a.g, a.b, a.a * p}; }

void czcionki(const std::string& dane) {
  auto& io = ImGui::GetIO(); std::string c = dane + "/czcionki/";
  const char* n[2][3] = {{"Inter-Medium", "Inter-SemiBold", "Inter-Bold"}, {"JetBrainsMono-Medium", "JetBrainsMono-SemiBold", "JetBrainsMono-Bold"}};
  // ImGui liczy rozmiar jako wysokosc wznoszenie+zejscie, CSS jako em: mnoznik wyrownuje litery do przegladarki, przesuniecie stawia linie bazowa jak w CSS
  const float skala[2] = {1.2100f, 1.3200f}, wzn[2] = {.96875f, 1.02f};
  for (int i = 0; i < 2; i++) for (int w = 0; w < 3; w++) { ImFontConfig k; k.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_NoHinting; k.OversampleH = 3; k.RasterizerMultiply = 1.15f; k.ExtraSizeScale = skala[i]; float off = 13 * (.96875f - wzn[i] / skala[i]); k.GlyphOffset.y = off; F[i][w] = io.Fonts->AddFontFromFileTTF((c + n[i][w] + ".ttf").c_str(), 13, &k); if (!F[i][w]) { F[i][w] = io.Fonts->AddFontDefault(); continue; }
    struct { const char* n; float s; } Z[] = {{"DejaVuSans", 1.164f}, {"NotoSansSymbols2-Regular", 1.699f}, {"NotoSansMath-Regular", 1.492f}}; for (auto& z : Z) { ImFontConfig q; q.MergeMode = true; q.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_NoHinting; q.OversampleH = 3; q.ExtraSizeScale = z.s; q.GlyphOffset.y = off; io.Fonts->AddFontFromFileTTF((c + z.n + ".ttf").c_str(), 13, &q); } }
  io.FontDefault = F[0][0];
}
ImFont* fUi(Waga w) { return F[0][w]; } ImFont* fMono(Waga w) { return F[1][w]; } ImFont* fNum(Waga w) { return F[M.numInter ? 0 : 1][w]; }
float wysLinii(float rozm) { return rozm * 1.35f; }
float szer(ImFont* f, float p, std::string_view t, float sp) { if (t.empty()) return 0; float w = f->CalcTextSizeA(p, FLT_MAX, 0, t.data(), t.data() + t.size()).x; if (sp != 0) { int n = 0; for (char c : t) if ((c & 0xC0) != 0x80) n++; w += sp * n; } return w; }
void tekst(ImDrawList* d, float x, float y, ImFont* f, float p, ImU32 c, std::string_view t, float sp) {
  if (t.empty()) return; y = std::floor(y + .5f);
  if (sp == 0) { d->AddText(f, p, {x, y}, c, t.data(), t.data() + t.size()); return; }
  const char* s = t.data(); const char* e = s + t.size(); auto* b = f->GetFontBaked(p);
  while (s < e) { unsigned ch; int n = ImTextCharFromUtf8(&ch, s, e); if (n <= 0) break; d->AddText(f, p, {x, y}, c, s, s + n); x += b->GetCharAdvance((ImWchar)ch) + sp; s += n; }
}
void tekstL(ImDrawList* d, float x, float y, float lh, ImFont* f, float p, ImU32 c, std::string_view t, float sp) { tekst(d, x, y + (lh - p * 1.21f) * .5f, f, p, c, t, sp); }
std::string skroc(ImFont* f, float p, const std::string& t, float maxW, float sp) { if (szer(f, p, t, sp) <= maxW) return t; std::string r = t; while (!r.empty()) { do r.pop_back(); while (!r.empty() && (r.back() & 0xC0) == 0x80); if (szer(f, p, r + "…", sp) <= maxW) return r + "…"; } return ""; }
std::vector<std::string> linie(ImFont* f, float p, const std::string& t, float w) { std::vector<std::string> o; const char* s = t.c_str(); const char* e = s + t.size(); while (s < e) { const char* n = f->CalcWordWrapPosition(p, s, e, w); if (n == s) n = s + 1; const char* k = n; while (k > s && k[-1] == ' ') k--; o.emplace_back(s, k); s = n; while (s < e && *s == ' ') s++; } if (o.empty()) o.emplace_back(); return o; }
std::string wielkie(std::string t) { for (size_t i = 0; i < t.size(); i++) { unsigned char c = t[i]; if (c < 128) t[i] = toupper(c);
    else if (c == 0xC3 && i + 1 < t.size() && (unsigned char)t[i + 1] == 0xB3) t[i + 1] = (char)0x93;
    else if (c == 0xC4 && i + 1 < t.size()) { unsigned char n = t[i + 1]; if (n == 0x85 || n == 0x87 || n == 0x99) t[i + 1] = n - 1; }
    else if (c == 0xC5 && i + 1 < t.size()) { unsigned char n = t[i + 1]; if (n == 0x82 || n == 0x84 || n == 0x9B || n == 0xBA || n == 0xBC) t[i + 1] = n - 1; } } return t; }
bool hover(const ImRect& r) { return ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseHoveringRect(r.Min, r.Max, true); }
bool klik(const ImRect& r) { return hover(r) && ImGui::IsMouseReleased(0) && !A.chwyt.aktywny && ImGui::GetIO().MouseDragMaxDistanceSqr[0] < 36 * S * S; }
ImVec2 btnRozm(const Btn& b) { float p = b.ikona ? 15 : b.maly ? 10 : 11, sp = .03f * p; float px_ = b.ikona ? 8 : b.maly ? 7 : 9, py = b.ikona ? 3 : b.maly ? 2 : 5; return {szer(fUi(W6), px(p), b.t, px(sp)) + px(px_ * 2 + 2), px(p * 1.21f + py * 2 + 2)}; }
bool btn(ImDrawList* d, float x, float y, const Btn& b) {
  ImVec2 z = btnRozm(b); ImRect r({x, y}, {x + z.x, y + z.y}); bool h = hover(r), cl = klik(r) && !b.wylaczony; float p = b.ikona ? 15 : b.maly ? 10 : 11, sp = .03f * p, rad = px(std::max(2.f, M.r - 2));
  Kolor bg = przezr(M.acc, .07f), br = M.line, tx = M.txt;
  if (b.rodzaj == Akcent) { bg = przezr(M.acc, .22f); br = M.acc; tx = M.acc; } if (b.rodzaj == Ostrz) { bg = {0, 0, 0, 0}; br = przezr(M.warn, .4f); tx = M.warn; } if (b.rodzaj == Zly) { bg = {0, 0, 0, 0}; br = przezr(M.bad, .5f); tx = M.bad; } if (b.rodzaj == Wlaczony) { bg = przezr(M.acc, .26f); br = M.acc; }
  if (b.wylaczony) { bg = {0, 0, 0, 0}; br = przezr(M.line, .6f); tx = M.dim; } else if (h) { br = M.acc; tx = M.acc; bg = przezr(M.acc, ImGui::IsMouseDown(0) ? .34f : .16f); }
  d->AddRectFilled(r.Min, r.Max, u(bg), rad); d->AddRect(r.Min, r.Max, u(br), rad, px(1));
  tekst(d, x + (z.x - szer(fUi(W6), px(p), b.t, px(sp))) * .5f, y + (z.y - px(p * 1.21f)) * .5f, fUi(W6), px(p), u(tx), b.t, px(sp));
  if (h) { if (!b.wylaczony) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); if (!b.tip.empty()) { auto n = b.tip.find('\n'); tipJuz(n == std::string::npos ? b.tip : b.tip.substr(0, n), n == std::string::npos ? "" : b.tip.substr(n + 1)); } } return cl;
}
void pasek(ImDrawList* d, const ImRect& r, float pct, const Kolor& c) { float rad = r.GetHeight() * .5f; d->AddRectFilled(r.Min, r.Max, u(przezr(M.txt, .1f)), rad); float w = r.GetWidth() * std::clamp(pct, 0.f, 100.f) / 100; if (w > .5f) d->AddRectFilled(r.Min, {r.Min.x + w, r.Max.y}, u(c), rad); }
void kropka(ImDrawList* d, float cx, float cy, const std::string& st) { Kolor c = M.dim; bool glow = false; if (st == "on" || st == "ready") { c = M.ok; glow = true; } if (st == "starting" || st == "stopping") { c = M.warn; glow = true; } if (st == "failed") c = M.bad; float r = px(4.5f); if (glow) for (int i = 3; i > 0; i--) d->AddCircleFilled({cx, cy}, r + px(i * 2.f), u(c, .09f)); d->AddCircleFilled({cx, cy}, r, u(c), 20); }
void pinezka(ImDrawList* d, float x, float y, float r, ImU32 c) { d->AddCircleFilled({x, y - r * .5f}, r * .62f, c, 12); d->AddTriangleFilled({x - r * .42f, y - r * .1f}, {x + r * .42f, y - r * .1f}, {x, y + r}, c); }

// ---- chmurki ----
namespace { struct { std::string t, b; ImGuiID id = 0; } tipB; ImGuiID tipPop = 0; double tipOd = 0, tipWidziany = -9; }
void tipJuz(const std::string& tytul, const std::string& tresc) { tipB.t = tytul; tipB.b = tresc; tipB.id = ImHashStr(tytul.c_str()); if (!tipB.id) tipB.id = 1; }
void tip(const ImRect& r, const std::string& tytul, const std::string& tresc) { if (!hover(r)) return; tipJuz(tytul, tresc); tipB.id ^= ImHashData(&r.Min, sizeof r.Min); if (!tipB.id) tipB.id = 1; }
void rysujTip(double teraz) {
  auto& io = ImGui::GetIO(); bool zajety = io.MouseDown[0] || io.MouseDown[1] || A.chwyt.aktywny || io.MousePos.x < -1e5f;
  if (!tipB.id || zajety) { tipPop = 0; tipB = {}; return; }
  if (tipB.id != tipPop) { tipPop = tipB.id; tipOd = teraz - tipWidziany < .6 ? teraz - 9 : teraz; }
  if (teraz - tipOd < .38) { tipB = {}; return; }
  tipWidziany = teraz; ImDrawList* d = ImGui::GetForegroundDrawList(); float pt = px(11.5f), pb = px(11), lh = px(15.5f), pad = px(10), maxW = px(380);
  struct W { std::string k, v; int rodzaj; };   // 0 tekst, 1 klucz-wartosc, 2 kreska
  std::vector<W> w; float kw = 0, vw = 0; { size_t a = 0; const std::string& s = tipB.b; while (a <= s.size() && !s.empty()) { size_t e = s.find('\n', a); if (e == std::string::npos) e = s.size(); std::string l = s.substr(a, e - a); a = e + 1; if (l == "-") w.push_back({"", "", 2}); else if (auto q = l.find('\t'); q != std::string::npos) { W x{l.substr(0, q), l.substr(q + 1), 1}; kw = std::max(kw, szer(fUi(W5), pb, x.k)); vw = std::max(vw, szer(fNum(W6), pb, x.v)); w.push_back(x); } else w.push_back({l, "", 0}); if (e >= s.size()) break; } }
  float tw = szer(fUi(W7), pt, tipB.t), szerok = std::max(tw, kw + vw + (kw > 0 ? px(18) : 0)); bool tekstJest = false; for (auto& x : w) if (x.rodzaj == 0) tekstJest = true; if (tekstJest) szerok = std::max(szerok, std::min(maxW, px(250))); szerok = std::min(std::max(szerok, px(90)), std::max(maxW, kw + vw + px(18)));
  std::vector<std::vector<std::string>> zaw(w.size()); float hh = px(17); for (size_t i = 0; i < w.size(); i++) { if (w[i].rodzaj == 0) { zaw[i] = linie(fUi(W5), pb, w[i].k, szerok); hh += lh * zaw[i].size(); } else hh += w[i].rodzaj == 2 ? px(9) : lh; } if (!w.empty()) hh += px(5);
  float W_ = szerok + pad * 2, H_ = hh + pad * 2 - px(4), x = io.MousePos.x + px(16), y = io.MousePos.y + px(20); if (x + W_ > io.DisplaySize.x - px(6)) x = io.MousePos.x - W_ - px(12); if (y + H_ > io.DisplaySize.y - px(6)) y = io.MousePos.y - H_ - px(10); x = std::max(px(6), x); y = std::max(px(6), y);
  float al = std::min(1., (teraz - tipOd - .38) / .12 + (teraz - tipOd > 5 ? 1 : 0)); if (al > 1) al = 1; float R = px(std::max(4.f, M.r));
  for (int i = 4; i > 0; i--) d->AddRectFilled({x - px(i * 1.5f), y - px(i * 1.5f) + px(3)}, {x + W_ + px(i * 1.5f), y + H_ + px(i * 1.5f) + px(3)}, IM_COL32(0, 0, 0, (int)(22 * al)), R + px(i * 1.5f));
  d->AddRectFilled({x, y}, {x + W_, y + H_}, u(mix(M.panel, .86f, M.acc), .985f * al), R); d->AddRect({x, y}, {x + W_, y + H_}, u(M.acc, .55f * al), R, px(1)); d->AddRectFilled({x, y + R}, {x + px(2), y + H_ - R}, u(M.acc, al));
  float yy = y + pad - px(2); tekst(d, x + pad, yy, fUi(W7), pt, u(M.txt, al), tipB.t); yy += px(17); if (!w.empty()) yy += px(4);
  for (size_t i = 0; i < w.size(); i++) { auto& q = w[i]; if (q.rodzaj == 2) { d->AddLine({x + pad, yy + px(4)}, {x + W_ - pad, yy + px(4)}, u(M.line, al), px(1)); yy += px(9); } else if (q.rodzaj == 1) { tekst(d, x + pad, yy, fUi(W5), pb, u(M.mut, al), q.k); tekst(d, x + W_ - pad - szer(fNum(W6), pb, q.v), yy, fNum(W6), pb, u(M.txt, al), q.v); yy += lh; } else for (auto& l : zaw[i]) { tekst(d, x + pad, yy, fUi(W5), pb, u(mix(M.txt, .78f, M.mut), al), l); yy += lh; } }
  tipB = {};
}
// ---- rysunki ----
Kolor rgb(unsigned v) { return {((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f, 1}; }
Kolor kolTp(int n) { return n >= 2 ? M.think : n == 1 ? M.acc : M.mut; }
void wentylator(ImDrawList* d, ImVec2 c, float r, float kat, ImU32 kol, int lop) {
  const float T = 6.2831853f; float krok = T / lop; for (int i = 0; i < lop; i++) { float a = kat + i * krok; d->PathLineTo(c); d->PathArcTo(c, r * .9f, a, a + krok * .58f, r > 10 ? 10 : 5); d->PathFillConvex(kol); }
}
float ikonaGpuSzer(float h, int n) { if (n <= 0) return h * 1.15f; float cw = h * 1.72f; return n * cw + (n - 1) * h * .28f; }
void ikonaGpu(ImDrawList* d, float x, float y, float h, int n, const Kolor& k, float kat, bool jasna) {
  float a = jasna ? 1.f : .72f, gr = std::max(1.f, h * .075f);
  if (n <= 0) { float s = h * .78f, x0 = x + h * .18f, y0 = y + (h - s) / 2; for (int i = 0; i < 3; i++) { float q = y0 + s * (i + .5f) / 3, r = x0 + s * (i + .5f) / 3; d->AddLine({x0 - h * .16f, q}, {x0 + s + h * .16f, q}, u(k, a * .7f), gr); d->AddLine({r, y0 - h * .16f}, {r, y0 + s + h * .16f}, u(k, a * .7f), gr); } d->AddRectFilled({x0, y0}, {x0 + s, y0 + s}, u(mix(M.panel, .8f, k)), h * .12f); d->AddRect({x0, y0}, {x0 + s, y0 + s}, u(k, a), h * .12f, gr); d->AddRect({x0 + s * .28f, y0 + s * .28f}, {x0 + s * .72f, y0 + s * .72f}, u(k, a * .8f), 0, gr); return; }
  float cw = h * 1.72f, odst = h * .28f;
  for (int j = 0; j < n; j++) { float x0 = x + j * (cw + odst), bx = x0 + h * .16f; d->AddRectFilled({bx, y}, {x0 + cw, y + h}, u(mix(M.panel, .78f, k)), h * .16f); d->AddRect({bx, y}, {x0 + cw, y + h}, u(k, a), h * .16f, gr);
    d->AddLine({x0 + gr * .5f, y - h * .1f}, {x0 + gr * .5f, y + h * 1.1f}, u(k, a), gr * 1.4f);                      // sledz
    float fr = h * .31f, w_ = cw - h * .16f; for (int f = 0; f < 2; f++) { ImVec2 c{bx + w_ * (f ? .73f : .27f), y + h * .5f}; d->AddCircle(c, fr, u(k, a * .55f), 0, gr * .8f); wentylator(d, c, fr, (f ? -kat : kat) + j * 1.3f, u(k, a), 3); } }
  if (n >= 2) for (int j = 0; j + 1 < n; j++) { float xa = x + j * (cw + odst) + cw * .62f, xb = x + (j + 1) * (cw + odst) + cw * .42f, yy = y - h * .2f; d->AddRectFilled({xa, yy - gr}, {xb, yy + gr}, u(k, a), gr); }   // mostek NVLink
}
float znaczekSzer(float h, const std::string& z) { return std::max(h, szer(fNum(W7), h * .68f, z) + h * .5f); }
void znaczek(ImDrawList* d, float x, float y, float h, const std::string& z, const Kolor& k) { float w = znaczekSzer(h, z), p = h * .68f; d->AddRectFilled({x, y}, {x + w, y + h}, u(k, .16f), h * .28f); d->AddRect({x, y}, {x + w, y + h}, u(k, .7f), h * .28f, std::max(1.f, px(1))); tekst(d, x + (w - szer(fNum(W7), p, z)) / 2, y + (h - p * 1.21f) / 2, fNum(W7), p, u(k), z); }

void styl() {
  auto& s = ImGui::GetStyle(); auto c = [](const Kolor& k) { return ImVec4{k.r, k.g, k.b, k.a}; }; auto* C = s.Colors; float R = px(M.r);
  s.WindowPadding = {0, 0}; s.ItemSpacing = {0, 0}; s.ItemInnerSpacing = {px(4), px(4)}; s.FramePadding = {px(5), px(3)}; s.WindowBorderSize = 0; s.ChildBorderSize = 0; s.PopupBorderSize = px(1); s.FrameBorderSize = px(1); s.WindowRounding = R; s.PopupRounding = R; s.FrameRounding = px(3); s.GrabRounding = px(3); s.ScrollbarSize = px(6); s.ScrollbarRounding = px(3); s.GrabMinSize = px(10); s.DisplaySafeAreaPadding = {0, 0}; s.AntiAliasedFill = true;
  C[ImGuiCol_Text] = c(M.txt); C[ImGuiCol_TextDisabled] = c(M.mut); C[ImGuiCol_WindowBg] = {0, 0, 0, 0}; C[ImGuiCol_ChildBg] = {0, 0, 0, 0}; C[ImGuiCol_PopupBg] = c(M.panel); C[ImGuiCol_Border] = c(przezr(M.acc, .45f)); C[ImGuiCol_BorderShadow] = {0, 0, 0, 0};
  C[ImGuiCol_FrameBg] = c(mix(M.panel, .8f, {0, 0, 0, 1})); C[ImGuiCol_FrameBgHovered] = c(mix(M.panel, .7f, M.acc)); C[ImGuiCol_FrameBgActive] = C[ImGuiCol_FrameBgHovered];
  C[ImGuiCol_Header] = c(przezr(M.acc, .26f)); C[ImGuiCol_HeaderHovered] = c(przezr(M.acc, .18f)); C[ImGuiCol_HeaderActive] = c(przezr(M.acc, .3f)); C[ImGuiCol_Button] = c(przezr(M.acc, .07f)); C[ImGuiCol_ButtonHovered] = c(przezr(M.acc, .18f)); C[ImGuiCol_ButtonActive] = c(przezr(M.acc, .3f));
  C[ImGuiCol_SliderGrab] = c(M.acc); C[ImGuiCol_SliderGrabActive] = c(M.rim); C[ImGuiCol_CheckMark] = c(M.acc); C[ImGuiCol_Separator] = c(M.line); C[ImGuiCol_ScrollbarBg] = {0, 0, 0, 0}; C[ImGuiCol_ScrollbarGrab] = c(M.line); C[ImGuiCol_ScrollbarGrabHovered] = c(przezr(M.acc, .5f)); C[ImGuiCol_ScrollbarGrabActive] = c(M.acc); C[ImGuiCol_TextSelectedBg] = c(przezr(M.acc, .35f)); C[ImGuiCol_NavCursor] = c(M.acc); C[ImGuiCol_ModalWindowDimBg] = {0, 0, 0, .55f};
}
bool poleTekstowe(const char* id, std::string* s, float w, bool wiel, float h, const char* pod) {
  ImGui::SetNextItemWidth(w); bool z = wiel ? ImGui::InputTextMultiline(id, s, {w, h}) : pod ? ImGui::InputTextWithHint(id, pod, s) : ImGui::InputText(id, s);
  if (ImGui::BeginPopupContextItem()) { auto* st = ImGui::GetInputTextState(ImGui::GetItemID());
    if (ImGui::MenuItem(t("ctx_copy_text"))) SDL_SetClipboardText(s->c_str());
    if (ImGui::MenuItem(t("ctx_paste"))) { char* c = SDL_GetClipboardText(); if (c) { *s += c; SDL_free(c); z = true; if (st) ImGui::ClearActiveID(); } }
    if (ImGui::MenuItem(t("ctx_clear"))) { s->clear(); z = true; if (st) ImGui::ClearActiveID(); } ImGui::EndPopup(); }
  return z;
}
}
static std::string grupy(long long v, char sep, const char* spPl) { std::string s = std::to_string(v < 0 ? -v : v), r; int n = 0; bool pl = spPl != nullptr; size_t L = s.size(); if (pl && L < 5) r = s; else { for (int i = (int)L - 1; i >= 0; i--) { r.insert(r.begin(), s[i]); if (++n % 3 == 0 && i > 0) { if (pl) r.insert(0, spPl); else r.insert(r.begin(), sep); } } } return (v < 0 ? "-" : "") + r; }
static std::string dziesietne(double v, int p) { if (std::isnan(v) || std::isinf(v)) return "—"; bool pl = A.jezyk == 1; double m = std::pow(10, p); double q = std::round(std::fabs(v) * m) / m; long long c = (long long)q; long long fr = std::llround((q - c) * m); if (fr >= (long long)m) { c++; fr = 0; } std::string s = (v < 0 && (c || fr) ? "-" : "") + grupy(c, ',', pl ? "\xC2\xA0" : nullptr); if (p) { char b[16]; snprintf(b, 16, "%0*lld", p, fr); s += (pl ? "," : "."); s += b; } return s; }
std::string n0(double v) { return std::isnan(v) ? "—" : dziesietne(std::round(v), 0); }
std::string n1(double v) { return dziesietne(v, 1); }
std::string n2(double v) { return dziesietne(v, 2); }
std::string nK(double v) { if (std::isnan(v)) return "—"; return v >= 1e6 ? n2(v / 1e6) + "M" : v >= 1e4 ? n1(v / 1e3) + "K" : n0(v); }
std::string czasPracy(double s) { long t = std::lround(s), h = t / 3600, m = t % 3600 / 60; char b[32]; if (h) snprintf(b, 32, "%ldh %ldm", h, m); else snprintf(b, 32, "%ldm %lds", m, t % 60); return b; }
