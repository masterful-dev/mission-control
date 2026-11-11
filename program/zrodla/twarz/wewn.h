#pragma once
// Stan wspolny silnika twarzy. Face = obiekt Face z face.js, Ruch = Anim z anim.js, Wybor = B z behaviors.js.
#include "twarz.h"
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>

namespace tw {
constexpr double X = std::numeric_limits<double>::quiet_NaN();   // undefined z makiety
inline double albo(double v, double d) { return v == v ? v : d; }              // v ?? d
inline double lub(double v, double d) { return v == v && v != 0 ? v : d; }     // v || d

double los();                                     // Math.random
void ziarno(uint32_t z);
inline double R(double a, double b) { return a + los() * (b - a); }
inline double clamp(double x, double a = 0, double b = 1) { return x < a ? a : x > b ? b : x; }
inline double sm(double a, double b, double x) { double t = clamp((x - a) / (b - a)); return t * t * (3 - 2 * t); }
inline double mix(double a, double b, double k) { return a + (b - a) * k; }
inline double env(double t, double a, double b, double c, double d) { return sm(a, b, t) * (1 - sm(c, d, t)); }
double noise(double t);
void poCzasie(double sek, std::function<void()> f);   // setTimeout liczony z T

#define KANALY_JEDEN(X) X(jaw) X(part) X(oh) X(ooh) X(kiss) X(press) X(roll) X(platysma) X(noseSq) X(noseDn)
#define KANALY_PARY(X, s) X(smile##s) X(grin##s) X(lowT##s) X(frown##s) X(stretch##s) X(pout##s) X(lid##s) X(squint##s) X(wide##s) X(browDn##s) X(brow##s) X(browIn##s) X(browOut##s) X(noseUp##s) X(flare##s)
#define KAN(n) n,
enum Kanal : int { KANALY_JEDEN(KAN) KANALY_PARY(KAN, L) KANALY_PARY(KAN, R) NK };
#undef KAN
constexpr int DP = smileR - smileL;               // odleglosc kanalu L od R

enum Tryb { W_user, W_none, W_read, W_skim, W_think, W_recall, W_upR, W_upL, W_sideR, W_search, W_deep, W_down, W_side, W_panel, W_pingpong, W_dart, W_floor, W_cursor };
struct Glowa { double yaw = 0, pitch = 0, roll = 0; };
struct Ctx { double mx = 0, my = 0, speed = .5; };
struct Nastr { double val = X, aro = X, eff = X, tired = X, stress = X; };
struct Efekty { double rise = X, glitch = X, think = X, dust = X, flick = X, scan = X, act = X, proj = X, flash = X; };
struct Ruch;
using Domk = void (*)(double* T, Glowa& H, double tt, Ruch& A, const Ctx& c, double t);
struct Opcje {
  Tryb look = W_user; double sp = X, hk = X; Glowa head; double talk = X, rate = X, b = X, sway = X, xr = X, pupil = X; bool fast = false, quiet = false;
  Nastr m; Efekty f; std::vector<std::pair<const char*, double>> ges; std::vector<const char*> on; Domk u = nullptr;
};
struct Wpis { const char* id; const char* g; const char* n[2]; const char* w[2]; const char* c; std::vector<std::pair<int, double>> P; Opcje o; };

struct Klip { double a = X, h = X, r = X, d = X; std::vector<std::pair<int, double>> ch; Glowa head; bool maGlowe = false; Glowa (*fn)(double) = nullptr; };
struct Biegnacy { const Klip* c; double t, gain, d; };
struct Mrug { double t, a, c, h, o; };
struct Spr { double x[NK] = {}, v[NK] = {}; };
struct Seg { char v; double d, l; };

struct Ruch {
  Nastr mood{.1, .35, 0, 0, 0};
  Spr pos, spk; double hsx = 0, hsv = 0, hfx = 0, hfv = 0;
  std::vector<Biegnacy> clips;
  struct { double x = 0, y = 0, tx = 0, ty = 0, sx = 0, sy = 0, st = 1, dur = .05, hold = 1, away = 0, col = -.6, line = 0, side = 0, pp = 0; bool maPp = false; } gz;
  Tryb mode = W_user;
  std::vector<Mrug> blinks; double blinkIn = 2, microIn = 4, breath = 0, bt = 0;
  struct { double amp = 0, t = 0, left = 0, pause = 0, level = 0, stress = 0; bool big = false, maSeg = false; Seg seg{}; std::vector<Seg> q; const double* f = nullptr; } sp;
  double S = 1, drx = 0, dry = 0, pupilAdd = 0, xr = 0; int lk = 0;

  void clip(const char* nazwa, double gain = 1);
  void blink(double a = 1, double speed = 1);
  void look(double x, double y, double hold = .6);
  void saccade(double x, double y);
  void setMode(Tryb m);
  void gaze(double dt, const Opcje& b, const Ctx& ctx);
  double blinks_(double dt, const Opcje& b);
  std::vector<Seg> syll();
  void speech(double dt, const Opcje& b, const Ctx& ctx, double* out);
  void bands(double dt, double act);
  void moodPose(double* T);
  void micro(double dt, const Opcje& b);
  void step(double dt, double t, const Opcje& b, double tt, const Ctx& ctx, const Nastr& moodT);
};
extern Ruch A;
void wiazKsztalty(const std::vector<std::string>& nazwy);   // Face.AU: kanaly -> numery ksztaltow

struct Fx { double rise = .1, mat = 0, glitch = 0, think = 0, orbit = 0, spin = 0, dust = 0, flick = 0, scan = 1, voice = 0, proj = .25, breath = 0, ctx = 0, tg = 0, act = 0, flow = 0; };
struct Face {
  std::vector<double> AU;
  struct { double x = 0, y = 0, pupil = 0, xr = 0, yr = 0; } eye;
  Glowa head;
  Twarz::Uzytkownik user;
  float bands[32] = {};
  Fx fx;
  double tint[3], tint2[3], eyeC[3], tintT[3], tint2T[3], eyeT[3];
  struct { double contours = 1, fine = 1, points = .5, glass = .6, glow = 1, scan = .5, density = 1, brows = 0; } style;
  struct { double cx = .5, cy = .46, s = 1; } view, viewT;
  bool maClip = false; double clip[2] = {0, 1}; bool podstawa = true, wlasneTlo = true, pokoj = true;
  bool ok = false, previewMat = false, maPointer = false;
  std::string look = "holo";
  float gl[64];
  double eyeLevel = .8, rip[4] = {0, 0, 0, 0}, eyeUp = 0, glowAdd = 0, px = 1, sw = 0;
  struct { double x = 0, y = 0, k = 0; bool on = false; } pointer;
  double tloA[3] = {0, 0, 0}, tloB[3] = {0, 0, 0};
  void setColors(const double* a, const double* c, const double* e);
  Face();
};
extern Face F;

/* zachowania i wybor */
struct Wybor {
  std::vector<Wpis> list;
  const Wpis* cur = nullptr; double curT = 0; const Wpis* over = nullptr; double overT = 0, overD = 0; std::string why;
  Ctx ctx; float strength = 1; bool follow = true; double flash = 0; Nastr moodT{.1, .35, 0, 0, 0};
  double orbitIn = 12, orbitT = 0, spinT = 0, spinV = .35;

  const Wpis* po(const std::string& id) const;
  void enter(const Wpis* b);
  void set(const std::string& id, const std::string& why);
  void play(const std::string& id, double d = 6);
  const Wpis* active() const { return over ? over : cur; }
  void step(double dt, double t);
  void orbit(double dt, const Efekty& e, Fx& f);
  void sense(const Praca& s);
  void spont(const Praca& s, double dt);
  std::pair<std::string, std::string> choose(const Praca& s, int jezyk);
};
extern Wybor B;
void kolorSkorki(const float* accent, const float* rim, const float* eye, const float* warn, const float* bad);
void startWyboru();

/* swiatla twarzy cyber */
void swiatlaKrok(double dt, double t);
void swiatlaFala(double x, double y);
void swiatlaPraca(const std::string& status, const std::string& faza, double tg);

/* render */
bool rysInit(const std::string& katalog, bool obraz);
void rysKoniec();
void rysKlatka(double dt, double T, int W, int H);
void rysWyglad(const std::string& id);
const std::string& rysWybrany();
const double* rysMysz();
}
