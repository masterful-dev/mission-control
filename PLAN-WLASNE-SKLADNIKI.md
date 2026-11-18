# Plan: własne składniki zamiast cudzych

Stan na 19.11.2026. Mission Control zawiera składniki innych autorów. Wszystkie mają licencje, które pozwalają używać ich w zamkniętym programie i go rozpowszechniać; jedyny obowiązek to dołączyć ich noty licencyjne. Pakiety robią to plikiem `THIRD-PARTY-NOTICES.txt`, więc dziś nikt nie ma podstaw do roszczeń. Zastąpienie wszystkiego własnym kodem jest możliwe, ale to praca na miesiące, nie na jeden krok. Kolejność od najtańszych:

| Krok | Składnik (licencja) | Czym zastąpić | Nakład | Ryzyko |
|---|---|---|---|---|
| 1 | nlohmann json (MIT) | własny mały parser i zapis JSON: program czyta jeden kształt danych (`/api/state`) i własne ustawienia | 2–3 dni | małe |
| 2 | cpp-httplib (MIT) | własny klient HTTP na gniazdach: tylko GET/POST/PUT/DELETE do 127.0.0.1 | 2–3 dni | małe |
| 3 | FastAPI, Starlette, Uvicorn, Pydantic, httpx, PyYAML (MIT/BSD) | usługa danych przepisana na C++ i wbudowana w program (albo na czysty Python z biblioteki standardowej). Znika też wymóg Pythona 3.12 i 36 MB bibliotek | 2–3 tygodnie | średnie: start i stop modeli, dziennik energii |
| 4 | psutil, pynvml (BSD) | odczyty wprost z `/proc`, `/sys` i biblioteki NVML sterownika (część kroku 3) | w kroku 3 | małe |
| 5 | czcionki Inter, JetBrains Mono, Noto, DejaVu (OFL, Bitstream) | własny krój pisma albo krój kupiony z licencją na wbudowanie | zakup albo tygodnie pracy grafika | małe technicznie |
| 6 | siatka twarzy ICT FaceKit (MIT) | własna rzeźba głowy z własną mimiką (Blender) | 2–4 tygodnie | średnie: wygląd twarzy |
| 7 | Dear ImGui (MIT) | własna warstwa rysowania i obsługi myszy: program i tak rysuje panele sam, z ImGui bierze okna, listy rozwijane, pola tekstowe i tekst | 3–5 tygodni | duże: pola tekstowe, menu |
| 8 | FreeType (FTL) | własny rasteryzator czcionek TrueType | 2–3 tygodnie | średnie: ostrość małych liter |
| 9 | SDL3 (zlib) | własna obsługa okna, myszy, klawiatury i OpenGL osobno dla X11, Wayland i Windows | 4–8 tygodni | duże: różnice między systemami |

Rozsądna granica: kroki 1–4 dają program bez żadnej cudzej biblioteki w usłudze danych i w wymianie danych, a pakiet maleje o ponad połowę. Kroki 7–9 to pisanie od nowa rzeczy, które robi sprawdzony kod na bardzo luźnych licencjach; zysk prawny jest żaden, a ryzyko błędów duże.

Czego nie da się zastąpić: sterownik karty graficznej, biblioteki systemu (X11, OpenGL, biblioteka C) i sam Python, dopóki usługa danych jest w Pythonie. Tych program nie dołącza, tylko z nich korzysta.
