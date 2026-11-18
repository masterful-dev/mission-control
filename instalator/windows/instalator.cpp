// Instalator Mission Control dla Windows (bez uprawnien administratora, instalacja dla biezacego uzytkownika).
// Ten sam plik zrodlowy daje dwa programy: instalator (pliki programu ma w zasobie nr 100) i odinstaluj.exe (-DODINSTALUJ).
// Zasob: [u32 liczba plikow] potem dla kazdego [u32 dlugosc sciezki][sciezka UTF-8][u32 rozmiar][dane].
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <string>
#include <vector>
#define NAZWA L"Mission Control"
#define WERSJA L"2.1.0"
static const wchar_t* KLUCZ = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\MissionControl";
static std::wstring folder(REFKNOWNFOLDERID id) { PWSTR p = nullptr; std::wstring s; if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) s = p; CoTaskMemFree(p); return s; }
static std::wstring katalogProgramu() { return folder(FOLDERID_LocalAppData) + L"\\Programs\\Mission Control"; }
static std::wstring skrotMenu() { return folder(FOLDERID_Programs) + L"\\Mission Control.lnk"; }
static std::wstring skrotPulpit() { return folder(FOLDERID_Desktop) + L"\\Mission Control.lnk"; }
static int pytanie(const std::wstring& t, UINT f) { return MessageBoxW(nullptr, t.c_str(), NAZWA L" " WERSJA, f | MB_SETFOREGROUND); }
#ifndef ODINSTALUJ
static std::wstring zUtf8(const char* s, int n) { int w = MultiByteToWideChar(CP_UTF8, 0, s, n, nullptr, 0); std::wstring o(w, 0); MultiByteToWideChar(CP_UTF8, 0, s, n, o.data(), w); return o; }
static void katalogi(const std::wstring& plik) { for (size_t i = 3; i < plik.size(); i++) if (plik[i] == L'\\') CreateDirectoryW(plik.substr(0, i).c_str(), nullptr); }
static bool skrot(const std::wstring& lnk, const std::wstring& cel, const std::wstring& kat) {
  IShellLinkW* sl = nullptr; if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&sl))) return false;
  sl->SetPath(cel.c_str()); sl->SetWorkingDirectory(kat.c_str()); sl->SetDescription(NAZWA); sl->SetIconLocation(cel.c_str(), 0);
  IPersistFile* pf = nullptr; bool ok = false; if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf))) { ok = SUCCEEDED(pf->Save(lnk.c_str(), TRUE)); pf->Release(); } sl->Release(); return ok;
}
static void rej(HKEY k, const wchar_t* n, const std::wstring& v) { RegSetValueExW(k, n, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t))); }
int WINAPI wWinMain(HINSTANCE h, HINSTANCE, PWSTR, int) {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); std::wstring kat = katalogProgramu();
  if (pytanie(L"WARUNKI KORZYSTANIA / TERMS OF USE\n\n"
      L"Program jest udostępniany w stanie, w jakim jest, bez żadnej gwarancji. Autor nie odpowiada za żadne szkody, utratę danych ani uszkodzenie sprzętu. Instalujesz i używasz programu wyłącznie na własną odpowiedzialność.\n\n"
      L"Bez pisemnej zgody autora nie wolno modyfikować programu, wykorzystywać jego kodu w całości ani we fragmentach, odtwarzać kodu ani rozpowszechniać wersji zmienionych.\n\n"
      L"The program is provided \"as is\", without any warranty. The author is not liable for any damage. You install and use it at your own risk. You may not modify it, use its code, reverse engineer it or distribute modified versions without the author's written consent.\n\n"
      L"Czy akceptujesz te warunki? / Do you accept these terms?", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return 0;
  if (pytanie(L"Zainstalować program Mission Control?\n\nKatalog: " + kat + L"\n\nPowstanie skrót w menu Start i na pulpicie. Uprawnienia administratora nie są potrzebne.", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
  HRSRC r = FindResourceW(h, MAKEINTRESOURCEW(100), RT_RCDATA); HGLOBAL g = r ? LoadResource(h, r) : nullptr; const BYTE* d = g ? (const BYTE*)LockResource(g) : nullptr; DWORD n = r ? SizeofResource(h, r) : 0;
  if (!d || n < 4) { pytanie(L"Instalator jest uszkodzony: brak plików programu.", MB_OK | MB_ICONERROR); return 1; }
  auto u32 = [&](size_t& p) { DWORD v; memcpy(&v, d + p, 4); p += 4; return v; };
  size_t p = 0; DWORD ile = u32(p); std::string lista; unsigned long long razem = 0;
  for (DWORD i = 0; i < ile; i++) {
    if (p + 4 > n) break; DWORD ln = u32(p); if (p + ln + 4 > n) break; std::wstring wz = zUtf8((const char*)d + p, (int)ln); lista.append((const char*)d + p, ln); lista += "\r\n"; p += ln; DWORD rozm = u32(p); if (p + rozm > n) break;
    for (auto& c : wz) if (c == L'/') c = L'\\'; std::wstring cel = kat + L"\\" + wz; katalogi(cel);
    HANDLE f = CreateFileW(cel.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { pytanie(L"Nie mogę zapisać pliku:\n" + cel + L"\n\nJeśli Mission Control jest uruchomione, zamknij je i uruchom instalator ponownie.", MB_OK | MB_ICONERROR); return 1; }
    DWORD zap = 0; BOOL ok = WriteFile(f, d + p, rozm, &zap, nullptr); CloseHandle(f); if (!ok || zap != rozm) { pytanie(L"Błąd zapisu pliku (brak miejsca na dysku?):\n" + cel, MB_OK | MB_ICONERROR); return 1; }
    p += rozm; razem += rozm;
  }
  { HANDLE f = CreateFileW((kat + L"\\pliki.txt").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr); if (f != INVALID_HANDLE_VALUE) { DWORD z; WriteFile(f, lista.data(), (DWORD)lista.size(), &z, nullptr); CloseHandle(f); } }
  std::wstring exe = kat + L"\\MissionControl.exe", od = kat + L"\\odinstaluj.exe";
  skrot(skrotMenu(), exe, kat); skrot(skrotPulpit(), exe, kat);
  HKEY k; if (RegCreateKeyExW(HKEY_CURRENT_USER, KLUCZ, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
    rej(k, L"DisplayName", NAZWA); rej(k, L"DisplayVersion", WERSJA); rej(k, L"Publisher", L"Masterful"); rej(k, L"InstallLocation", kat); rej(k, L"DisplayIcon", exe); rej(k, L"UninstallString", L"\"" + od + L"\"");
    DWORD jeden = 1, kb = (DWORD)(razem / 1024); RegSetValueExW(k, L"NoModify", 0, REG_DWORD, (const BYTE*)&jeden, 4); RegSetValueExW(k, L"NoRepair", 0, REG_DWORD, (const BYTE*)&jeden, 4); RegSetValueExW(k, L"EstimatedSize", 0, REG_DWORD, (const BYTE*)&kb, 4); RegCloseKey(k); }
  if (pytanie(L"Mission Control jest zainstalowane.\n\nUruchomić teraz?", MB_YESNO | MB_ICONINFORMATION) == IDYES) ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, kat.c_str(), SW_SHOWNORMAL);
  CoUninitialize(); return 0;
}
#else
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  wchar_t ja[MAX_PATH]; GetModuleFileNameW(nullptr, ja, MAX_PATH); std::wstring kat = ja; kat = kat.substr(0, kat.find_last_of(L'\\'));
  if (pytanie(L"Odinstalować program Mission Control?\n\nUstawienia programu zostają w profilu użytkownika.", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
  // lista zainstalowanych plikow: usuwamy tylko to, co wgral instalator
  std::string lista; { HANDLE f = CreateFileW((kat + L"\\pliki.txt").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr); if (f != INVALID_HANDLE_VALUE) { DWORD n = GetFileSize(f, nullptr), z = 0; lista.resize(n); ReadFile(f, lista.data(), n, &z, nullptr); CloseHandle(f); } }
  if (lista.empty()) { pytanie(L"Nie znalazłem listy plików programu w katalogu:\n" + kat + L"\n\nNiczego nie usuwam.", MB_OK | MB_ICONERROR); return 1; }
  std::vector<std::wstring> katalogi_; size_t a = 0; while (a < lista.size()) { size_t e = lista.find("\r\n", a); if (e == std::string::npos) e = lista.size(); std::string w = lista.substr(a, e - a); a = e + 2; if (w.empty() || w == "odinstaluj.exe") continue;
    int n = MultiByteToWideChar(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0); std::wstring ws(n, 0); MultiByteToWideChar(CP_UTF8, 0, w.c_str(), (int)w.size(), ws.data(), n); for (auto& c : ws) if (c == L'/') c = L'\\';
    std::wstring pl = kat + L"\\" + ws; if (!DeleteFileW(pl.c_str()) && GetLastError() == ERROR_SHARING_VIOLATION) { pytanie(L"Mission Control jest uruchomione. Zamknij je i uruchom odinstalowanie ponownie.", MB_OK | MB_ICONWARNING); return 1; }
    for (size_t i = pl.size(); i > kat.size(); i--) if (pl[i - 1] == L'\\') katalogi_.push_back(pl.substr(0, i - 1)); }
  for (auto& k : katalogi_) RemoveDirectoryW(k.c_str());       // najglebsze katalogi sa na liscie pierwsze; niepuste zostaja
  for (auto& k : katalogi_) RemoveDirectoryW(k.c_str());
  DeleteFileW((kat + L"\\pliki.txt").c_str()); DeleteFileW(skrotMenu().c_str()); DeleteFileW(skrotPulpit().c_str());
  RegDeleteKeyW(HKEY_CURRENT_USER, KLUCZ);
  { HKEY k; if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) { RegDeleteValueW(k, L"CentrumMisjiV2"); RegCloseKey(k); } }
  pytanie(L"Mission Control zostało odinstalowane.", MB_OK | MB_ICONINFORMATION);
  // ten program nie moze usunac sam siebie: robi to wiersz polecen chwile po jego zamknieciu
  std::wstring cmd = L"cmd.exe /c ping 127.0.0.1 -n 3 >nul & del /q \"" + std::wstring(ja) + L"\" & rmdir \"" + kat + L"\"";
  STARTUPINFOW si{}; si.cb = sizeof si; si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE; PROCESS_INFORMATION pi{};
  if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
  return 0;
}
#endif
