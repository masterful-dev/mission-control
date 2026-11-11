// Dyski komputera, na ktorym program dziala: wykrywane z systemu, bez listy w konfiguracji.
// Linux: /proc/self/mounts, etykiety z /dev/disk/by-label, opisy z x-gvfs-name w /etc/fstab, temperatury z hwmon.
// Windows: litery dyskow z etykietami woluminow.
#include "dyski.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/statvfs.h>
#endif
namespace fs = std::filesystem;
namespace {
std::mutex mu; std::vector<Dysk> gotowe; std::atomic<bool> swieze{false}, ruszyl{false};
#ifndef _WIN32
std::string plik(const fs::path& p) { std::ifstream f(p); std::string s; std::getline(f, s); while (!s.empty() && (s.back() == ' ' || s.back() == '\n' || s.back() == '\r')) s.pop_back(); size_t a = s.find_first_not_of(' '); return a == std::string::npos ? "" : s.substr(a); }
// /proc/mounts zapisuje spacje jako \040
std::string odOsemkowo(const std::string& s) { std::string o; for (size_t i = 0; i < s.size(); i++) { if (s[i] == '\\' && i + 3 < s.size() && isdigit((unsigned char)s[i + 1])) { o += (char)std::stoi(s.substr(i + 1, 3), nullptr, 8); i += 3; } else o += s[i]; } return o; }
// udev zapisuje znaki w nazwach etykiet jako \x20
std::string odSzesnastkowo(const std::string& s) { std::string o; for (size_t i = 0; i < s.size(); i++) { if (s[i] == '\\' && i + 3 < s.size() && s[i + 1] == 'x') { o += (char)std::stoi(s.substr(i + 2, 2), nullptr, 16); i += 3; } else o += s[i]; } return o; }
std::string odProcentow(const std::string& s) { std::string o; for (size_t i = 0; i < s.size(); i++) { if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) { o += (char)std::stoi(s.substr(i + 1, 2), nullptr, 16); i += 2; } else o += s[i]; } return o; }
// sda2 -> sda, nvme0n1p1 -> nvme0n1: katalog nadrzedny w /sys
std::string dyskFizyczny(const std::string& part) { std::error_code e; fs::path p = fs::canonical("/sys/class/block/" + part, e); if (e) return part; if (fs::exists(p / "partition", e)) return p.parent_path().filename().string(); return part; }
std::vector<Dysk> wykryj() {
  std::error_code e; std::map<std::string, std::string> etykiety, gvfs; std::map<std::string, double> temp;
  if (fs::exists("/dev/disk/by-label", e)) for (auto& x : fs::directory_iterator("/dev/disk/by-label", e)) { fs::path c = fs::canonical(x.path(), e); if (!e) etykiety[c.filename().string()] = odSzesnastkowo(x.path().filename().string()); }
  { std::ifstream f("/etc/fstab"); std::string l; while (std::getline(f, l)) { if (l.empty() || l[0] == '#') continue; std::istringstream s(l); std::string dev, mp, typ, op; s >> dev >> mp >> typ >> op; auto q = op.find("x-gvfs-name="); if (q == std::string::npos) continue; std::string v = op.substr(q + 12); if (auto c = v.find(','); c != std::string::npos) v = v.substr(0, c); gvfs[odOsemkowo(mp)] = odProcentow(v); } }
  // temperatury: NVMe ma wlasny czujnik, dyski SATA maja go po zaladowaniu modulu drivetemp
  if (fs::exists("/sys/class/hwmon", e)) for (auto& h : fs::directory_iterator("/sys/class/hwmon", e)) { std::string n = plik(h.path() / "name"); if (n != "nvme" && n != "drivetemp") continue; std::string tv = plik(h.path() / "temp1_input"); if (tv.empty()) continue; double tc = atof(tv.c_str()) / 1000; fs::path dev = fs::canonical(h.path() / "device", e); if (e) continue;
    if (n == "nvme") { for (auto& b : fs::directory_iterator(dev, e)) { std::string bn = b.path().filename().string(); if (bn.rfind("nvme", 0) == 0 && bn.find('n', 4) != std::string::npos) temp[bn] = tc; } }
    else if (fs::exists(dev / "block", e)) for (auto& b : fs::directory_iterator(dev / "block", e)) temp[b.path().filename().string()] = tc; }
  static const std::set<std::string> sieciowe = {"nfs", "nfs4", "cifs", "smb3", "fuse.sshfs", "ceph", "glusterfs"}, pomin = {"squashfs", "iso9660", "tmpfs", "devtmpfs", "overlay", "ramfs", "efivarfs"};
  std::vector<Dysk> o; std::map<std::string, size_t> poUrz; std::ifstream f("/proc/self/mounts"); std::string l;
  while (std::getline(f, l)) { std::istringstream s(l); std::string dev, mp, typ, op; s >> dev >> mp >> typ >> op; mp = odOsemkowo(mp); bool siec = sieciowe.count(typ) > 0, zfs = typ == "zfs";
    if (!siec && !zfs && dev.rfind("/dev/", 0) != 0) continue; if (pomin.count(typ) || dev.rfind("/dev/loop", 0) == 0) continue;
    if (mp.rfind("/boot", 0) == 0 || mp.rfind("/snap", 0) == 0 || mp.rfind("/var/lib/docker", 0) == 0 || mp.rfind("/var/snap", 0) == 0 || mp.rfind("/run/docker", 0) == 0 || mp.rfind("/sys", 0) == 0 || mp.rfind("/proc", 0) == 0) continue;
    // jedno urzadzenie zamontowane kilka razy (podwolumeny btrfs): zostaje najkrotsza sciezka
    if (auto it = poUrz.find(dev); it != poUrz.end()) { if (mp.size() < o[it->second].path.size()) o[it->second].path = mp; continue; }
    Dysk d; d.path = mp; d.urzadzenie = dev; d.fs = typ == "fuseblk" ? "ntfs" : typ; d.sieciowy = siec; d.systemowy = mp == "/"; d.tylkoOdczyt = ("," + op + ",").find(",ro,") != std::string::npos;
    if (dev.rfind("/dev/", 0) == 0) { fs::path c = fs::canonical(dev, e); std::string part = (e ? fs::path(dev) : c).filename().string(), fiz = dyskFizyczny(part); if (auto i = etykiety.find(part); i != etykiety.end()) d.nazwa = i->second;
      d.model = plik("/sys/block/" + fiz + "/device/model"); d.obrotowy = plik("/sys/block/" + fiz + "/queue/rotational") == "1"; d.wymienny = plik("/sys/block/" + fiz + "/removable") == "1"; if (auto i = temp.find(fiz); i != temp.end()) d.temp_c = i->second; }
    poUrz[dev] = o.size(); o.push_back(d); }
  for (auto& d : o) {
    // nazwa z menedzera plikow ("NAZWA — opis") ma pierwszenstwo przed sama etykieta
    if (auto i = gvfs.find(d.path); i != gvfs.end()) { std::string v = i->second; for (const char* sep : {" \xE2\x80\x94 ", " - ", ": "}) if (auto q = v.find(sep); q != std::string::npos) { d.opis = v.substr(q + strlen(sep)); v = v.substr(0, q); break; } if (d.nazwa.empty() || v != d.nazwa) { if (d.nazwa.empty()) d.nazwa = v; else if (d.opis.empty()) d.opis = v; } }
    if (d.nazwa.empty()) d.nazwa = d.path == "/" ? "System" : fs::path(d.path).filename().string(); if (d.nazwa.empty()) d.nazwa = d.path;
    struct statvfs v; if (statvfs(d.path.c_str(), &v) == 0 && v.f_blocks > 0) { double b = (double)v.f_frsize / (1024. * 1024 * 1024); d.total_gb = v.f_blocks * b; d.free_gb = v.f_bavail * b; d.used_gb = (v.f_blocks - v.f_bfree) * b; d.percent = d.used_gb / std::max(1e-9, d.used_gb + d.free_gb) * 100; } }
  o.erase(std::remove_if(o.begin(), o.end(), [](const Dysk& d) { return d.total_gb <= 0; }), o.end());
  std::stable_sort(o.begin(), o.end(), [](const Dysk& a, const Dysk& b) { if (a.systemowy != b.systemowy) return a.systemowy; return a.nazwa < b.nazwa; });
  return o;
}
#else
std::string utf8(const wchar_t* w) { int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr); std::string s(n > 0 ? n - 1 : 0, 0); if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr); return s; }
std::vector<Dysk> wykryj() {
  std::vector<Dysk> o; wchar_t buf[512]; DWORD n = GetLogicalDriveStringsW(511, buf); wchar_t sysd[MAX_PATH] = L"C:\\"; GetWindowsDirectoryW(sysd, MAX_PATH);
  for (wchar_t* p = buf; p < buf + n && *p; p += wcslen(p) + 1) { UINT typ = GetDriveTypeW(p); if (typ != DRIVE_FIXED && typ != DRIVE_REMOVABLE && typ != DRIVE_REMOTE) continue; ULARGE_INTEGER wolne, razem, wolneRazem; SetErrorMode(SEM_FAILCRITICALERRORS); if (!GetDiskFreeSpaceExW(p, &wolne, &razem, &wolneRazem) || !razem.QuadPart) continue;
    wchar_t et[256] = L"", sp[64] = L""; DWORD fl = 0; GetVolumeInformationW(p, et, 255, nullptr, nullptr, &fl, sp, 63); Dysk d; d.path = utf8(p); d.urzadzenie = d.path.substr(0, 2); d.nazwa = utf8(et); if (d.nazwa.empty()) d.nazwa = d.urzadzenie; else d.opis = d.urzadzenie; d.fs = utf8(sp); d.wymienny = typ == DRIVE_REMOVABLE; d.sieciowy = typ == DRIVE_REMOTE; d.tylkoOdczyt = fl & FILE_READ_ONLY_VOLUME; d.systemowy = towupper(p[0]) == towupper(sysd[0]);
    double b = 1 / (1024. * 1024 * 1024); d.total_gb = razem.QuadPart * b; d.free_gb = wolne.QuadPart * b; d.used_gb = (razem.QuadPart - wolneRazem.QuadPart) * b; d.percent = d.used_gb / std::max(1e-9, d.total_gb) * 100; o.push_back(d); }
  return o;
}
#endif
void petla() { for (;;) { std::vector<Dysk> d; try { d = wykryj(); } catch (...) {} { std::lock_guard g(mu); gotowe = std::move(d); swieze = true; } std::this_thread::sleep_for(std::chrono::seconds(4)); } }
}
namespace Dyski {
void start() { if (!ruszyl.exchange(true)) std::thread(petla).detach(); }
bool nowe(std::vector<Dysk>& d) { if (!swieze) return false; std::lock_guard g(mu); d = gotowe; swieze = false; return true; }
std::vector<Dysk> teraz() { return wykryj(); }
}
