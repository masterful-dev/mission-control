// Render twarzy: odpowiednik face.js. Siatka anatomiczna z ruchami miesni, podzial i normalne na GPU, warstwy:
// tafla i fala glosu, struny swiatla, oczy, usta, skora z kresek, czastki, poswiata.
#include "gl33.h"
#include "wewn.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <map>

namespace tw {
static constexpr int PW = 512, CW = 128, SW = 2048; static constexpr double PI = 3.14159265358979323846;
using M4 = std::array<double, 16>; using M3 = std::array<double, 9>; using V3 = std::array<double, 3>;

#define UNIFORMY(X) X(uPos) X(uNrm) X(uRest) X(uRestN) X(uAttr) X(uAttr2) X(uPore) X(uC) X(uSt) X(uOff) X(uNb) X(uHead) X(uT) X(uBreath) X(uGlitch) X(uVP) X(uCam) X(uTint) X(uTint2) X(uMat) X(uSweep) X(uFlick) X(uThink) X(uBrows) \
  X(uRight) X(uStyle) X(uAct) X(uVoice) X(uEye) X(uRot) X(uR) X(uPupil) X(uKind) X(uIrE) X(uTa) X(uTb) X(uAt) X(uTs) X(uRes) X(uMouse) X(uRip) X(uGl) X(uBlinkL) X(uBlinkR) X(uFloor) X(uRise) X(uMir) X(uBands) \
  X(uOrbit) X(uSpin) X(uDust) X(uPx) X(uN) X(uFill) X(t) X(d) X(uThr) X(s) X(b) X(uGlow) X(uScan) X(uAsp) X(uHz) X(uRoom) X(uA) X(uB) X(uS)
struct Prog { GLuint p = 0;
#define X(n) GLint n = -1;
  UNIFORMY(X)
#undef X
};
struct Cel { GLuint t = 0, f = 0; int w = 0, h = 0; };
struct Czesc { GLuint v = 0; int n = 0; };
struct Oczy { double L[3], R[3], r; };
struct Wyglad { bool ok = false; std::vector<float> P0; Oczy eyes; double pitch = 0, at[4], ts[2]; GLuint ta = 0, tb = 0; double mouse[4] = {0, 0, 0, 0}; };
struct Ksztalt { std::string n; std::vector<uint16_t> i; std::vector<float> d; };

static std::string KAT; static bool OBRAZ = false;
static int W = 0, H = 0;
static struct {
  int n = 0, n0 = 0, ph = 0, ch = 0; const float* P0 = nullptr; std::vector<float> P0h, cp; Oczy eyesH; const Oczy* eyes = nullptr; double lookK = 1; std::vector<Ksztalt> shapes; bool rest = false;
  GLuint tOff = 0, tSt = 0, tNb = 0, tC = 0, tPos = 0, fPos = 0, tNrm = 0, fNrm = 0, tRest = 0, fRest = 0, tRestN = 0, fRestN = 0, tAttr = 0, tAttr2 = 0, tPore = 0, vao0 = 0, vEye = 0, vFib = 0, vNeu = 0, vFon = 0, vFlo = 0, vPart = 0, vLine = 0;
  int nEye = 0, nFib = 0, nNeu = 0, nFon = 0, nPart = 0, nLine = 0; Czesc sk, hole, gum, tooth, wet, occ;
  std::string want; Wyglad* L = nullptr; struct { GLuint f = 0, c = 0, d = 0; } ms; Cel s; std::vector<Cel> mips;
} G;
static struct { Prog st, nr, sk, dp, ey, mo, oc, we, fi, ne, fo, fl, pa, li, dn, up, co, cy, e2, tlo; } pr;
static Wyglad CYBER;
static std::vector<GLuint> zTex, zBuf, zVao, zFbo, zProg;

/* paczka danych z narzedzia/eksport_twarzy.mjs */
struct Paczka { void* d = nullptr; size_t n = 0; std::map<std::string, std::pair<const uint8_t*, size_t>> s;
  bool czytaj(const std::string& plik) { d = SDL_LoadFile(plik.c_str(), &n); if (!d || n < 8 || std::memcmp(d, "TWRZ", 4)) { SDL_Log("twarz: nie moge wczytac %s", plik.c_str()); return false; } const uint8_t* b = (const uint8_t*)d; uint32_t ile; std::memcpy(&ile, b + 4, 4);
    for (uint32_t i = 0; i < ile; i++) { const uint8_t* e = b + 8 + i * 48; uint64_t o, l; std::memcpy(&o, e + 28, 8); std::memcpy(&l, e + 36, 8); s[(const char*)e] = {b + o, (size_t)l}; } return true; }
  template <class T> const T* dane(const char* k, size_t* ile = nullptr) const { auto& e = s.at(k); if (ile) *ile = e.second / sizeof(T); return (const T*)e.first; }
  ~Paczka() { SDL_free(d); } };
static void oczy(const Paczka& p, Oczy& o) { const double* e = p.dane<double>("oczy"); std::copy(e, e + 3, o.L); std::copy(e + 3, e + 6, o.R); o.r = e[6]; }

static GLuint sh(GLenum t, const std::string& s, const char* nazwa) { GLuint x = gl.CreateShader(t); const char* c = s.c_str(); gl.ShaderSource(x, 1, &c, nullptr); gl.CompileShader(x); GLint ok = 0; gl.GetShaderiv(x, GL_COMPILE_STATUS, &ok);
  if (!ok) { char l[4096]; gl.GetShaderInfoLog(x, sizeof l, nullptr, l); SDL_Log("twarz: shader %s: %s", nazwa, l); gl.DeleteShader(x); return 0; } return x; }
static std::string plikShadera(const char* n) { const std::string p = KAT + "/twarz/shadery/" + n + ".glsl"; size_t l = 0; void* d = SDL_LoadFile(p.c_str(), &l); if (!d) { SDL_Log("twarz: brak %s", p.c_str()); return {}; } std::string s((const char*)d, l); SDL_free(d);
  const std::string v = "#version 300 es"; if (s.compare(0, v.size(), v) == 0) s.replace(0, v.size(), "#version 330 core"); return s; }
static bool blad = false;
static Prog progZ(const std::string& vs, const std::string& fs, const char* nv, const char* nf, std::initializer_list<const char*> at = {}) { Prog r; GLuint v = sh(GL_VERTEX_SHADER, vs, nv), f = sh(GL_FRAGMENT_SHADER, fs, nf); if (!v || !f) { blad = true; return r; }
  r.p = gl.CreateProgram(); gl.AttachShader(r.p, v); gl.AttachShader(r.p, f); int i = 0; for (const char* a : at) gl.BindAttribLocation(r.p, i++, a); gl.LinkProgram(r.p); GLint ok = 0; gl.GetProgramiv(r.p, GL_LINK_STATUS, &ok); gl.DeleteShader(v); gl.DeleteShader(f);
  if (!ok) { char l[4096]; gl.GetProgramInfoLog(r.p, sizeof l, nullptr, l); SDL_Log("twarz: program %s+%s: %s", nv, nf, l); blad = true; return r; }
#define X(n) r.n = gl.GetUniformLocation(r.p, #n);
  UNIFORMY(X)
#undef X
  zProg.push_back(r.p); return r; }
static Prog prog(const char* v, const char* f, std::initializer_list<const char*> at = {}) { return progZ(plikShadera(v), plikShadera(f), v, f, at); }
static GLuint tex(int w, int h, GLenum ifmt, GLenum fmt, GLenum type, const void* data, GLenum filt = GL_NEAREST) { GLuint t; gl.GenTextures(1, &t); zTex.push_back(t); gl.BindTexture(GL_TEXTURE_2D, t); gl.TexImage2D(GL_TEXTURE_2D, 0, ifmt, w, h, 0, fmt, type, data); gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filt); gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filt); gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); return t; }
static GLuint fbo(GLuint t) { GLuint f; gl.GenFramebuffers(1, &f); zFbo.push_back(f); gl.BindFramebuffer(GL_FRAMEBUFFER, f); gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t, 0); return f; }
static void bindT(int unit, GLuint t, GLint loc) { gl.ActiveTexture(GL_TEXTURE0 + unit); gl.BindTexture(GL_TEXTURE_2D, t); gl.Uniform1i(loc, unit); }
static GLuint vao() { GLuint v; gl.GenVertexArrays(1, &v); zVao.push_back(v); return v; }
static GLuint buf(GLenum cel, const void* d, size_t bajty) { GLuint b; gl.GenBuffers(1, &b); zBuf.push_back(b); gl.BindBuffer(cel, b); gl.BufferData(cel, bajty, d, GL_STATIC_DRAW); return b; }
static void atr(int i, const std::vector<float>& a, int c) { buf(GL_ARRAY_BUFFER, a.data(), a.size() * 4); gl.EnableVertexAttribArray(i); gl.VertexAttribPointer(i, c, GL_FLOAT, GL_FALSE, 0, nullptr); }
static Czesc czesc(std::initializer_list<const std::vector<uint32_t>*> m) { std::vector<uint32_t> a; for (auto* p : m) a.insert(a.end(), p->begin(), p->end()); Czesc c; c.v = vao(); gl.BindVertexArray(c.v); buf(GL_ELEMENT_ARRAY_BUFFER, a.data(), a.size() * 4); gl.BindVertexArray(0); c.n = (int)a.size(); return c; }

/* faktura porow: mapa gradientow, powtarzalna */
static GLuint poreTex() { constexpr int S = 256; std::vector<float> h(S * S, 0.f), lo(S * S, 0.f); auto wrap = [](int v) { return (v + S) % S; };
  const double fa[3][2] = {{8, .5}, {24, .34}, {64, .24}};
  for (auto& q : fa) { const int f = (int)q[0]; const double a = q[1]; std::vector<float> g(f * f); for (auto& v : g) v = (float)los(); const double c = (double)S / f;
    for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) { const double u = x / c, v = y / c; const int i = (int)std::floor(u), j = (int)std::floor(v); const double p = u - i, r = v - j, s = p * p * (3 - 2 * p), t = r * r * (3 - 2 * r);
      auto at = [&](int a2, int b2) { return (double)g[(b2 % f) * f + (a2 % f)]; }; const double n = a * ((at(i, j) * (1 - s) + at(i + 1, j) * s) * (1 - t) + (at(i, j + 1) * (1 - s) + at(i + 1, j + 1) * s) * t);
      h[y * S + x] = (float)((double)h[y * S + x] + n); if (f < 64) lo[y * S + x] = (float)((double)lo[y * S + x] + n); } }
  for (int k = 0; k < 2600; k++) { const int cx = (int)(los() * S), cy = (int)(los() * S); const double d = .35 + los() * .7, r = 1 + los() * 1.2;
    for (int y = -3; y <= 3; y++) for (int x = -3; x <= 3; x++) { float& e = h[wrap(cy + y) * S + wrap(cx + x)]; e = (float)((double)e - d * std::exp(-(x * x + y * y) / (r * r))); } }
  std::vector<uint8_t> o(S * S * 4); auto u8 = [](double v) { return (uint8_t)(int)std::max(0.0, std::min(255.0, v)); };
  for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) { const double gx = (double)h[y * S + wrap(x + 1)] - h[y * S + wrap(x - 1)], gy = (double)h[wrap(y + 1) * S + x] - h[wrap(y - 1) * S + x]; const int i = (y * S + x) * 4; o[i] = u8(128 + gx * 150); o[i + 1] = u8(128 + gy * 150); o[i + 2] = u8(((double)lo[y * S + x] - .1) * 300); o[i + 3] = 255; }
  GLuint t; gl.GenTextures(1, &t); zTex.push_back(t); gl.BindTexture(GL_TEXTURE_2D, t); gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, S, S, 0, GL_RGBA, GL_UNSIGNED_BYTE, o.data()); gl.GenerateMipmap(GL_TEXTURE_2D); gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT); return t; }

static bool load() { Paczka D; if (!D.czytaj(KAT + "/twarz/holo.bin")) return false; const double* lb = D.dane<double>("liczby"); G.n0 = (int)lb[0]; G.n = (int)lb[1]; const int ile = (int)lb[2]; const int n = G.n, n0 = G.n0; G.ph = (n + PW - 1) / PW; G.ch = (n0 + CW - 1) / CW;
  { size_t l; const float* p = D.dane<float>("pos0", &l); G.P0h.assign(p, p + l); } G.P0 = G.P0h.data(); oczy(D, G.eyesH); G.eyes = &G.eyesH; G.lookK = 1; G.cp.assign((size_t)CW * G.ch * 4, 0.f);
  { auto& e = D.s.at("nazwy"); std::string t((const char*)e.first, e.second); G.shapes.resize(ile); size_t q = 0; std::vector<std::string> nazwy; for (int i = 0; i < ile; i++) { size_t z = t.find('\n', q); G.shapes[i].n = t.substr(q, z == std::string::npos ? z : z - q); q = z + 1; nazwy.push_back(G.shapes[i].n); }
    wiazKsztalty(nazwy); for (int i = 0; i < ile; i++) { size_t a, b; auto k = "s" + std::to_string(i); const uint16_t* pi = D.dane<uint16_t>((k + "i").c_str(), &a); const float* pd = D.dane<float>((k + "d").c_str(), &b); G.shapes[i].i.assign(pi, pi + a); G.shapes[i].d.assign(pd, pd + b); } }
  if (!OBRAZ) return true;
  const int ph = G.ph; size_t nIdx, nNb;
  const uint32_t* off = D.dane<uint32_t>("off"); const uint32_t* noff = D.dane<uint32_t>("noff"); const uint16_t* idx = D.dane<uint16_t>("idx", &nIdx); const float* w = D.dane<float>("w"); const uint32_t* nb = D.dane<uint32_t>("nb", &nNb);
  { std::vector<uint32_t> o4((size_t)PW * ph * 4, 0); for (int i = 0; i < n; i++) { o4[i * 4] = off[i]; o4[i * 4 + 1] = off[i + 1]; o4[i * 4 + 2] = noff[i]; o4[i * 4 + 3] = noff[i + 1]; } G.tOff = tex(PW, ph, GL_RGBA32UI, GL_RGBA_INTEGER, GL_UNSIGNED_INT, o4.data()); }
  { const int sh_ = (int)((nIdx + SW - 1) / SW); std::vector<float> st((size_t)SW * sh_ * 2, 0.f); for (size_t i = 0; i < nIdx; i++) { st[i * 2] = idx[i]; st[i * 2 + 1] = w[i]; } G.tSt = tex(SW, sh_, GL_RG32F, GL_RG, GL_FLOAT, st.data()); }
  { const int nh = (int)((nNb / 2 + SW - 1) / SW); std::vector<uint32_t> nbp((size_t)SW * nh * 2, 0); std::copy(nb, nb + nNb, nbp.begin()); G.tNb = tex(SW, nh, GL_RG32UI, GL_RG_INTEGER, GL_UNSIGNED_INT, nbp.data()); }
  G.tC = tex(CW, G.ch, GL_RGBA32F, GL_RGBA, GL_FLOAT, nullptr);
  G.tPos = tex(PW, ph, GL_RGBA32F, GL_RGBA, GL_FLOAT, nullptr); G.fPos = fbo(G.tPos); G.tNrm = tex(PW, ph, GL_RGBA32F, GL_RGBA, GL_FLOAT, nullptr); G.fNrm = fbo(G.tNrm);
  G.tRest = tex(PW, ph, GL_RGBA32F, GL_RGBA, GL_FLOAT, nullptr); G.fRest = fbo(G.tRest); G.tRestN = tex(PW, ph, GL_RGBA32F, GL_RGBA, GL_FLOAT, nullptr); G.fRestN = fbo(G.tRestN);
  auto u8tex = [&](const char* k) { size_t l; const uint8_t* a = D.dane<uint8_t>(k, &l); std::vector<uint8_t> ap((size_t)PW * ph * 4, 0); std::copy(a, a + l, ap.begin()); return tex(PW, ph, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, ap.data()); };
  G.tAttr = u8tex("attr");
  G.vao0 = vao();
  // material trojkata: 0 skora, 1 jama ust, 2 oczodol, 3 dziasla i jezyk, 4 zeby, 5 lzy, 6 cien powieki
  { size_t nt; const uint32_t* tris = D.dane<uint32_t>("tris"); const uint8_t* tm = D.dane<uint8_t>("tmat", &nt); std::vector<uint32_t> parts[7]; for (size_t i = 0; i < nt; i++) { auto& q = parts[tm[i]]; q.push_back(tris[i * 3]); q.push_back(tris[i * 3 + 1]); q.push_back(tris[i * 3 + 2]); }
    G.sk = czesc({&parts[0]}); G.hole = czesc({&parts[1], &parts[2]}); G.gum = czesc({&parts[3]}); G.tooth = czesc({&parts[4]}); G.wet = czesc({&parts[5]}); G.occ = czesc({&parts[6]}); }
  G.tAttr2 = u8tex("attr2");
  G.tPore = poreTex();
  // kula oka
  { std::vector<float> E; std::vector<uint32_t> EI; const int NU = 64, NV = 48; for (int j = 0; j <= NV; j++) for (int i = 0; i <= NU; i++) { const double th = (double)j / NV * PI, p2 = (double)i / NU * 6.2832; E.push_back((float)(std::sin(th) * std::cos(p2))); E.push_back((float)(std::sin(th) * std::sin(p2))); E.push_back((float)std::cos(th)); }
    for (int j = 0; j < NV; j++) for (int i = 0; i < NU; i++) { const uint32_t a = j * (NU + 1) + i, c = a + NU + 1; for (uint32_t v : {a, c, a + 1, a + 1, c, c + 1}) EI.push_back(v); }
    G.vEye = vao(); gl.BindVertexArray(G.vEye); atr(0, E, 3); buf(GL_ELEMENT_ARRAY_BUFFER, EI.data(), EI.size() * 4); gl.BindVertexArray(0); G.nEye = (int)EI.size(); }
  // wlokna z danych: dzis puste, twarz nie ma brwi ani rzes
  { size_t a, b, c; const float* R_ = D.dane<float>("fibRoot", &a); const float* O = D.dane<float>("fibOff", &b); const float* K = D.dane<float>("fibK", &c); G.nFib = (int)a; if (a) { G.vFib = vao(); gl.BindVertexArray(G.vFib); atr(0, {R_, R_ + a}, 1); atr(1, {O, O + b}, 3); atr(2, {K, K + c}, 2); gl.BindVertexArray(0); } }
  // struny: dol, przegub szyi, gora; wszystko inne liczy shader
  { size_t l; const float* C = D.dane<float>("cols", &l); const int m = (int)l / 4; std::vector<float> P((size_t)m * 12), K((size_t)m * 8); const double FL = -1.32;
    for (int i = 0; i < m; i++) { const double x = C[i * 4], z = C[i * 4 + 1], sd = los(), top = (double)C[i * 4 + 2] + std::pow(los(), 3) * .42 + .02, mid = -.70, bot = FL + los() * .3; const double ys[4] = {bot, mid, mid, top};
      for (int j = 0; j < 4; j++) { float* p = &P[(i * 4 + j) * 3]; p[0] = (float)x; p[1] = (float)ys[j]; p[2] = (float)z; float* k = &K[(i * 4 + j) * 2]; k[0] = (float)sd; k[1] = (float)top; } }
    G.nNeu = m * 4; G.vNeu = vao(); gl.BindVertexArray(G.vNeu); atr(0, P, 3); atr(1, K, 2); gl.BindVertexArray(0); }
  // fontanna fali glosu
  { const int NS = 5600; std::vector<float> Af((size_t)NS * 8), U((size_t)NS * 2, 0.f); auto gs = [] { double u = 0; for (int i = 0; i < 6; i++) u += los(); return (u - 3) / 1.5; };
    for (int i = 0; i < NS; i++) { const double x = gs() * .78, z = gs() * .10, sd = los(), w_ = .5 + los() * .5; const float q[8] = {(float)x, (float)z, (float)sd, (float)w_, (float)x, (float)z, (float)sd, (float)w_}; std::copy(q, q + 8, &Af[(size_t)i * 8]); U[i * 2 + 1] = 1; }
    G.nFon = NS * 2; G.vFon = vao(); gl.BindVertexArray(G.vFon); atr(0, Af, 4); atr(1, U, 1); gl.BindVertexArray(0); }
  { G.vFlo = vao(); gl.BindVertexArray(G.vFlo); atr(0, {-3, -3, 3, -3, -3, 3, 3, 3}, 2); gl.BindVertexArray(0); }
  // czastki
  { const int NP = 4000; std::vector<float> Rr((size_t)NP * 4); for (auto& v : Rr) v = (float)los(); G.nPart = NP; G.vPart = vao(); gl.BindVertexArray(G.vPart); atr(0, Rr, 4); gl.BindVertexArray(0); }
  // projektor i pierscienie
  { std::vector<float> Lp, K; auto circle = [&](double r, double y, double k, int m, double tilt, double a0, double a1) { auto P = [&](double t) { Lp.push_back((float)(std::cos(t) * r)); Lp.push_back((float)(y + std::sin(t) * r * std::sin(tilt))); Lp.push_back((float)(std::sin(t) * r * std::cos(tilt))); };
      for (int i = 0; i < m; i++) { const double u0 = (double)i / m, u1 = (double)(i + 1) / m; P(a0 + (a1 - a0) * u0); P(a0 + (a1 - a0) * u1); for (int j = 0; j < 2; j++) { K.push_back((float)u0); K.push_back((float)k); } } };
    circle(1.06, .02, 1, 260, 1.33, -1.1, 4.25); circle(1.14, .02, 2, 260, 1.33, -1.1, 4.25);
    G.nLine = (int)Lp.size() / 3; G.vLine = vao(); gl.BindVertexArray(G.vLine); atr(0, Lp, 3); atr(1, K, 2); gl.BindVertexArray(0); }
  return true;
}

static void coarse() { float* c = G.cp.data(); const float* P = G.P0; const int n = G.n0; for (int i = 0; i < n; i++) { c[i * 4] = P[i * 3]; c[i * 4 + 1] = P[i * 3 + 1]; c[i * 4 + 2] = P[i * 3 + 2]; }
  if (!G.rest) for (size_t j = 0; j < G.shapes.size(); j++) { const double w = F.AU[j]; if (std::fabs(w) < .002) continue; const Ksztalt& s = G.shapes[j]; const uint16_t* I = s.i.data(); const float* Dl = s.d.data(); const size_t m = s.i.size();
    for (size_t k = 0; k < m; k++) { float* e = c + (size_t)I[k] * 4; const float* d = Dl + k * 3; e[0] = (float)((double)e[0] + (double)d[0] * w); e[1] = (float)((double)e[1] + (double)d[1] * w); e[2] = (float)((double)e[2] + (double)d[2] * w); } }
  gl.BindTexture(GL_TEXTURE_2D, G.tC); gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, CW, G.ch, GL_RGBA, GL_FLOAT, c); }
static double Tt = 0;
static void m3u(GLint l, const M3& m) { float f[9]; for (int i = 0; i < 9; i++) f[i] = (float)m[i]; gl.UniformMatrix3fv(l, 1, GL_FALSE, f); }
static void m4u(GLint l, const M4& m) { float f[16]; for (int i = 0; i < 16; i++) f[i] = (float)m[i]; gl.UniformMatrix4fv(l, 1, GL_FALSE, f); }
static void v3u(GLint l, const double* v) { gl.Uniform3f(l, (float)v[0], (float)v[1], (float)v[2]); }
static void u1(GLint l, double v) { gl.Uniform1f(l, (float)v); }
static void stencil(GLuint f, const M3& hm) { gl.BindFramebuffer(GL_FRAMEBUFFER, f); gl.Viewport(0, 0, PW, G.ph); gl.UseProgram(pr.st.p); const Prog& u = pr.st; bindT(0, G.tC, u.uC); bindT(1, G.tSt, u.uSt); bindT(2, G.tOff, u.uOff); m3u(u.uHead, hm); u1(u.uT, Tt); u1(u.uBreath, F.fx.breath); u1(u.uGlitch, F.fx.glitch); gl.BindVertexArray(G.vao0); gl.DrawArrays(GL_TRIANGLES, 0, 3); }
static void normalne(GLuint f, GLuint p) { gl.BindFramebuffer(GL_FRAMEBUFFER, f); gl.UseProgram(pr.nr.p); bindT(0, p, pr.nr.uPos); bindT(1, G.tOff, pr.nr.uOff); bindT(2, G.tNb, pr.nr.uNb); gl.DrawArrays(GL_TRIANGLES, 0, 3); }

/* pozycje i normalne spoczynkowe: raz na twarz */
static void restPose() { Fx& f = F.fx; const double br = f.breath, gt = f.glitch; f.breath = 0; f.glitch = 0; G.rest = true; coarse(); stencil(G.fRest, {1, 0, 0, 0, 1, 0, 0, 0, 1}); G.rest = false; f.breath = br; f.glitch = gt; normalne(G.fRestN, G.tRest); }

static GLuint picTex(int w, int h, const void* px, bool mip) { GLuint t; gl.GenTextures(1, &t); zTex.push_back(t); gl.BindTexture(GL_TEXTURE_2D, t); gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
  if (mip) gl.GenerateMipmap(GL_TEXTURE_2D); gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mip ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR); gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); return t; }
static bool loadLook() { Wyglad& L = CYBER; if (L.ok) return true; Paczka d; if (!d.czytaj(KAT + "/twarz/cyber.bin")) return false; size_t n; const float* p = d.dane<float>("pos0", &n); L.P0.assign(p, p + n); oczy(d, L.eyes); L.pitch = *d.dane<double>("pitch"); const double* at = d.dane<double>("atlas"); std::copy(at, at + 4, L.at); const double* ts = d.dane<double>("rozmiar"); std::copy(ts, ts + 2, L.ts);
  if (OBRAZ) { const double* a = d.dane<double>("wymA"); const double* b = d.dane<double>("wymB"); pr.cy = prog("SHELL_V", "CY_F"); pr.e2 = prog("EYE_V", "EYE_C", {"aP"}); L.ta = picTex((int)a[0], (int)a[1], d.dane<uint8_t>("texA"), true); L.tb = picTex((int)b[0], (int)b[1], d.dane<uint8_t>("texB"), false); }
  L.ok = true; return true; }
static void applyLook(const std::string& id) { const bool c = id == "cyber"; F.look = id; G.L = c ? &CYBER : nullptr; G.P0 = c ? CYBER.P0.data() : G.P0h.data(); G.eyes = c ? &CYBER.eyes : &G.eyesH; F.eyeUp = c ? CYBER.pitch / .3 : 0; if (OBRAZ) restPose(); }
void rysWyglad(const std::string& id_) { const std::string id = id_ == "cyber" ? id_ : "holo"; if (id == "cyber" && !loadLook()) return; G.want = id; }
const std::string& rysWybrany() { return G.want.empty() ? F.look : G.want; }
const double* rysMysz() { static const double s[2] = {.5, .5}; return G.L ? G.L->mouse : s; }
static void cyU(const Prog& p, const Wyglad& C) { bindT(7, C.ta, p.uTa); bindT(8, C.tb, p.uTb); gl.Uniform4f(p.uAt, (float)C.at[0], (float)C.at[1], (float)C.at[2], (float)C.at[3]); gl.Uniform2f(p.uTs, (float)C.ts[0], (float)C.ts[1]); u1(p.uRes, H); gl.Uniform4fv(p.uGl, 16, F.gl); gl.Uniform4f(p.uMouse, (float)C.mouse[0], (float)C.mouse[1], (float)C.mouse[2], (float)C.mouse[3]); gl.Uniform4f(p.uRip, (float)F.rip[0], (float)F.rip[1], (float)F.rip[2], (float)F.rip[3]); }

static void usunPole(GLuint t, GLuint f) { gl.DeleteTextures(1, &t); gl.DeleteFramebuffers(1, &f); zTex.erase(std::remove(zTex.begin(), zTex.end(), t), zTex.end()); zFbo.erase(std::remove(zFbo.begin(), zFbo.end(), f), zFbo.end()); }
static void resize(int w, int h) { if (w == W && h == H) return; W = w; H = h;
  if (G.s.t) usunPole(G.s.t, G.s.f); for (auto& m : G.mips) usunPole(m.t, m.f); G.mips.clear(); if (G.ms.f) { gl.DeleteFramebuffers(1, &G.ms.f); gl.DeleteRenderbuffers(1, &G.ms.c); gl.DeleteRenderbuffers(1, &G.ms.d); }
  auto nt = [](int a, int c) { Cel x; x.t = tex(a, c, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, nullptr, GL_LINEAR); x.f = fbo(x.t); x.w = a; x.h = c; gl.ClearColor(0, 0, 0, 0); gl.Clear(GL_COLOR_BUFFER_BIT); return x; };
  G.s = nt(w, h); auto& m = G.ms; gl.GenFramebuffers(1, &m.f); gl.GenRenderbuffers(1, &m.c); gl.GenRenderbuffers(1, &m.d); const int S = 4;
  gl.BindRenderbuffer(GL_RENDERBUFFER, m.c); gl.RenderbufferStorageMultisample(GL_RENDERBUFFER, S, GL_RGBA16F, w, h); gl.BindRenderbuffer(GL_RENDERBUFFER, m.d); gl.RenderbufferStorageMultisample(GL_RENDERBUFFER, S, GL_DEPTH_COMPONENT24, w, h);
  gl.BindFramebuffer(GL_FRAMEBUFFER, m.f); gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m.c); gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m.d);
  int a = w, c = h; for (int i = 0; i < 6; i++) { a = std::max(2, a >> 1); c = std::max(2, c >> 1); G.mips.push_back(nt(a, c)); } }

static M4 persp(double f, double a, double n, double q) { const double t = 1 / std::tan(f / 2); return {t / a, 0, 0, 0, 0, t, 0, 0, 0, 0, (q + n) / (n - q), -1, 0, 0, 2 * q * n / (n - q), 0}; }
static M4 mul(const M4& a, const M4& c) { M4 o{}; for (int x = 0; x < 4; x++) for (int r = 0; r < 4; r++) for (int k = 0; k < 4; k++) o[x * 4 + r] += a[k * 4 + r] * c[x * 4 + k]; return o; }
static M3 m3(const M3& a, const M3& c) { M3 o{}; for (int x = 0; x < 3; x++) for (int q = 0; q < 3; q++) for (int k = 0; k < 3; k++) o[x * 3 + q] += a[k * 3 + q] * c[x * 3 + k]; return o; }
static M3 rot(double y, double p, double r) { const double cy = std::cos(y), sy = std::sin(y), cp = std::cos(p), sp = std::sin(p), cr = std::cos(r), sr = std::sin(r); return m3({cy, 0, -sy, 0, 1, 0, sy, 0, cy}, m3({1, 0, 0, 0, cp, sp, 0, -sp, cp}, {cr, sr, 0, -sr, cr, 0, 0, 0, 1})); }
static V3 mv3(const M3& m, const V3& v) { return {m[0] * v[0] + m[3] * v[1] + m[6] * v[2], m[1] * v[0] + m[4] * v[1] + m[7] * v[2], m[2] * v[0] + m[5] * v[1] + m[8] * v[2]}; }
static double dot(const V3& a, const V3& c) { return a[0] * c[0] + a[1] * c[1] + a[2] * c[2]; }

void rysKlatka(double dt, double T, int szer, int wys) {
  Tt = T; GLint cel = 0;
  if (OBRAZ) { gl.GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &cel); gl.Disable(GL_SCISSOR_TEST); gl.Disable(GL_BLEND); gl.Disable(GL_DEPTH_TEST); gl.Disable(GL_CULL_FACE); gl.Disable(GL_STENCIL_TEST); gl.Disable(GL_FRAMEBUFFER_SRGB); gl.Enable(GL_MULTISAMPLE); gl.Enable(GL_PROGRAM_POINT_SIZE); gl.ColorMask(1, 1, 1, 1); gl.DepthMask(GL_TRUE); gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4); gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0); }
  // zmiana twarzy: obecna rozplywa sie, nowa buduje sie od dolu
  if (!G.want.empty() && G.want != F.look) { G.lookK = std::max(0.0, G.lookK - dt * 2.4); if (G.lookK == 0 || F.fx.mat < .01) { G.lookK = 0; applyLook(G.want); } } else G.lookK = std::min(1.0, G.lookK + dt * .8);
  for (int i = 0; i < 3; i++) { const double k = std::min(1.0, dt * 2.6); F.tint[i] += (F.tintT[i] - F.tint[i]) * k; F.tint2[i] += (F.tint2T[i] - F.tint2[i]) * k; F.eyeC[i] += (F.eyeT[i] - F.eyeC[i]) * k; }
  { const double k = std::min(1.0, dt * 5); F.view.cx += (F.viewT.cx - F.view.cx) * k; F.view.cy += (F.viewT.cy - F.view.cy) * k; F.view.s += (F.viewT.s - F.view.s) * k; }
  if (!OBRAZ) { W = szer; H = wys; } else resize(std::max(2, szer), std::max(2, wys));
  const Fx& f = F.fx; const double fm = f.mat * G.lookK; Wyglad* C = G.L; const auto& st = F.style; const auto& v = F.view; const double asp = (double)W / H; const M4 P = persp(.4538, asp, .5, 30); const double yu = F.user.yaw, pu = F.user.pitch, cd = 4.6;
  const V3 z{std::sin(yu) * std::cos(pu), std::sin(pu), std::cos(yu) * std::cos(pu)}, ctr{0, -.16, 0}; const double lx = std::hypot(z[2], z[0]); const V3 xx{z[2] / lx, 0, -z[0] / lx}, y{z[1] * xx[2] - z[2] * xx[1], z[2] * xx[0] - z[0] * xx[2], z[0] * xx[1] - z[1] * xx[0]}, eye{ctr[0] + z[0] * cd, ctr[1] + z[1] * cd, ctr[2] + z[2] * cd};
  const M4 view{xx[0], y[0], z[0], 0, xx[1], y[1], z[1], 0, xx[2], y[2], z[2], 0, -dot(xx, eye), -dot(y, eye), -dot(z, eye), 1}; const double s = v.s * F.user.zoom;
  const M4 vp = mul({s, 0, 0, 0, 0, s, 0, 0, 0, 0, 1, 0, v.cx * 2 - 1, 1 - v.cy * 2, 0, 1}, mul(P, view));
  const M3 hm = rot(F.head.yaw, F.head.pitch + (C ? C->pitch : 0), F.head.roll);
  if (C) { swiatlaKrok(dt, T); if (F.maPointer) { C->mouse[0] = F.pointer.x * asp; C->mouse[1] = 1 - F.pointer.y; C->mouse[2] = F.pointer.k; C->mouse[3] = 0; } else std::fill(C->mouse, C->mouse + 4, 0.0); }
  if (!OBRAZ) return;
  coarse(); stencil(G.fPos, hm); normalne(G.fNrm, G.tPos);
  // rysujemy tylko pas miedzy panelami: reszta okna jest zasloniete, a karta graficzna oszczedza pamiec
  const int cx0 = std::max(0, (int)std::floor((F.maClip ? F.clip[0] : 0) * W)), cx1 = std::min(W, (int)std::ceil((F.maClip ? F.clip[1] : 1) * W));
  gl.BindFramebuffer(GL_FRAMEBUFFER, G.ms.f); gl.Viewport(0, 0, W, H); gl.Enable(GL_SCISSOR_TEST); gl.Scissor(cx0, 0, cx1 - cx0, H); gl.ClearColor(0, 0, 0, 0); gl.DepthMask(GL_TRUE); gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  auto col = [&](const Prog& p) { v3u(p.uTint, F.tint); v3u(p.uTint2, F.tint2); };
  auto holoU = [&](const Prog& p) { v3u(p.uRight, xx.data()); gl.Uniform4f(p.uStyle, (float)st.contours, (float)((C ? 262 : 205) * lub(st.fine, 1)), (float)st.points, (float)st.glass); u1(p.uAct, f.act); u1(p.uVoice, f.voice); };
  auto fetch = [&](const Prog& p) { bindT(0, G.tPos, p.uPos); if (p.uNrm >= 0) bindT(1, G.tNrm, p.uNrm); if (p.uRest >= 0) bindT(2, G.tRest, p.uRest); if (p.uRestN >= 0) bindT(6, G.tRestN, p.uRestN); if (p.uAttr >= 0) bindT(3, G.tAttr, p.uAttr); if (p.uAttr2 >= 0) bindT(4, G.tAttr2, p.uAttr2); };
  gl.Enable(GL_BLEND); gl.BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  const double FLOOR = -1.32;
  // pomieszczenie, tafla i fala glosu (zawsze, takze bez modelu)
  const double hz[2] = {vp[5] * FLOOR + vp[13], vp[7] * FLOOR + vp[15]};
  if (F.podstawa) { const Prog& p = pr.fl; gl.UseProgram(p.p); m4u(p.uVP, vp); u1(p.uFloor, FLOOR); col(p); u1(p.uT, T); u1(p.uVoice, f.voice); u1(p.uRise, f.rise); gl.BindVertexArray(G.vFlo); gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4); }
  if (F.podstawa) { const Prog& p = pr.fo; gl.UseProgram(p.p); m4u(p.uVP, vp); u1(p.uT, T); u1(p.uFloor, FLOOR); u1(p.uRise, f.rise); gl.Uniform1fv(p.uBands, 32, F.bands); u1(p.uVoice, f.voice); u1(p.uAct, f.act); col(p); gl.BindVertexArray(G.vFon); u1(p.uMir, 1); gl.DrawArrays(GL_LINES, 0, G.nFon); u1(p.uMir, 0); gl.DrawArrays(GL_LINES, 0, G.nFon); }
  if (fm > .002) {
    // 1. struny swiatla
    { const Prog& p = pr.ne; gl.UseProgram(p.p); m4u(p.uVP, vp); m3u(p.uHead, C ? rot(F.head.yaw, F.head.pitch, F.head.roll) : hm); u1(p.uT, T); u1(p.uAct, f.act); u1(p.uVoice, f.voice); u1(p.uMat, fm); u1(p.uFloor, FLOOR); u1(p.uRise, f.rise); col(p); gl.BindVertexArray(G.vNeu); gl.DrawArrays(GL_LINES, 0, G.nNeu); }
    // 2. oczy i usta: nieprzezroczyste, z glebia
    gl.Disable(GL_BLEND); gl.Enable(GL_DEPTH_TEST); gl.DepthFunc(GL_LEQUAL);
    { const Prog& p = C ? pr.e2 : pr.ey; gl.UseProgram(p.p); m4u(p.uVP, vp); v3u(p.uCam, eye.data()); v3u(p.uEye, F.eyeC); v3u(p.uTint2, F.tint2); v3u(p.uTint, F.tint); holoU(p); if (C) u1(p.uIrE, F.eyeLevel); u1(p.uT, T); u1(p.uPupil, F.eye.pupil); u1(p.uThink, f.think); u1(p.uMat, fm); u1(p.uR, G.eyes->r);
      gl.BindVertexArray(G.vEye); const V3 pv{0, -.58, -.04};
      const struct { const double* c; double gx, gy; } oko[2] = {{G.eyes->L, F.eye.x + F.eye.xr, F.eye.y + F.eye.yr}, {G.eyes->R, F.eye.x - F.eye.xr, F.eye.y + F.eye.yr}};
      for (auto& e : oko) { const V3 q = mv3(hm, {e.c[0] - pv[0], e.c[1] - pv[1], e.c[2] - pv[2]}); const M3 r = m3(hm, rot(e.gx * .42, -e.gy * .3, 0)); gl.Uniform3f(p.uC, (float)(q[0] + pv[0]), (float)(q[1] + pv[1]), (float)(q[2] + pv[2])); m3u(p.uRot, r); gl.DrawElements(GL_TRIANGLES, G.nEye, GL_UNSIGNED_INT, nullptr); } }
    { const Prog& p = pr.mo; gl.UseProgram(p.p); fetch(p); m4u(p.uVP, vp); v3u(p.uCam, eye.data()); col(p); holoU(p); u1(p.uT, T); u1(p.uMat, fm);
      const Czesc* m[3] = {&G.hole, &G.gum, &G.tooth}; for (int k = 0; k < 3; k++) { u1(p.uKind, k); gl.BindVertexArray(m[k]->v); gl.DrawElements(GL_TRIANGLES, m[k]->n, GL_UNSIGNED_INT, nullptr); } }
    // 3. skora: najpierw glebia, potem kolor
    { const Prog& p = pr.dp; gl.UseProgram(p.p); fetch(p); m4u(p.uVP, vp); gl.ColorMask(0, 0, 0, 0); gl.BindVertexArray(G.sk.v); gl.DrawElements(GL_TRIANGLES, G.sk.n, GL_UNSIGNED_INT, nullptr); gl.ColorMask(1, 1, 1, 1); }
    gl.Enable(GL_BLEND); gl.DepthMask(GL_FALSE);
    { const Prog& p = C ? pr.cy : pr.sk; gl.UseProgram(p.p); fetch(p); if (C) cyU(p, *C); m4u(p.uVP, vp); v3u(p.uCam, eye.data()); col(p); holoU(p); u1(p.uT, T); u1(p.uMat, fm); u1(p.uFlick, f.flick); u1(p.uThink, f.think); F.sw = std::fmod(F.sw + .0028 * f.scan, 1.0); u1(p.uSweep, .8 - F.sw * 2.2); u1(p.uBrows, C ? .5 : st.brows); bindT(5, G.tPore, p.uPore); gl.BindVertexArray(G.sk.v); gl.DrawElements(GL_TRIANGLES, G.sk.n, GL_UNSIGNED_INT, nullptr); }
    // cien powieki i wilgotny brzeg oka
    { const Prog& p = pr.oc; gl.UseProgram(p.p); fetch(p); m4u(p.uVP, vp); u1(p.uMat, fm); gl.BindVertexArray(G.occ.v); gl.DrawElements(GL_TRIANGLES, G.occ.n, GL_UNSIGNED_INT, nullptr); }
    { const Prog& p = pr.we; gl.UseProgram(p.p); fetch(p); m4u(p.uVP, vp); v3u(p.uCam, eye.data()); col(p); u1(p.uMat, fm); gl.BindVertexArray(G.wet.v); gl.DrawElements(GL_TRIANGLES, G.wet.n, GL_UNSIGNED_INT, nullptr); }
    // 4. brwi i rzesy
    if (G.nFib) { const Prog& p = pr.fi; gl.UseProgram(p.p); fetch(p); m4u(p.uVP, vp); m3u(p.uHead, hm); col(p); u1(p.uMat, fm); u1(p.uT, T); u1(p.uAct, f.act); u1(p.uVoice, f.voice); u1(p.uBrows, st.brows); gl.BindVertexArray(G.vFib); gl.DrawArrays(GL_LINES, 0, G.nFib); }
  } else { gl.DepthMask(GL_FALSE); gl.Enable(GL_DEPTH_TEST); gl.DepthFunc(GL_LEQUAL); }
  if (F.pokoj) { const Prog& p = pr.li; gl.UseProgram(p.p); m4u(p.uVP, vp); u1(p.uT, T); gl.Uniform4f(p.uFill, (float)f.ctx, (float)f.tg, 0, (float)f.proj); col(p); gl.BindVertexArray(G.vLine); gl.DrawArrays(GL_LINES, 0, G.nLine); }
  { const Prog& p = pr.pa; gl.UseProgram(p.p); fetch(p); m4u(p.uVP, vp); u1(p.uT, T); u1(p.uMat, fm); u1(p.uOrbit, f.orbit); u1(p.uSpin, f.spin); u1(p.uDust, f.dust * st.density); u1(p.uPx, F.px * ((double)H / 900) * v.s); u1(p.uN, G.n - 1); col(p); gl.BindVertexArray(G.vPart); gl.DrawArrays(GL_POINTS, 0, (int)std::floor(G.nPart * std::min(1.0, .35 + .65 * st.density) + .5)); }
  gl.DepthMask(GL_TRUE); gl.Disable(GL_BLEND); gl.Disable(GL_DEPTH_TEST); gl.BindVertexArray(G.vao0);
  gl.BindFramebuffer(GL_READ_FRAMEBUFFER, G.ms.f); gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, G.s.f); gl.Disable(GL_SCISSOR_TEST); gl.BlitFramebuffer(cx0, 0, cx1, H, cx0, 0, cx1, H, GL_COLOR_BUFFER_BIT, GL_NEAREST); gl.BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  // poswiata: lancuch w dol i w gore
  auto cut = [&](const Cel& m) { const double k = (double)m.w / W; const int a = std::max(0, (int)std::floor(cx0 * k) - 2); gl.Scissor(a, 0, std::min(m.w, (int)std::ceil(cx1 * k) + 2) - a, m.h); };
  gl.Enable(GL_SCISSOR_TEST); const Cel* src = &G.s; for (size_t i = 0; i < G.mips.size(); i++) { const Cel& m = G.mips[i]; gl.BindFramebuffer(GL_FRAMEBUFFER, m.f); gl.Viewport(0, 0, m.w, m.h); cut(m); gl.UseProgram(pr.dn.p); bindT(0, src->t, pr.dn.t); gl.Uniform2f(pr.dn.d, (float)(.5 / m.w), (float)(.5 / m.h)); u1(pr.dn.uThr, i ? 0 : .62); gl.DrawArrays(GL_TRIANGLES, 0, 3); src = &m; }
  gl.Enable(GL_BLEND); gl.BlendFunc(GL_ONE, GL_ONE);
  for (int i = (int)G.mips.size() - 1; i > 0; i--) { const Cel& m = G.mips[i - 1]; const Cel& c = G.mips[i]; gl.BindFramebuffer(GL_FRAMEBUFFER, m.f); gl.Viewport(0, 0, m.w, m.h); cut(m); gl.UseProgram(pr.up.p); bindT(0, c.t, pr.up.t); gl.Uniform2f(pr.up.d, (float)(.5 / c.w), (float)(.5 / c.h)); gl.DrawArrays(GL_TRIANGLES, 0, 3); }
  gl.Disable(GL_BLEND);
  gl.BindFramebuffer(GL_FRAMEBUFFER, (GLuint)cel); gl.Viewport(0, 0, W, H);
  // tlo strony: gradient z body, canvas lezy na nim jako obraz z premnozona alfa
  gl.Disable(GL_SCISSOR_TEST);
  if (F.wlasneTlo) { const Prog& p = pr.tlo; gl.UseProgram(p.p); v3u(p.uA, F.tloA); v3u(p.uB, F.tloB); gl.Uniform2f(p.uS, (float)W, (float)H); u1(p.uPx, F.px); gl.DrawArrays(GL_TRIANGLES, 0, 3); }
  gl.UseProgram(pr.co.p); bindT(0, G.s.t, pr.co.s); bindT(1, G.mips[0].t, pr.co.b);
  u1(pr.co.uGlow, st.glow * .22 * (1 + F.glowAdd)); u1(pr.co.uScan, st.scan); u1(pr.co.uGlitch, f.glitch); u1(pr.co.uT, T); u1(pr.co.uPx, F.px); v3u(pr.co.uTint, F.tint); v3u(pr.co.uTint2, F.tint2); u1(pr.co.uAsp, asp); u1(pr.co.uHz, (hz[0] / hz[1]) * .5 + .5); u1(pr.co.uRoom, F.pokoj ? 1 : 0);
  gl.Enable(GL_BLEND); gl.BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); gl.Enable(GL_SCISSOR_TEST); gl.Scissor(cx0, 0, cx1 - cx0, H); gl.DrawArrays(GL_TRIANGLES, 0, 3); gl.Disable(GL_SCISSOR_TEST); gl.Disable(GL_BLEND);
  gl.UseProgram(0); gl.BindVertexArray(0); gl.ActiveTexture(GL_TEXTURE0); gl.BindTexture(GL_TEXTURE_2D, 0);
}

static const char* const TLO_F = R"(#version 330 core
in vec2 v; uniform vec3 uA,uB; uniform vec2 uS; uniform float uPx; out vec4 o;
void main(){vec2 p=vec2(gl_FragCoord.x,uS.y-gl_FragCoord.y)/uPx,c=vec2(.44,.42)*uS/uPx;float t=length((p-c)/vec2(1100.,760.));o=vec4(mix(uA,uB,clamp(t/.72,0.,1.)),1.);})";

bool rysInit(const std::string& katalog, bool obraz) { KAT = katalog; OBRAZ = obraz; W = H = 0; blad = false;
  if (obraz) { if (!wczytajGl()) return false;
    pr.st = prog("QUAD", "STENCIL"); pr.nr = prog("QUAD", "NORMAL"); pr.sk = prog("SHELL_V", "SHELL_F"); pr.dp = prog("SHELL_V", "DEPTH_F");
    pr.ey = prog("EYE_V", "EYE_F", {"aP"}); pr.mo = prog("SHELL_V", "MOUTH_F"); pr.oc = prog("SHELL_V", "OCC_F"); pr.we = prog("SHELL_V", "WET_F"); pr.fi = prog("FIB_V", "FIB_F", {"aRoot", "aOff", "aK"}); pr.ne = prog("NEU_V", "NEU_F", {"aP", "aK"}); pr.fo = prog("FON_V", "FON_F", {"aS", "aU"}); pr.fl = prog("FLO_V", "FLO_F", {"aP"});
    pr.pa = prog("PART_V", "PART_F", {"aR"}); pr.li = prog("LINE_V", "LINE_F", {"aL", "aK"}); pr.dn = prog("QUAD", "DOWN"); pr.up = prog("QUAD", "UP"); pr.co = prog("QUAD", "COMP"); pr.tlo = progZ(plikShadera("QUAD"), TLO_F, "QUAD", "TLO");
    if (blad) return false; }
  if (!load()) return false;
  // pozycje spoczynkowe raz
  if (obraz) { gl.Disable(GL_SCISSOR_TEST); gl.Disable(GL_BLEND); gl.Disable(GL_DEPTH_TEST); restPose(); gl.BindFramebuffer(GL_FRAMEBUFFER, 0); gl.UseProgram(0); gl.BindVertexArray(0); gl.ActiveTexture(GL_TEXTURE0); }
  F.ok = true; return !blad;
}
void rysKoniec() { F.ok = false; if (OBRAZ) { if (G.ms.f) { gl.DeleteFramebuffers(1, &G.ms.f); gl.DeleteRenderbuffers(1, &G.ms.c); gl.DeleteRenderbuffers(1, &G.ms.d); }
    for (GLuint p : zProg) gl.DeleteProgram(p); if (!zFbo.empty()) gl.DeleteFramebuffers((GLsizei)zFbo.size(), zFbo.data()); if (!zTex.empty()) gl.DeleteTextures((GLsizei)zTex.size(), zTex.data()); if (!zBuf.empty()) gl.DeleteBuffers((GLsizei)zBuf.size(), zBuf.data()); if (!zVao.empty()) gl.DeleteVertexArrays((GLsizei)zVao.size(), zVao.data()); }
  zProg.clear(); zFbo.clear(); zTex.clear(); zBuf.clear(); zVao.clear(); G = {}; pr = {}; CYBER = Wyglad(); W = H = 0; F.look = "holo"; F.eyeUp = 0;
}
}
