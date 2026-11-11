#pragma once
// Symulator z makiety: dziala, gdy nie ma API Centrum, i na zyczenie.
#include "stan.h"
#include <string>
namespace Sim {
extern bool autoPlan; extern double mouseT;
void init(Stan& s, Praca& f);
void tick(Stan& s, Praca& f, float dt, double teraz);
void start(Stan& s, Praca& f, const std::string& id);
void stop(Stan& s, Praca& f, const std::string& id = "");
void demo(Stan& s, Praca& f, const std::string& k);
const Model* dziala(const Stan& s);
}
