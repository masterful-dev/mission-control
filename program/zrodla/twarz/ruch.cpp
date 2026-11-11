// Silnik ruchu twarzy: odpowiednik anim.js. Co klatke sklada ruch z warstw:
// poza zachowania + nastroj + gesty + mowa + wzrok + mruganie + oddech + mikroruchy. Wyjscie: F.AU, F.eye, F.head, F.bands.
#include "wewn.h"
#include <algorithm>
#include <map>
#include <string>

namespace tw {
static double rnd(double a) { double x = std::sin(a * 127.1 + 311.7) * 43758.5453; return x - std::floor(x); }
double noise(double t) { double i = std::floor(t), f = t - i, u = f * f * (3 - 2 * f); return (rnd(i) * (1 - u) + rnd(i + 1) * u) * 2 - 1; }
static const char* pick(std::initializer_list<const char*> a) { return a.begin()[(size_t)std::floor(los() * a.size())]; }
static double pickD(double a, double b) { return std::floor(los() * 2) == 0 ? a : b; }

/* kanaly: mieszanka ksztaltow mimiki ze skanow (nazwy ARKit). L = prawa strona ekranu (+x) */
using Mapa = std::vector<std::pair<std::string, double>>;
static Mapa SHAPE[NK];
static void budujKanaly() {
  SHAPE[jaw] = {{"jawOpen", .62}}; SHAPE[part] = {{"mouthLowerDown_L", .55}, {"mouthLowerDown_R", .55}, {"mouthUpperUp_L", .24}, {"mouthUpperUp_R", .24}};
  SHAPE[oh] = {{"mouthFunnel", .85}}; SHAPE[ooh] = {{"mouthFunnel", .42}, {"mouthPucker", .58}}; SHAPE[kiss] = {{"mouthPucker", .92}};
  SHAPE[press] = {{"mouthPress_L", .9}, {"mouthPress_R", .9}}; SHAPE[roll] = {{"mouthRollLower", .8}, {"mouthRollUpper", .55}};
  SHAPE[platysma] = {{"mouthStretch_L", .3}, {"mouthStretch_R", .3}, {"mouthFrown_L", .24}, {"mouthFrown_R", .24}};
  SHAPE[noseSq] = {{"noseSneer_L", .5}, {"noseSneer_R", .5}}; SHAPE[noseDn] = {{"mouthShrugUpper", .5}};
  for (int b = 0; b < 2; b++) { const std::string s = b ? "R" : "L"; const int o = b * DP;
    SHAPE[smileL + o] = {{"mouthSmile_" + s, .78}, {"cheekSquint_" + s, .34}, {"mouthDimple_" + s, .1}}; SHAPE[grinL + o] = {{"mouthUpperUp_" + s, .7}, {"mouthSmile_" + s, .34}};
    SHAPE[lowTL + o] = {{"mouthLowerDown_" + s, .9}}; SHAPE[frownL + o] = {{"mouthFrown_" + s, .95}}; SHAPE[stretchL + o] = {{"mouthStretch_" + s, .85}}; SHAPE[poutL + o] = {{"mouthShrugLower", .36}, {"mouthShrugUpper", .2}};
    SHAPE[lidL + o] = {{"eyeBlink_" + s, 1}}; SHAPE[squintL + o] = {{"eyeSquint_" + s, .82}, {"cheekSquint_" + s, .42}}; SHAPE[wideL + o] = {{"eyeWide_" + s, .55}};
    SHAPE[browDnL + o] = {{"browDown_" + s, 1}}; SHAPE[browL + o] = {{"browInnerUp_" + s, .55}, {"browOuterUp_" + s, .8}}; SHAPE[browInL + o] = {{"browInnerUp_" + s, 1}}; SHAPE[browOutL + o] = {{"browOuterUp_" + s, 1}};
    SHAPE[noseUpL + o] = {{"noseSneer_" + s, .9}}; SHAPE[flareL + o] = {{"noseFlare_" + s, 1}}; }
}
struct Waga { int k; double w; };
static std::vector<Waga> WAGI[NK];
static int iBlinkL, iBlinkR, iOutL, iInL, iInR, iOutR, iUpL, iUpR, iDnL, iDnR;
void wiazKsztalty(const std::vector<std::string>& nazwy) {
  budujKanaly(); F.AU.assign(nazwy.size(), 0);
  auto nr = [&](const std::string& n) { auto i = std::find(nazwy.begin(), nazwy.end(), n); return i == nazwy.end() ? -1 : (int)(i - nazwy.begin()); };
  for (int k = 0; k < NK; k++) { WAGI[k].clear(); for (auto& [n, w] : SHAPE[k]) { int i = nr(n); if (i >= 0) WAGI[k].push_back({i, w}); } }
  iBlinkL = nr("eyeBlink_L"); iBlinkR = nr("eyeBlink_R"); iOutL = nr("eyeLookOut_L"); iInL = nr("eyeLookIn_L"); iInR = nr("eyeLookIn_R"); iOutR = nr("eyeLookOut_R");
  iUpL = nr("eyeLookUp_L"); iUpR = nr("eyeLookUp_R"); iDnL = nr("eyeLookDown_L"); iDnR = nr("eyeLookDown_R");
}
static void add2(double* T, int k, double v) { T[k] += v; T[k + DP] += v; }

/* sprezyny: [czestosc, tlumienie]. Brwi i glowa maja lekki przerzut, powieki sa ostre */
static void SPR(int k, double& w, double& z) {
  int b = k >= smileL ? (k - smileL) % DP + smileL : k;
  if (b == lidL || b == squintL || b == wideL) { w = 26; z = 1; }
  else if (b == browDnL || b == browL || b == browInL || b == browOutL) { w = 15; z = .64; }
  else if (b == noseUpL || b == flareL || b == noseSq || b == noseDn) { w = 12; z = .8; }
  else { w = 13; z = .84; }
}
/* krok najwyzej 6 ms: stabilna takze przy spadku klatek */
static double spring(double& x, double& v, double t, double w, double z, double dt) {
  const int n = (int)std::ceil(dt / .006); const double h = dt / n;
  for (int i = 0; i < n; i++) { v += (w * w * (t - x) - 2 * z * w * v) * h; x += v * h; }
  return x;
}

/* gesty oscylacyjne */
static Glowa g_nod(double u) { return {0, std::sin(u * 6.283) * .062 * (1 - u), 0}; }
static Glowa g_nod2(double u) { return {0, std::sin(u * 12.566) * .055 * (1 - u * .6), 0}; }
static Glowa g_nodSmall(double u) { return {0, std::sin(u * 3.1416) * .024, 0}; }
static Glowa g_beat(double u) { return {0, std::sin(u * 3.1416) * .03, std::sin(u * 3.1416) * .008}; }
static Glowa g_shake(double u) { return {std::sin(u * 15.7) * .065 * (1 - u), 0, 0}; }
static Glowa g_shakeSmall(double u) { return {std::sin(u * 12.566) * .028 * (1 - u), 0, 0}; }
static const std::map<std::string, Klip> CLIPS = {
#include "tabela_gestow.inc"
};

/* mowa: wizemy w kanalach; przejscia daje sprezyna + zapowiedz kolejnej glowski */
using Wiz = std::vector<std::pair<int, double>>;
static const Wiz* VIS(char z) {
  static const Wiz a{{jaw, .4}, {part, .3}, {stretchL, .06}, {stretchR, .06}}, e{{jaw, .2}, {part, .34}, {stretchL, .3}, {stretchR, .3}, {grinL, .08}, {grinR, .08}},
    i{{jaw, .08}, {part, .34}, {stretchL, .46}, {stretchR, .46}, {grinL, .12}, {grinR, .12}}, o{{jaw, .3}, {part, .25}, {oh, .22}, {kiss, .16}},
    u{{jaw, .1}, {part, .22}, {ooh, .34}, {kiss, .4}}, y{{jaw, .13}, {part, .3}, {stretchL, .12}, {stretchR, .12}},
    m{{press, .34}}, f{{jaw, .05}, {roll, .42}, {lowTL, .05}, {lowTR, .05}}, s{{jaw, .05}, {part, .4}, {stretchL, .24}, {stretchR, .24}, {grinL, .12}, {grinR, .12}},
    t{{jaw, .09}, {part, .34}, {stretchL, .12}, {stretchR, .12}}, l{{jaw, .2}, {part, .3}}, k{{jaw, .15}, {part, .24}}, r{{jaw, .12}, {part, .28}, {kiss, .08}}, c{{jaw, .08}, {part, .36}, {kiss, .24}};
  switch (z) { case 'a': return &a; case 'e': return &e; case 'i': return &i; case 'o': return &o; case 'u': return &u; case 'y': return &y; case 'm': return &m; case 'f': return &f;
    case 's': return &s; case 't': return &t; case 'l': return &l; case 'k': return &k; case 'r': return &r; default: return &c; }
}
static const char VOW[] = "aaaeeeiioooyu", CON[] = "ttttsskkmmmllrrffct";
constexpr int NVOW = sizeof VOW - 1, NCON = sizeof CON - 1;
/* formanty: [F1, F2] jako numer pasma, spolgloski: szum wysoko */
static const double* FORM(char c) {
  static const double a[2] = {8, 13}, e[2] = {5, 19}, i[2] = {3, 24}, o[2] = {6, 9}, u[2] = {3, 7}, y[2] = {4, 15}, m[2] = {2, 4}, f[2] = {20, 27}, s[2] = {24, 29}, t[2] = {16, 26}, l[2] = {5, 12}, k[2] = {11, 20}, r[2] = {6, 14}, cc[2] = {21, 27};
  switch (c) { case 'a': return a; case 'e': return e; case 'i': return i; case 'o': return o; case 'u': return u; case 'y': return y; case 'm': return m; case 'f': return f;
    case 's': return s; case 't': return t; case 'l': return l; case 'k': return k; case 'r': return r; default: return cc; }
}

Ruch A;

void Ruch::clip(const char* nazwa, double gain) { auto i = CLIPS.find(nazwa); if (i == CLIPS.end()) return; const Klip& c = i->second; clips.push_back({&c, 0, gain, lub(c.d, c.a + c.h + c.r)}); }
void Ruch::blink(double a, double speed) { blinks.push_back({0, a, .075 / speed, .028 / speed, .17 / speed}); }
void Ruch::look(double x, double y, double hold) { saccade(x, y); gz.hold = hold; }
void Ruch::saccade(double x, double y) { auto& g = gz; const double amp = std::hypot(x - g.x, y - g.y); g.sx = g.x; g.sy = g.y; g.tx = x; g.ty = y; g.st = 0; g.dur = .022 + .085 * amp;
  if (amp > .55 && los() < .62) { double a = .78 + los() * .22; blink(a, 1.2); } }
void Ruch::setMode(Tryb m) { if (m != mode) { mode = m; gz.hold = 0; gz.away = 0; } }

/* wzrok: kazdy tryb zwraca kolejny punkt fiksacji [x, y, czas] */
struct Fiks { double x, y, h; };
static Fiks GZ(Tryb tryb, Ruch& A, double sp) {
  auto& g = A.gz;
  switch (tryb) {
    case W_none: return {g.tx, g.ty, 1};
    case W_read:   /* czytanie: skoki w prawo, powroty do poczatku wiersza, czasem cofniecie */
      if (g.col > .55) { g.col = R(-.66, -.54); g.line = std::fmod(g.line + 1, 8); return {g.col, .12 - g.line * .07, R(.2, .3) / sp}; }
      if (los() < .11) g.col -= R(.1, .24); else g.col += R(.13, .27);
      return {g.col, .12 - g.line * .07 + R(-.008, .008), R(.15, .28) / sp};
    case W_skim: g.line = std::fmod(g.line + 1, 9); return {R(-.34, .34), .3 - g.line * .08, R(.1, .19) / sp};   /* przelatywanie: pionowo w dol, malo fiksacji */
    case W_think: { const double s = lub(g.side, 1); g.side = los() < .28 ? -s : s; return {g.side * R(.42, .68), R(.34, .62), R(1, 2.6)}; }   /* wzrok ucieka w gore, na bok, trzyma punkt */
    case W_recall: return {-R(.5, .72), R(.42, .66), R(.8, 2)};
    case W_upR: return {R(.46, .6), R(.4, .54), R(1.4, 3)};
    case W_upL: return {-R(.46, .6), R(.4, .54), R(1.4, 3)};
    case W_sideR: return {R(.6, .78), R(-.04, .08), R(1.2, 2.6)};
    case W_search: return {R(-.72, .72), R(-.1, .62), R(.14, .36) / sp};
    case W_deep: return {R(-.12, .12), R(-.5, -.34), R(1.6, 3.4)};
    case W_down: return {R(-.34, .34), R(-.62, -.44), R(.18, .5)};
    case W_side: return {R(.7, .9), R(-.26, -.1), R(.4, 1)};
    case W_panel: if (!g.maPp) { g.pp = pickD(-1, 1); g.maPp = true; } return {g.pp * R(.74, .9), R(-.2, .22), R(.5, 1)};
    case W_pingpong: g.side = -lub(g.side, 1); return {g.side * R(.56, .72), R(-.1, .1), R(.7, 1.1)};
    case W_dart: return {R(-.72, .72), R(-.34, .32), R(.12, .34) / sp};
    case W_floor: return {R(-.3, .3), -R(.5, .72), R(.8, 1.8)};
    default:
      if (g.away != 0) { g.away = 0; return {R(-.05, .05), R(-.04, .04), R(1.8, 4.6)}; }
      if (los() < .2) { g.away = 1; const double s = pickD(-1, 1); return {s * R(.25, .55), R(-.34, .26), R(.4, 1.1)}; }
      return {R(-.07, .07), R(-.05, .05), R(.7, 1.9)};
  }
}

void Ruch::gaze(double dt, const Opcje& b, const Ctx& ctx) { auto& g = gz; const Nastr& m = mood; g.hold -= dt; g.st += dt;
  if (mode == W_cursor) { const double d = std::hypot(ctx.mx - g.tx, ctx.my - g.ty); if (d > .3) saccade(clamp(ctx.mx, -1, 1), clamp(ctx.my, -.8, .8)); else { g.tx += (clamp(ctx.mx, -1, 1) - g.tx) * std::min(1.0, dt * 9); g.ty += (clamp(ctx.my, -.8, .8) - g.ty) * std::min(1.0, dt * 9); } }
  else if (g.hold <= 0) { const double sp = lub(b.sp, 1) * (1 + (ctx.speed - .5) * .5) * (1 - m.tired * .4) * (1 + m.stress * .3); const Fiks q = GZ(mode, *this, sp); saccade(q.x, q.y); g.hold = q.h; }
  if (g.st < g.dur) { const double u = g.st / g.dur, e = 1 - std::pow(1 - u, 3); g.x = mix(g.sx, g.tx, e); g.y = mix(g.sy, g.ty, e); } else { g.x += (g.tx - g.x) * std::min(1.0, dt * 20); g.y += (g.ty - g.y) * std::min(1.0, dt * 20); }
  /* mikrosakady i drgania fiksacji */
  drx = noise(bt * 2.3) * .012; dry = noise(bt * 2.1 + 7) * .011;
}

double Ruch::blinks_(double dt, const Opcje& b) { const Nastr& m = mood; blinkIn -= dt * albo(b.b, 1) * (1 + m.stress * .7 + (m.aro > .6 ? .3 : 0));
  if (blinkIn <= 0) { const double q = los(), slow = m.tired > .4 ? .55 : 1; blink(q < .16 ? .62 : 1, slow); if (q > .9) poCzasie(.24, [slow] { A.blink(1, slow); }); blinkIn = R(2.1, 5.6); }
  double v = 0; size_t n = 0;
  for (size_t i = 0; i < blinks.size(); i++) { Mrug& k = blinks[i]; k.t += dt; const double t = k.t, c = k.c, h = k.h, o = k.o, a = k.a; double e;
    if (t < c) e = std::pow(t / c, 1.4); else if (t < c + h) e = 1; else if (t < c + h + o) { const double u = (t - c - h) / o; e = 1 - u * u * (3 - 2 * u); } else continue;
    v = std::max(v, e * a); blinks[n++] = k; }
  blinks.resize(n);
  return v;
}

/* losowy syllabowy strumien mowy z frazami, akcentem i oddechem */
std::vector<Seg> Ruch::syll() { std::vector<Seg> q;
  if (los() < .78) { const char v = CON[(int)std::floor(los() * NCON)]; const double d = R(.05, .085); q.push_back({v, d, .32}); }
  { const char v = VOW[(int)std::floor(los() * NVOW)]; const double d = R(.085, .155); q.push_back({v, d, 1}); }
  if (los() < .22) { const char v = CON[(int)std::floor(los() * NCON)]; const double d = R(.04, .07); q.push_back({v, d, .3}); }
  return q;
}
void Ruch::speech(double dt, const Opcje& b, const Ctx& ctx, double* out) { auto& p = sp; const double want = lub(b.talk, 0), rate = lub(b.rate, 1) * mix(.82, 1.25, ctx.speed);
  p.amp += (want - p.amp) * std::min(1.0, dt * 9);
  double tgt[NK] = {}, lev = 0; const double* form = nullptr;
  if (want > .02) {
    if (p.pause > 0) { p.pause -= dt; if (p.pause <= 0) { if (p.big) clip("inhale", .8); clip("beat", .8); p.left = 5 + std::floor(los() * 10); p.q.clear(); } }
    else {
      p.t += dt * rate;
      if (!p.maSeg || p.t >= p.seg.d) { p.t = 0;
        if (p.q.empty()) {
          if (p.left <= 0) { p.big = los() < .45; p.pause = p.big ? R(.32, .62) : R(.13, .26); p.maSeg = false; if (los() < .55) blink(1, 1); if (los() < .2) clip(pick({"microSmile", "lipPress", "nodSmall"})); }
          else { p.left--; p.q = syll(); p.stress = los() < .2 ? 1 : 0; if (p.stress != 0) { const double r = los(); clip(r < .4 ? "browFlash" : r < .75 ? "beat" : "nodSmall", .85); } } }
        if (!p.q.empty()) { p.seg = p.q.front(); p.q.erase(p.q.begin()); p.maSeg = true; } }
      if (p.maSeg) { const Seg* n = p.q.empty() ? nullptr : &p.q[0]; const double u = p.t / p.seg.d; const Wiz* cur = VIS(p.seg.v); const Wiz* nxt = n ? VIS(n->v) : nullptr; const double w = nxt ? sm(.5, 1, u) * .75 : 0, g = (1 + p.stress * .32) * (.86 + noise(bt * 3) * .12);
        for (auto& [k, v] : *cur) tgt[k] = v * (1 - w) * g;
        if (nxt) for (auto& [k, v] : *nxt) tgt[k] = tgt[k] + v * w * g;
        lev = p.seg.l * (.74 + .26 * p.stress); form = FORM(p.seg.v); }
    }
  }
  for (int k = 0; k < NK; k++) { const double t = tgt[k] * p.amp; out[k] += spring(spk.x[k], spk.v[k], t, 42, 1, dt); }
  p.level += (lev * p.amp - p.level) * std::min(1.0, dt * 24); if (form) p.f = form;
}

void Ruch::bands(double dt, double act) { float* Bn = F.bands; auto& p = sp; const double* f = p.f; const double t = bt;
  for (int i = 0; i < 32; i++) { double v = .012 + (.5 + .5 * noise(t * .9 + i * .71)) * (.05 + act * .10);
    if (p.level > .01 && f) { const double a = i - f[0], c = i - f[1]; v += p.level * (std::exp(-a * a / 7) * .85 + std::exp(-c * c / 10) * .6 + .1 * (.5 + .5 * noise(t * 13 + i))); }
    const double b0 = Bn[i], k = v > b0 ? dt * 42 : dt * 8; Bn[i] = (float)(b0 + (v - b0) * std::min(1.0, k)); }
}

void Ruch::moodPose(double* T) { const Nastr& m = mood; const double a = m.aro - .35;
  if (m.val > 0) { add2(T, smileL, m.val * .28); add2(T, squintL, m.val * .09); } else { add2(T, frownL, -m.val * .26); add2(T, browInL, -m.val * .3); }
  if (a > 0) { add2(T, wideL, a * .36); add2(T, browL, a * .16); } else add2(T, lidL, -a * .34);
  add2(T, browDnL, m.eff * .3); add2(T, squintL, m.eff * .15); T[press] += m.eff * .14; T[noseSq] += m.eff * .17;
  add2(T, lidL, m.tired * .34); add2(T, browInL, m.tired * .14); add2(T, frownL, m.tired * .08); T[jaw] += m.tired * .018;
  T[press] += m.stress * .15; T[platysma] += m.stress * .26; add2(T, flareL, m.stress * .28); add2(T, browInL, m.stress * .18); add2(T, wideL, m.stress * .08);
}

void Ruch::micro(double dt, const Opcje& b) { microIn -= dt; if (microIn > 0) return; const Nastr& m = mood; microIn = R(2.6, 8) / (1 + m.stress);
  if (b.quiet) return; const double s = m.stress + m.eff;
  const char* c = pick({"lipPress", "microSmile", "swallow", "flare", "lipRoll", "browUpL", "browUpR", "tiltL", "tiltR", "smirkL", "squintShort"});
  if (s > .7 && los() < .6) c = pick({"worryFlick", "swallow", "lipPress", "furrow", "flare", "microFrown"});
  if (m.val > .35 && los() < .5) c = pick({"microSmile", "smirkR", "smirkL"});
  const double g = R(.35, .75); clip(c, g);
}

/* jedna klatka */
void Ruch::step(double dt, double t, const Opcje& b, double tt, const Ctx& ctx, const Nastr& moodT) {
  if (!F.ok) return; bt = t; Nastr& m = mood;
  double T[NK] = {}; Glowa H{0, 0, .012};
  /* nastroj: wolny, plynny; zachowanie i telemetria ustawiaja cel */
  const double tau = b.fast ? 3 : 1.05;
  auto nast = [&](double& mk, double mt, double mo, double lo) { const bool ma = mt == mt; const double g = (ma ? mt : albo(mo, 0)) + (ma ? lub(mo, 0) * .4 : 0); mk += (clamp(g, lo, 1) - mk) * std::min(1.0, dt * tau); };
  nast(m.val, b.m.val, moodT.val, -1); nast(m.aro, b.m.aro, moodT.aro, 0); nast(m.eff, b.m.eff, moodT.eff, 0); nast(m.tired, b.m.tired, moodT.tired, 0); nast(m.stress, b.m.stress, moodT.stress, 0);
  const Wpis* w = B.active(); for (auto& [k, v] : w->P) T[k] += v;
  H.yaw += b.head.yaw; H.pitch += b.head.pitch; H.roll += b.head.roll;
  moodPose(T);
  pupilAdd = 0; xr = albo(b.xr, -.08);
  if (b.u) b.u(T, H, tt, *this, ctx, t);
  /* gesty zachowania: b.ges = {nazwa: razy na minute} */
  for (auto& [k, v] : b.ges) if (los() < v / 60 * dt) { const double g = R(.7, 1.05); clip(k, g); }
  micro(dt, b);
  /* gesty biegnace */
  Glowa Hf;
  size_t n = 0;
  for (size_t i = 0; i < clips.size(); i++) { Biegnacy& q = clips[i]; q.t += dt; const Klip& c = *q.c; if (q.t >= q.d) continue; double e = 1;
    if (c.a == c.a) { if (q.t < c.a) e = sm(0, c.a, q.t); else if (q.t > c.a + c.h) e = 1 - sm(0, c.r, q.t - c.a - c.h); }
    e *= q.gain * S; for (auto& [k, v] : c.ch) T[k] += v * e;
    if (c.fn) { const Glowa h = c.fn(q.t / q.d); Hf.yaw += h.yaw * q.gain; Hf.pitch += h.pitch * q.gain; Hf.roll += h.roll * q.gain; } else if (c.maGlowe) { Hf.yaw += c.head.yaw * e; Hf.pitch += c.head.pitch * e; Hf.roll += c.head.roll * e; }
    clips[n++] = q; }
  clips.resize(n);
  /* wzrok, glowa za oczami */
  setMode(b.look); gaze(dt, b, ctx);
  auto& g = gz; const double hk = albo(b.hk, .2);
  H.yaw += g.tx * .42 * hk * (F.look == "cyber" ? 2.6 : 1); H.pitch -= g.ty * .3 * hk;
  /* zycie: kolysanie, oddech */
  const double sw = albo(b.sway, 1) * (1 + m.stress * .4) * (1 - m.tired * .3);
  H.yaw += noise(t * .29) * .040 * sw; H.pitch += noise(t * .23 + 4) * .022 * sw + m.tired * .055 - m.aro * .012; H.roll += noise(t * .21 + 8) * .022 * sw;
  if (m.stress > .05) { H.yaw += noise(t * 15) * .006 * m.stress; H.pitch += noise(t * 17 + 3) * .005 * m.stress; }
  const double rate = .21 + m.stress * .2 + m.eff * .05; breath += dt * rate * 6.283; const double br = std::sin(breath); F.fx.breath = br; H.pitch += br * .0035;
  T[flareL] += std::max(0.0, br) * (.03 + m.stress * .22); T[flareR] += std::max(0.0, br) * (.03 + m.stress * .22);
  /* sprezyny */
  for (int k = 0; k < NK; k++) { double sw_, sz; SPR(k, sw_, sz); const double as = k == smileR ? 1.08 : k == browL ? .95 : k == frownL ? 1.06 : k == squintR ? 1.05 : 1;
    spring(pos.x[k], pos.v[k], T[k] * (k == lidL || k == lidR || k == jaw ? 1 : S) * as, sw_, sz, dt); }
  /* glowa obraca sie tylko wokol wlasnej osi (HEAD_AXES): pochylenia i przechyly z tabel sa wylaczone */
  const double a = spring(hsx, hsv, H.yaw, b.fast ? 9 : 5.2, .78, dt), c = spring(hfx, hfv, Hf.yaw, 19, .62, dt), HoYaw = a + c, HoPitch = 0; F.head.yaw = HoYaw;
  /* wyjscie: mowa dodaje sie po sprezynach, zeby nie byla przymulona */
  double O[NK]; for (int k = 0; k < NK; k++) O[k] = pos.x[k]; speech(dt, b, ctx, O);
  /* oko w glowie: glowa sie obraca, wzrok zostaje na celu */
  const double ex = clamp(g.x + drx - HoYaw / .42, -1.15, 1.15), eu = F.eyeUp, ey = clamp(g.y + dry + HoPitch / .3 + eu, -1, 1 + eu); auto& E = F.eye;
  E.x = ex; E.y = ey; E.xr = xr; E.yr = 0;
  const double bv = blinks_(dt, b);
  for (int o = 0; o <= DP; o += DP) { O[lidL + o] = mix(clamp(O[lidL + o] + .10 * (1 - clamp(O[wideL + o] * 2)), 0, 1), 1, bv); O[wideL + o] *= 1 - bv; O[squintL + o] += bv * .08; O[browDnL + o] += bv * .05; }
  const double pup = .12 + m.eff * .42 + (m.aro - .35) * .3 + m.stress * .12 - std::max(m.val, 0.0) * .03 + noise(t * .9) * .05 + lub(b.pupil, 0) + pupilAdd;
  E.pupil += (clamp(pup, -.7, 1) - E.pupil) * std::min(1.0, dt * 4.5);
  auto& AU = F.AU; std::fill(AU.begin(), AU.end(), 0.0);
  for (int k = 0; k < NK; k++) { const double v = O[k] < 0 ? 0 : O[k] > 1.15 ? 1.15 : O[k]; if (v < .002) continue; for (auto& q : WAGI[k]) AU[q.k] += q.w * v; }
  /* powieka opada wolniej na poczatku: polprzymkniete oko nie wyglada jak zamkniete */
  AU[iBlinkL] = std::pow(AU[iBlinkL], 1.6); AU[iBlinkR] = std::pow(AU[iBlinkR], 1.6);
  /* powieki ida za okiem: te ruchy sa w skanach */
  const double lx = ex + xr, rx = ex - xr, gk = .8;
  AU[iOutL] = std::max(0.0, lx) * gk; AU[iInL] = std::max(0.0, -lx) * gk; AU[iInR] = std::max(0.0, rx) * gk; AU[iOutR] = std::max(0.0, -rx) * gk;
  AU[iUpL] = AU[iUpR] = std::max(0.0, ey) * gk; AU[iDnL] = AU[iDnR] = std::max(0.0, -ey) * gk;
  bands(dt, F.fx.act); F.fx.voice += (sp.level - F.fx.voice) * std::min(1.0, dt * 20);
}
}
