#include "api.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <httplib.h>
#include <nlohmann_json.hpp>
using json = nlohmann::json;
namespace {
std::string baza; std::string host = "127.0.0.1"; int port = 8770; std::atomic<bool> ok{false}, swiezy{false};
std::mutex mS, mZ, mO; std::condition_variable cZ; Stan bufor;
struct Zlec { std::string m, p, b; Api::Odp o; }; std::deque<Zlec> kolejka; std::deque<std::function<void()>> odp;
double d(const json& j, const char* k, double v = 0) { auto i = j.find(k); return i != j.end() && i->is_number() ? i->get<double>() : v; }
double dn(const json& j, const char* k) { return d(j, k, BRAK); }
std::string st(const json& j, const char* k, const char* v = "") { auto i = j.find(k); return i != j.end() && i->is_string() ? i->get<std::string>() : v; }
long l(const json& j, const char* k) { return (long)d(j, k); }
std::string krotka(const std::string& n) { for (const char* k : {"V100", "T400", "A100", "H100", "3090", "4090"}) if (n.find(k) != std::string::npos) return k; return n.substr(0, 8); }

Stan parsuj(const json& j) {
  Stan s;
  for (auto& q : j.value("gpus", json::array())) { Gpu g; g.index = l(q, "index"); g.name = st(q, "name"); g.krotka = krotka(g.name); g.vram_used_mb = d(q, "vram_used_mb"); g.vram_total_mb = d(q, "vram_total_mb", 1); g.temp_c = d(q, "temp_c"); g.hbm_temp_c = dn(q, "hbm_temp_c"); g.power_w = dn(q, "power_w"); g.power_limit_w = d(q, "power_limit_w"); g.util_gpu = d(q, "util_gpu"); g.util_mem = d(q, "util_mem"); g.sm_clock_mhz = d(q, "sm_clock_mhz"); g.mem_clock_mhz = d(q, "mem_clock_mhz"); g.fan_pct = dn(q, "fan_pct"); g.nvlink = q.value("nvlink", false);
    for (auto& p : q.value("processes", json::array())) g.processes.push_back({(int)l(p, "pid"), d(p, "vram_mb"), st(p, "name"), p.value("system", false)}); s.gpus.push_back(g); }
  if (auto c = j.value("cpu", json::object()); !c.empty()) { s.cpu.percent = d(c, "percent"); s.cpu.cores = l(c, "cores"); s.cpu.load1 = d(c, "load1"); s.cpu.temp_package_c = d(c, "temp_package_c"); s.cpu.temp_hottest_c = d(c, "temp_hottest_c"); s.cpu.freq_mhz = d(c, "freq_mhz"); if (c.contains("per")) s.cpu.per = c["per"].get<std::vector<double>>(); }
  if (auto r = j.value("ram", json::object()); !r.empty()) s.ram = {d(r, "total_gb"), d(r, "used_gb"), d(r, "available_gb"), d(r, "free_gb"), d(r, "percent"), 0, d(r, "swap_total_gb"), d(r, "swap_used_gb")};
  for (auto& q : j.value("disks", json::array())) { Dysk x; x.path = st(q, "path"); x.nazwa = x.path; x.total_gb = d(q, "total_gb"); x.used_gb = d(q, "used_gb"); x.free_gb = d(q, "free_gb"); x.percent = d(q, "percent"); s.disks.push_back(x); }
  if (auto p = j.value("psu", json::object()); !p.empty()) { s.psu.power_w = d(p, "power_w"); if (p.contains("temps") && p["temps"].is_object()) for (auto& [k, v] : p["temps"].items()) if (v.is_number()) s.psu.temps.push_back({k, v.get<double>()});  if (p.contains("fans") && p["fans"].is_array() && !p["fans"].empty()) s.psu.fan_rpm = dn(p["fans"][0], "rpm"); }
  int nv = 0; for (auto& q : j.value("nvme", json::array())) s.nvme.push_back({std::to_string(nv++), d(q, "temp_c")});
  if (auto n = j.value("net", json::object()); !n.empty()) s.net = {d(n, "rx_mb_s"), d(n, "tx_mb_s"), st(n, "hostname")};
  for (auto& q : j.value("models", json::array())) { Model m; m.id = st(q, "id"); m.name = st(q, "name", m.id.c_str()); m.cat = st(q, "category", "TP1"); m.status = st(q, "status", "stopped"); m.port = l(q, "port"); m.pin = l(q, "pin"); m.ctx = l(q, "ctx"); m.ram = d(q, "ram_gb"); m.mozeStart = q.value("can_start", true); m.powod = st(q, "plan_reason"); m.rodzaj = st(q, "kind"); { std::string pc; if (q.contains("cmd") && q["cmd"].is_array()) for (auto& x : q["cmd"]) if (x.is_string()) pc += (pc.empty() ? "" : " ") + x.get<std::string>(); if (pc.empty()) { pc = st(q, "script"); if (q.contains("args") && q["args"].is_array()) for (auto& x : q["args"]) if (x.is_string()) pc += " " + x.get<std::string>(); } m.polecenie = pc; } { json kop = q; for (const char* k : {"status", "can_start", "plan_placement", "plan_reason", "switch_from", "running_placement", "pids", "adopted", "started"}) kop.erase(k); m.surowy = kop.dump(); }
    for (auto& p : q.value("placements", json::array())) { Wariant w; if (p.contains("gpus")) for (auto& x : p["gpus"]) if (x.is_number()) w.gpus.push_back(x); if (p.contains("vram")) for (auto& x : p["vram"]) if (x.is_number()) w.vram.push_back(x); std::string o; for (size_t i = 0; i < w.gpus.size(); i++) { if (i) o += " + "; o += "GPU " + std::to_string(w.gpus[i]) + " · " + std::to_string(i < w.vram.size() ? w.vram[i] : 0) + " MiB"; } w.opis = o; m.pl.push_back(w); }
    if (!m.pl.empty()) { const Wariant& w = m.pl[0]; m.gpus = w.gpus; for (int v : w.vram) m.vram = std::max<double>(m.vram, v); } s.models.push_back(m); }
  for (auto& q : j.value("llm", json::array())) { Llm m; m.id = st(q, "id"); m.name = st(q, "name"); m.status = st(q, "status"); m.port = l(q, "port"); m.queued = l(q, "queued"); m.up_s = d(q, "up_s"); m.up_ok = q.value("up_ok", false); m.n_ctx = l(q, "n_ctx"); m.ctx = l(q, "ctx");
    if (auto x = q.value("sesja", json::object()); !x.empty()) { m.sesja.tg_avg = dn(x, "tg_avg"); m.sesja.pp_avg = dn(x, "pp_avg"); m.sesja.czas_avg = dn(x, "czas_avg"); m.sesja.ok = l(x, "ok"); m.sesja.bledy = l(x, "bledy"); m.sesja.przerwane = l(x, "przerwane"); m.sesja.zapytan = l(x, "zapytan"); m.sesja.wejscie = d(x, "wejscie"); m.sesja.wyjscie = d(x, "wyjscie"); }
    for (auto& r : q.value("hist", json::array())) { WpisHist h; h.id = l(r, "id"); h.kind = st(r, "kind"); h.status = st(r, "status"); h.in = d(r, "in"); h.cached = d(r, "cached"); h.out = d(r, "out"); h.ptps = d(r, "ptps"); h.gtps = dn(r, "gtps"); h.acc = dn(r, "acc"); h.dur = d(r, "dur"); m.hist.push_back(h); }
    if (auto x = q.find("ladowanie"); x != q.end() && x->is_object()) m.ladowanie = Ladowanie{d(*x, "pct"), d(*x, "wczytano_mb"), d(*x, "rozmiar_mb"), d(*x, "vram_mb")};
    if (auto x = q.find("active"); x != q.end() && x->is_object()) { Aktywne a; a.id = l(*x, "id"); a.phase = st(*x, "phase"); a.kind = st(*x, "kind"); a.eff = st(*x, "eff"); a.tool = st(*x, "tool"); a.in = d(*x, "in"); a.cache = d(*x, "cache"); a.out = d(*x, "out"); a.proc = d(*x, "proc"); a.inst = d(*x, "inst"); a.avg = dn(*x, "avg"); a.ptps = d(*x, "ptps"); a.el = d(*x, "el"); if (x->contains("tools") && (*x)["tools"].is_array()) for (auto& t : (*x)["tools"]) if (t.is_string()) a.tools.push_back(t); m.active = a; }
    s.llm.push_back(m); }
  if (auto e = j.value("energia", json::object()); !e.empty()) { auto& n = s.energia; n.teraz_w = d(e, "teraz_w"); n.teraz_w_gniazdko = d(e, "teraz_w_gniazdko"); n.psu_sprawnosc = d(e, "psu_sprawnosc", .92); n.psu_gpu_sprawnosc = d(e, "psu_gpu_sprawnosc", .9); n.psu_glowny_w = d(e, "psu_glowny_w"); n.psu_gpu_w = d(e, "psu_gpu_w"); n.urzadzenia_w = d(e, "urzadzenia_w"); n.ostatnia_godzina_sred_w = d(e, "ostatnia_godzina_sred_w"); n.miesiac_sred_w = d(e, "miesiac_sred_w"); n.dzis_kwh = d(e, "dzis_kwh"); n.miesiac_kwh = d(e, "miesiac_kwh"); n.miesiac_godzin_pracy = d(e, "miesiac_godzin_pracy"); n.miesiac_urzadzenia_kwh = d(e, "miesiac_urzadzenia_kwh");
    if (e.contains("psu_gpu_karty") && e["psu_gpu_karty"].is_object()) { n.gpu0_w = d(e["psu_gpu_karty"], "0"); n.gpu1_w = d(e["psu_gpu_karty"], "1"); }
    for (auto& q : e.value("godziny", json::array())) { Slupek b; b.v = d(q, "wh"); b.ts = (long long)d(q, "ts"); n.godziny.push_back(b); } for (auto& q : e.value("dni", json::array())) { Slupek b; b.v = d(q, "kwh"); b.data = st(q, "data"); n.dni.push_back(b); } for (auto& q : e.value("urzadzenia", json::array())) n.urzadzenia.push_back({st(q, "nazwa"), (int)l(q, "sztuk"), d(q, "moc_w")}); }
  for (auto& q : j.value("orphans", json::array())) s.orphans.push_back({(int)l(q, "pid"), st(q, "name"), st(q, "cmdline"), d(q, "vram_mb")});
  return s;
}
void petlaStanu() {
  httplib::Client c(host, port); c.set_connection_timeout(1); c.set_read_timeout(2); c.set_keep_alive(true);
  for (;;) { auto r = c.Get("/api/state");
    if (r && r->status == 200) { try { Stan s = parsuj(json::parse(r->body)); std::lock_guard g(mS); bufor = std::move(s); swiezy = true; ok = true; } catch (...) { ok = false; } } else ok = false;
    std::this_thread::sleep_for(std::chrono::milliseconds(ok ? 500 : 2000)); }
}
void petlaZlecen() {
  httplib::Client c(host, port); c.set_connection_timeout(2); c.set_read_timeout(60);
  for (;;) { Zlec z; { std::unique_lock g(mZ); cZ.wait(g, [] { return !kolejka.empty(); }); z = std::move(kolejka.front()); kolejka.pop_front(); }
    httplib::Result r = z.m == "GET" ? c.Get(z.p) : z.m == "DELETE" ? c.Delete(z.p) : z.m == "PUT" ? c.Put(z.p, z.b, "application/json") : c.Post(z.p, z.b, "application/json");
    int kod = r ? r->status : 0; std::string t = r ? r->body : "brak polaczenia";
    if (z.o) { std::lock_guard g(mO); odp.push_back([o = z.o, kod, t] { o(kod, t); }); } }
}
}
namespace Api {
void start(const std::string& a) { baza = a; std::string r = a; if (auto p = r.find("://"); p != std::string::npos) r = r.substr(p + 3); if (auto p = r.find('/'); p != std::string::npos) r = r.substr(0, p); if (auto p = r.find(':'); p != std::string::npos) { host = r.substr(0, p); port = atoi(r.c_str() + p + 1); } else host = r;
  std::thread(petlaStanu).detach(); std::thread(petlaZlecen).detach(); }
const std::string& adres() { return baza; }
bool polaczone() { return ok; }
bool nowyStan(Stan& s) { if (!swiezy) return false; std::lock_guard g(mS); s = bufor; swiezy = false; return true; }
void zlec(const std::string& m, const std::string& p, const std::string& b, Odp o) { { std::lock_guard g(mZ); kolejka.push_back({m, p, b, std::move(o)}); } cZ.notify_one(); }
void pompuj() { std::deque<std::function<void()>> q; { std::lock_guard g(mO); q.swap(odp); } for (auto& f : q) f(); }
}
