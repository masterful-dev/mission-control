// Centrum Misji: okno programu. Linux i Windows, SDL3 + OpenGL 3.3 + Dear ImGui.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <cstddef>
#include <dlfcn.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif
#include "aplikacja.h"
#include "api.h"
#include "sim.h"
#include "ui.h"
#include "scena.h"
#include "twarz/twarz.h"
#include "dyski.h"
namespace fs = std::filesystem;
namespace {
// program nie wiaze sie z libGL: funkcje bierzemy od SDL
struct { void (*Viewport)(int, int, int, int); void (*ClearColor)(float, float, float, float); void (*Clear)(unsigned); void (*PixelStorei)(unsigned, int); void (*ReadBuffer)(unsigned); void (*ReadPixels)(int, int, int, int, unsigned, unsigned, void*); } G;
template <class F> void wez(F& f, const char* n) { f = (F)SDL_GL_GetProcAddress(n); }
std::string szukajDanych() { const char* b = SDL_GetBasePath(); fs::path p = b ? b : "."; for (auto c : {p / "dane", p / ".." / "program" / "dane", p / ".." / "dane"}) if (fs::exists(c / "czcionki")) return c.string(); return "dane"; }
// rdzenie procesora: API ich nie podaje, liczymy na miejscu
void rdzenie(std::vector<double>& per) {
#ifdef __linux__
  static std::vector<std::pair<double, double>> pop; std::ifstream f("/proc/stat"); std::string l; size_t i = 0; std::vector<double> o; while (std::getline(f, l)) { if (l.rfind("cpu", 0) != 0) break; if (l[3] == ' ') continue; std::istringstream s(l); std::string n; double v, tot = 0, idle = 0; s >> n; for (int k = 0; s >> v; k++) { tot += v; if (k == 3 || k == 4) idle += v; } if (pop.size() <= i) pop.push_back({tot, idle}); double dt = tot - pop[i].first, di = idle - pop[i].second; pop[i] = {tot, idle}; o.push_back(dt > 0 ? 100 * (1 - di / dt) : 0); i++; } per = o;
#else
  (void)per;
#endif
}
void zPrawdziwych(float dt) {
  Praca& f = A.f; const Model* run = nullptr; for (auto& m : A.s.models) if (m.status == "ready" || m.status == "starting" || m.status == "stopping") { run = &m; break; } const Llm* l = A.s.llm.empty() ? nullptr : &A.s.llm[0]; const Aktywne* a = l && l->active ? &*l->active : nullptr; std::string pop = f.status;
  f.status = !run ? "none" : run->status == "starting" ? "loading" : run->status == "stopping" ? "stopping" : "ready"; f.load = l && l->ladowanie ? l->ladowanie->pct / 100 : f.status == "ready" ? 1 : f.load; if (pop == "loading" && f.status == "ready") f.justReady = 2.6f; f.justReady = std::max(0.f, f.justReady - dt); f.down = l && f.status == "ready" && !l->up_ok;
  static std::string pFaza = "idle"; f.phase = !a ? "idle" : a->phase == "generating" ? "answering" : a->phase; if (pFaza != f.phase) { if (pFaza == "reading") Zach::graj("read_done", .9f); if (pFaza == "thinking" && f.phase == "answering") Zach::graj("eureka", 1.7f); if (pFaza == "tool") Zach::graj("tool_ok", 1.1f); pFaza = f.phase; }
  f.el = a ? a->el : 0; f.tg = a ? a->inst : 0; f.pp = a ? a->ptps : 0; f.tgAvg = l && jest(l->sesja.tg_avg) ? l->sesja.tg_avg : 0; f.promptTok = a ? a->in : 0; f.cacheFrac = a && a->in > 0 ? a->cache / a->in : 0; f.kind = a ? a->kind : ""; f.queue = l ? l->queued : 0; f.ctx = l && l->n_ctx ? (float)l->ctx / l->n_ctx : 0; f.tool = a ? a->tool : ""; f.acc = .7f; f.idleS = a ? 0 : f.idleS + dt; f.mouseRecent = A.teraz - Sim::mouseT < 2.5; f.gpuTemp = 0; for (size_t i = 0; i < A.s.gpus.size() && i < 2; i++) f.gpuTemp = std::max<float>(f.gpuTemp, A.s.gpus[i].temp_c); f.vram = A.s.gpus.empty() ? 0 : A.s.gpus[0].vram_used_mb / A.s.gpus[0].vram_total_mb;
}
void aktualizuj(float dt) {
  Api::pompuj(); static Stan swiezy; static bool byl = false; bool nowy = !A.wymusSim && Api::nowyStan(swiezy);
  if (nowy) { if (!A.prawdziwe) { A.prawdziwe = true; for (auto& h : A.hist) h = {}; A.dni30.clear(); A.dni30Nast = 0; } double mg = 0; for (auto& m : swiezy.models) if (m.status == "ready" || m.status == "starting") mg += m.ram; swiezy.ram.model_gb = std::min(mg, swiezy.ram.used_gb); static std::vector<double> per; static double ost = -9; if (A.teraz - ost >= 1) { rdzenie(per); ost = A.teraz; } if (swiezy.cpu.per.empty()) swiezy.cpu.per = per; A.s = swiezy; A.uzupelnijModele(); byl = true; }
  if (A.prawdziwe && !A.wymusSim && !Api::polaczone() && !byl) A.prawdziwe = false;
  if (A.prawdziwe) zPrawdziwych(dt); else { static bool init = false; static double autostart = 0; if (!init || A.s.models.empty() || byl) { Sim::init(A.s, A.f); A.uzupelnijModele(); A.dni30.clear(); A.dni30Nast = 0; init = true; byl = false; autostart = A.teraz + 2.5; } if (autostart > 0 && A.teraz >= autostart) { autostart = 0; if (!Sim::dziala(A.s)) Sim::start(A.s, A.f, "warp-flash-tp2"); } Sim::tick(A.s, A.f, dt, A.teraz); }
  Dyski::nowe(A.dyski);
  // 30 dni energii: przy pierwszym pokazaniu, po zmianie daty i co 10 minut
  if (A.energiaTryb == "dni30" && A.teraz >= A.dni30Nast) A.odswiezDni30();
  if (A.teraz >= A.nastHist) { A.nastHist = A.teraz + .5; for (size_t i = 0; i < A.s.gpus.size() && i < A.hist.size(); i++) { auto& g = A.s.gpus[i]; auto& h = A.hist[i]; h.vram.push_back(g.vram_used_mb / g.vram_total_mb * 100); h.temp.push_back(g.temp_c); h.power.push_back(jest(g.power_w) ? g.power_w : 0); h.util.push_back(g.util_gpu); for (auto* q : {&h.vram, &h.temp, &h.power, &h.util}) if (q->size() > 240) q->pop_front(); } }
}
// zasobnik: ikona z menu, X chowa okno tylko wtedy, gdy ikona naprawde powstala
SDL_Tray* zasobnik = nullptr; SDL_Window* okno_ = nullptr;
#ifndef _WIN32
// GNOME odrzuca prosbe aplikacji ze starym czasem klikniecia i tylko miga ikona: wiadomosc idzie jak z paska zadan, bez czasu
bool aktywujX11() {
  auto p = SDL_GetWindowProperties(okno_); void* d = SDL_GetPointerProperty(p, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr); unsigned long w = (unsigned long)SDL_GetNumberProperty(p, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0); void* x = d && w ? dlopen("libX11.so.6", RTLD_LAZY) : nullptr; if (!x) return false;
  auto atom = (unsigned long (*)(void*, const char*, int))dlsym(x, "XInternAtom"); auto root = (unsigned long (*)(void*))dlsym(x, "XDefaultRootWindow"); auto wyslij = (int (*)(void*, unsigned long, int, long, void*))dlsym(x, "XSendEvent"); auto flush = (int (*)(void*))dlsym(x, "XFlush"); bool ok = atom && root && wyslij && flush;
  if (ok) { struct { int type; unsigned long serial; int send_event; void* display; unsigned long window, message_type; int format; long l[5]; long pad[12]; } e{}; e.type = 33; e.window = w; e.message_type = atom(d, "_NET_ACTIVE_WINDOW", 0); e.format = 32; e.l[0] = 2; wyslij(d, root(d), 0, (1L << 19) | (1L << 20), &e); flush(d); }
  dlclose(x); return ok;
}
#endif
void pokazOkno(void*, SDL_TrayEntry*) {
  SDL_ShowWindow(okno_);
#ifdef _WIN32
  if (SDL_GetWindowFlags(okno_) & SDL_WINDOW_MINIMIZED) SDL_RestoreWindow(okno_);
  SDL_RaiseWindow(okno_);
#else
  if (!aktywujX11()) SDL_RaiseWindow(okno_);
#endif
}
// jedna kopia programu: autostart, ikona, menu i zasobnik trafiaja do tego samego okna
#ifdef _WIN32
HANDLE kopia = nullptr;
bool pierwszaKopia() { kopia = CreateEventW(nullptr, FALSE, FALSE, L"Local\\CentrumMisjiV2Pokaz"); if (kopia && GetLastError() == ERROR_ALREADY_EXISTS) { SetEvent(kopia); return false; } return true; }
bool prosbaOPokaz() { return kopia && WaitForSingleObject(kopia, 0) == WAIT_OBJECT_0; }
#else
int kopia = -1;
bool pierwszaKopia() { sockaddr_un a{}; a.sun_family = AF_UNIX; int n = snprintf(a.sun_path + 1, sizeof a.sun_path - 1, "centrum-misji-v2-%u", (unsigned)getuid()); socklen_t d = offsetof(sockaddr_un, sun_path) + 1 + n; kopia = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0); if (kopia < 0 || !bind(kopia, (sockaddr*)&a, d)) return true; bool zajete = errno == EADDRINUSE; if (zajete) sendto(kopia, "p", 1, 0, (sockaddr*)&a, d); close(kopia); kopia = -1; return !zajete; }
bool prosbaOPokaz() { char b; bool p = false; while (kopia >= 0 && recv(kopia, &b, 1, 0) > 0) p = true; return p; }
#endif
void zamknijProgram(void*, SDL_TrayEntry*) { SDL_Event e{}; e.type = SDL_EVENT_QUIT; SDL_PushEvent(&e); }
void ustawZasobnik() {
  if (A.o.tray == (zasobnik != nullptr)) return; if (!A.o.tray) { SDL_DestroyTray(zasobnik); zasobnik = nullptr; return; }
  SDL_Surface* s = SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_RGBA32); if (!s) return; auto c = ui::M.acc; for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) { float r = std::hypot(x - 15.5f, y - 15.5f); float a = std::max({std::clamp(1.5f - std::fabs(r - 13), 0.f, 1.f), std::clamp(1.5f - std::fabs(r - 7), 0.f, 1.f), std::clamp(3 - r, 0.f, 1.f)}); auto* p = (Uint8*)s->pixels + y * s->pitch + x * 4; p[0] = Uint8(c.r * 255); p[1] = Uint8(c.g * 255); p[2] = Uint8(c.b * 255); p[3] = Uint8(a * 255); }
  zasobnik = SDL_CreateTray(s, t("brand")); SDL_DestroySurface(s); if (!zasobnik) { A.o.tray = false; A.toast(SDL_GetError()); return; } SDL_TrayMenu* m = SDL_CreateTrayMenu(zasobnik); SDL_SetTrayEntryCallback(SDL_InsertTrayEntryAt(m, -1, t("tray_show"), SDL_TRAYENTRY_BUTTON), pokazOkno, nullptr); SDL_SetTrayEntryCallback(SDL_InsertTrayEntryAt(m, -1, t("ctx_quit"), SDL_TRAYENTRY_BUTTON), zamknijProgram, nullptr);
}
// sterowanie z pliku: testy prawdziwej sciezki myszy i rozmiarow okna
struct Test { double doCzasu = 0; std::vector<std::string> linie; size_t i = 0; int czekaj = 0; float x = 0, y = 0; bool jest = false; std::string zrzut; int mierz = 0; std::vector<float> czasy; } T;
void zrzut(const std::string& n, int w, int h) { std::vector<unsigned char> b((size_t)w * h * 3); G.PixelStorei(GL_PACK_ALIGNMENT, 1); G.ReadBuffer(GL_BACK); G.ReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, b.data()); FILE* f = fopen(n.c_str(), "wb"); if (!f) return; fprintf(f, "P6\n%d %d\n255\n", w, h); for (int y = h - 1; y >= 0; y--) fwrite(&b[(size_t)y * w * 3], 1, (size_t)w * 3, f); fclose(f); }
void krokTestu(SDL_Window* win) {
  if (T.czekaj > 0) { T.czekaj--; return; } if (A.teraz < T.doCzasu) return; auto& io = ImGui::GetIO(); while (T.i < T.linie.size()) { std::istringstream s(T.linie[T.i++]); std::string c; s >> c; if (c.empty() || c[0] == '#') continue;
    if (c == "czekaj") { s >> T.czekaj; return; } if (c == "czas") { double q; s >> q; T.doCzasu = A.teraz + q; return; } if (c == "ruch") { s >> T.x >> T.y; T.czekaj = 2; return; } if (c == "klik" || c == "pklik") { s >> T.x >> T.y; int b = c == "klik" ? 0 : 1; io.AddMousePosEvent(T.x, T.y); io.AddMouseButtonEvent(b, true); io.AddMouseButtonEvent(b, false); T.czekaj = 4; return; }
    if (c == "pomiar") { s >> T.mierz; T.czasy.clear(); T.czekaj = T.mierz + 2; return; } if (c == "wcisnij") { s >> T.x >> T.y; io.AddMousePosEvent(T.x, T.y); io.AddMouseButtonEvent(0, true); T.czekaj = 3; return; } if (c == "pusc") { s >> T.x >> T.y; io.AddMousePosEvent(T.x, T.y); io.AddMouseButtonEvent(0, false); T.czekaj = 4; return; } if (c == "kolo") { float d; s >> d; io.AddMouseWheelEvent(0, d); T.czekaj = 2; return; } if (c == "rozmiar") { int w, h; s >> w >> h; SDL_RestoreWindow(win); SDL_SetWindowSize(win, w, h); SDL_SyncWindow(win); T.czekaj = 12; return; } if (c == "zrzut") { s >> T.zrzut; return; } if (c == "klawisz") { std::string k; s >> k; ImGuiKey q = k == "Esc" ? ImGuiKey_Escape : k == "f" ? ImGuiKey_F : ImGuiKey_D; io.AddKeyEvent(q, true); io.AddKeyEvent(q, false); T.czekaj = 3; return; } if (c == "sim") { std::string k; s >> k; Sim::demo(A.s, A.f, k); continue; } if (c == "skorka") { std::string k; s >> k; A.wybierzSkorke(k); continue; } if (c == "jezyk") { int j; s >> j; A.wybierzJezyk(j); continue; } if (c == "twarz") { std::string k; s >> k; A.wybierzTwarz(k); continue; } if (c == "energia") { std::string k; s >> k; A.energiaTryb = k; if (k == "dni30") A.odswiezDni30(); continue; } if (c == "dysk") { for (auto& d : Dyski::teraz()) printf("dysk %s | %s | %s | %s | %s | %.1f/%.1f GB | %.0f C\n", d.nazwa.c_str(), d.path.c_str(), d.opis.c_str(), d.model.c_str(), d.fs.c_str(), d.free_gb, d.total_gb, d.temp_c); fflush(stdout); continue; } if (c == "dwuklik") { s >> T.x >> T.y; io.AddMousePosEvent(T.x, T.y); for (int q = 0; q < 2; q++) { io.AddMouseButtonEvent(0, true); io.AddMouseButtonEvent(0, false); } T.czekaj = 4; return; } if (c == "panel") { std::string k; int v; s >> k >> v; A.pokazPanel(k, v); continue; } if (c == "zwin") { std::string k; int v; s >> k >> v; A.zwin(k, v); continue; } if (c == "zasobnik") { int v; s >> v; A.o.tray = v; ustawZasobnik(); printf("zasobnik=%d blad=%s\n", zasobnik != nullptr, SDL_GetError()); fflush(stdout); continue; } if (c == "tekst") { std::string r; std::getline(s, r); printf("%s\n", r.c_str()); fflush(stdout); continue; } if (c == "koniec") { SDL_Event e{}; e.type = SDL_EVENT_QUIT; SDL_PushEvent(&e); return; } }
}
}
int main(int argc, char** argv) {
  bool dok = getenv("DSH_PLUS_DOK") != nullptr; std::string adres = getenv("DSH_PLUS_CENTRUM") ? getenv("DSH_PLUS_CENTRUM") : "http://127.0.0.1:8770"; int ww = 0, wh = 0; std::string test; A.daneDir.clear();
  for (int i = 1; i < argc; i++) { std::string a = argv[i]; if (a == "--sim") A.wymusSim = true; else if (a == "--dok") dok = true; else if (a == "--adres" && i + 1 < argc) adres = argv[++i]; else if (a == "--rozmiar" && i + 2 < argc) { ww = atoi(argv[++i]); wh = atoi(argv[++i]); } else if (a == "--test" && i + 1 < argc) test = argv[++i]; else if (a == "--dane" && i + 1 < argc) A.daneDir = argv[++i]; }
  if (!dok && test.empty() && !A.wymusSim && !pierwszaKopia()) return 0;
  SDL_SetHint(SDL_HINT_APP_ID, "centrum-misji-v2"); SDL_SetAppMetadata("Mission Control", "2.1.0", "pl.masterful.centrum-misji-v2"); if (!SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
  A.wczytaj();
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE); SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1); SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24); SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
  SDL_WindowFlags fl = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN; if (dok) fl |= SDL_WINDOW_BORDERLESS; else if (!ww) fl |= SDL_WINDOW_MAXIMIZED; if (!test.empty()) fl |= SDL_WINDOW_NOT_FOCUSABLE;
  // ekran startu: ten, na ktorym okno zamknieto, inaczej glowny. Bez tego GNOME otwiera okno tam, gdzie akurat jest kursor
  SDL_DisplayID ekran = 0; if (A.o.ekranZapisany) { SDL_Point q{A.o.ekranX, A.o.ekranY}; ekran = SDL_GetDisplayForPoint(&q); } if (!ekran) ekran = SDL_GetPrimaryDisplay(); int poz = dok ? SDL_WINDOWPOS_UNDEFINED : SDL_WINDOWPOS_CENTERED_DISPLAY(ekran);
  SDL_PropertiesID pw = SDL_CreateProperties(); SDL_SetStringProperty(pw, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Mission Control"); SDL_SetNumberProperty(pw, SDL_PROP_WINDOW_CREATE_X_NUMBER, poz); SDL_SetNumberProperty(pw, SDL_PROP_WINDOW_CREATE_Y_NUMBER, poz); SDL_SetNumberProperty(pw, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, ww ? ww : 1680); SDL_SetNumberProperty(pw, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, wh ? wh : 962); SDL_SetNumberProperty(pw, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, fl);
  SDL_Window* win = SDL_CreateWindowWithProperties(pw); SDL_DestroyProperties(pw); if (!win) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; } SDL_SetWindowMinimumSize(win, 420, 340); SDL_GLContext gl = SDL_GL_CreateContext(win); if (!gl) { fprintf(stderr, "GL: %s\n", SDL_GetError()); return 1; } SDL_GL_MakeCurrent(win, gl); SDL_GL_SetSwapInterval(1); wez(G.Viewport, "glViewport"); wez(G.ClearColor, "glClearColor"); wez(G.Clear, "glClear"); wez(G.PixelStorei, "glPixelStorei"); wez(G.ReadBuffer, "glReadBuffer"); wez(G.ReadPixels, "glReadPixels");
  IMGUI_CHECKVERSION(); ImGui::CreateContext(); auto& io = ImGui::GetIO(); io.IniFilename = nullptr; io.LogFilename = nullptr; ImGui_ImplSDL3_InitForOpenGL(win, gl); ImGui_ImplOpenGL3_Init("#version 330");
  if (A.daneDir.empty()) A.daneDir = szukajDanych(); ui::czcionki(A.daneDir); if (!test.empty()) A.plikUst += ".test"; if (!Scena::init()) fprintf(stderr, "Scena: brak funkcji OpenGL\n"); if (!Twarz::init(A.daneDir)) fprintf(stderr, "Twarz: nie wczytano danych z %s\n", A.daneDir.c_str()); Twarz::ustawWyglad(A.o.twarz); Dyski::start(); A.ustawSkorke(A.skorki()[A.o.skorka]); if (!A.wymusSim) Api::start(adres);
  if (SDL_Surface* ik = SDL_LoadPNG((A.daneDir + "/ikona.png").c_str())) { SDL_SetWindowIcon(win, ik); SDL_DestroySurface(ik); }   // ikona okna i paska zadan
  okno_ = win; SDL_SetWindowTitle(win, "Mission Control"); SDL_ShowWindow(win);
  if (dok) { long long x = SDL_GetNumberProperty(SDL_GetWindowProperties(win), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0); if (x) { printf("DOK okno=%lld\n", x); fflush(stdout); } }
  if (!test.empty()) { std::ifstream f(test); std::string l; while (std::getline(f, l)) T.linie.push_back(l); T.jest = true; }
  bool dziala = true; Uint64 pop = SDL_GetPerformanceCounter(); double czest = SDL_GetPerformanceFrequency(), T0 = 0; float skala = 0, zoomPop = 0;
  while (dziala) {
    SDL_Event e; while (SDL_PollEvent(&e)) { ImGui_ImplSDL3_ProcessEvent(&e); if (e.type == SDL_EVENT_QUIT) dziala = false; if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == SDL_GetWindowID(win)) { if (zasobnik) SDL_HideWindow(win); else dziala = false; } }
    if (!dok && !T.jest) ustawZasobnik();
    if (prosbaOPokaz()) pokazOkno(nullptr, nullptr);
    // GNOME potrafi cofnac maksymalizacje z pierwszego pokazania: okno zostaje wtedy miedzy monitorami, pod paskiem zadan
    static bool pilnujRozmiaru = !dok && !ww && !T.jest; if (pilnujRozmiaru && T0 > 1.5) { pilnujRozmiaru = false; if (!(SDL_GetWindowFlags(win) & SDL_WINDOW_MAXIMIZED)) { fprintf(stderr, "okno: ponowna maksymalizacja\n"); SDL_MaximizeWindow(win); } }
    if (SDL_GetWindowFlags(win) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) { SDL_WaitEventTimeout(nullptr, 200); pop = SDL_GetPerformanceCounter(); continue; }
    Uint64 n = SDL_GetPerformanceCounter(); float dt = std::min(.1f, (float)((n - pop) / czest)); pop = n; T0 += dt; A.teraz = T0;
    if (T.mierz > 0) { T.czasy.push_back(dt * 1000); if (--T.mierz == 0) { auto v = T.czasy; std::sort(v.begin(), v.end()); double su = 0; for (float q : v) su += q; printf("klatki %zu: %.1f kl./s, mediana %.2f ms, p99 %.2f ms, najdluzsza %.1f ms\n", v.size(), 1000 * v.size() / su, v[v.size() / 2], v[v.size() * 99 / 100], v.back()); fflush(stdout); } }
    float sk = SDL_GetWindowDisplayScale(win); if (sk <= 0) sk = 1; if (sk != skala || A.zoom != zoomPop) { skala = sk; zoomPop = A.zoom; ui::S = sk * A.zoom; ui::styl(); Twarz::skala(std::min(sk, 1.5f)); }
    int fw, fh; SDL_GetWindowSizeInPixels(win, &fw, &fh); G.Viewport(0, 0, fw, fh); G.ClearColor(ui::M.bg.r, ui::M.bg.g, ui::M.bg.b, 1); G.Clear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL3_NewFrame(); if (T.jest) { krokTestu(win); io.AddMousePosEvent(T.x, T.y); } ImGui::NewFrame();
    aktualizuj(dt); Scena::rysuj(fw, fh, T0); Twarz::klatka(dt, T0, A.f, A.jezyk, A.o.rings, fw, fh); budujInterfejs(); ImGui::Render(); G.Viewport(0, 0, fw, fh); ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData()); if (!T.zrzut.empty()) { zrzut(T.zrzut, fw, fh); T.zrzut.clear(); } SDL_GL_SwapWindow(win);
  }
  if (!dok && !T.jest) { SDL_Rect r; if (SDL_GetDisplayBounds(SDL_GetDisplayForWindow(win), &r)) { A.o.ekranX = r.x + r.w / 2; A.o.ekranY = r.y + r.h / 2; A.o.ekranZapisany = true; } }
  A.zapisz(); Twarz::koniec(); ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplSDL3_Shutdown(); ImGui::DestroyContext(); SDL_GL_DestroyContext(gl); SDL_DestroyWindow(win); SDL_Quit(); _Exit(0);
}
