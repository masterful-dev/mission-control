#include "sim.h"
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <nlohmann_json.hpp>
#include "gen_dane.inc"
using json = nlohmann::json;
namespace {
double rnd(double a, double b) { return a + (double)rand() / RAND_MAX * (b - a); }
struct Plan { std::string kind; int in, cache; std::vector<std::pair<std::string, float>> phases, then; int out; std::string tool; bool code = false, slow = false; float speedUp = 0, acc = 0; int queue = 0; };
const std::vector<Plan> PLAN = {
  {"chat", 1200, 0, {{"reading", 2.5f}, {"thinking", 5}, {"answering", 8}}, {}, 640},
  {"agent", 84210, 9000, {{"reading", 6}, {"thinking", 6}, {"tool", 5}, {"answering", 7}}, {}, 910, "read_file"},
  {"agent", 61000, 58000, {{"reading", 2.5f}, {"answering", 7}}, {}, 1200, "", false, false, 0, .9f},
  {"agent", 22000, 3000, {{"reading", 4}, {"thinking", 6}, {"answering", 8}}, {}, 1800, "", true},
  {"agent", 30000, 2000, {{"reading", 3}, {"thinking", 16}}, {{"answering", 6}}, 2600, "", false, false, 9},
  {"chat", 4000, 1000, {{"reading", 2}, {"answering", 9}}, {}, 700, "", false, true},
  {"compact", 98000, 90000, {{"compact", 6}}, {}, 3100},
  {"agent", 12000, 1000, {{"reading", 2}, {"answering", 7}}, {}, 500, "", false, false, 0, 0, 4},
};
struct Req { Plan p; long id = 0; std::vector<std::pair<std::string, float>> ph, then; std::string phase; float d = 0, t = 0, el = 0, tok = 0, proc = 0, gS = 0, gAvg = 0; int gN = 0; };
std::optional<Req> req;
int planI = 0; float idleFor = 0, loadRate = 1 / 7.f, stopT = 0, downT = 0, overheat = 0, ctxTok = 0, speedTg = 0, speedPp = 0; double upAt = 0;
Sesja ses; float sesN = 0, tgS = 0, ppS = 0, durS = 0; std::vector<WpisHist> hist2;
double E(double a, double b, double c, float dt) { return a + (b - a) * (1 - std::pow(1 - c, dt * 60)); }
Model* running(Stan& s) { for (auto& m : s.models) if (m.status == "ready" || m.status == "starting") return &m; return nullptr; }

void finish(const std::string& st) {
  if (!req) return; auto& r = *req; double g = r.gAvg > 0 ? r.gAvg : speedTg * .97, pp = speedPp * rnd(.92, 1.02), dur = r.el;
  ses.zapytan++; if (st == "OK") ses.ok++; else if (st == "PRZERWANE") ses.przerwane++; else ses.bledy++;
  ses.wejscie += r.p.in; ses.wyjscie += r.p.out; sesN++; tgS += g; ppS += pp; durS += dur; ses.tg_avg = tgS / sesN; ses.pp_avg = ppS / sesN; ses.czas_avg = durS / sesN;
  WpisHist h; h.id = r.id; h.kind = r.p.kind; h.in = r.p.in; h.cached = r.p.cache; h.out = r.p.out; h.ptps = pp; h.gtps = r.p.kind == "compact" ? BRAK : g; h.acc = rnd(.62, .9); h.dur = dur; h.status = st;
  hist2.insert(hist2.begin(), h); if (hist2.size() > 7) hist2.resize(7);
  if (r.p.kind == "compact") ctxTok = 24000; else ctxTok = std::min(131072.f, ctxTok + (r.p.in - r.p.cache) * .4f + r.p.out);
  float cf = (float)r.p.cache / r.p.in; req.reset(); idleFor = 0;
  (void)cf;
}
void next() {
  auto& r = *req;
  if (r.ph.empty() && !r.then.empty()) { r.ph = r.then; r.then.clear(); }
  if (r.ph.empty()) return finish("OK");
  auto p = r.ph.front(); r.ph.erase(r.ph.begin()); r.phase = p.first; r.d = p.second; r.t = 0;
}
void submit(const Praca& f, const Plan& p) {
  if (f.status != "ready" || f.down) return;
  Req r; r.p = p; r.id = ses.zapytan + 1; r.ph = p.phases; r.then = p.then; req = r; idleFor = 0; next();
}
}
namespace Sim {
bool autoPlan = true; double mouseT = -9;
const Model* dziala(const Stan& s) { return running(const_cast<Stan&>(s)); }
void init(Stan& s, Praca& f) {
  s = Stan{}; f = Praca{};
  auto G = [](int i, const char* n, const char* k, double u, double t, double h, double w, double lim, double ut, double tot) { Gpu g; g.index = i; g.name = n; g.krotka = k; g.vram_used_mb = u; g.vram_total_mb = tot; g.temp_c = t; g.hbm_temp_c = h; g.power_w = w; g.power_limit_w = lim; g.util_gpu = ut; return g; };
  s.gpus = {G(0, "Tesla V100-SXM2-32GB", "V100", 278, 35, 36, 42, 300, 0, 32768), G(1, "Tesla V100-SXM2-32GB", "V100", 278, 43, 42, 41, 300, 0, 32768), G(2, "NVIDIA T400 4GB", "T400", 1046, 46, BRAK, BRAK, 31.3, 28, 4096)}; s.gpus[0].nvlink = s.gpus[1].nvlink = true;
  s.cpu = {17, 24, std::vector<double>(24, 10), 6.4, 60, 60, 3070};
  s.ram = {62.5, 10.1, 52.4, 38.2, 16, 0, 2, 2};
  s.disks.clear();
  s.psu.power_w = 146; s.psu.temps = {{"VRM", 54}, {"case", 40.8}}; s.nvme = {{"0", 60.9}, {"1", 48.9}, {"2", 40.9}}; s.net = {.1, .02};
  for (auto& j : json::parse(GEN_MODELE)) { Model m; m.id = j["id"]; m.name = j["name"]; m.cat = j["cat"]; m.port = j["port"]; m.ctx = j["ctx"]; m.ram = j["ram"]; m.vram = j["vram"]; m.gpus = j["gpus"].get<std::vector<int>>(); for (auto& p : j["pl"]) { Wariant w; w.opis = p; std::string o = w.opis; for (size_t q = o.find("GPU "); q != std::string::npos; q = o.find("GPU ", q + 4)) { w.gpus.push_back(atoi(o.c_str() + q + 4)); w.vram.push_back((int)m.vram); } if (m.pl.empty() && w.gpus.size() < m.gpus.size()) { w.gpus = m.gpus; w.vram.assign(m.gpus.size(), (int)m.vram); } m.pl.push_back(w); } s.models.push_back(m); }
  auto e = json::parse(GEN_ENERGIA); auto& n = s.energia; { time_t teraz = time(nullptr); teraz -= teraz % 3600; size_t ng = e["godziny"].size(), i = 0; for (auto& q : e["godziny"]) { Slupek b; b.v = q["wh"]; b.ts = (long long)teraz - (long long)(ng - 1 - i++) * 3600; n.godziny.push_back(b); } tm l = *localtime(&teraz); int dzis = l.tm_mday, k = 0; for (auto& q : e["dni"]) { if (++k > dzis) break; Slupek b; b.v = q["kwh"]; char db[16]; snprintf(db, 16, "%04d-%02d-%02d", l.tm_year + 1900, l.tm_mon + 1, k); b.data = db; n.dni.push_back(b); } while ((int)n.dni.size() < dzis) { Slupek b; b.v = 6 + (n.dni.size() * 37 % 40) / 10.; char db[16]; snprintf(db, 16, "%04d-%02d-%02d", l.tm_year + 1900, l.tm_mon + 1, (int)n.dni.size() + 1); b.data = db; n.dni.push_back(b); } } n.urzadzenia = {{"monitors", 3, 28}, {"audio", 1, 1.1}};
  n.teraz_w = 224.9; n.teraz_w_gniazdko = 331.6; n.psu_glowny_w = 164; n.psu_gpu_w = 83; n.urzadzenia_w = 85.1; n.ostatnia_godzina_sred_w = 430.5; n.miesiac_sred_w = 395.2; n.dzis_kwh = 10.3639; n.miesiac_kwh = 34.8161; n.miesiac_godzin_pracy = 88.09; n.miesiac_urzadzenia_kwh = 7.4965;
  req.reset(); hist2.clear(); planI = 0; idleFor = 0;
}
void start(Stan& s, Praca& f, const std::string& id) {
  auto it = std::find_if(s.models.begin(), s.models.end(), [&](auto& x) { return x.id == id; }); if (it == s.models.end()) return;
  Model* old = running(s); if (old && old != &*it) { old->status = "stopped"; s.llm.clear(); req.reset(); }
  it->status = "starting"; f.status = "loading"; f.load = 0; f.down = false; loadRate = 1 / 7.f; ses = Sesja{}; sesN = tgS = ppS = durS = 0;
  bool fl = id.find("flash") != std::string::npos; speedTg = fl ? 143.9f : 92; speedPp = fl ? 3985 : 2100; upAt = 0; hist2.clear(); planI = 0; ctxTok = 0;
}
void stop(Stan& s, Praca& f, const std::string& id) {
  Model* m = nullptr; if (id.empty()) m = running(s); else for (auto& x : s.models) if (x.id == id) m = &x; if (!m) return;
  m->status = "stopping"; f.status = "stopping"; req.reset(); stopT = 2.2f;
}
void demo(Stan& s, Praca& f, const std::string& k) {
  if (k == "load") { if (!running(s)) start(s, f, "warp-flash-tp2"); }
  if (k == "unload") stop(s, f);
  if (k == "prompt") submit(f, PLAN[rand() % 4]);
  if (k == "hard") submit(f, PLAN[4]);
  if (k == "slow") submit(f, PLAN[5]);
  if (k == "heat") overheat = 11;
  if (k == "err" && f.status == "ready") { if (req) finish("BŁĄD"); f.down = true; downT = 5; }
}
void tick(Stan& s, Praca& f, float dt, double teraz) {
  Model* m = running(s); upAt += dt;
  if (f.status == "loading") { f.load = std::min(1.f, f.load + dt * loadRate); if (f.load >= 1 && m) { m->status = "ready"; f.status = "ready"; f.justReady = 2.6f; idleFor = 0; upAt = 0; } }
  if (f.status == "stopping") { stopT -= dt; if (stopT <= 0) { for (auto& q : s.models) if (q.status == "stopping") q.status = "stopped"; f.status = "none"; s.llm.clear(); } }
  f.justReady = std::max(0.f, f.justReady - dt);
  if (f.down) { downT -= dt; if (downT <= 0) { f.down = false; idleFor = 0; } }
  if (overheat > 0) overheat -= dt;
  double tg = 0, pp = 0;
  if (req && !f.down) {
    auto& r = *req; float sp = r.p.speedUp > 0 ? r.p.speedUp : 8; r.t += dt; r.el += dt * sp;
    if (r.phase == "reading") { pp = speedPp * rnd(.92, 1.03); r.proc = std::min<float>(r.p.in - r.p.cache, r.proc + (r.p.in - r.p.cache) * dt / r.d); }
    if (r.phase == "thinking" || r.phase == "answering") { tg = speedTg * rnd(.93, 1.04) * (r.p.slow ? .56 : 1); r.tok += r.p.out * dt / (r.d * (r.phase == "thinking" ? 3 : 1.6f)); r.gS += tg; r.gN++; r.gAvg = r.gS / r.gN; }
    if (r.t >= r.d) next();
  } else if (f.status == "ready" && !f.down) { idleFor += dt; if (autoPlan && idleFor > 4.5f) submit(f, PLAN[planI++ % PLAN.size()]); }
  const Req* a = req ? &*req : nullptr;
  f.phase = a ? a->phase : "idle"; f.el = a ? a->el : 0; f.tg = tg; f.pp = pp; f.tgAvg = speedTg; f.promptTok = a ? a->p.in : 0; f.cacheFrac = a ? (float)a->p.cache / a->p.in : 0; f.code = a && a->p.code; f.kind = a ? a->p.kind : ""; f.queue = a ? a->p.queue : 0; f.acc = a && a->p.acc > 0 ? a->p.acc : .7f; f.tool = a ? a->p.tool : "";
  f.ctx = ctxTok / 131072; f.idleS = idleFor * 8; f.mouseRecent = teraz - mouseT < 2.5;
  double busy = a ? (a->phase == "tool" ? .1 : .95) : 0, load = f.status == "loading" ? .25 : 0;
  double need = m && (f.status == "ready" || f.status == "loading") ? m->vram * (f.status == "loading" ? f.load : 1) : 0;
  for (size_t i = 0; i < s.gpus.size(); i++) { auto& g = s.gpus[i];
    if (i < 2) { bool act = m && std::count(m->gpus.begin(), m->gpus.end(), (int)i); double tgt = act ? 278 + need : 278;
      g.vram_used_mb = E(g.vram_used_mb, std::min(32500., tgt), .2, dt); double u = act ? busy * rnd(.85, 1) * 100 + load * 100 : 0;
      g.util_gpu = E(g.util_gpu, u, .25, dt); g.util_mem = g.util_gpu * .7; g.power_w = E(g.power_w, 42 + g.util_gpu * 2.1, .2, dt);
      double hot = overheat > 0 ? 84 : 0; g.temp_c = E(g.temp_c, std::max(hot, 36 + i * 6 + g.util_gpu * .34), hot ? .09 : .03, dt); g.hbm_temp_c = g.temp_c + 1 + g.util_gpu * .05;
      g.sm_clock_mhz = g.util_gpu > 5 ? 1380 : 135; g.mem_clock_mhz = 877;
      g.processes = {{1933, 4, "Xorg", true}}; if (m && act) g.processes.push_back({48213 + (int)i, std::round(g.vram_used_mb - 278), m->id, false});
    } else g.util_gpu = E(g.util_gpu, rnd(20, 34), .2, dt); }
  f.gpuTemp = std::max(s.gpus[0].temp_c, s.gpus[1].temp_c); f.vram = s.gpus[0].vram_used_mb / 32768;
  auto& c = s.cpu; double cb = busy * 14 + load * 60;
  c.percent = E(c.percent, 12 + cb + rnd(-3, 3), .2, dt); for (size_t i = 0; i < c.per.size(); i++) c.per[i] = E(c.per[i], std::min(100., std::max(1., c.percent * (i % 5 ? .7 : 2) + rnd(-12, 14))), .25, dt);
  c.temp_package_c = E(c.temp_package_c, 54 + c.percent * .35, .05, dt); c.temp_hottest_c = c.temp_package_c + 3; c.load1 = E(c.load1, 4 + c.percent * .16, .05, dt); c.freq_mhz = E(c.freq_mhz, 3000 + c.percent * 12, .1, dt);
  auto& R = s.ram; R.model_gb = E(R.model_gb, m ? m->ram * (f.status == "loading" ? f.load : 1) : 0, .2, dt); R.used_gb = 10.1 + R.model_gb; R.available_gb = R.total_gb - R.used_gb; R.percent = R.used_gb / R.total_gb * 100;
  s.net = {rnd(.05, .3), rnd(.01, .1)};
  auto& en = s.energia; en.gpu0_w = s.gpus[0].power_w; en.gpu1_w = s.gpus[1].power_w; en.psu_gpu_w = en.gpu0_w + en.gpu1_w; en.psu_glowny_w = E(en.psu_glowny_w, 150 + c.percent * 1.1, .15, dt); s.psu.power_w = en.psu_glowny_w;
  en.teraz_w = en.psu_glowny_w + en.psu_gpu_w; en.teraz_w_gniazdko = en.psu_glowny_w / en.psu_sprawnosc + en.psu_gpu_w / en.psu_gpu_sprawnosc + en.urzadzenia_w;
  en.dzis_kwh += en.teraz_w_gniazdko * dt / 3.6e6; en.miesiac_kwh += en.teraz_w_gniazdko * dt / 3.6e6;
  if (m) { Llm l; l.id = m->id; l.name = m->name; l.port = m->port; l.up_s = upAt; l.status = m->status; l.up_ok = f.status == "ready" && !f.down; l.n_ctx = m->ctx; l.ctx = std::lround(ctxTok + (a ? std::min<float>(a->tok, a->p.out) : 0)); l.sesja = ses; l.queued = f.queue; l.hist = hist2;
    if (f.status == "loading") l.ladowanie = Ladowanie{f.load * 100., f.load * 44000., 44000, need * 2};
    if (a) { Aktywne x; x.id = a->id; x.phase = a->phase; x.kind = a->p.kind; x.in = a->p.in; x.cache = a->p.cache; x.out = std::round(a->tok); x.proc = std::round(a->proc); x.inst = tg; x.avg = a->gAvg > 0 ? a->gAvg : BRAK; x.ptps = pp; x.el = a->el; x.eff = a->p.kind == "agent" ? "high" : ""; x.tool = a->p.tool; if (a->phase == "tool") x.tools = {a->p.tool}; l.active = x; }
    s.llm = {l}; }
}
}
