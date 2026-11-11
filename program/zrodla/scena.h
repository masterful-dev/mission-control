#pragma once
// Tlo okna i zywa podstawa sceny (widmo aktywnosci). Parametry ustawia okno.cpp, rysuje glowny.cpp przed interfejsem.
namespace Scena {
struct Par {
  float cx = 800, cy = 480, pol = 400, bazaY = 700;   // piksele okna, y od gory: srodek sceny, polowa jej szerokosci, linia podstawy
  float poziom = 0, blask = 1, skala = 1, mgla = .5f, siatka = .25f, puls = -1, ruch = 1; bool widmo = true;
  float kolor[3] = {.5f, .74f, 1}, akcent[3] = {.5f, .74f, 1}, tlo[3] = {.04f, .05f, .07f};
  float hist[64] = {};                               // aktywnosc: [0] teraz, dalej przeszlosc
};
Par& par();
bool init();                                         // kontekst OpenGL juz istnieje
void rysuj(int szerPx, int wysPx, double T);
}
