#pragma once
// Polaczenie z API Centrum: stan co pol sekundy i zlecenia w osobnych watkach.
#include "stan.h"
#include <functional>
#include <string>
namespace Api {
using Odp = std::function<void(int kod, const std::string& tresc)>;
void start(const std::string& adres);          // http://127.0.0.1:8770
const std::string& adres();
bool polaczone();
bool nowyStan(Stan& s);                        // true, gdy przyszedl swiezy stan
void zlec(const std::string& metoda, const std::string& sciezka, const std::string& body, Odp odp = {});
void pompuj();                                 // odpowiedzi w watku okna
}
