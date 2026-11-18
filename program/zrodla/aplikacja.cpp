#include "aplikacja.h"
#include "api.h"
#include "sim.h"
#include "ui.h"
#include <SDL3/SDL.h>
#include <nlohmann_json.hpp>
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#endif
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <array>
#include <unordered_map>
#include "gen_dane_teksty.inc"
using json = nlohmann::json; using namespace ui;
Aplikacja A;
namespace {
std::unordered_map<std::string, std::array<std::string, 2>> teksty;
std::map<std::string, Skorka> wbudowane;
Skorka zJson(const json& j) { Skorka s; auto S_ = [&](const char* k, std::string& v) { if (j.contains(k)) v = j[k]; }; auto F_ = [&](const char* k, float& v) { if (j.contains(k)) v = j[k]; }; S_("name", s.name); S_("accent", s.accent); S_("rim", s.rim); S_("eye", s.eye); S_("bg", s.bg); S_("panel", s.panel); S_("text", s.text); S_("warn", s.warn); S_("bad", s.bad); S_("font", s.font); F_("contours", s.contours); F_("fine", s.fine); F_("points", s.points); F_("glass", s.glass); F_("glow", s.glow); F_("scan", s.scan); F_("density", s.density); F_("opacity", s.opacity); F_("radius", s.radius); return s; }
json doJson(const Skorka& s) { return {{"name", s.name}, {"accent", s.accent}, {"rim", s.rim}, {"eye", s.eye}, {"bg", s.bg}, {"panel", s.panel}, {"text", s.text}, {"warn", s.warn}, {"bad", s.bad}, {"font", s.font}, {"contours", s.contours}, {"fine", s.fine}, {"points", s.points}, {"glass", s.glass}, {"glow", s.glow}, {"scan", s.scan}, {"density", s.density}, {"opacity", s.opacity}, {"radius", s.radius}}; }
void ukladDomyslny() { A.okna.clear(); A.swobodny = false; A.zwiniete.clear(); A.ukryte.clear(); for (auto& p : definicjePaneli()) { A.zwiniete[p.id] = false; A.ukryte[p.id] = false; } }
void odp(int k, const std::string& s) { if (k >= 200 && k < 300) return; std::string m = s; auto j = json::parse(s, nullptr, false); if (j.is_object() && j.contains("detail") && j["detail"].is_string()) m = j["detail"]; A.toast(std::string(t("err")) + ": " + m.substr(0, 110)); }
std::string male(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
bool granica(const std::string& s, size_t i) { return i >= s.size() || !isalnum((unsigned char)s[i]); }
// nazwa do listy: bez nazwy silnika na poczatku i bez dopisku TP1/TP2 (to pokazuja znaczek silnika i rysunek kart)
std::string czystaNazwa(std::string n, const std::string& silnik) {
  std::string sn = male(silnik), ln = male(n); if (!sn.empty() && ln.rfind(sn, 0) == 0 && n.size() > sn.size() && !isalnum((unsigned char)n[sn.size()])) n = n.substr(sn.size());
  for (const char* tp : {"TP1", "TP2", "tp1", "tp2"}) for (size_t q = n.find(tp); q != std::string::npos; q = n.find(tp, q)) { if ((q == 0 || !isalnum((unsigned char)n[q - 1])) && granica(n, q + 3)) n.erase(q, 3); else q += 3; }
  auto zamien = [&](const std::string& a, const std::string& b) { bool z = false; for (size_t q = n.find(a); q != std::string::npos; q = n.find(a, q)) { n.replace(q, a.size(), b); z = true; } return z; };
  for (int i = 0; i < 6; i++) { bool z = false; z |= zamien("  ", " "); z |= zamien("( ", "("); z |= zamien(" )", ")"); z |= zamien("(,", "("); z |= zamien(",)", ")"); z |= zamien("()", ""); z |= zamien(" ,", ","); z |= zamien(",,", ","); z |= zamien("· ·", "·"); if (!z) break; }
  while (!n.empty() && (n.back() == ' ' || n.back() == ',' || n.back() == '-')) n.pop_back(); size_t a = n.find_first_not_of(" ,-"); return a == std::string::npos ? "" : n.substr(a);
}
std::string wykryjSilnik(const Model& m) {
  std::string s = male(m.name + " " + m.polecenie);
  for (auto [k, id] : std::initializer_list<std::pair<const char*, const char*>>{{"warp", "warp"}, {"llama-server", "llamacpp"}, {"llama.cpp", "llamacpp"}, {"llama_cpp", "llamacpp"}, {"ninfer", "ninfer"}, {"vllm", "vllm"}, {"sglang", "sglang"}, {"ollama", "ollama"}, {"comfy", "comfyui"}, {"tabby", "exllama"}, {"exllama", "exllama"}, {"koboldcpp", "llamacpp"}, {"lmstudio", "llamacpp"}}) if (s.find(k) != std::string::npos) return id;
  if (s.find(".py") != std::string::npos || s.find("python") != std::string::npos) return "python"; if (s.find(".sh") != std::string::npos) return "skrypt"; return "inny";
}
}
const char* t(const std::string& k) { auto i = teksty.find(k); if (i == teksty.end()) { auto& e = teksty[k]; e = {k, k}; return e[0].c_str(); } auto& v = i->second; return v[A.jezyk].empty() ? v[0].c_str() : v[A.jezyk].c_str(); }
const char* L(const char* en, const char* pl) { return A.jezyk ? pl : en; }
Model* modelPoId(const std::string& id) { for (auto& m : A.s.models) if (m.id == id) return &m; return nullptr; }
const std::vector<SilnikDef>& silnikiZnane() {
  static std::vector<SilnikDef> s = {{"warp", "WARP", "W", 0x4fd1c5}, {"llamacpp", "llama.cpp", "L", 0xf6ad55}, {"ninfer", "NInfer", "N", 0xb794f4}, {"vllm", "vLLM", "V", 0x63b3ed}, {"sglang", "SGLang", "S", 0x68d391}, {"ollama", "Ollama", "O", 0xe2e8f0}, {"exllama", "ExLlama", "E", 0xfc8181}, {"comfyui", "ComfyUI", "C", 0xf687b3}, {"python", "Python", "Py", 0xecc94b}, {"skrypt", "Script", "Sh", 0xa0aec0}, {"inny", "Other", "•", 0x8fa3b0}};
  s[9].nazwa = L("Script", "Skrypt"); s[10].nazwa = L("Other", "Inny"); return s;
}
SilnikDef silnikPoId(const std::string& id) { for (auto& s : silnikiZnane()) if (s.id == id) return s; if (id.empty()) return silnikiZnane().back(); SilnikDef d{id, id, "", 0x9ad1ff}; size_t n = 1; while (n < id.size() && ((unsigned char)id[n] & 0xC0) == 0x80) n++; d.znak = id.substr(0, n); for (auto& c : d.znak) c = (char)toupper((unsigned char)c); return d; }
void Aplikacja::uzupelnijModele() {
  for (auto& m : s.models) { auto it = silniki.find(m.id); m.silnik = it != silniki.end() && !it->second.empty() ? it->second : wykryjSilnik(m);
    int n = 0; if (!m.pl.empty()) { auto& w = m.pl[0]; for (size_t i = 0; i < w.gpus.size(); i++) if (i >= w.vram.size() || w.vram[i] >= 64) n++; } else n = m.vram > 0 ? std::max<int>(1, m.gpus.size()) : (int)m.gpus.size(); m.ileGpu = n;
    std::string sn = silnikPoId(m.silnik).nazwa; m.krotka = czystaNazwa(m.name, sn); if (m.krotka.empty()) m.krotka = m.name; }
}
void Aplikacja::ustawSilnik(const std::string& model, const std::string& sil) { if (sil.empty()) silniki.erase(model); else silniki[model] = sil; uzupelnijModele(); zapisz(); }
bool Aplikacja::dyskUkryty(const std::string& p) const { return std::find(dyskiUkryte.begin(), dyskiUkryte.end(), p) != dyskiUkryte.end(); }
void Aplikacja::ukryjDysk(const std::string& p, bool ukr) { dyskiUkryte.erase(std::remove(dyskiUkryte.begin(), dyskiUkryte.end(), p), dyskiUkryte.end()); if (ukr) dyskiUkryte.push_back(p); zapisz(); }
// dziennik minutowy serwera zsumowany po datach. Serwer z zakresem od-do potrafi oddac sam naglowek: wtedy bierzemy caly dziennik
static void pobierzDni30(std::string dz, std::string od, time_t n, bool caly) {
  Api::zlec("GET", caly ? std::string("/api/energia.csv") : "/api/energia.csv?od=" + od + "&do=" + dz, "", [dz, od, n, caly](int k, const std::string& body) { if (k != 200) { A.dni30Nast = A.teraz + 20; return; } std::map<std::string, double> suma; std::istringstream f(body); std::string w; bool pierwszy = true; while (std::getline(f, w)) { if (pierwszy) { pierwszy = false; continue; } if (w.size() < 12) continue; size_t c1 = w.find(','), c2 = w.find(',', c1 + 1), c3 = w.find(',', c2 + 1), c4 = w.find(',', c3 + 1); if (c4 == std::string::npos) continue; std::string d = w.substr(0, 10); if (d < od) continue; suma[d] += atof(w.substr(c3 + 1, c4 - c3 - 1).c_str()); }
    if (suma.empty() && !caly) { pobierzDni30(dz, od, n, true); return; }
    std::vector<Slupek> o; for (int i = 29; i >= 0; i--) { time_t q = n - i * 86400; tm lq = *localtime(&q); char b[16]; strftime(b, 16, "%Y-%m-%d", &lq); Slupek sl; sl.data = b; auto it = suma.find(b); sl.v = it == suma.end() ? 0 : it->second / 1000; o.push_back(sl); } A.dni30 = o; A.dni30Dla = dz; });
}
// energia dzien po dniu z ostatnich 30 dni: dziennik minutowy serwera zsumowany po datach
void Aplikacja::odswiezDni30() {
  time_t n = time(nullptr); tm l = *localtime(&n); char dzis[16], od[16]; strftime(dzis, 16, "%Y-%m-%d", &l); time_t p = n - 29 * 86400; tm lp = *localtime(&p); strftime(od, 16, "%Y-%m-%d", &lp); dni30Nast = teraz + 3600;
  if (!prawdziwe) { dni30.clear(); for (int i = 29; i >= 0; i--) { time_t q = n - i * 86400; tm lq = *localtime(&q); char b[16]; strftime(b, 16, "%Y-%m-%d", &lq); Slupek sl; sl.data = b; bool jest_ = false; for (auto& d : s.energia.dni) if (d.data == b) { sl.v = d.v; jest_ = true; } if (!jest_) sl.v = 6.5 + 3.5 * std::fabs(std::sin(i * 1.7)); dni30.push_back(sl); } dni30Dla = dzis; return; }
  pobierzDni30(dzis, od, n, false);
}
void Aplikacja::wczytaj() {
  auto J = json::parse(GEN_TEKSTY); for (auto& [k, v] : J["en"].items()) teksty[k][0] = v.get<std::string>(); for (auto& [k, v] : J["pl"].items()) teksty[k][1] = v.get<std::string>(); for (auto& e : DODATKOWE) teksty[e[0]] = {e[1], e[2]};
  auto K = json::parse(GEN_SKORKI); for (auto& [k, v] : K.items()) wbudowane[k] = zJson(v); for (auto& p : definicjePaneli()) P[p.id] = p; ukladDomyslny();
  int n = 0; if (SDL_Locale** l = SDL_GetPreferredLocales(&n)) { if (n > 0 && l[0] && std::string(l[0]->language) == "pl") jezyk = 1; SDL_free(l); } if (const char* e = getenv("DSH_PLUS_JEZYK")) jezyk = std::string(e) == "pl";
  char* pp = SDL_GetPrefPath("Masterful", "centrum-misji-v2"); plikUst = std::string(pp ? pp : "") + "ustawienia.json"; SDL_free(pp); std::ifstream f(plikUst); if (!f) return; auto j = json::parse(f, nullptr, false); if (j.is_discarded() || !j.is_object()) return;
  try {
    if (j.contains("opts")) { auto& q = j["opts"]; o.skorka = q.value("skin", o.skorka); o.twarz = q.value("face", o.twarz); o.rings = q.value("rings", false); o.react = q.value("react", true); o.tray = q.value("tray", false); o.caption = q.value("caption", true); o.karty = q.value("cards", true); o.widmo = q.value("trace", true); o.pelneNazwy = q.value("full_names", false); o.czujniki = q.value("sensors", true); o.przyciaganie = q.value("snap", true); o.ruch = q.value("motion", 1.f); }
    if (j.contains("ekran") && j["ekran"].is_array() && j["ekran"].size() == 2) { o.ekranX = j["ekran"][0].get<int>(); o.ekranY = j["ekran"][1].get<int>(); o.ekranZapisany = true; }
    if (j.contains("okna") && j["okna"].is_object()) { for (auto& [k, v] : j["okna"].items()) if (P.count(k) && v.is_array() && v.size() == 4) okna[k] = {v[0].get<float>(), v[1].get<float>(), v[2].get<float>(), v[3].get<float>()}; swobodny = j.value("swobodny", false) && okna.size() == P.size(); if (!swobodny) okna.clear(); }
    if (j.contains("folds")) for (auto& [k, v] : j["folds"].items()) if (P.count(k)) zwiniete[k] = v; if (j.contains("hidden")) for (auto& [k, v] : j["hidden"].items()) if (P.count(k)) ukryte[k] = v;
    if (j.contains("skins")) for (auto& [k, v] : j["skins"].items()) wlasne[k] = zJson(v); if (j.contains("lang")) jezyk = j["lang"] == "pl"; zoom = std::clamp(j.value("zoom", 1.f), .7f, 2.f);
    if (j.contains("models")) { auto& q = j["models"]; mSort = q.value("sort", mSort); mMalejaco = q.value("desc", false); mFiltr = std::clamp(q.value("filter", 0), 0, 2); if (q.contains("engines") && q["engines"].is_object()) for (auto& [k, v] : q["engines"].items()) if (v.is_string()) silniki[k] = v; }
    energiaTryb = j.value("energy_view", energiaTryb); if (energiaTryb != "godziny" && energiaTryb != "dni30" && energiaTryb != "miesiac") energiaTryb = "godziny"; gpuMiara = j.value("gpu_metric", gpuMiara);
    if (j.contains("disks_hidden") && j["disks_hidden"].is_array()) for (auto& v : j["disks_hidden"]) if (v.is_string()) dyskiUkryte.push_back(v);
  } catch (...) {}
  if (const char* e = getenv("DSH_PLUS_JEZYK")) jezyk = std::string(e) == "pl"; if (!skorki().count(o.skorka)) o.skorka = "ice";
}
void Aplikacja::zapisz() {
  json ok = json::object(), sk = json::object(); for (auto& [k, v] : okna) ok[k] = json::array({v.x, v.y, v.w, v.h}); for (auto& [k, v] : wlasne) sk[k] = doJson(v);
  json j = {{"opts", {{"skin", o.skorka}, {"face", o.twarz}, {"rings", o.rings}, {"react", o.react}, {"tray", o.tray}, {"caption", o.caption}, {"cards", o.karty}, {"trace", o.widmo}, {"full_names", o.pelneNazwy}, {"sensors", o.czujniki}, {"snap", o.przyciaganie}, {"motion", o.ruch}}}, {"okna", ok}, {"swobodny", swobodny}, {"folds", zwiniete}, {"hidden", ukryte}, {"skins", sk}, {"lang", jezyk ? "pl" : "en"}, {"zoom", zoom},
    {"models", {{"sort", mSort}, {"desc", mMalejaco}, {"filter", mFiltr}, {"engines", silniki}}}, {"energy_view", energiaTryb}, {"gpu_metric", gpuMiara}, {"disks_hidden", dyskiUkryte}};
  if (o.ekranZapisany) j["ekran"] = json::array({o.ekranX, o.ekranY}); std::string tmp = plikUst + ".nowy"; { std::ofstream f(tmp); f << j.dump(1); if (!f) return; } std::error_code e; std::filesystem::rename(tmp, plikUst, e);
}
std::map<std::string, Skorka> Aplikacja::skorki() { auto m = wbudowane; for (auto& [k, v] : wlasne) m[k] = v; return m; }
void Aplikacja::ustawSkorke(const Skorka& s_) { auto& m = ui::M; m.acc = hex(s_.accent); m.rim = hex(s_.rim); m.bg = hex(s_.bg); m.panel = hex(s_.panel); m.txt = hex(s_.text); m.warn = hex(s_.warn); m.bad = hex(s_.bad); m.pa = s_.opacity; m.r = s_.radius; m.numInter = s_.font == "Inter"; ui::styl();
  StylTwarzy y{}; auto c = [](const std::string& h, float* o_) { Kolor k = hex(h); o_[0] = k.r; o_[1] = k.g; o_[2] = k.b; }; c(s_.accent, y.accent); c(s_.rim, y.rim); c(s_.eye, y.eye); c(s_.warn, y.warn); c(s_.bad, y.bad); c(s_.bg, y.bg); y.contours = s_.contours; y.fine = s_.fine; y.points = s_.points; y.glass = s_.glass; y.glow = s_.glow; y.scan = s_.scan; y.density = s_.density; Twarz::ustawStyl(y); }
void Aplikacja::wybierzSkorke(const std::string& k) { o.skorka = k; ustawSkorke(skorki()[k]); zapisz(); }
void Aplikacja::zapiszSkorke(Skorka s_) { std::string k = "u_"; for (char c : s_.name) k += isalnum((unsigned char)c) ? (char)tolower(c) : '_'; wlasne[k] = s_; wybierzSkorke(k); }
void Aplikacja::wybierzTwarz(const std::string& k) { o.twarz = k; Twarz::ustawWyglad(k); zapisz(); }
void Aplikacja::wybierzJezyk(int j) { jezyk = j; uzupelnijModele(); zapisz(); }
void Aplikacja::toast(const std::string& m) { toastTxt = m; toastDo = teraz + 2.2; }
void Aplikacja::zwin(const std::string& id, int v) { zwiniete[id] = v < 0 ? !zwiniete[id] : v; zapisz(); }
void Aplikacja::pokazPanel(const std::string& id, bool w) { ukryte[id] = !w; if (w) zwiniete[id] = false; zapisz(); }
void Aplikacja::resetUkladu() { ukladDomyslny(); zapisz(); toast(L("Default layout restored", "Przywrócono układ domyślny")); }
void Aplikacja::pokazDialog(Dialog d, const std::string& id, int gpu) { if (d == Dialog::Duzy) { duzy = id; zwiniete[id] = false; } if ((d == Dialog::Model || d == Dialog::Import) && !prawdziwe) { toast(t("toast_mock")); return; } dialog = d; dId = id; dGpu = gpu; otworzDialog = true; }
void Aplikacja::potwierdz(const std::string& q, std::function<void()> f) { dTekst = q; dAkcja = std::move(f); pokazDialog(Dialog::Potwierdz); }
void Aplikacja::akcjaModelu(const std::string& a, const std::string& id, int w) {
  Model* m = modelPoId(id); if (!m) return; std::string u_ = "/api/models/" + id;
  if (a == "switch") return pokazDialog(Dialog::Przelacz, id);
  if (a == "edit") return pokazDialog(Dialog::Model, id);
  if (a == "props") return pokazDialog(Dialog::Wlasciwosci, id);
  if (a == "log") { dTekst = "…"; pokazDialog(Dialog::Log, id); if (prawdziwe) Api::zlec("GET", u_ + "/log?n=200", "", [](int k, const std::string& s_) { std::string o_; auto j = json::parse(s_, nullptr, false); if (k == 200 && j.is_object()) for (auto& l : j.value("lines", json::array())) if (l.is_string()) o_ += l.get<std::string>() + "\n"; A.dTekst = o_.empty() ? s_ : o_; }); else dTekst = "[sim] " + m->id + " port " + std::to_string(m->port) + "\n[sim] ctx " + std::to_string(m->ctx) + "\n[sim] " + (m->pl.empty() ? "" : m->pl[0].opis); return; }
  if (a == "force") return potwierdz(std::string(t("conf_force")) + " " + m->name + "?", [this, id, u_] { if (prawdziwe) Api::zlec("POST", u_ + "/stop?force=1", "", odp); else Sim::stop(s, f, id); });
  if (a == "remove") return potwierdz(std::string(t("conf_remove")) + " " + m->name + "?", [this, u_] { if (prawdziwe) Api::zlec("DELETE", u_, "", odp); else toast(t("toast_mock")); });
  if (!prawdziwe) { if (a == "start") Sim::start(s, f, id); else if (a == "stop") Sim::stop(s, f, id); else if (a == "restart") { Sim::stop(s, f, id); Sim::start(s, f, id); } else if (a == "pin") m->pin = !m->pin; return; }
  if (a == "start") Api::zlec("POST", u_ + "/start", json{{"placement_index", w}}.dump(), odp);
  else if (a == "stop") Api::zlec("POST", u_ + "/stop", "", odp);
  else if (a == "restart") Api::zlec("POST", u_ + "/stop", "", [u_](int k, const std::string& b) { odp(k, b); if (k >= 200 && k < 300) Api::zlec("POST", u_ + "/start", "{}", odp); });
  else if (a == "pin") Api::zlec("PUT", u_ + "/pin", json{{"pin", m->pin ? 0 : 1}}.dump(), odp);
}
void Aplikacja::zwolnijGpu(int i) { if (prawdziwe) Api::zlec("POST", "/api/gpus/" + std::to_string(i) + "/free", "", [i](int k, const std::string& b) { odp(k, b); if (k == 200) A.toast("GPU " + std::to_string(i) + ": OK"); }); else if (auto* m = Sim::dziala(s)) { if (std::count(m->gpus.begin(), m->gpus.end(), i)) Sim::stop(s, f); } }
void Aplikacja::zabijProces(int pid, const std::string& n) { potwierdz(std::string(t("conf_kill")) + " " + std::to_string(pid) + " (" + n + ")?", [this, pid] { if (prawdziwe) Api::zlec("POST", "/api/orphans/" + std::to_string(pid) + "/kill", "", odp); else toast(t("toast_mock")); }); }
void Aplikacja::kopiuj(const std::string& s_) { SDL_SetClipboardText(s_.c_str()); toast(t("copied")); }
void Aplikacja::raportEnergii() { if (!prawdziwe) { toast(t("toast_mock")); return; } pokazDialog(Dialog::Raport); }
void Aplikacja::csvEnergii() { if (!prawdziwe) { toast(t("toast_mock")); return; } SDL_OpenURL((Api::adres() + "/api/energia.csv").c_str()); }
// katalog w menedzerze plikow: sciezka zakodowana, bo bywa w niej spacja, wykrzyknik albo polska litera
void Aplikacja::otworzSciezke(const std::string& p) {
#ifdef _WIN32
  SDL_OpenURL(p.c_str());
#else
  std::string u_ = "file://"; char b[4]; for (unsigned char c : p) { if (isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') u_ += (char)c; else { snprintf(b, 4, "%%%02X", c); u_ += b; } } if (!SDL_OpenURL(u_.c_str())) toast(SDL_GetError());
#endif
}
// Start z systemem: stan czytany z systemu, nie z ustawien
#ifdef _WIN32
static const wchar_t* KLUCZ_RUN = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
bool Aplikacja::startZSystemem() { HKEY k; if (RegOpenKeyExW(HKEY_CURRENT_USER, KLUCZ_RUN, 0, KEY_READ, &k)) return false; bool v = RegQueryValueExW(k, L"CentrumMisjiV2", nullptr, nullptr, nullptr, nullptr) == 0; RegCloseKey(k); return v; }
void Aplikacja::ustawStartZSystemem(bool wl) { HKEY k; if (RegOpenKeyExW(HKEY_CURRENT_USER, KLUCZ_RUN, 0, KEY_SET_VALUE, &k)) return; if (wl) { wchar_t p[1024]; DWORD n = GetModuleFileNameW(nullptr, p + 1, 1020); p[0] = L'"'; p[n + 1] = L'"'; p[n + 2] = 0; RegSetValueExW(k, L"CentrumMisjiV2", 0, REG_SZ, (const BYTE*)p, (DWORD)((n + 3) * sizeof(wchar_t))); } else RegDeleteValueW(k, L"CentrumMisjiV2"); RegCloseKey(k); }
#else
static std::filesystem::path plikStartu() { const char* c = getenv("XDG_CONFIG_HOME"); std::filesystem::path p = c && *c ? c : std::string(getenv("HOME") ? getenv("HOME") : ".") + "/.config"; return p / "autostart" / "centrum-misji-v2.desktop"; }
bool Aplikacja::startZSystemem() { return std::filesystem::exists(plikStartu()); }
void Aplikacja::ustawStartZSystemem(bool wl) { auto p = plikStartu(); std::error_code e; if (!wl) { std::filesystem::remove(p, e); return; } std::filesystem::create_directories(p.parent_path(), e); auto x = std::filesystem::read_symlink("/proc/self/exe", e); std::ofstream(p) << "[Desktop Entry]\nType=Application\nName=Mission Control\nExec=\"" << x.string() << "\"\nTerminal=false\n"; }
#endif
