// Twarz: uklad klatki i interfejs. Odpowiednik frame() z app.js oraz wejscia z faceInput().
#include "wewn.h"
#include <SDL3/SDL.h>
#include <algorithm>

namespace tw {
Face F;
static uint32_t stanLos = 0; static bool zasiane = false; static uint64_t ileLos = 0;
void ziarno(uint32_t z) { stanLos = z; zasiane = true; ileLos = 0; }
double los() { if (!zasiane) ziarno((uint32_t)SDL_GetPerformanceCounter()); ileLos++;
  stanLos += 0x6D2B79F5u; uint32_t t = stanLos; t = (t ^ (t >> 15)) * (1u | t); t = (t + (t ^ (t >> 7)) * (61u | t)) ^ t; return (double)(t ^ (t >> 14)) / 4294967296.0; }

struct Budzik { double kiedy; std::function<void()> f; };
static std::vector<Budzik> budziki; static double teraz = 0;
void poCzasie(double sek, std::function<void()> f) { budziki.push_back({teraz + sek, std::move(f)}); }
static void zegar(double T) { teraz = T;
  for (;;) { auto b = std::min_element(budziki.begin(), budziki.end(), [](const Budzik& x, const Budzik& y) { return x.kiedy < y.kiedy; }); if (b == budziki.end() || b->kiedy > T) break; auto f = std::move(b->f); budziki.erase(b); f(); }
}
}

namespace Twarz {
using namespace tw;
bool init(const std::string& katalog) { startWyboru(); return rysInit(katalog, true); }
bool initBezObrazu(const std::string& katalog) { startWyboru(); return rysInit(katalog, false); }
void koniec() { rysKoniec(); budziki.clear(); }
void ziarno(uint32_t z) { tw::ziarno(z); }
void skala(float px) { F.px = px; }
void ustawStyl(const StylTwarzy& s) { startWyboru(); kolorSkorki(s.accent, s.rim, s.eye, s.warn, s.bad); auto& t = F.style; t.contours = s.contours; t.fine = s.fine; t.points = s.points; t.glass = s.glass; t.glow = s.glow; t.scan = s.scan; t.density = s.density;
  for (int i = 0; i < 3; i++) { F.tloB[i] = s.bg[i]; F.tloA[i] = s.accent[i] * .09 + s.bg[i] * .91; } }
void ustawWyglad(const std::string& id) { rysWyglad(id); }
const std::string& wyglad() { return rysWybrany(); }
Uzytkownik& uzytkownik() { return F.user; }
void widok(float cx, float cy, float s, float clipL, float clipR) { F.viewT.cx = cx; F.viewT.cy = cy; F.viewT.s = s; F.clip[0] = clipL; F.clip[1] = clipR; F.maClip = true; }
void kursor(float x, float y, bool naScenie) { F.maPointer = true; auto& q = F.pointer; q.x = x; q.y = y; q.on = naScenie;
  B.ctx.mx = std::max(-1.0, std::min(1.0, ((double)x - F.view.cx) * 2.1)); B.ctx.my = std::max(-1.0, std::min(1.0, -((double)y - F.view.cy + .1) * 2.1)); }
void kliknieto() { if (F.look == "cyber") { const double* m = rysMysz(); swiatlaFala(m[0], m[1]); } }
float material() { return (float)F.fx.mat; }
void pokazBezModelu() { F.previewMat = true; }
void podstawa(bool o) { F.podstawa = o; }
void otoczenie(bool wlasneTlo, bool pokoj) { F.wlasneTlo = wlasneTlo; F.pokoj = pokoj; }

void klatka(float dt_, double T, const Praca& f, int jezyk, bool pierscienie, int szerPx, int wysPx) { swiatlaPraca(f.status, f.phase, f.tg);
  const double dt = dt_; Fx& fx = F.fx;
  zegar(T);
  const auto [id, why] = B.choose(f, jezyk); B.spont(f, dt); B.ctx.speed = f.tg != 0 ? std::min(1.0, f.tg / 150.0) : .5; B.set(id, why);
  const double targetMat = f.status == "loading" ? f.load : f.status == "ready" ? 1 : f.status == "stopping" ? 0 : (B.over && F.previewMat ? 1 : 0);
  if (!B.over) F.previewMat = false;
  fx.mat += (targetMat - fx.mat) * std::min(1.0, dt * (f.status == "stopping" ? 2.2 : 3.4));
  fx.ctx = pierscienie ? f.ctx : 0; fx.tg = pierscienie ? (f.tg != 0 ? std::min(1.0, f.tg / 200.0) : fx.tg * .96) : 0; fx.proj = albo(B.active()->o.f.proj, f.status == "ready" ? .55 : .3);
  B.step(dt, T);
  rysKlatka(dt, T, szerPx, wysPx);
}
const Stan stan() { Stan s; s.au = F.AU.data(); s.nau = (int)F.AU.size(); s.eye[0] = F.eye.x; s.eye[1] = F.eye.y; s.eye[2] = F.eye.pupil; s.eye[3] = F.eye.xr; s.yaw = F.head.yaw; s.bands = F.bands; s.gl = F.gl;
  const Fx& f = F.fx; const double w[] = {f.rise, f.mat, f.glitch, f.think, f.orbit, f.spin, f.dust, f.flick, f.scan, f.voice, f.proj, f.breath, f.ctx, f.tg, f.act}; std::copy(w, w + 15, s.fx);
  std::copy(F.tint, F.tint + 3, s.kol); std::copy(F.tint2, F.tint2 + 3, s.kol + 3); std::copy(F.eyeC, F.eyeC + 3, s.kol + 6); s.look = F.look.c_str(); s.glowAdd = F.glowAdd; s.eyeLevel = F.eyeLevel; std::copy(F.rip, F.rip + 4, s.rip); s.los = ileLos; s.zach = B.active()->id; s.powod = B.why.c_str(); return s; }
}
