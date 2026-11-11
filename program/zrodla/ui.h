#pragma once
// Motyw i klocki rysowane: to, co w makiecie robi style.css. Wszystkie wymiary podane w pikselach CSS, mnozy je ui::S.
#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include <string_view>
#include <vector>

struct Kolor { float r = 0, g = 0, b = 0, a = 1; };
struct Motyw { Kolor acc, rim, bg, panel, txt, mut{.561f, .694f, .753f}, dim{.373f, .518f, .588f}, line{.47f, .863f, 1, .16f}, warn, bad, ok{.275f, .902f, .651f}, think{.659f, .518f, 1}, g[3]{{.224f, .529f, .898f}, {.851f, .349f, .149f}, {.098f, .62f, .439f}}; float pa = .78f, r = 6; bool numInter = false; };

namespace ui {
extern Motyw M; extern float S; extern float W, H;          // W, H: okno w pikselach CSS
inline float px(float v) { return v * S; }
Kolor hex(const std::string& h);
std::string hex(const Kolor& k);
ImU32 u(const Kolor& k, float a = 1);
Kolor mix(const Kolor& a, float p, const Kolor& b);          // color-mix(in srgb, a p, b)
Kolor przezr(const Kolor& a, float p);                       // color-mix(in srgb, a p, transparent)

enum Waga { W5, W6, W7 };
void czcionki(const std::string& dane);
ImFont* fUi(Waga w); ImFont* fMono(Waga w); ImFont* fNum(Waga w);
float szer(ImFont* f, float px_, std::string_view t, float sp = 0);
float wysLinii(float rozm);                                  // line-height 1.35
void tekst(ImDrawList* d, float x, float y, ImFont* f, float px_, ImU32 c, std::string_view t, float sp = 0);
void tekstL(ImDrawList* d, float x, float y, float lh, ImFont* f, float px_, ImU32 c, std::string_view t, float sp = 0);   // wysrodkowany w linii lh
std::string skroc(ImFont* f, float px_, const std::string& t, float maxW, float sp = 0);
std::string wielkie(std::string t);
std::vector<std::string> linie(ImFont* f, float p, const std::string& t, float w);   // zawijanie po slowach, wysokosc wiersza ustala rysujacy

bool hover(const ImRect& r);                                 // nad tym miejscem, nic nie zaslania
bool klik(const ImRect& r);
enum Rodzaj { Zwykly, Akcent, Ostrz, Zly, Wlaczony };
struct Btn { std::string t; int rodzaj = Zwykly; bool maly = false, ikona = false; std::string tip; bool wylaczony = false; };
ImVec2 btnRozm(const Btn& b);
bool btn(ImDrawList* d, float x, float y, const Btn& b);
void pasek(ImDrawList* d, const ImRect& r, float pct, const Kolor& c);
void kropka(ImDrawList* d, float cx, float cy, const std::string& status);
void pinezka(ImDrawList* d, float x, float y, float r, ImU32 c);

// chmurki: tytul i tresc; wiersz "klucz\twartosc" trafia do dwoch kolumn, wiersz "-" to kreska
void tip(const ImRect& r, const std::string& tytul, const std::string& tresc = "");
void tipJuz(const std::string& tytul, const std::string& tresc = "");   // wolajacy wie, ze kursor jest nad elementem
void rysujTip(double teraz);
// rysunki: karta graficzna z wentylatorami, uklad procesora, znaczek silnika
void wentylator(ImDrawList* d, ImVec2 c, float r, float kat, ImU32 kol, int lopatki = 5);
float ikonaGpuSzer(float h, int n);
void ikonaGpu(ImDrawList* d, float x, float y, float h, int n, const Kolor& k, float kat, bool jasna);
float znaczekSzer(float h, const std::string& znak);
void znaczek(ImDrawList* d, float x, float y, float h, const std::string& znak, const Kolor& k);
Kolor kolTp(int ileGpu);                                     // 1 karta: akcent, 2 karty: fiolet, 0: szary
Kolor rgb(unsigned v);

void styl();                                                 // kolory ImGui z motywu
// kontrolki w oknach
bool poleTekstowe(const char* id, std::string* s, float w, bool wielolinia = false, float h = 0, const char* podpowiedz = nullptr);
}
std::string n0(double v); std::string n1(double v); std::string n2(double v); std::string nK(double v); std::string czasPracy(double s);
