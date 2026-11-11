// Swiatla twarzy cybernetycznej: odpowiednik twarze/cyber-swiatla.js.
// Kazda grupa w F.gl: poziom, sila impulsu, predkosc, gestosc impulsow; gestosc ujemna = wskaznik swiecacy do poziomu.
#include "wewn.h"
#include <algorithm>
#include <map>

namespace tw {
enum { korona, koronaDol, skronL, skronP, policzekL, policzekP, krawedzL, krawedzP, zuchwaL, zuchwaP, szyjaL, szyjaP, ramiona, kolnierz, kregoslup, mechanizmy };
using Grupy = std::vector<int>;
static const Grupy CZOLO{korona, koronaDol, skronL, skronP}, TWARZ{policzekL, policzekP, krawedzL, krawedzP}, SZYJA{szyjaL, szyjaP, ramiona},
  WSZYSTKO{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
struct Impuls { Grupy gr; double a, v, n; };
struct Faza { std::vector<Impuls> impuls; double poziom; };
/* faza pracy modelu: ktore grupy niosa impulsy, jak szybko i w ktora strone (wartosc ujemna = od glowy w dol) */
static const std::map<std::string, Faza> FAZY{
  {"idle", {{{SZYJA, .9, .42, 2}, {{kolnierz}, .7, .3, 2}, {{korona, koronaDol}, .7, .3, 2}, {{skronL, skronP}, .9, .22, 1}}, .5}},
  {"reading", {{{SZYJA, 1.5, .9, 3}, {TWARZ, 1.1, .7, 2}}, .72}},
  {"thinking", {{{CZOLO, 1.8, 1.1, 3}, {{krawedzL, krawedzP}, 1., .6, 2}}, .7}},
  {"tool", {{{{zuchwaL, zuchwaP}, 1.4, 1.3, 3}, {{kolnierz}, 1.4, 1.6, 3}}, .68}},
  {"answering", {{{SZYJA, 1.3, -1., 4}, {TWARZ, .8, -.6, 2}}, .78}},
  {"compact", {{{WSZYSTKO, 1., .5, 1.5}}, .6}},
};
static std::string fazaPracy = "idle"; static double tgPracy = 0;
void swiatlaPraca(const std::string& status, const std::string& faza, double tg) { fazaPracy = status == "ready" && FAZY.count(faza) ? faza : "idle"; tgPracy = tg; }
/* zachowania, ktore konczy blysk: [grupy, sila] */
struct Zdarz { Grupy gr; double a; };
static const std::map<std::string, Zdarz> ZDARZENIA{{"eureka", {WSZYSTKO, 2.6}}, {"awaken", {WSZYSTKO, 2.2}}, {"surprised", {CZOLO, 2}}, {"shock", {WSZYSTKO, 3}}, {"poked", {TWARZ, 1.8}}, {"wink", {{skronL, skronP}, 2}}, {"overheat", {SZYJA, 2}}, {"tool_call", {{kolnierz}, 2}}};

static float blysk[16] = {}; static double za = 3, poziom = .62, ripT = 9; static std::string ost;
static void flash(const Grupy& gr, double a) { for (int g : gr) blysk[g] = (float)std::max((double)blysk[g], a); }
/* uderzenie w twarz: fala rozchodzi sie po liniach od miejsca klikniecia */
void swiatlaFala(double x, double y) { ripT = 0; F.rip[0] = x; F.rip[1] = y; }
void swiatlaKrok(double dt, double t) { const Fx& f = F.fx; float* o = F.gl; const Wpis* b = B.active(); const Faza& Z = FAZY.at(fazaPracy);
  if (ost != b->id) { ost = b->id; auto e = ZDARZENIA.find(ost); if (e != ZDARZENIA.end()) flash(e->second.gr, e->second.a); }
  // przeskoki: pojedyncze linie blyskaja same z siebie, tym czesciej im wiecej pracy
  za -= dt * (1 + f.act * 3 + f.think * 2); if (za <= 0) { za = 1.5 + los() * 3.5; const int g = WSZYSTKO[(int)(los() * 16)]; const double v = 1 + los() * 1.4; blysk[g] = (float)std::max((double)blysk[g], v); }
  poziom += (Z.poziom - poziom) * std::min(1.0, dt * 2);
  const double oddech = .8 + .2 * std::sin(t * 1.1); const double q = los(), drz = 1 - f.flick * .5 * (q < .3 ? 1 : 0), strob = f.glitch > .05 ? (std::sin(t * 46) > 0 ? 1.5 : .25) : 1;
  for (int g = 0; g < 16; g++) { blysk[g] = (float)std::max(0.0, (double)blysk[g] - dt * 2.6); const int i = g * 4; o[i] = (float)(poziom * oddech * drz * strob + (double)blysk[g] * 2.2); o[i + 1] = 0; o[i + 2] = .4f; o[i + 3] = 1; }
  for (auto& p : Z.impuls) for (int g : p.gr) { const int i = g * 4; o[i + 1] = (float)(p.a * (.5 + f.act * .8 + f.think * .3)); o[i + 2] = (float)(p.v * (1 + tgPracy / 240)); o[i + 3] = (float)p.n; }
  // mowa: zuchwa i kolnierz swieca razem z glosem
  for (int g : {zuchwaL, zuchwaP, kolnierz}) o[g * 4] = (float)(o[g * 4] + f.voice * 2.4);
  for (int g : {szyjaL, szyjaP}) o[g * 4] = (float)(o[g * 4] + f.voice * 1.3);
  // kregoslup to wskaznik wypelnienia kontekstu; mechanizmy pracuja wolno
  { const int i = kregoslup * 4; o[i] = 1.5f; o[i + 1] = (float)std::max(.08, lub(f.ctx, 0)); o[i + 3] = -1; }
  o[mechanizmy * 4] = (float)(o[mechanizmy * 4] * (.7 + .3 * std::sin(t * 2.3)));
  // krawedzie sa szerokie: przy tym samym poziomie swiecilyby za mocno
  for (int g : {krawedzL, krawedzP, ramiona}) o[g * 4] = (float)(o[g * 4] * .6);
  // teczowki jasnieja przy mysleniu i pracy
  F.eyeLevel = .5 + f.think * .3 + f.act * .2 + f.voice * .25 + std::max(blysk[korona], blysk[skronL]) * .3;
  // fala po kliknieciu
  ripT += dt; F.rip[2] = ripT * .85; F.rip[3] = std::max(0.0, 1 - ripT / 1.1);
  if (F.maPointer) F.pointer.k += ((F.pointer.on ? 1 : 0) - F.pointer.k) * std::min(1.0, dt * 8);
}
}
