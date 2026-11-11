#pragma once
// Dyski tego komputera: lista z nazwami, opisami, zajetoscia i temperatura, odswiezana w tle co 4 s.
#include "stan.h"
#include <vector>
namespace Dyski {
void start();
bool nowe(std::vector<Dysk>& d);   // true, gdy jest swiezy odczyt
std::vector<Dysk> teraz();         // odczyt od razu (testy)
}
