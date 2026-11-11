#pragma once
// Twarz: render, ruch i zachowania. Odpowiednik makieta-v2: face.js, face-shaders.js, anim.js, behaviors.js, twarze/cyber-swiatla.js.
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "../stan.h"   // Praca: co model robi w tej chwili

// To, co skorka przekazuje twarzy. Kolory 0..1.
struct StylTwarzy {
  float accent[3], rim[3], eye[3], warn[3], bad[3], bg[3];
  float contours, fine, points, glass, glow, scan, density;
};

struct Zachowanie { std::string id, grupa, n[2], w[2], kolor; };

namespace Twarz {
struct Uzytkownik { float yaw = 0, pitch = 0, zoom = 1; };
bool init(const std::string& katalogDanych);           // kontekst OpenGL juz istnieje
void koniec();
void ustawStyl(const StylTwarzy& s);
void ustawWyglad(const std::string& id);                // holo, cyber
const std::string& wyglad();
Uzytkownik& uzytkownik();                               // Face.user
void widok(float cx, float cy, float s, float clipL, float clipR);  // Face.viewT i Face.clip, ulamki okna
void kursor(float x, float y, bool naScenie);           // Face.pointer i B.ctx.mx/my, ulamki okna
void kliknieto();                                       // fala swiatla po kliknieciu w twarz cyber
float material();                                       // Face.fx.mat
void pokazBezModelu();
void otoczenie(bool wlasneTlo, bool pokoj);                // wlasneTlo: gradient na cale okno (V2 ma swoje tlo); pokoj: luki i pasy za twarza
void podstawa(bool oryginalna);                           // tafla z fala glosu pod twarza: V2 moze ja zastapic zywa podstawa sceny                                  // Face.previewMat = true
// Jedna klatka: to, co w makiecie robi Face.onFrame = frame (choose, spont, set, mat, step), potem render do biezacego framebuffera.
void klatka(float dt, double T, const Praca& f, int jezyk, bool pierscienie, int szerPx, int wysPx);
void skala(float px);                                   // Face.px: min(devicePixelRatio, 1.5), domyslnie 1
// Do testow.
bool initBezObrazu(const std::string& katalogDanych);   // sam ruch, bez OpenGL; klatka nie rysuje
void ziarno(uint32_t z);                                // powtarzalne losowanie: ten sam ciag co Math.random z tym samym ziarnem w makiecie
struct Stan { const double* au; int nau; double eye[4], yaw; const float* bands; const float* gl; double fx[15], kol[9]; const char* look; double glowAdd, eyeLevel, rip[4]; uint64_t los; const char* zach; const char* powod; };
const Stan stan();
}

namespace Zach {
const std::vector<Zachowanie>& lista();                 // B.list
const std::vector<std::pair<std::string, std::array<std::string, 2>>>& grupy();  // B.GROUPS
void graj(const std::string& id, float sekundy);        // B.play
const Zachowanie& aktywne();                            // B.active()
const std::string& powod();                             // B.why
bool nadpisane();                                       // B.over
bool& podaza();                                         // B.follow
float& sila();                                          // B.strength
std::array<float, 3> kolorAktywnego();                  // B.colors[b.c][0]
std::array<float, 3> kolor(const std::string& nazwa);   // B.colors[nazwa][0]
}
