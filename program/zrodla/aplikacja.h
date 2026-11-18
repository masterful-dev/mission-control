#pragma once
// Stan aplikacji: ustawienia, uklad paneli, to co pod kursorem, okna dialogowe.
#include <imgui.h>
#include <imgui_internal.h>
#include "stan.h"
#include "twarz/twarz.h"
#include <array>
#include <functional>
#include <map>
#include <string>
#include <vector>

const char* t(const std::string& klucz);
const char* L(const char* en, const char* pl);          // tekst w biezacym jezyku

struct Skorka { std::string name, accent, rim, eye, bg, panel, text, warn, bad, font; float contours = 1, fine = 1, points = .5f, glass = .5f, glow = 1, scan = .25f, density = .6f, opacity = .8f, radius = 6; };
struct Opcje {
  std::string skorka = "ice", twarz = "holo";
  bool warunki = false;                  // warunki korzystania zaakceptowane (pytanie przy pierwszym uruchomieniu)
  bool rings = false, react = true;      // rings: luki i pasy za twarza z oryginalu (domyslnie wylaczone)
  bool startup = false, tray = false, caption = true, karty = true, widmo = true, pelneNazwy = false, czujniki = true, przyciaganie = true, ekranZapisany = false;
  float ruch = 1; int ekranX = 0, ekranY = 0;
};
// To, co jest pod kursorem: prawy przycisk buduje z tego menu. el mowi, jaki to element.
struct Cel {
  std::string el, panel, model, dysk, wartosc, proces, opis;
  int gpu = -1, pid = -1; long zapytanie = -1; bool scena = false, gora = false;
};
struct Prost { float x = 0, y = 0, w = 0, h = 0; };      // ulamki pola pod gornym paskiem
enum { CH_L = 1, CH_P = 2, CH_G = 4, CH_D = 8, CH_RUCH = 16 };
struct Chwyt { std::string id; int tryb = 0; bool aktywny = false; ImVec2 m0; ImRect r0; };
struct Panel { std::string id, tytul; bool grow = false; float natH = 0, naglowekH = 0; ImRect r; bool widac = false; };
enum class Dialog { Brak, Ustawienia, Zwolnij, Sprawnosc, Przelacz, Log, Model, Import, Raport, Potwierdz, Duzy, Wlasciwosci, Warunki };
struct SilnikDef { std::string id, nazwa, znak; unsigned rgb; };

struct Aplikacja {
  Opcje o; std::map<std::string, Prost> okna; bool swobodny = false; std::map<std::string, bool> zwiniete, ukryte; std::map<std::string, Skorka> wlasne; int jezyk = 0;
  Stan s; Praca f; std::array<HistGpu, 8> hist; bool prawdziwe = false, wymusSim = false; double teraz = 0, nastHist = 0;
  std::map<std::string, Panel> P; bool skupienie = false, demoOff = false; int widok = 0; float zoom = 1;
  Cel pod, menu; Chwyt chwyt;
  std::string toastTxt; double toastDo = 0;
  Dialog dialog = Dialog::Brak; bool otworzDialog = false; std::string dId, dTekst; int dGpu = 0; std::function<void()> dAkcja;
  std::string ustTab = "general"; std::string duzy;
  int gpuSel = -1; std::string gpuMiara = "vram", energiaTryb = "godziny", modelRoz; bool energiaZasady = false; std::map<std::string, int> wariant;
  // lista modeli
  std::string mSort = "gpu"; bool mMalejaco = false; int mFiltr = 0; std::map<std::string, std::string> silniki;
  // dyski tego komputera i energia z 30 dni
  std::vector<Dysk> dyski; std::vector<std::string> dyskiUkryte; std::vector<Slupek> dni30; std::string dni30Dla; double dni30Nast = 0;
  float zoomTwarzyMax = 1;                              // najwieksze przyblizenie, przy ktorym glowa miesci sie w wolnym polu
  double puls = -9;                                      // chwila klikniecia w scene
  std::string daneDir, plikUst;
  // czynnosci
  void wczytaj(); void zapisz();
  std::map<std::string, Skorka> skorki(); void ustawSkorke(const Skorka& s); void wybierzSkorke(const std::string& k); void zapiszSkorke(Skorka s);
  void wybierzTwarz(const std::string& k); void wybierzJezyk(int j); void toast(const std::string& m);
  void zwin(const std::string& id, int v = -1); void pokazPanel(const std::string& id, bool widac); void resetUkladu(); void pokazDialog(Dialog d, const std::string& id = "", int gpu = 0);
  void akcjaModelu(const std::string& a, const std::string& id, int wariant = 0); void zwolnijGpu(int i); void zabijProces(int pid, const std::string& nazwa);
  void kopiuj(const std::string& s); void potwierdz(const std::string& pytanie, std::function<void()> f);
  void raportEnergii(); void csvEnergii(); void otworzSciezke(const std::string& p); bool startZSystemem(); void ustawStartZSystemem(bool wl);
  void uzupelnijModele(); void ustawSilnik(const std::string& model, const std::string& silnik); void odswiezDni30();
  bool dyskUkryty(const std::string& p) const; void ukryjDysk(const std::string& p, bool ukryj);
};
extern Aplikacja A;

const std::vector<SilnikDef>& silnikiZnane();
SilnikDef silnikPoId(const std::string& id);             // nieznany: wlasna nazwa, znak z pierwszej litery
std::string kluczSortu(const std::string& kol);
// panele.cpp
void rysujCialoPanelu(const std::string& id, float w, float hDost);
void rysujAkcjePanelu(const std::string& id, ImDrawList* d, float xPrawy, float y, float& zuzyte);
std::string podpisPanelu(const std::string& id);
const std::vector<Panel>& definicjePaneli();
std::string opisModelu(const Model& m);                  // tresc chmurki i okna wlasciwosci
std::string opisGpu(const Gpu& g);
std::string opisDysku(const Dysk& d);
const std::vector<Dysk>& dyskiWidoczne(bool wszystkie = false);
// okno.cpp, menu.cpp, dialogi.cpp
void budujInterfejs();
void rysujMenu();
void rysujDialogi();
Model* modelPoId(const std::string& id);
struct OpisStanu { std::string lab, kl, info, c; };
OpisStanu opisStanu();                                   // etykieta z panelu LLM, ta sama w podpisie sceny
