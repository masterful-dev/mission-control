#pragma once
// Stan systemu. Ksztalt jak GET /api/state serwera Centrum; pola z dopiskiem "klient" wylicza program.
#include <cmath>
#include <deque>
#include <optional>
#include <string>
#include <utility>
#include <vector>

constexpr double BRAK = NAN;   // null w API
inline bool jest(double v) { return !std::isnan(v); }

struct ProcesGpu { int pid = 0; double vram_mb = 0; std::string name; bool system = false; };
struct Gpu {
  int index = 0; std::string name, krotka;
  double vram_used_mb = 0, vram_total_mb = 1, temp_c = 0, hbm_temp_c = BRAK, power_w = BRAK, power_limit_w = 0;
  double util_gpu = 0, util_mem = 0, sm_clock_mhz = 0, mem_clock_mhz = 0, fan_pct = BRAK; bool nvlink = false;
  std::vector<ProcesGpu> processes;
};
struct Cpu { double percent = 0; int cores = 0; std::vector<double> per; double load1 = 0, temp_package_c = 0, temp_hottest_c = 0, freq_mhz = 0; };
struct Ram { double total_gb = 0, used_gb = 0, available_gb = 0, free_gb = 0, percent = 0, model_gb = 0, swap_total_gb = 0, swap_used_gb = 0; };
// Dysk: program wykrywa je sam na komputerze, na ktorym dziala (dyski.cpp). Lista z API jest tylko zapasem.
struct Dysk {
  std::string path, nazwa, opis, urzadzenie, model, fs;
  double total_gb = 0, used_gb = 0, free_gb = 0, percent = 0, temp_c = BRAK;
  bool obrotowy = false, wymienny = false, sieciowy = false, tylkoOdczyt = false, systemowy = false;
};
struct Nvme { std::string name; double temp_c = 0; };
struct Psu { double power_w = 0, fan_rpm = BRAK; std::vector<std::pair<std::string, double>> temps; };
struct Siec { double rx = 0, tx = 0; std::string host; };
struct Wariant { std::vector<int> gpus; std::vector<int> vram; std::string opis; };
struct Model {
  std::string id, name, cat, status = "stopped", powod, surowy, rodzaj, polecenie;   // surowy: wpis z API do formularza edycji
  int port = 0, pin = 0; long ctx = 0; double ram = 0, vram = 0; bool mozeStart = true;
  std::vector<int> gpus; std::vector<Wariant> pl;
  // klient: nazwa bez silnika i bez TP1/TP2, silnik (wykryty albo wybrany), liczba kart (0 = sam procesor)
  std::string krotka, silnik; int ileGpu = 1;
};
struct Aktywne {
  long id = 0; std::string phase, kind, eff, tool; std::vector<std::string> tools;
  double in = 0, cache = 0, out = 0, proc = 0, inst = 0, avg = BRAK, ptps = 0, el = 0;
};
struct Sesja { double tg_avg = BRAK, pp_avg = BRAK, czas_avg = BRAK, wejscie = 0, wyjscie = 0; long ok = 0, bledy = 0, przerwane = 0, zapytan = 0; };
struct WpisHist { long id = 0; std::string kind, status; double in = 0, cached = 0, out = 0, ptps = 0, gtps = BRAK, acc = BRAK, dur = 0; };
struct Ladowanie { double pct = 0, wczytano_mb = 0, rozmiar_mb = 0, vram_mb = 0; };
struct Llm {
  std::string id, name, status; int port = 0, queued = 0; double up_s = 0; bool up_ok = false; long n_ctx = 0, ctx = 0;
  Sesja sesja; std::vector<WpisHist> hist; std::optional<Ladowanie> ladowanie; std::optional<Aktywne> active;
};
struct Slupek { double v = 0; long long ts = 0; std::string data; bool pusty = false; };   // godzina: ts, dzien: data RRRR-MM-DD
struct Urzadzenie { std::string nazwa; int sztuk = 1; double moc_w = 0; };
struct Energia {
  double teraz_w = 0, teraz_w_gniazdko = 0, psu_sprawnosc = .92, psu_gpu_sprawnosc = .9, psu_glowny_w = 0, psu_gpu_w = 0, gpu0_w = 0, gpu1_w = 0, urzadzenia_w = 0;
  double ostatnia_godzina_sred_w = 0, miesiac_sred_w = 0, dzis_kwh = 0, miesiac_kwh = 0, miesiac_godzin_pracy = 0, miesiac_urzadzenia_kwh = 0;
  std::vector<Slupek> godziny, dni; std::vector<Urzadzenie> urzadzenia;
};
struct Sierota { int pid = 0; std::string name, cmdline; double vram_mb = 0; };
struct Stan {
  std::vector<Gpu> gpus; Cpu cpu; Ram ram; std::vector<Dysk> disks; Psu psu; std::vector<Nvme> nvme; Siec net;
  std::vector<Model> models; std::vector<Llm> llm; Energia energia; std::vector<Sierota> orphans;
};
struct HistGpu { std::deque<float> vram, temp, power, util; };

// Co model robi w tej chwili: wypelnia zrodlo danych (symulator albo API), czyta scena.
struct Zasady { float slow = 30, dlugo = 300, trudno = 90, cieplo = 80, kontekst = 85; };   // progi, od ktorych twarz reaguje
struct Praca {
  std::string status = "none";   // none, loading, ready, stopping
  float load = 0, justReady = 0;
  std::string phase = "idle";    // idle, reading, thinking, tool, answering, compact
  float el = 0, tg = 0, tgAvg = 0, pp = 0, cacheFrac = 0, ctx = 0, acc = 0, gpuTemp = 43, vram = 0, idleS = 0;
  int promptTok = 0, queue = 0, users = 1;
  bool code = false, mouseRecent = false, down = false;
  std::string kind, tool;
  Zasady rules;
};
