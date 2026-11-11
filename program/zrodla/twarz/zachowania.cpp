// Zachowania twarzy: tabela i wybor. Odpowiednik behaviors.js. Ruch liczy ruch.cpp.
#include "wewn.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace tw {
static const char* const MUSE[6] = {"muse_inward", "muse_hope", "muse_skeptic", "muse_wonder", "muse_focus", "muse_insight"};

/* domkniecia: wlasna os czasu zachowania */
static const Domk u_awaken = [](double* T, Glowa& H, double tt, Ruch& A, const Ctx&, double) { const double o = sm(.45, 1.9, tt); T[lidL] += 1 - o; T[lidR] += 1 - o; H.pitch += (1 - sm(.2, 1.6, tt)) * .11; T[browL] += env(tt, .9, 1.5, 1.9, 2.6) * .34; T[browR] += env(tt, .9, 1.5, 1.9, 2.6) * .34; A.pupilAdd = .55 * (1 - sm(1.4, 2.6, tt)); T[smileL] += sm(1.7, 2.5, tt) * .34; T[smileR] += sm(1.8, 2.6, tt) * .34; if (tt < .2) A.lk = 0; if (tt > 1.5 && !A.lk) { A.lk = 1; A.look(0, 0, 2); } };
static const Domk u_dissolve = [](double* T, Glowa& H, double tt, Ruch&, const Ctx&, double) { const double c = sm(.1, 1.1, tt); T[lidL] += c; T[lidR] += c; H.pitch += c * .08; };
static const Domk u_yawn = [](double* T, Glowa& H, double tt, Ruch&, const Ctx&, double) { const double q = env(tt, .5, 1.7, 2.6, 3.7); T[jaw] += q * .78; T[part] += env(tt, .05, .5, 3.3, 3.9) * .4; T[squintL] += q * .5; T[squintR] += q * .5; T[lidL] += q * .55; T[lidR] += q * .55; T[browL] += q * .32; T[browR] += q * .32; T[noseUpL] += q * .2; T[noseUpR] += q * .2; T[stretchL] += q * .16; T[stretchR] += q * .16; H.pitch -= q * .09; H.roll += q * .04; };
static const Domk u_drowsy = [](double* T, Glowa& H, double, Ruch&, const Ctx&, double t) { const double dip = std::pow(.5 + .5 * std::sin(t * .47), 5); T[lidL] += dip * .42; T[lidR] += dip * .45; H.pitch += dip * .1; };
static const Domk u_sleep = [](double* T, Glowa& H, double, Ruch&, const Ctx&, double t) { H.pitch += std::sin(t * .7) * .012; T[part] += (.5 + .5 * std::sin(t * .7)) * .05; };
static const Domk u_eureka = [](double* T, Glowa& H, double tt, Ruch&, const Ctx&, double) { const double a = env(tt, .02, .2, .8, 1.5); T[browL] += a * .62; T[browR] += a * .62; T[wideL] += a * .42; T[wideR] += a * .42; T[smileL] += sm(.2, .7, tt) * .6; T[smileR] += sm(.24, .74, tt) * .62; H.pitch -= env(tt, .02, .24, .5, 1.1) * .08; };
static const Domk u_overheat = [](double* T, Glowa&, double, Ruch&, const Ctx&, double t) { const double p = .5 + .5 * std::sin(t * 2.7); T[jaw] += p * .08; T[flareL] += p * .3; T[flareR] += p * .3; };
static const Domk u_compact = [](double* T, Glowa& H, double, Ruch&, const Ctx&, double t) { const double p = std::pow(.5 + .5 * std::sin(t * 2.2), 2); T[press] += p * .16; H.pitch += p * .012; };
static const Domk u_wink = [](double* T, Glowa& H, double tt, Ruch&, const Ctx&, double) { const double w = env(tt, .05, .16, .5, .72); T[lidL] += w; T[squintL] += w * .4; T[browDnL] += w * .3; H.roll += w * .05; };
static const Domk u_dizzy = [](double*, Glowa& H, double, Ruch& A, const Ctx&, double t) { A.gz.tx = std::cos(t * 6) * .62; A.gz.ty = std::sin(t * 6) * .46; A.gz.st = 9; A.xr = .05; H.roll += std::sin(t * 2.1) * .07; H.yaw += std::cos(t * 1.7) * .06; };

static std::vector<Wpis> tabela() { return {
#include "tabela_zachowan.inc"
}; }

/* B.colors: nazwa -> trzy kolory sRGB 0..1 */
struct Pal { const char* n; double c[3][3]; };
static void hexRgb(const char* s, double* o) { unsigned v = 0; std::sscanf(s + 1, "%x", &v); o[0] = (v >> 16 & 255) / 255.0; o[1] = (v >> 8 & 255) / 255.0; o[2] = (v & 255) / 255.0; }
static Pal PAL[6];
static void hex(const double* c, double* o) { for (int i = 0; i < 3; i++) o[i] = std::pow(c[i], 1.9); }
static Pal* pal(const std::string& n) { for (auto& p : PAL) if (n == p.n) return &p; return nullptr; }

Wybor B;
void Face::setColors(const double* a, const double* c, const double* e) { hex(a, tintT); hex(c, tint2T); hex(e ? e : c, eyeT); }
Face::Face() { double a[3], b[3], c[3]; hexRgb("#55dcff", a); hexRgb("#b8f2ff", b); hexRgb("#7be8ff", c); hex(a, tint); hex(b, tint2); hex(c, eyeC); hex(a, tintT); hex(b, tint2T); hex(c, eyeT); std::fill(gl, gl + 64, .8f); }

void startWyboru() { if (!B.list.empty()) return;
  static const char* const KOL[6][4] = {{"accent", "#7fbcff", "#dcefff", "#a9d8ff"}, {"warn", "#ffb545", "#ffe7c4", "#ffc76b"}, {"hot", "#ff8a3c", "#ffd2b0", "#ff9d5c"}, {"hard", "#a98cff", "#e2d8ff", "#c2adff"}, {"bad", "#ff4f5e", "#ffcdd1", "#ff7480"}, {"good", "#5fe8cf", "#d5fff5", "#8ef2df"}};
  for (int i = 0; i < 6; i++) { PAL[i].n = KOL[i][0]; for (int j = 0; j < 3; j++) hexRgb(KOL[i][j + 1], PAL[i].c[j]); }
  B.list = tabela(); B.cur = B.po("absent"); }
void kolorSkorki(const float* ac, const float* rim, const float* eye, const float* warn, const float* bad) { auto w = [](double* o, const float* c) { for (int i = 0; i < 3; i++) o[i] = c[i]; };
  Pal* a = pal("accent"); w(a->c[0], ac); w(a->c[1], rim); w(a->c[2], eye); Pal* n = pal("warn"); w(n->c[0], warn); w(n->c[2], warn); Pal* z = pal("bad"); w(z->c[0], bad); w(z->c[2], bad); }

const Wpis* Wybor::po(const std::string& id) const { for (auto& e : list) if (id == e.id) return &e; return nullptr; }
void Wybor::enter(const Wpis* b) {
  for (const char* c : b->o.on) { if (!std::strcmp(c, "blink2")) { A.blink(1, 1.2); poCzasie(.2, [] { A.blink(1, 1.2); }); } else A.clip(c); }
  if (los() < .45) A.blink(.9, 1.15);
  if (b->o.f.flash == b->o.f.flash && b->o.f.flash != 0) flash = b->o.f.flash;
}
void Wybor::set(const std::string& id, const std::string& w) { if (id == cur->id) { why = w; return; } const Wpis* n = po(id); cur = n ? n : po("idle"); curT = 0; why = w; if (!over) enter(cur); }
void Wybor::play(const std::string& id, double d) { over = po(id); overT = 0; overD = d; if (over) enter(over); }
void Wybor::step(double dt, double t) {
  curT += dt;
  if (over) { overT += dt; if (overT > overD) { over = nullptr; enter(cur); } }
  const Wpis* w = active(); const Opcje& b = w->o; const double tt = over ? overT : curT; const Efekty& e = b.f; Fx& f = F.fx; const double r = std::min(1.0, dt * 3.2);
  A.S = strength; A.step(dt, t, b, tt, ctx, moodT);
  f.glitch += (lub(e.glitch, 0) - f.glitch) * std::min(1.0, dt * 7);
  f.think += (lub(e.think, 0) - f.think) * r; f.dust += (lub(e.dust, 0) - f.dust) * r; f.flick += (lub(e.flick, 0) - f.flick) * r; f.scan += (lub(e.scan, 1) - f.scan) * r;
  f.act += (albo(e.act, .2) - f.act) * std::min(1.0, dt * 2.2);
  orbit(dt, e, f);
  const double rise = albo(e.rise, .55 + f.mat * .3 + f.act * .15); f.rise += (rise - f.rise) * std::min(1.0, dt * 1.6);
  flash = std::max(0.0, flash - dt * 1.6); F.glowAdd = flash * .9;
  Pal* p = pal(w->c); if (!p) p = pal("accent"); F.setColors(p->c[0], p->c[1], p->c[2]);
}
/* wirujace kropki: zapala je myslenie, a miedzy zadaniami krotki pokaz co 45-100 s; im dluzej trwaja, tym szybciej wiruja */
void Wybor::orbit(double dt, const Efekty& e, Fx& f) { orbitIn -= dt; if (orbitIn <= 0 && f.mat > .9) { orbitT = R(7, 11); orbitIn = R(45, 100); }
  orbitT = std::max(0.0, orbitT - dt); const double on = std::max(lub(e.think, 0), orbitT > 0 ? 1.0 : 0.0); const bool run = on > .05;
  spinT = run ? spinT + dt : 0; const double v = run ? .7 + std::min(spinT, 14.0) * .75 : .35;
  spinV += (v - spinV) * std::min(1.0, dt * (run ? 1.1 : .7)); f.spin += spinV * dt; f.orbit += (on - f.orbit) * std::min(1.0, dt * 1.6);
}
/* telemetria -> ciagly nastroj (obok wybranego zachowania) */
void Wybor::sense(const Praca& s) { Nastr& m = moodT; const double tg = s.tg, tgAvg = s.tgAvg, heat = s.rules.cieplo, slow = tgAvg != 0 && tg != 0 ? clamp(1 - tg / tgAvg) : 0, hot = clamp((s.gpuTemp - 60.0) / std::max(1.0, heat - 60));
  m.stress = clamp(std::max({slow * 1.2, hot * .6, ((double)s.ctx - .7) * 2.5, ((double)s.vram - .93) * 12, s.queue * .1})); m.tired = clamp((double)s.el / std::max(1.0, (double)s.rules.dlugo) * .7 + (double)s.idleS / 1800 * .5);
  m.eff = clamp(s.phase == "thinking" ? .2 + (double)s.el / std::max(1.0, (double)s.rules.trudno) * .5 : s.phase == "reading" ? .14 : 0); m.val = .1 - m.stress * .3; m.aro = .35 + (s.phase == "idle" ? 0 : .12);
}
/* zdarzenia bez wyzwalacza z serwera: krotkie wstawki miedzy fazami */
void Wybor::spont(const Praca& s, double dt) { if (over) return; auto p = [&](double perMin) { return los() < perMin / 60 * dt; };
  if (s.status != "ready" || s.down) return;
  if (s.phase == "idle") { if (s.idleS > 200 && s.idleS < 600 && p(1.2)) play("yawn", 3.9); else if (p(1.6)) play("glance_panel", 1.7); }
  if (s.phase == "thinking" && p(3)) play("doubt", 2.4);
  if (s.phase == "answering" && p(5)) play("speak_pause", .7);
}
static std::string f0(double v) { char b[32]; std::snprintf(b, sizeof b, "%.0f", std::floor(v + .5)); return b; }
static std::string fg(double v) { char b[32]; std::snprintf(b, sizeof b, "%g", v); return b; }
/* dane modelu -> zachowanie + powod */
std::pair<std::string, std::string> Wybor::choose(const Praca& s, int L) {
  sense(s); const double el = s.el, hard = s.rules.trudno, ctx_ = s.ctx;
  if (s.down) return {"shock", L == 1 ? "serwer nie odpowiada" : "server not responding"};
  if (s.status == "none") return {"absent", ""};
  if (s.status == "loading") return {"materialize", f0((double)s.load * 100) + "%"};
  if (s.status == "stopping") return {"dissolve", ""};
  if (s.justReady > 0) return {"awaken", ""};
  if (s.gpuTemp >= s.rules.cieplo) return {"overheat", "GPU " + f0(s.gpuTemp) + " °C ≥ " + fg(s.rules.cieplo) + " °C"};
  if (s.phase == "compact") return {"compact", ""};
  if (s.phase == "reading") {
    if ((double)s.cacheFrac > .7) return {"read_fast", "cache " + f0((double)s.cacheFrac * 100) + "%"};
    if (s.promptTok > 50000) return {"read_long", f0(s.promptTok / 1000.0) + "K tok"};
    return {s.code ? "read_squint" : std::fmod(el, 30) > 22 ? "read_again" : "read", ""};
  }
  if (s.phase == "thinking") {
    if (el > hard) return {"strain", f0(el) + " s ≥ " + fg(hard) + " s"};
    if (el > hard * .55) return {"think_deep", f0(el) + " s"};
    if (F.look == "cyber") return {MUSE[(long)std::floor(el / 4.6) % 6], ""};
    const double q = std::fmod(el, 28); return {q < 8 ? "think" : q < 13 ? "think_scan" : q < 18 ? "think_recall" : q < 23 ? "think_compare" : "think_hmm", ""};
  }
  if (s.phase == "tool") { const bool szuka = s.tool.find("search") != std::string::npos || s.tool.find("grep") != std::string::npos || s.tool.find("web") != std::string::npos;
    return {el > 3 ? (szuka ? "tool_search" : "tool_wait") : "tool_call", s.tool}; }
  if (s.phase == "answering") {
    const double slow = s.tgAvg != 0 ? 1 - (double)s.tg / s.tgAvg : 0;
    if (slow * 100 >= s.rules.slow) return {"worried", "TG −" + f0(slow * 100) + "%"};
    if (el > s.rules.dlugo) return {"tired", f0(el) + " s"};
    if (s.queue >= 3) return {"hurried", std::to_string(s.queue)};
    if (ctx_ * 100 >= s.rules.kontekst) return {"overwhelmed", f0(ctx_ * 100) + "%"};
    if (s.users > 1) return {"multi_user", ""};
    if ((double)s.acc > .86) { char b[32]; std::snprintf(b, sizeof b, "accept %.2f", (double)s.acc); return {"confident", b}; }
    return {s.code ? "speak_code" : s.kind == "chat" ? "speak_explain" : std::fmod(el, 40) > 30 ? "speak_list" : "speak", ""};
  }
  if ((double)s.vram >= .97) return {"vram_tense", "VRAM " + f0((double)s.vram * 100) + "%"};
  if (s.idleS > 1800) return {"sleep", ""};
  if (s.idleS > 600) return {"drowsy", ""};
  if (follow && s.mouseRecent) return {std::hypot(ctx.mx, ctx.my) < .32 ? "curious" : "follow", ""};
  return {s.idleS > 120 ? "daydream" : s.idleS > 40 ? "micro_smile" : "idle", ""};
}
}

namespace Zach {
using namespace tw;
static std::vector<Zachowanie> L; static std::vector<std::pair<std::string, std::array<std::string, 2>>> G; static Zachowanie Z0;
const std::vector<Zachowanie>& lista() { if (L.empty()) { startWyboru(); for (auto& e : B.list) L.push_back({e.id, e.g, {e.n[0], e.n[1]}, {e.w[0], e.w[1]}, e.c}); } return L; }
const std::vector<std::pair<std::string, std::array<std::string, 2>>>& grupy() { if (G.empty()) G = {{"life", {"Lifecycle", "Cykl życia"}}, {"idle", {"Idle", "Spoczynek"}}, {"read", {"Reading", "Czytanie"}}, {"think", {"Thinking", "Myślenie"}}, {"speak", {"Speaking", "Mówienie"}}, {"tool", {"Tools", "Narzędzia"}}, {"stress", {"Pressure", "Presja"}}, {"fault", {"Faults", "Awarie"}}, {"touch", {"Touch", "Dotyk"}}}; return G; }
void graj(const std::string& id, float s) { startWyboru(); if (B.po(id)) B.play(id, s); }
const Zachowanie& aktywne() { auto& l = lista(); const Wpis* a = B.active(); return l[(size_t)(a - B.list.data())]; }
const std::string& powod() { return B.why; }
bool nadpisane() { return B.over != nullptr; }
bool& podaza() { return B.follow; }
float& sila() { return B.strength; }
std::array<float, 3> kolor(const std::string& n) { startWyboru(); Pal* p = pal(n); if (!p) p = pal("accent"); return {(float)p->c[0][0], (float)p->c[0][1], (float)p->c[0][2]}; }
std::array<float, 3> kolorAktywnego() { startWyboru(); return kolor(B.active()->c); }
}
