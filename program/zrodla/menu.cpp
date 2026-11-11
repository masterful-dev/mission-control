// Menu prawego przycisku: tylko to, co dotyczy miejsca pod kursorem. Menu calego programu jest na tle sceny i na gornym pasku.
#include "aplikacja.h"
#include "sim.h"
#include "api.h"
#include "ui.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
using namespace ui;
namespace {
bool poz(const std::string& l, bool on = false, bool wl = true) { return ImGui::MenuItem(l.c_str(), nullptr, on, wl); }
void info(const std::string& s) { ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(u(M.acc))); ImGui::PushFont(fUi(W7), px(10.5f)); ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + px(300)); ImGui::TextUnformatted(wielkie(s).c_str()); ImGui::PopTextWrapPos(); ImGui::PopFont(); ImGui::PopStyleColor(); ImGui::Dummy({1, px(2)}); }
void linia() { ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0}); ImGui::Dummy({1, px(3)}); ImGui::Separator(); ImGui::Dummy({1, px(3)}); ImGui::PopStyleVar(); }
std::string nazwa(const Model& m) { return A.o.pelneNazwy || m.krotka.empty() ? m.name : m.krotka; }
std::string skrot(const std::string& s, size_t n = 44) { return s.size() > n ? s.substr(0, n - 2) + "…" : s; }
void kopiujWartosc(const Cel& c) { if (!c.wartosc.empty() && poz(std::string(L("Copy: ", "Kopiuj: ")) + skrot(c.wartosc, 40))) A.kopiuj(c.wartosc); }
void podmenuSilnika(Model& m) {
  if (!ImGui::BeginMenu(L("Engine", "Silnik"))) return; bool wlasny = A.silniki.count(m.id);
  if (poz(L("Detect automatically", "Wykrywaj automatycznie"), !wlasny)) A.ustawSilnik(m.id, ""); linia();
  for (auto& s : silnikiZnane()) if (poz(s.nazwa, m.silnik == s.id)) A.ustawSilnik(m.id, s.id); ImGui::EndMenu();
}
void menuModelu(Model& m) {
  info(nazwa(m)); bool stoi = m.status == "stopped";
  if (stoi) { if (m.pl.size() > 1) { if (ImGui::BeginMenu((std::string("▶ ") + t("start")).c_str())) { for (int i = 0; i < (int)m.pl.size(); i++) if (poz(m.pl[i].opis)) A.akcjaModelu("start", m.id, i); ImGui::EndMenu(); } } else if (poz(std::string("▶ ") + t("start"))) A.akcjaModelu("start", m.id); }
  else { if (poz(t("stop"))) A.akcjaModelu("stop", m.id); if (poz(t("restart"))) A.akcjaModelu("restart", m.id); if (poz(t("force"))) A.akcjaModelu("force", m.id); }
  if (poz(std::string(t("switch")) + "…")) A.akcjaModelu("switch", m.id); linia();
  if (poz(L("Properties…", "Właściwości…"))) A.akcjaModelu("props", m.id); podmenuSilnika(m);
  if (poz(std::string(t("edit")) + "…")) A.akcjaModelu("edit", m.id); if (poz(t("log"))) A.akcjaModelu("log", m.id); if (poz(m.pin ? t("unpin") : t("pin"))) A.akcjaModelu("pin", m.id); linia();
  if (poz(L("Copy name", "Kopiuj nazwę"))) A.kopiuj(m.name); if (poz(L("Copy address", "Kopiuj adres"))) A.kopiuj("http://127.0.0.1:" + std::to_string(m.port)); linia();
  if (poz(t("remove"))) A.akcjaModelu("remove", m.id);
}
void menuListy() {
  info(t("p_models")); if (poz(t("add_model"))) A.pokazDialog(Dialog::Model); if (poz(t("import"))) A.pokazDialog(Dialog::Import); linia();
  if (ImGui::BeginMenu(L("Sort by", "Sortuj według"))) { struct { const char* id; const char* en; const char* pl; } S_[] = {{"gpu", "Number of cards", "Liczba kart"}, {"nazwa", "Name", "Nazwa"}, {"silnik", "Engine", "Silnik"}, {"ctx", "Context", "Kontekst"}, {"vram", "VRAM per card", "VRAM na kartę"}, {"port", "Port", "Port"}, {"stan", "State", "Stan"}}; for (auto& q : S_) if (poz(L(q.en, q.pl), A.mSort == q.id)) { A.mSort = q.id; A.zapisz(); } linia(); if (poz(L("Ascending", "Rosnąco"), !A.mMalejaco)) { A.mMalejaco = false; A.zapisz(); } if (poz(L("Descending", "Malejąco"), A.mMalejaco)) { A.mMalejaco = true; A.zapisz(); } ImGui::EndMenu(); }
  if (ImGui::BeginMenu(L("Show", "Pokaż"))) { const char* n[3] = {L("All models", "Wszystkie modele"), L("1 GPU · TP1", "1 GPU · TP1"), L("2 GPU · TP2", "2 GPU · TP2")}; for (int i = 0; i < 3; i++) if (poz(n[i], A.mFiltr == i)) { A.mFiltr = i; A.zapisz(); } ImGui::EndMenu(); }
  if (poz(L("Full names from the registry", "Pełne nazwy z rejestru"), A.o.pelneNazwy)) { A.o.pelneNazwy = !A.o.pelneNazwy; A.zapisz(); }
}
void menuGpu(int i, bool karta) {
  if (i < 0 || i >= (int)A.s.gpus.size()) return; auto& g = A.s.gpus[i]; info("GPU " + std::to_string(i) + " · " + g.krotka);
  if (poz(std::string(t("ctx_free")))) A.pokazDialog(Dialog::Zwolnij, "", i); if (poz(t("ctx_details"), A.gpuSel == i)) { A.gpuSel = A.gpuSel == i ? -1 : i; A.ukryte["gpu"] = false; }
  if (!karta && ImGui::BeginMenu(L("Chart shows", "Wykres pokazuje"))) { for (auto [k, n] : std::initializer_list<std::pair<const char*, const char*>>{{"vram", "VRAM %"}, {"util", "UTIL %"}, {"temp", "°C"}, {"power", "W"}}) if (poz(n, A.gpuMiara == k)) { A.gpuMiara = k; A.gpuSel = i; } ImGui::EndMenu(); }
  if (!g.processes.empty() && ImGui::BeginMenu(t("processes"))) { for (auto& p : g.processes) if (poz(p.name + " · " + std::to_string(p.pid) + " · " + n0(p.vram_mb) + " MiB", false, !p.system)) A.zabijProces(p.pid, p.name); ImGui::EndMenu(); }
  linia(); if (poz(L("Copy card summary", "Kopiuj opis karty"))) { std::string s = opisGpu(g); std::replace(s.begin(), s.end(), '\t', ' '); A.kopiuj("GPU " + std::to_string(i) + " " + g.name + "\n" + s); }
}
// lewy przycisk na karcie na scenie: co mozna na niej uruchomic
void menuStartuNaKarcie(int i) {
  info(std::string(L("Start on GPU ", "Uruchom na GPU ")) + std::to_string(i)); bool cos = false;
  for (auto& m : A.s.models) if (m.status != "stopped" && std::count(m.gpus.begin(), m.gpus.end(), i)) { if (poz(std::string(t("stop")) + ": " + skrot(nazwa(m)))) A.akcjaModelu("stop", m.id); cos = true; } if (cos) linia();
  int n1 = 0, n2 = 0;
  for (auto& m : A.s.models) { if (m.status != "stopped" || m.ileGpu != 1) continue; for (int w = 0; w < (int)m.pl.size(); w++) if (m.pl[w].gpus.size() == 1 && m.pl[w].gpus[0] == i) { if (poz("▶ " + skrot(nazwa(m)) + "   " + (m.ctx ? n0(m.ctx / 1024.) + "K" : ""), false, true)) A.akcjaModelu("start", m.id, w); n1++; break; } }
  if (n1) linia();
  for (auto& m : A.s.models) { if (m.status != "stopped" || m.ileGpu < 2) continue; for (int w = 0; w < (int)m.pl.size(); w++) if (std::count(m.pl[w].gpus.begin(), m.pl[w].gpus.end(), i)) { if (poz("▶▶ " + skrot(nazwa(m)) + "   " + L("both cards", "obie karty"))) A.akcjaModelu("start", m.id, w); n2++; break; } }
  if (!n1 && !n2) { ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(u(M.mut))); ImGui::TextUnformatted(L("no models for this card", "brak modeli dla tej karty")); ImGui::PopStyleColor(); }
  linia(); if (poz(t("ctx_free"))) A.pokazDialog(Dialog::Zwolnij, "", i);
}
void menuDysku(const std::string& sciezka) {
  const Dysk* d = nullptr; for (auto& x : dyskiWidoczne(true)) if (x.path == sciezka) d = &x; info(d ? d->nazwa : sciezka);
  if (poz(t("ctx_open_dir"))) A.otworzSciezke(sciezka); if (poz(t("ctx_copy_path"))) A.kopiuj(sciezka); if (d && poz(L("Copy disk summary", "Kopiuj opis dysku"))) { std::string s = opisDysku(*d); std::replace(s.begin(), s.end(), '\t', ' '); A.kopiuj(d->nazwa + "\n" + s); } linia();
  if (poz(L("Hide this disk", "Ukryj ten dysk"))) A.ukryjDysk(sciezka, true);
}
void menuDyskow() {
  if (ImGui::BeginMenu(L("Disks on the list", "Dyski na liście"))) { for (auto& x : dyskiWidoczne(true)) if (poz(x.nazwa + "  " + x.path, !A.dyskUkryty(x.path))) A.ukryjDysk(x.path, !A.dyskUkryty(x.path)); linia(); if (poz(L("Show all", "Pokaż wszystkie"))) { A.dyskiUkryte.clear(); A.zapisz(); } ImGui::EndMenu(); }
  if (poz(L("Sensors under the list", "Czujniki pod listą"), A.o.czujniki)) { A.o.czujniki = !A.o.czujniki; A.zapisz(); }
}
void menuEnergii(const Cel& c) {
  if (ImGui::BeginMenu(L("Chart shows", "Wykres pokazuje"))) { for (auto [k, n] : std::initializer_list<std::pair<const char*, const char*>>{{"godziny", L("48 hours", "48 godzin")}, {"dni30", L("30 days", "30 dni")}, {"miesiac", L("Current month", "Obecny miesiąc")}}) if (poz(n, A.energiaTryb == k)) { A.energiaTryb = k; A.zapisz(); if (A.energiaTryb == "dni30") A.odswiezDni30(); } ImGui::EndMenu(); }
  if (poz(t("ctx_report"))) A.raportEnergii(); if (poz(t("e_eff"))) A.pokazDialog(Dialog::Sprawnosc); if (poz(t("e_csv"))) A.csvEnergii(); if (poz(t("e_rules"), A.energiaZasady)) A.energiaZasady = !A.energiaZasady; (void)c;
}
void menuPanelu(const std::string& id) {
  bool zw = A.zwiniete[id]; if (poz(zw ? L("Unfold", "Rozwiń") : L("Fold", "Zwiń"))) A.zwin(id); if (poz(t("ctx_big"))) A.pokazDialog(Dialog::Duzy, id); if (poz(L("Hide this panel", "Ukryj ten panel"))) A.pokazPanel(id, false);
  if (A.swobodny && poz(L("Default layout of all panels…", "Układ domyślny wszystkich paneli…"))) A.potwierdz(L("Restore the default layout? Your own positions and sizes of the panels will be forgotten.", "Przywrócić układ domyślny? Własne położenia i rozmiary paneli zostaną zapomniane."), [] { A.resetUkladu(); });
}
void podmenuPaneli() { if (ImGui::BeginMenu(t("ctx_panels"))) { for (auto& p : definicjePaneli()) if (poz(t(p.tytul), !A.ukryte[p.id])) A.pokazPanel(p.id, A.ukryte[p.id]); ImGui::EndMenu(); } }
void podmenuZrodla() { if (ImGui::BeginMenu(t("ctx_data"))) { if (poz(t("data_real"), A.prawdziwe, Api::polaczone() || A.wymusSim)) { A.wymusSim = false; } if (poz(t("data_sim"), !A.prawdziwe)) { A.wymusSim = true; A.prawdziwe = false; } ImGui::EndMenu(); } }
void menuSceny() {
  info(L("Scene", "Scena"));
  if (ImGui::BeginMenu(t("ctx_face"))) { for (const char* k : {"holo", "cyber"}) if (poz(t(std::string("face_") + k), A.o.twarz == k)) A.wybierzTwarz(k); ImGui::EndMenu(); }
  if (poz(t("ctx_recenter"))) { auto& U = Twarz::uzytkownik(); U.yaw = U.pitch = 0; U.zoom = A.zoomTwarzyMax; }
  if (poz(t("show_ring"), A.o.rings)) { A.o.rings = !A.o.rings; A.zapisz(); } if (poz(t("react"), A.o.react)) { A.o.react = !A.o.react; A.zapisz(); }
  if (poz(L("Small card bar at the bottom of the scene", "Mały pasek kart na dole sceny"), A.o.karty)) { A.o.karty = !A.o.karty; A.zapisz(); } if (poz(L("Live base instead of the voice wave on the slab", "Żywa podstawa zamiast fali głosu na tafli"), A.o.widmo)) { A.o.widmo = !A.o.widmo; A.zapisz(); } if (poz(L("Caption with the model's activity", "Podpis z czynnością modelu"), A.o.caption)) { A.o.caption = !A.o.caption; A.zapisz(); }
  if (ImGui::BeginMenu(L("Motion", "Ruch"))) { for (auto [v, n] : std::initializer_list<std::pair<float, const char*>>{{0.f, L("Still", "Bez ruchu")}, {.5f, L("Calm", "Spokojny")}, {1.f, L("Normal", "Zwykły")}, {1.6f, L("Lively", "Żywy")}}) if (poz(n, std::fabs(A.o.ruch - v) < .05f)) { A.o.ruch = v; A.zapisz(); } ImGui::EndMenu(); }
  if (poz(A.skupienie ? t("ctx_exit_focus") : L("Scene only (F)", "Sama scena (F)"))) A.skupienie = !A.skupienie; linia();
}
void menuProgramu() {
  if (ImGui::BeginMenu(t("ctx_models"))) { for (auto& m : A.s.models) { bool run = m.status != "stopped"; if (poz(std::string(m.ileGpu >= 2 ? "▣▣ " : m.ileGpu == 1 ? "▣ " : "· ") + skrot(nazwa(m)), run)) A.akcjaModelu(run ? "stop" : "start", m.id); } ImGui::EndMenu(); }
  podmenuPaneli(); if (ImGui::BeginMenu(t("ctx_skin"))) { for (auto& [k, s] : A.skorki()) if (poz(s.name, k == A.o.skorka)) A.wybierzSkorke(k); ImGui::EndMenu(); }
  if (ImGui::BeginMenu(t("ctx_lang"))) { if (poz("English", A.jezyk == 0)) A.wybierzJezyk(0); if (poz("Polski", A.jezyk == 1)) A.wybierzJezyk(1); ImGui::EndMenu(); }
  if (ImGui::BeginMenu(t("ctx_ui_size"))) { for (int z : {80, 90, 100, 110, 125, 150, 175, 200}) if (poz(std::to_string(z) + "%", std::abs(A.zoom * 100 - z) < 1)) { A.zoom = z / 100.f; A.zapisz(); } ImGui::EndMenu(); }
  podmenuZrodla(); linia();
  if (poz(t("ctx_fullscreen"))) { SDL_Window* w = SDL_GL_GetCurrentWindow(); SDL_SetWindowFullscreen(w, !(SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN)); }
  if (poz(std::string(t("ctx_reset")) + (A.swobodny ? "…" : ""))) { if (A.swobodny) A.potwierdz(L("Restore the default layout? Your own positions and sizes of the panels will be forgotten.", "Przywrócić układ domyślny? Własne położenia i rozmiary paneli zostaną zapomniane."), [] { A.resetUkladu(); }); else A.resetUkladu(); }
  if (poz(t("ctx_settings"))) A.pokazDialog(Dialog::Ustawienia); if (poz(t("ctx_quit"))) { SDL_Event e{}; e.type = SDL_EVENT_QUIT; SDL_PushEvent(&e); }
}
}
void rysujMenu() {
  ImGui::SetNextWindowSizeConstraints({px(200), 0}, {FLT_MAX, ui::H * S - px(10)}); ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {px(12), px(10)}); ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {px(16), px(9)}); ImGui::PushFont(fUi(W5), px(12));
  if (ImGui::BeginPopup("##menu", ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove)) {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
    const Cel& c = A.menu; const std::string& e = c.el; Model* m = c.model.empty() ? nullptr : modelPoId(c.model);
    if (e == "karta_start") menuStartuNaKarcie(c.gpu);
    else if (e == "karta") { menuGpu(c.gpu, true); linia(); menuSceny(); }
    else if (e == "proces" && c.pid >= 0) { info(c.proces + " · PID " + std::to_string(c.pid)); if (poz(t("ctx_kill"))) A.zabijProces(c.pid, c.proces); if (poz(L("Copy PID", "Kopiuj PID"))) A.kopiuj(std::to_string(c.pid)); if (c.gpu >= 0 && poz(std::string(t("ctx_free")))) A.pokazDialog(Dialog::Zwolnij, "", c.gpu); }
    else if (m && c.panel != "llm") { menuModelu(*m); if (c.panel == "models") { linia(); menuListy(); } }
    else if (e == "gpu" || (c.gpu >= 0 && c.panel == "gpu")) { menuGpu(c.gpu, false); if (!c.wartosc.empty()) { linia(); kopiujWartosc(c); } }
    else if (e == "dysk" && !c.dysk.empty()) { menuDysku(c.dysk); linia(); menuDyskow(); }
    else if (e == "slupek") { info(skrot(c.wartosc)); kopiujWartosc(c); if (!c.opis.empty() && poz(L("Report for this day…", "Raport za ten dzień…"))) { extern void raportZaDzien(const std::string&); raportZaDzien(c.opis); } linia(); menuEnergii(c); }
    else if (e == "zapytanie") { info(std::string(t("request")) + " #" + std::to_string(c.zapytanie)); if (poz(L("Copy this row", "Kopiuj ten wiersz"))) A.kopiuj(c.wartosc); if (m && poz(t("log"))) A.akcjaModelu("log", m->id); }
    else if (e == "zegar") { info(L("Clock", "Zegar")); if (poz(L("Copy date and time", "Kopiuj datę i godzinę"))) A.kopiuj(c.wartosc); }
    else if (e == "moc") { info(t("power")); kopiujWartosc(c); linia(); menuEnergii(c); }
    else if (e == "zrodlo") { info(L("Data source", "Źródło danych")); if (poz(L("Live data", "Prawdziwe dane"), A.prawdziwe, Api::polaczone() || A.wymusSim)) A.wymusSim = false; if (poz(t("data_sim"), !A.prawdziwe)) { A.wymusSim = true; A.prawdziwe = false; } if (poz(L("Copy address", "Kopiuj adres"))) A.kopiuj(Api::adres()); }
    else if (e == "widmo" || e == "nvlink" || e == "scena" || e == "twarz") { menuSceny(); menuProgramu(); }
    else if (e == "gora" || e == "marka" || e == "przycisk" || e == "demo") { info(std::string(t("brand")) + " V2"); menuProgramu(); }
    else if (!c.panel.empty()) {
      info(t(A.P.count(c.panel) ? A.P[c.panel].tytul : c.panel)); if (!c.wartosc.empty()) { kopiujWartosc(c); linia(); }
      if (c.panel == "llm" && m) { if (poz(t("stop"))) A.akcjaModelu("stop", m->id); if (poz(t("restart"))) A.akcjaModelu("restart", m->id); if (poz(t("log"))) A.akcjaModelu("log", m->id); if (poz(L("Properties…", "Właściwości…"))) A.akcjaModelu("props", m->id); linia(); }
      if (c.panel == "models") { menuListy(); linia(); } if (c.panel == "energy") { menuEnergii(c); linia(); } if (c.panel == "disks") { menuDyskow(); linia(); }
      if (c.panel == "gpu") { if (ImGui::BeginMenu(t("free_gpu"))) { for (int i = 0; i < (int)A.s.gpus.size(); i++) if (poz("GPU " + std::to_string(i) + " · " + A.s.gpus[i].krotka)) A.pokazDialog(Dialog::Zwolnij, "", i); ImGui::EndMenu(); } if (!A.s.orphans.empty() && ImGui::BeginMenu(t("ctx_orphans"))) { for (auto& o : A.s.orphans) if (poz(std::to_string(o.pid) + " · " + o.name)) A.zabijProces(o.pid, o.name); ImGui::EndMenu(); } linia(); }
      menuPanelu(c.panel); }
    else { menuSceny(); menuProgramu(); }
    ImGui::EndPopup();
  }
  ImGui::PopFont(); ImGui::PopStyleVar(2);
}
