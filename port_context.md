# JaszczurHAL Porting Context

## Cel

Celem jest stopniowe przepiecie `rp2040-doom` na JaszczurHAL jako glowna
platforme uruchomieniowa i demonstracja, ze JaszczurHAL nadaje sie do
prowadzenia nietrywialnego portu gry.

Port nie musi od razu dzialac runtime'owo i nie musi startowac na najslabszym
RP2040/Pico 2 MB. Priorytetem pierwszego etapu jest doprowadzenie do
kontrolowanej kompilacji, a potem etapowe uruchamianie subsystemow.

## Twarde Zalozenia

0. budujemy kontekst o JaszczurHAL z pliku AGENT_CONTEXT.md. Należy traktować ten 
  kontekst/dokument jako dokument referencyjny, nie jako opis obecnego repo.
1. Nie dopisujemy alternatywnego, bocznego targetu typu `jaszczurhal_pico_sdk_min`.
  Obecne buildowanie w repo traktujemy jako referencję do source-setów,
  flag i zależności, nie jako docelowy model. Docelowy model to CMake
  zgodny z JaszczurHAL, generujący szkic `.ino` i wystawiający targety
  używane przez taski VS Code: build, upload i monitor.
2. Nie tworzymy nowej rownoleglej sciezki "czysty Pico SDK + wybrane pliki HAL".
3. Uzywamy istniejacego konstruktu aplikacyjnego JaszczurHAL:
   `app_start`, `app_task0`, opcjonalnie `app_task1`.
4. Uzywamy obecnego mechanizmu CMake JaszczurHAL, ktory generuje szkic `.ino`
   i kompiluje aplikacje przez Arduino-Pico.
5. Jesli czegos brakuje w JaszczurHAL, dopisujemy to do JaszczurHAL, a nie jako
   lokalny hack w `rp2040-doom`.
6. Migracja ma byc etapowa: najpierw kompilacja, potem uruchamianie i naprawianie
   kolejnych subsystemow.
7. usuwamy z tego portu wszystko co nie jest potrzebne pod pico - wszelakie zależności
  typu SDL, nawet jeśli nie są wykorzystywane przez pico. Kod ma być docelowo czystym
  portem zawierającym tylko to, co potrzebne.
8. Dostosowujemy Doom do JaszczurHAL, nie JaszczurHAL do historycznych zalozen
   obecnego portu Pico SDK. Istniejace mechanizmy JaszczurHAL sa priorytetowe.
9. Desktopowe elementy Chocolate Doom sa poza zakresem budowania portu. Mozna je
   czytac wylacznie jako kontekst do zrozumienia mechanizmow gry, ale nie nalezy
   ich linkowac, utrzymywac ani naprawiac w sciezce JaszczurHAL/Pico.
10. Każdy z etapów, po wzdrożeniu ma być w niniejszym dokumencie oznaczany jako 
  "zrobiony", pod warunkiem spełnienia kryteriów.
11. Stan repo nie jest ważny w tym scope. Nie jest istotne które pliki są, bądź
  nie są śledzone.
12. Obowiązuje polityka "zero warnings". Build ma być czysty na tyle, na ile jest
  to możliwe. Co ważne: traktujemy ją jako kryterium dla zakończonych etapów, nie 
  dla pierwszego rozpoznawczego configure/build.

## Obecny Punkt Startu

`rp2040-doom` jest obecnie zbudowany wokol Pico SDK, pico-extras i bardzo
sprzetowego backendu:

- `src/i_main.c` zawiera klasyczny `main()` oraz inicjalizacje zegara,
  napiecia, stdio i sieci Pico.
- `src/pico/i_video.c` uzywa `pico_scanvideo`/PIO/VGA.
- `src/pico/i_picosound.c` uzywa `pico_audio_i2s`.
- `src/pico/i_input.c` uzywa TinyUSB/UART.
- `src/pico/piconet.c` uzywa bezposrednio I2C, DMA, IRQ i alarmow.
- `src/pico/picoflash.c` programuje flash bezposrednio przez ROM/Pico SDK.
- `src/pd_render.cpp` mocno korzysta z semaforow, multicore, hardware interp
  i synchronizacji z wyswietlaniem.

`JaszczurHAL` ma juz przydatne moduly:

- `HAL_ENABLE_DACLESS` i `HAL_ENABLE_DMA_PWM_AUDIO` dla PWM audio.
- `HAL_ENABLE_ILI9341`, `HAL_ENABLE_ST7789`, `HAL_ENABLE_ST7735`,
  `HAL_ENABLE_ST7796S` dla TFT.
- `HAL_ENABLE_SPI`, `HAL_ENABLE_GPIO`, `HAL_ENABLE_I2C`, `HAL_ENABLE_PWM_FREQ`,
  timery, synchronizacje i narzedzia systemowe.

## Docelowy Ksztalt Integracji

Docelowo Doom ma byc aplikacja JaszczurHAL, a nie osobnym firmwarem Pico SDK.

Minimalny model aplikacyjny:

- `app_start()` inicjalizuje HAL, zasoby Dooma, WAD/WHD, audio, display, input.
- `app_task0()` wykonuje glowna petle Dooma albo wywoluje krok petli gry.
- `app_task1()` moze przejac prace obecnego core1: render helper, display pump
  albo audio/service, zalezenie od tego, co bedzie najstabilniejsze.
- CMake JaszczurHAL generuje `.ino`, a `.ino` wywoluje powyzsze hooki.

Istniejacy `main()` z Dooma nie powinien byc punktem wejscia w tej sciezce.
Punkt wejścia wykorzystujący obecny main() nalezy przepiąć / przepisać na `app_start()` 
w JaszczurHAL. 
`app_task0()` wchodzi w niekończącą się pętle Dooma po zainicjowaniu wszystkich niezbędnych
zasobów i zależności przez `app_start()`.
Nie traktujemy tego jako problemu semantycznego "blokowania" taska. Istotne jest,
aby firmware wykonywal potrzebna funkcjonalnosc, a nie aby sztucznie dzielic petle
Dooma, jesli na danym etapie nie pomaga to w porcie.

## Plan Etapowy

### Etap 1: Aplikacja JaszczurHAL Bez Uruchamiania Gry

Status: zrobiony.

Cel: stworzyc aplikacyjny wrapper dla Dooma w modelu JaszczurHAL.

Zakres:

- Dodac aplikacyjny entrypoint zgodny z JaszczurHAL: `app_start`, `app_task0`,
  opcjonalnie `app_task1`.
- Nie dodawac nowego typu targetu CMake.
- Wpiac source set Dooma w istniejacy mechanizm generowania `.ino`.
- Source set ma dotyczyc sciezki Pico/minimalnego portu Dooma. Nie zaczynamy od
  desktopowego builda i nie budujemy zaleznosci desktopowych.
- Na tym etapie mozna jeszcze nie linkowac pelnego render/audio/input.
- Obecny codebase ma być samodzielną aplikacją (firmware) używającą JaszczurHAL
  jako dostarczyciela środowiska uruchomieniowego (kontekst aplikacji, audio/video itp).

Kryterium sukcesu:

- CMake JaszczurHAL generuje `.ino` dla aplikacji doomowej.
- Kompilacja dochodzi do pierwszego realnego zestawu bledow zaleznosci, zamiast
  zatrzymywac sie na braku struktury aplikacji.

Wynik wdrozenia:

- Top-level CMake generuje szkic `rp2040_doom.ino`, podpina JaszczurHAL przez
  `arduino-cli` i wystawia targety `firmware`, `firmware_debug`,
  `firmware_upload`, `firmware_compile_db`.
- Dodany jest minimalny wrapper aplikacyjny JaszczurHAL z `app_start()` i
  `app_task0()`.
- `cmake --build .build/cmake --target firmware` dochodzi do kompilacji
  doomowego source-setu. Obecny pierwszy realny blocker to kod sieci/piconet w
  `src/d_loop.c`: przy `NO_USE_NET=1` `net_client_connected` staje sie makrem
  `false`, a sciezka `DOOM_TINY` nadal probuje je przypisac i wywolac
  `piconet_stop()`.

### Etap 2: Rozdzielenie `main()` Od Logiki Dooma

Status: zrobiony.

Cel: pozbyc sie konfliktu pomiedzy `main()` Dooma i `setup()`/`loop()` z
Arduino-Pico.

Zakres:

- Wydzielic z `src/i_main.c` funkcje startowa mozliwa do wywolania z
  `app_start()`.
- Inicjalizacje specyficzne dla Pico SDK przeniesc za abstrakcje JaszczurHAL.
  Jeśli jest tu coś, czego JaszczurHAL nie wspiera wprost, nalezy to w JaszczurHAL
  zaimplementować.
- dostosowanie dołączonych tasków vscode/skryptów w scripts tak, by były w stanie
  skompilować kod, sflashować płytkę, odpalić konsolę, itp. Wedle istniejącego 
  schematu skrótów klawiszowych: ctrl-shift-1, ctrl-shift-2, itp. 
  Pliki zostały przeniesione z innego projektu (TimerNTP), w związku z tym należy
  je przystosować do obecnego portu Doom.
- wersja w core_version.txt nie jest aż tak istotna, można przyjąć wersję 5.4.0 
  i do niej dostosować skrypty.
- aktualny CMake to tylko referencja. Nie należy go traktować jako twardą zależność.

Kryterium sukcesu:

- Doomowy kod startowy linkuje sie bez wlasnego `main()`.
- skróty klawiszowe działają pod vscode, i można zbudować aplikację za ich pomocą,
  można uruchomić konsolę podglądu serial-persistent.py

Wynik wdrozenia:

- `src/i_main.c` wystawia teraz `I_DoomMain(int argc, char **argv)`, a klasyczny
  `main()` jest kompilowany tylko poza sciezka `JASZCZURHAL_PORT`.
- Wrapper JaszczurHAL w `src/jaszczurhal/doom_app.cpp` wywoluje `I_DoomMain()`
  raz z `app_task0()`, zgodnie z zalozeniem, ze task moze wejsc w glowna petle
  Dooma.
- Doomowa sciezka JaszczurHAL nie wykonuje juz bezposredniej inicjalizacji zegara,
  napiecia, `stdio_init_all()` ani `I_Init()` nalezacych do starego entrypointu
  Pico SDK.
- Dodany jest tymczasowy backend `src/jaszczurhal/doom_backend_stub.c`, ktory
  domyka linkowanie bez `pico_scanvideo`, `pico_audio_i2s`, TinyUSB inputu i
  piconet. Prawdziwe backendy display/audio/input zostaja na Etapy 4-6.
- CMake buduje firmware przez Arduino-Pico 5.4.0 i JaszczurHAL z FQBN
  `rp2040:rp2040:rpipicow:flash=2097152_524288,usbstack=tinyusb`, generujac
  `.build/firmware.elf`, `.build/firmware.bin`, `.build/firmware.uf2` i map file.
- Taski i skrypty VS Code sa przestawione na projekt `rp2040_doom`: build,
  upload oraz persistent serial monitor uzywaja obecnych targetow/skryptow i
  domyslnej sciezki JaszczurHAL `../libraries/JaszczurHAL`.
- Weryfikacja: `./scripts/configure-cmake.sh`,
  `cmake --build .build/cmake --target firmware`,
  `cmake --build .build/cmake --target firmware_compile_db`,
  `./scripts/refresh-intellisense.sh` oraz
  `python3 scripts/serial-persistent.py --help` przechodza. Logi builda i
  compile database nie zawieraja dopasowan `warning:` ani `error:`.

### Etap 3: Wstępne czyszczenie z zależności desktopowych.

Status: zrobiony.

- jeśli kontekst użycia danej biblioteki jest jasny, dotyczy desktopu (nie pico),
  należy chirurgicznie odciąć to ją codebase, i przenieść do osobnego folderu
  _unused. Cel: możliwość przywrócenia, jeśli coś pójdzie nie-tak.
- przeniesienie obecnego buildsystemu do _unused. Cel: jak powyżej.
- _unused zatrzymane jest tylko jako kontekst w trakcie portowania do JaszczurHAL.
  po uruchomieniu buildowania, i przeportowaniu zależności audio/video, folder należy
  usunąć.
- Czesci desktopowe nie sa etapem funkcjonalnym portu. Sa tylko materialem
  referencyjnym i maja stopniowo zniknac ze sciezki budowania.
- Czyszczenie desktopu trzeba robić po ustabilizowaniu source-setu. SDL 
  występuje szeroko, więc najbezpieczniej najpierw zbudować minimalny target 
  JaszczurHAL, a dopiero potem przenosić oczywiste desktopowe rzeczy do _unused.

Kryterium sukcesu:
- po przeniesieniu starych zaależności do _unused projekt wciąż się kompiluje bez 
  błędów i warningów.

Wynik wdrozenia:

- Stary buildsystem/autotools/CMake dla desktopu i podprojektow zostal
  przeniesiony do `_unused/buildsystem`.
- Desktopowe katalogi pomocnicze (`data`, `man`, `midiproc`, `pcsound`, `pkg`,
  `textscreen`, `win32`) zostaly przeniesione do `_unused/desktop_support`.
- Desktopowe implementacje platformy z `src/` oparte o SDL/SDL_mixer/SDL_net,
  plikowe backendy hosta, config hostowy, GUS/Timidity, desktopowe metadane oraz
  narzedzie setup zostaly przeniesione do `_unused/desktop_support/src`.
- Nie-doomowe gry z pakietu Chocolate (`heretic`, `hexen`, `strife`) zostaly
  przeniesione do `_unused/non_pico_games`; aktywna sciezka zostaje `DOOM_ONLY`.
- Aktywna sciezka JaszczurHAL/Pico nie wymaga juz naglowkow SDL w `src/i_main.c`
  ani `src/i_swap.h`; endian swap jest lokalny i niezalezny od SDL.
- Weryfikacja: `cmake --build .build/cmake --target firmware`,
  `cmake --build .build/cmake --target firmware_compile_db` oraz
  `./scripts/refresh-intellisense.sh` przechodza. Logi nie zawieraja dopasowan
  `warning:` ani `error:`, a IntelliSense uzywa
  `.build/arduino/compile_commands_patched.json`.

### Etap 4: Audio Przez JaszczurHAL

Status: zrobiony.

Cel: zastapic `pico_audio_i2s` przez JaszczurHAL PWM audio.

Zakres:

- Przepisac koncowke `src/pico/i_picosound.c` na `HAL_ENABLE_DMA_PWM_AUDIO`
  albo `HAL_ENABLE_DACLESS`.
- Zachowac istniejacy mixer ADPCM/SFX/OPL jak najdluzej.
- Dodac adapter `int16_t stereo PCM` -> `uint16_t PWM`.
- stereo zostaje zsumowane do mono. Miksowanie L/R, clipping, bias do unsigned 
  PWM, docelowa rozdzielczość: 12-bit

Braki HAL do sprawdzenia:

- Czy obecny `hal_dma_pwm_audio` wystarcza dla stalego bufora generowanego przez
  Doom mixer.
- Czy potrzebna jest wersja callbacku bardziej podobna do `audio_buffer_pool`.

Kryterium sukcesu:

- Kod audio kompiluje sie bez `pico_audio_i2s`.
- HAL przejmuje konfiguracje PWM/DMA.

Wynik wdrozenia:

- `hal_project_config.h` wlacza `HAL_ENABLE_DMA_PWM_AUDIO`, a aktywny source-set
  buduje teraz prawdziwy backend `src/pico/i_picosound.c` zamiast stubu
  `sound_pico_module`.
- `src/pico/i_picosound.c` uzywa `hal_dma_pwm_audio_create/start/stop` i
  callbacku HAL do uzupelniania podwojnych buforow PWM; HAL przejmuje konfiguracje
  PWM/DMA.
- Mixer ADPCM/SFX zostal zachowany, a publiczny hook
  `I_PicoSoundSetMusicGenerator()` dla generatora PCM muzyki pozostaje dostepny.
  Sam `music_opl_module` jest nadal stubem z wczesniejszego etapu.
- Dodany jest adapter miksu `int32_t stereo PCM` -> mono `uint16_t PWM`:
  L/R sa sumowane do mono, wynik jest clipowany do zakresu signed 16-bit,
  biasowany do unsigned PWM i skalowany do 12-bitowego okresu PWM.
- Konfiguracja audio mieszka w `src/pico/i_picosound.h`: domyslnie pin PWM 6,
  12-bitowy okres `4096`, blok DMA `1024` probki i czestotliwosc wynikajaca z
  `F_CPU / 4096`.
- Generowany `.ino` jawnie dolacza `Adafruit_TinyUSB.h`, a CMake nie nadpisuje
  juz `build.usbstack_flags`; dzieki temu Arduino CLI linkuje prawdziwa
  biblioteke Adafruit TinyUSB zamiast zostawiac nierozwiazane callbacki USB.
- Weryfikacja: `cmake --build .build/cmake --target firmware`,
  `cmake --build .build/cmake --target firmware_compile_db` oraz
  `./scripts/refresh-intellisense.sh` przechodza. Logi nie zawieraja dopasowan
  `warning:` ani `error:`, a aktywna sciezka kodu nie zawiera juz dopasowan
  `pico/audio_i2s`, `audio_i2s`, `audio_new_producer_pool`,
  `take_audio_buffer`, `give_audio_buffer` ani `pico_audio`.

### Etap 5: Display TFT Przez JaszczurHAL

Status: zrobiony.

Cel: zastapic `pico_scanvideo`/VGA przez TFT 240x320 lub 320x240 landscape.

Zakres:

- Na poczatku zostawic logiczne `SCREENWIDTH=320`, `SCREENHEIGHT=200`.
- Docelowo uzywac ILI9341/ST7789 w landscape 320x240.
- Render Dooma pozostaje 320x200; display backend dodaje pasy albo centrowanie.
- Zastapic scanline callbacki funkcja skladajaca linie/bloki RGB565.
- Nie uzywac per-pixel GFX jako glownej sciezki renderowania.
- Istniejace publiczne API JaszczurHAL do rysowania bitmap/blokow moze zostac
  uzyte jako smoke-test poprawnosci renderingu.
- Docelowo priorytetem jest maksymalna wydajnosc transferu obrazu, wiec jezeli
  obecne API bedzie za wolne, rozszerzamy JaszczurHAL o szybka sciezke TFT.

Braki HAL do dopisania:

- `write_pixels_fast` / `write_pixels_be` / `write_pixels_dma` dla TFT.
- API do ustawienia okna i strumieniowego wrzucania wielu pikseli bez malego
  chunkowania i bez per-pixel overheadu.
- Potencjalnie SPI DMA fast path.

Kryterium sukcesu:

- Backend display kompiluje sie bez `pico_scanvideo`.
- Mozna wyslac testowy pelny obraz albo linie obrazu przez JaszczurHAL TFT.

Wynik wdrozenia:

- Aktywny source-set buduje teraz `src/jaszczurhal/doom_video_hal.c`, a
  `src/jaszczurhal/doom_backend_stub.c` nie definiuje juz symboli wideo.
- `hal_project_config.h` wlacza domyslnie `HAL_ENABLE_ILI9341` oraz
  `HAL_DISPLAY_ILI9341`; jezeli projekt poda inny sterownik
  `HAL_ENABLE_ST7789`, `HAL_ENABLE_ST7735` albo `HAL_ENABLE_ST7796S`, wybor
  `HAL_DISPLAY_*` zostanie dopasowany do tego sterownika.
- Backend utrzymuje logiczny bufor Dooma 320x200 jako indexed/palette buffer,
  generuje palete RGB565 z `PLAYPAL`, obsluguje `I_SetPaletteNum()` dla
  palet obrazen/bonusow i sklada tylko jedna linie RGB565 naraz.
- `I_InitGraphics()` inicjalizuje TFT przez `hal_display_init()` oraz
  `hal_display_configure()`; domyslnie piny sa zgodne z przykladem HAL:
  `CS=17`, `DC=20`, `RST=21`, native `240x320`, rotacja `90`.
- Obraz 320x200 jest centrowany na panelu 320x240, a tlo/pasy sa czyszczone
  na czarno. Transfer ramki idzie obecnie przez
  `hal_display_draw_rgb_bitmap(..., width=320, height=1)` dla kolejnych linii.
- `pd_end_frame()` przestal byc pustym stubem: dla trybu `PD_COLUMNS` dorysowuje
  minimalnie automape, status bar, HUD, intermission/finale/demo screen i menu,
  po czym wysyla ramke do TFT.
- Obecne API HAL jest wystarczajace jako smoke-test linii obrazu. Wydajnosciowo
  nadal zostaje do zrobienia szybka sciezka okna/push-pixels/SPI DMA, zeby nie
  wysylac kazdej linii jako osobnej bitmapy.
- Weryfikacja: `cmake --build .build/cmake --target firmware`,
  `cmake --build .build/cmake --target firmware_compile_db` oraz
  `./scripts/refresh-intellisense.sh` przechodza. Firmware po tym etapie:
  `542144 B` flash i `187560 B` RAM. W aktywnych plikach backendu nie ma
  dopasowan `pico_scanvideo`, `video_doom`, `pico/scanvideo`.

### Etap 6: Input

Status: zrobiony.

Cel: przejsc z TinyUSB/UART backendu Dooma na wejscie obslugiwane przez
JaszczurHAL.

Pierwsza sciezka:

- Uzyc prostego odczytu przyciskow podlaczonych do Pico przez HAL GPIO jako
  minimalnego inputu smoke-testowego.
- Zmapowac przyciski na podstawowe eventy Dooma: ruch, strzal, uzycie, menu,
  potwierdzenie/cofniecie.

Opcje pozniejsze:

- USB HID host / klawiatura sa opcja na pozniej, po uruchomieniu podstawowej
  funkcjonalnosci gry.
- Tymczasowe UART/serial mozna wykorzystac tylko jesli bedzie to najprostsza
  sciezka diagnostyczna, ale nie jest to glowny kierunek portu.

Kryterium sukcesu:

- Gra moze otrzymywac eventy bez bezposredniego `tuh_*` w warstwie Dooma.

Wynik wdrozenia:

- Aktywny source-set buduje teraz `src/jaszczurhal/doom_input_hal.c`, a
  `src/jaszczurhal/doom_backend_stub.c` nie definiuje juz symboli inputu.
- Minimalny backend wejscia uzywa `hal_gpio_set_mode()` i `hal_gpio_read()`.
  Przyciski sa domyslnie aktywne stanem niskim z `HAL_GPIO_INPUT_PULLUP`, czyli
  fizyczny przycisk zwiera pin do GND.
- Domyslna mapa pinow: `UP=2`, `DOWN=3`, `LEFT=4`, `RIGHT=5`, `FIRE=7`,
  `USE=8`, `MENU=9`, `ACCEPT=10`, `BACK=11`. Kazdy pin mozna nadpisac przez
  makra `DOOM_INPUT_PIN_*`, a polaryzacje przez `DOOM_INPUT_ACTIVE_LOW`.
- Backend generuje doomowe `ev_keydown`/`ev_keyup` przez `D_PostEvent()` i
  mapuje przyciski na podstawowe klawisze Dooma: strzalki, `KEY_RCTRL`,
  spacje/uzycie, `KEY_ESCAPE`, `KEY_ENTER` i `KEY_BACKSPACE`.
- `I_GetEvent()`/`I_GetEventTimeout()` polluja GPIO raz na tic i wykrywaja
  zmiany stanu bez TinyUSB HID hosta ani UART.
- Stary `src/pico/i_input.c` zostaje jako nieaktywny kontekst historyczny; nie
  jest linkowany w sciezce JaszczurHAL.
- Weryfikacja: `cmake --build .build/cmake --target firmware`,
  `cmake --build .build/cmake --target firmware_compile_db` oraz
  `./scripts/refresh-intellisense.sh` przechodza. Log firmware nie zawiera
  dopasowan `warning:` ani `error:`, a aktywny source-set zawiera
  `src/jaszczurhal/doom_input_hal.c` zamiast `src/pico/i_input.c`.

### Etap 7: Flash, WAD/WHD I Save

Cel: uporzadkowac dostep do flasha i danych gry pod JaszczurHAL.

Zakres:

- Utrzymac WHD/WHDX jako format danych.
- Zdecydowac, czy WAD/WHD jest osadzany w firmware, umieszczany w flash FS,
  czy programowany w osobnym obszarze.
- Adresy i layout typu `TINY_WAD_ADDR` sa decyzja tego etapu, nie blokerem
  pierwszej kompilacji.
- Przeniesc operacje flash/save za HAL API.

Braki HAL do sprawdzenia/dopisania:

- Raw flash sector erase/program API dla RP2040/Arduino-Pico.
- Bezpieczna praca z XIP/cache/interrupt masking podczas programowania flasha.

Kryterium sukcesu:

- Kod save/load nie zalezy bezposrednio od `picoflash.c`.

Status: zrobiony dla aktywnego buildu JaszczurHAL.

Wynik:

- WHD/WHDX zostaje osobnym, surowym payloadem flash czytanym przez XIP.
- Layout zapisany w `hardware_layout.md`:
  - `DOOM_FLASH_XIP_BASE=0x10000000`
  - `DOOM_WHD_FLASH_ADDR=0x10200000`
  - `TINY_WAD_ADDR=DOOM_WHD_FLASH_ADDR`
  - `DOOM_FLASH_SIZE_BYTES=0x400000`
- Ten layout zaklada minimum 4 MB fizycznego flasha. `doom1.whx` ma ok.
  1.8 MB, wiec nie miesci sie razem z aktualnym firmware w pierwotnym
  ukladzie 2 MB Pico/Pico W.
- Stary adres `0x10048000` zostal usuniety, bo przy obecnym rozmiarze
  firmware zachodzil na obraz programu.
- Dodano `src/jaszczurhal/doom_storage_hal.c` i `.h` jako wlasciciela
  adresow WHD oraz konca flasha dla portu JaszczurHAL.
- `src/w_file_memory.c` w sciezce `JASZCZURHAL_PORT` bierze baze WHD z
  `DOOM_STORAGE_WHD_BASE`, bez Pico `binary_info`.
- Legacy implementacja save przez `picoflash.c` zostala odgrodzona od portu
  JaszczurHAL warunkiem `!JASZCZURHAL_PORT`.
- Aktywne hooki save dla JaszczurHAL sa w `doom_storage_hal.c`: odczyt slotow
  zwraca puste sloty, a zapis zwraca `false`, dopoki JaszczurHAL nie wystawi
  publicznego raw flash erase/program API. `NO_USE_SAVE=1` nadal zostaje.

Weryfikacja:

- `cmake --build .build/cmake --target firmware` przechodzi bez `warning:` i
  `error:`.
- `cmake --build .build/cmake --target firmware_compile_db` przechodzi.

### Etap 8: Multicore I Synchronizacja

Cel: zastapic bezposrednie Pico SDK semafory/multicore przez model JaszczurHAL.
JaszczurHAL posiada pełen support dla multicore, więc nie powinno stanowić to 
problemu. Oczywiście należy to zweryfikować.

Zakres:

- Zmapowac obecne `core1()` i `pd_core1_loop()` na `app_task1()`.
- Ocenić, czy obecne semafory Pico zostaja jako tymczasowa warstwa, czy HAL
  dostaje brakujace prymitywy.
- Pilnowac, aby audio/display nie blokowaly petli gry.

Kryterium sukcesu:

- Render/helper core nie wymaga bezposredniego `multicore_launch_core1()` w
  kodzie aplikacji.

Status: zrobiony dla aktywnego buildu JaszczurHAL.

Wynik:

- `hal_project_config.h` wlacza `HAL_ENABLE_APP_TASK1`.
- CMake generuje teraz `.ino` z `setup()`, `loop()` i warunkowym `loop1()`.
  `loop1()` wola `app_task1()`, zgodnie z kontraktem JaszczurHAL dla RP2040.
- `src/jaszczurhal/doom_app.cpp` definiuje `app_task1()` jako miejsce na
  `pd_core1_loop()`. Przy obecnym aktywnym backendzie `pd_core1_loop()` jest
  lekkim stubem, bo stary `pd_render.cpp` nie jest jeszcze wlaczony do source
  setu.
- `src/i_main.c` i `src/i_system.h` nie wlaczaja juz aktywnie
  `pico/multicore.h` ani `pico/sem.h` w sciezce `JASZCZURHAL_PORT`.
- `src/pico/i_system.c` w sciezce `JASZCZURHAL_PORT` uzywa `hal_delay_ms()`
  zamiast `sleep_ms()` w petli wyjscia.
- Stare `multicore_launch_core1()` pozostaje tylko w legacy
  `src/pico/i_video.c`, ktory nie jest czescia aktywnego buildu JaszczurHAL.
- `loop1` i `app_task1` sa widoczne w mapie firmware, wiec core-1 entry
  faktycznie trafia do obrazu.

Weryfikacja:

- `cmake --build .build/cmake --target firmware` przechodzi bez `warning:` i
  `error:`.
- `cmake --build .build/cmake --target firmware_compile_db` przechodzi.

### Etap 9: Siec / Piconet

Multiplaywe nie jest częścią pierwszego portu. Należy traktować to jako opcję na
przyszłość, i tylko jeśli podstawowa funkcjonalność portu będzie bezproblemowo działać.

Status: odlozony poza pierwszy port.

Wynik:

- Aktywny build JaszczurHAL zostaje bez Piconet (`NO_USE_NET=1`,
  `USE_PICO_NET` wylaczone).
- `src/pico/i_system.c` nie wlacza juz `piconet.h`, gdy `USE_PICO_NET` jest
  wylaczone.
- Stuby sieciowe pozostaja tam, gdzie sa potrzebne do linkowania odchudzonego
  builda Dooma.

Warunek powrotu:

- Najpierw potwierdzic stabilne uruchomienie gry, input, audio, display i WHD
  na docelowym hardware; dopiero potem zdecydowac, czy Piconet ma isc przez
  HAL I2C/API sieciowe czy pozostac osobnym legacy eksperymentem.

### Etap 10: Timer I Porzadkowanie Wygenerowanego Szkicu

Cel: przeniesc aktywny timer Dooma z Pico SDK na JaszczurHAL oraz uszczelnic
generator szkicu po zmianach source-setu.

Kryterium sukcesu:

- Aktywna sciezka timera nie uzywa `pico/time.h` ani `sleep_ms()`.
- Usuniecie pliku z `DOOM_PORT_SOURCES` usuwa go takze z wygenerowanego szkicu,
  bez zostawiania starego symlinka kompilowanego przez Arduino CLI.

Status: zrobiony.

Wynik:

- Dodano `src/jaszczurhal/doom_timer_hal.c`.
- Aktywny source-set buduje timer JaszczurHAL zamiast `src/pico/i_timer.c`.
- `I_GetTimeMS()` uzywa `hal_millis()`, a `I_Sleep()` uzywa
  `hal_delay_ms()`.
- `I_GetTime()` zostal naprawiony do klasycznego przeliczenia
  `(ms * TICRATE) / 1000`; poprzednia sciezka Pico zwracala `TICRATE * ms`,
  czyli tic rate ok. 1000 razy za szybki.
- CMake czysci teraz `${SKETCH_SRC_DIR}` przed ponownym linkowaniem source-setu,
  wiec stare symlinki nie zostaja w szkicu po wypieciu pliku.

Weryfikacja:

- `cmake --build .build/cmake --target firmware` przechodzi bez `warning:` i
  `error:`.
- `cmake --build .build/cmake --target firmware_compile_db` przechodzi.

### Etap 11: String Compatibility Bez Pico Stub

Cel: usunac kolejny aktywny plik z historycznego backendu Pico, zachowujac
minimalne funkcje kompatybilnosci wymagane przez Doom.

Kryterium sukcesu:

- Aktywny source-set nie buduje `src/pico/stubs.c`.
- `stricmp()` i `strnicmp()` sa dostarczane bez `pico.h`.

Status: zrobiony.

Wynik:

- Dodano `src/jaszczurhal/doom_string_hal.c`.
- `doom_string_hal.c` dostarcza ASCII-only `stricmp()` i `strnicmp()` uzywane
  przez `doomtype.h` jako zamienniki `strcasecmp()` i `strncasecmp()`.
- `src/pico/stubs.c` zostal wypiety z aktywnego `DOOM_PORT_SOURCES`.
- Czyszczenie `${SKETCH_SRC_DIR}` z Etapu 10 usuwa tez stary symlink
  `src/pico/stubs.c` z wygenerowanego szkicu.

Weryfikacja:

- `cmake --build .build/cmake --target firmware` przechodzi bez `warning:` i
  `error:`.

### Etap 12: Stan Hardware/Runtime Po Pierwszym Uruchomieniu Gry

Status: czesciowo dziala, renderer nadal niekompletny.

Platforma testowa:

- Plytka: Waveshare RP2040-Plus, wersja 4 MB flash.
- Build/FQBN: aktywny CMake uzywa zwyklego
  `rp2040:rp2040:rpipico:usbstack=tinyusb`. Wazne: proba uzycia FQBN z
  niestandardowym layoutem `flash=4194304_2097152` powodowala, ze firmware
  praktycznie nie startowal. Powrot do zwyklego `rpipico` naprawil boot.
- Display: 2.8" TFT ILI9341, logiczny Doom 320x200 centrowany na panelu
  320x240. Obraz zyje, status bar i gra sa widoczne.
- WHX: `doom1.whx` jest wgrywany osobno przez
  `sudo scripts/upload-whx-picotool.sh` pod adres `0x10200000`.
  Kopiowanie laczonego UF2 przez BOOTSEL/MSC okazalo sie niewiarygodne dla
  payloadu WHX powyzej ok. 1-2 MB na tej plytce; skrypt `upload-uf2.sh`
  zostal sprowadzony do uploadu samego `.build/firmware.uf2`.

Obecny runtime:

- Firmware startuje, USB CDC wraca, logi boot/render sa widoczne.
- WHX jest wykrywany w flashu, Chocolate Doom startuje i dochodzi do gry.
- Gra jest grywalna w sensie podstawowym: bohater chodzi i strzela, status bar
  dziala, widac bron, sciany, podloge/sufit i HUD.
- Ostatni dzialajacy wynik wydajnosci byl orientacyjnie ok. 10 FPS na oko.
- Ostatni bezpieczny build po rollbacku ryzykownej kolejki kolumn:
  `Sketch uses 307028 bytes (7%)`, global variables `161364 bytes (61%)`,
  zostaje `100780 bytes` SRAM dla stosu/heap. `.build/firmware.uf2` jest
  aktualnym artefaktem do testu.

Obecny renderer:

- Aktywny renderer jest nadal w `src/jaszczurhal/doom_backend_stub.c`; nie
  jest to pelny oryginalny `src/pd_render.cpp`.
- Z oryginalnego podejscia przeniesiono juz:
  - dekodowanie patchy WHD do kolumn,
  - cache zdekompresowanych kolumn patchy,
  - dekodowanie flatow WHD,
  - kolejke plane column -> renderowanie floor/ceiling jako poziome spany,
    wywolywane przez `DoomHAL_RenderQueuedPlanes()` z `src/doom/r_main.c`.
- Cache kolumn patchy jest obecnie maly i bezpieczny SRAM-owo:
  `HAL_PATCH_COLUMN_CACHE_SLOTS=32`, LRU, logowany jako
  `pcache=hits/misses`.
- Ryzykowny eksperyment z ramkowa kolejka kolumn scian/nieba zostal wycofany.
  Wariant z `HAL_COLUMN_QUEUE_MAX=768` i wiekszym cache spowodowal za duzy
  pobor SRAM: global variables wzrosly do ok. `188504 bytes (71%)`, a runtime
  padl na:
  `Unable to allocate 47792 bytes of RAM for zone`
  przy `available=11ab0 reserve=6000 target=bab0`. Nie wracac do statycznej
  duzej kolejki w `.bss`; jesli kolejka kolumn bedzie potrzebna, musi byc
  mniejsza, pasmowa, albo wspoldzielic istniejacy bufor roboczy.

Znane problemy renderingu na sprzecie:

- Gra sie uruchamia, ale render nie jest jeszcze poprawny.
- Na scianach i przy krawedziach czasami brakuje fragmentow podlogi/sufitu.
- Sprite'y renderuja sie zle:
  - wrogowie potrafia przebijac przez sciany i sa widoczni tam, gdzie nie
    powinni byc,
  - jednoczesnie przed bohaterem czasami nie widac wrogow, do ktorych gra
    pozwala strzelac.
- Najbardziej prawdopodobna przyczyna to brak pelnego oryginalnego mechanizmu
  sortowania/clippingu z `pd_render.cpp`: `push_down_x`, list kolumn,
  reclipowania fuzz/masked columns, poprawnego drawseg/sprite occlusion i
  oryginalnej organizacji visplane/flat runs.
- Flagi aktywnego portu nadal sa bardzo agresywne pamieciowo i renderowo
  (`NO_DRAWSEGS=1`, `NO_VISSPRITES=0`, `NO_MASKED_FLOOR_CLIP=0`,
  `NO_VISPLANE_GUTS=1`, `PD_COLUMNS=1`, `PD_SCALE_SORT=1`). Przy naprawie
  sprite'ow trzeba zaczac od analizy, ktore z tych uproszczen zabija
  poprawne maskowanie za scianami.

Najrozsadniejszy punkt powrotu:

1. Nie ruszac juz WHX/boot/display jako pierwszego podejrzanego; te elementy
   sa wystarczajaco potwierdzone.
2. Zaczac od sprite clipping / occlusion:
   sprawdzic `R_DrawMasked()`, `R_DrawMaskedColumn()`, aktywne znaczenie
   `NO_DRAWSEGS`, `NO_VISSPRITES`, `NO_MASKED_FLOOR_CLIP` i to, co w
   oryginalnym `pd_render.cpp` robia listy kolumn oraz `push_down_x`.
3. Dopiero potem przenosic wieksze kawalki `pd_render.cpp`. Nie przenosic
   ponownie duzej statycznej kolejki kolumn; SRAM jest zbyt ciasny.
4. Jesli potrzebna bedzie kolejka/grupowanie, projektowac ja jako bufor
   recyklingowany, pasmowy albo oparty o istniejace `s_work_area`, a kazdy
   krok sprawdzac buildem i realnym runtime zone memory.

## Etap 13: JaszczurHAL TFT Stream / SPI DMA

Dodano szybki tor wrzucania pikseli do TFT w lokalnej bibliotece
`../libraries/JaszczurHAL`:

- publiczne API w `hal_display.h`:
  - `hal_display_begin_write(x, y, w, h)`,
  - `hal_display_write_pixels_fast(const uint16_t *, count)`,
  - `hal_display_write_pixels_be(const uint8_t *, byte_count)`,
  - `hal_display_write_pixels_dma(const uint8_t *, byte_count)`,
  - `hal_display_end_write()`.
- `fast` robi hurtowy byteswap RGB565 native-endian -> BE w driverze.
- `be` przyjmuje gotowe bajty RGB565 high-byte/low-byte i nie chunkuje ich
  dodatkowo.
- `dma` uzywa `hal_spi_write_dma()`; na RP2040 jest blokujacy SPI TX DMA,
  a na mock/STM32 fallback do zwyklego `hal_spi_write()`.
- Driver ILI9341 i ST77xx trzymaja CS/DC/SPI transaction przez caly stream.
- Mocki i testy HAL zostaly rozszerzone o nowy kontrakt streamu.

Backend wideo Dooma (`src/jaszczurhal/doom_video_hal.c`) zostal przepiety:

- poprzednio: 200 razy na ramke `hal_display_draw_rgb_bitmap(..., h=1)`;
- teraz: jedno `hal_display_begin_write()` na okno 320x200, kazda linia
  konwertowana do bufora BE RGB565 i wysylana przez
  `hal_display_write_pixels_dma()`, potem `hal_display_end_write()`.

Weryfikacja po zmianie:

- `ctest` dla JaszczurHAL: `test_hal_display`, `test_hal_spi`,
  `test_ili9341_driver`, `test_st77xx_driver` - wszystkie PASS.
- Build firmware Dooma PASS:
  `Sketch uses 307532 bytes (7%)`, global variables `161380 bytes (61%)`,
  zostaje `100764 bytes` SRAM.
- To nie naprawia jeszcze glitchy sprite/floor/occlusion; zmienia tylko tor
  transferu pikseli do TFT i powinno mocno ograniczyc narzut ustawiania okna
  oraz SPI CPU-copy.

## Etap 14: Pierwsza Naprawa Maskowania Sprite'ow

Status: zrobiony jako maly krok diagnostyczno-naprawczy, wymaga testu na
sprzecie.

Wynik:

- Aktywny build JaszczurHAL przestal definiowac `NO_MASKED_FLOOR_CLIP=1`;
  obecnie uzywa `NO_MASKED_FLOOR_CLIP=0`.
- Dzieki temu `R_DrawMaskedColumn()` ponownie respektuje `mfloorclip` i
  `mceilingclip`.
- Po tescie runtime, w ktorym wrogowie nadal byli niewidoczni, wlaczono
  `NO_VISSPRITES=0` i `NO_DRAW_SPRITES=0`. Sprite'y swiata nie sa juz rysowane
  natychmiast w `R_ProjectSprite()` przez `R_DrawSpriteEarly()`, tylko trafiaja
  do klasycznej kolejki `vissprites` i sa rysowane z `R_DrawMasked()` po
  scianach i plane'ach.
- Pelne `MAXVISSPRITES=128` okazalo sie za drogie SRAM-owo: firmware budowal sie,
  ale runtime padal w `Z_Init()` na
  `Unable to allocate 69144 bytes of RAM for zone` przy
  `available=16e18 reserve=6000 target=10e18`. Aktualny build JaszczurHAL ustawia
  `MAXVISSPRITES=32`, z fallbackiem `overflowsprite` dla nadmiarowych sprite'ow.
- Po zmniejszeniu do `MAXVISSPRITES=32` runtime nadal potrafil pasc w `Z_Init()`,
  bo auto-target zone urosl do `0x11e98` (73368 B), a `malloc()` nie znalazl
  ciaglego bloku. Build JaszczurHAL ustawia teraz jawnie
  `JASZCZURHAL_ZONE_BYTES=65536` i `JASZCZURHAL_ZONE_MIN_BYTES=49152`, zeby zone
  nie rosla automatycznie do granicy nieprzechodzacej przez allocator.
- `NO_DRAWSEGS=1` zostaje, wiec build nadal nie doklada duzych buforow
  `openings` ani `drawsegs`. Proba uzycia koncowych `floorclip`/`ceilingclip`
  jako lekkiego przyblizenia occlusion nadal zostawiala wrogow niewidocznych,
  mimo logu `spr=21/8/8/8` i `masked=55`. Aktualny testowy wariant w sciezce
  bez drawsegow wrocil do pelnoekranowego clipu
  `maxfloorceilingcliparray`/`minfloorceilingcliparray`, zeby sprawdzic, czy
  problemem byl overclip.
- Sentinel `unsorted` w `R_SortVisSprites()` jest statyczny, zeby usunac
  warning `-Wdangling-pointer` po wlaczeniu `vissprites`.
- Przy okazji usunieto warningi `-Wold-style-declaration` w `src/doom/r_state.h`
  i `src/doom/r_main.c`, zmieniajac `const static` na `static const`.

Weryfikacja:

- `cmake --build .build/cmake --target firmware` przechodzi bez ostrzezen.
- `cmake --build .build/cmake --target firmware_compile_db` przechodzi.
- Rozmiar po zmianie z `MAXVISSPRITES=32` i zone 64 KB:
  `Sketch uses 308116 bytes (7%)`, global variables `162936 bytes (62%)`,
  zostaje `99208 bytes` SRAM.
- Dodano tymczasowa diagnostyke sprite'ow do logu `[render]`:
  `spr=seen/projected/queued/drawn`.
  - `seen`: liczba wywolan `R_ProjectSprite()` dla rzeczy znalezionych w
    sektorach.
  - `projected`: sprite przeszedl testy widocznosci ekranowej i ma zakres X.
  - `queued`: sprite trafil do `vissprites`.
  - `drawn`: `R_DrawMasked()` wywolal `R_DrawSprite()` dla sprite'a.

Do sprawdzenia runtime:

- Czy sprite'y widoczne przed graczem wrocily po przesunieciu rysowania na koniec
  ramki i usunieciu koncowego `floorclip`/`ceilingclip` z trybu bez drawsegow.
- Czy wrogowie za scianami zaczna przebijac przez solidne kolumny, co byloby
  oczekiwanym kosztem pelnoekranowego clipu bez drawsegow.
- Jesli `masked=0`, wkleic najnowsze `spr=...`: to powie, czy problem jest w
  thinglist/sektorach (`seen=0`), projekcji (`projected=0`), kolejce
  (`queued=0`) czy samym rysowaniu/dekodowaniu kolumn (`drawn>0`, `masked=0`).
- Jezeli problem zostanie tylko czesciowo poprawiony, nastepny krok to lekki
  mechanizm occlusion/sortowania bez pelnego `MAXOPENINGS=SCREENWIDTH*64`.

## Etap 15: Core1 Async TFT Flush

Status: zrobiony jako pierwszy bezpieczny krok wykorzystania core1, wymaga
testu runtime/FPS.

Wynik:

- `app_task1()` nie jest juz praktycznie puste: `pd_core1_loop()` wola teraz
  `DoomVideo_Core1Poll()`.
- `I_FinishUpdate()` nie wysyla juz ramki synchronicznie na core0. Po
  aktualizacji palety zleca flush aktualnego `I_VideoBuffer` do core1 i wraca.
- Core1 wykonuje konwersje indexed framebuffer -> RGB565 BE oraz
  `hal_display_begin_write()` / `hal_display_write_pixels_dma()` /
  `hal_display_end_write()`.
- Nie dodano drugiego pelnego framebufferu, bo 320x200 to ok. 64 KB SRAM i nie
  miesci sie sensownie obok zone 64 KB. Zamiast tego `pd_begin_frame()` czeka na
  zakonczenie poprzedniego flushu zanim core0 wyczysci i zacznie rysowac do tego
  samego bufora.
- To daje czesciowe nakladanie: core0 moze wyjsc z `pd_end_frame()` i wykonac
  prace petli gry do nastepnego `pd_begin_frame()`, ale nie moze zaczac
  nastepnego renderu przed koncem transferu TFT.
- Log `[render]` dostal pole `flush=done/waits/wait_ms`:
  - `done`: ile flushy wykonal core1,
  - `waits`: ile razy core0 musial czekac w barierze na zakonczenie flushu,
  - `wait_ms`: laczny czas czekania core0 w milisekundach.
- Pierwszy wariant logowal `waits` jako liczbe obrotow tight-loopa i dawal
  milionowe wartosci. Aktualny wariant czeka w krokach `hal_delay_us(50)` i
  mierzy czas przez `hal_micros()`, wiec diagnostyka jest porownywalna miedzy
  ramkami.

Weryfikacja:

- `cmake --build .build/cmake --target firmware` przechodzi bez ostrzezen.
- `cmake --build .build/cmake --target firmware_compile_db` przechodzi.
- Rozmiar po zmianie: `Sketch uses 308580 bytes (7%)`, global variables
  `162964 bytes (62%)`, zostaje `99180 bytes` SRAM.

Do sprawdzenia runtime:

- Czy log pokazuje rosnace `flush=done/...`, co potwierdza prace core1.
- Jak szybko rosnie `wait_ms`; duza wartosc oznacza, ze core0 nadal dogania
  transfer TFT i blokuje sie na poczatku nastepnej ramki.
- Czy nie pojawia sie tearing/losowe artefakty przy wspoldzielonym framebufferze.

## Etap 16: Lekki Sprite Occlusion Dla Pelnych Scian

Status: wycofany z domyslnej sciezki po tescie runtime; kod zostal jako
eksperyment za flaga `DOOM_SPRITE_OCCLUSION=0`.

Cel:

- Ograniczyc widocznosc wrogow poruszajacych sie za pelnymi scianami bez
  przenoszenia pelnych list kolumn z `pd_render.cpp` i bez wlaczania
  `drawsegs/openings`.

Wynik testu:

- Dodano tani per-X occluder dla pelnych scian jednosektorowych (`midtexture`).
- `r_segs.c` rejestruje dla kazdej kolumny X najblizsza pelna sciane:
  `x`, zakres `yl..yh` i `rw_scale`.
- `R_DrawSprite()` w sciezce `JASZCZURHAL_PORT && NO_DRAWSEGS` przycina kolumne
  sprite'a, jezeli w tej samej kolumnie X istnieje blizsza pelna sciana.
- To nie jest jeszcze pelny `push_down_x`: nie obsluguje poprawnie wszystkich
  przypadkow okien, drzwi, masked mid textures, fuzz ani splitowania kolumn.
  Ma tylko zatrzymac najgorszy przypadek sprite'ow widocznych przez solidne
  sciany.
- Log `[render]` dostal pole `occ=columns/clipped`:
  - `columns`: ile kolumn X ma zapamietany occluder pelnej sciany,
  - `clipped`: ile kolumn sprite'ow przycieto przez ten occluder.
- Runtime pokazal regresje: logi typu `occ=320/...` korelowaly ze znikaniem
  wrogow, czyli occluder byl zbyt szeroki/agresywny dla aktualnej sciezki bez
  pelnych `drawsegs/openings`.
- Domyslnie `DOOM_SPRITE_OCCLUSION=0`, wiec `occ=0/0`, a sprite'y wracaja do
  poprzedniego zachowania. Kod moze posluzyc pozniej jako punkt zaczepienia,
  ale dopiero po przeniesieniu bardziej oryginalnego clipowania/sortowania.

Weryfikacja:

- `cmake --build .build/cmake --target firmware` przechodzi bez ostrzezen.
- `cmake --build .build/cmake --target firmware_compile_db` przechodzi.
- Rozmiar po wylaczeniu domyslnej sciezki occludera i zmianach z etapu 17:
  `Sketch uses 308700 bytes (7%)`, global variables `166900 bytes (63%)`,
  zostaje `95244 bytes` SRAM.

Do sprawdzenia runtime:

- Czy `occ=0/0` i wrogowie znow sa widoczni.
- Problem wrogow widocznych za scianami nadal nie jest rozwiazany poprawnie;
  nastepny bezpieczniejszy kierunek to przenoszenie oryginalnych struktur
  clipowania zamiast zgadywania per-X.

Wynik runtime:

- Potwierdzone: po `DOOM_SPRITE_OCCLUSION=0` wrogowie wrocili. Regresja
  znikajacych sprite'ow byla skutkiem zbyt agresywnego occludera.
- Nie traktowac prostego per-X occludera jako poprawki produkcyjnej. Nastepna
  proba zaslaniania sprite'ow powinna bazowac na bardziej oryginalnym
  mechanizmie `drawsegs/openings` albo dokladniejszym odpowiedniku
  `push_down_x`.

## Etap 17: Wieksza Kolejka Plane'ow I Diagnostyka Dropow

Status: zrobione i przetestowane czesciowo; artefakty renderowania nadal
wystepuja.

Powod:

- W logach pojawilo sie `planes=1002`, a kolejka `HAL_PLANE_QUEUE_MAX` miala
  tylko 768 wpisow. To oznaczalo mozliwy overflow kolejki rysowania
  floor/ceiling i pasuje do objawow: niepelne tekstury, paski albo przebijajace
  kolorowe tlo pod "kwasem".

Wynik:

- `HAL_PLANE_QUEUE_MAX` zwiekszono z 768 do 1536.
- `queue_flat_vertical()` nie traktuje pustego zakresu po clampie jako bledu,
  tylko jako poprawnie pominieta kolumne.
- Dodano licznik `s_debug_plane_drops` i pole logu `pdrop`.
  - `pdrop=0`: kolejka plane'ow wystarczyla w danej ramce.
  - `pdrop>0`: nadal gubimy kolumny floor/ceiling i trzeba dalej zwiekszyc
    bufor albo przeniesc bardziej oryginalne laczenie visplane'ow.

Weryfikacja:

- `cmake --build .build/cmake --target firmware` przechodzi.
- `cmake --build .build/cmake --target firmware_compile_db` przechodzi.
- Rozmiar: `Sketch uses 308700 bytes (7%)`, global variables
  `166900 bytes (63%)`, zostaje `95244 bytes` SRAM.

Do sprawdzenia runtime:

- W scenach, gdzie bylo `planes=1002`, oczekiwane jest teraz `pdrop=0`.
- Jesli artefakty nadal sa przy `pdrop=0`, problem jest raczej w samej
  kolejnosci/clipowaniu visplane'ow albo kolumn, nie w pojemnosci kolejki.

Wynik runtime:

- Wrogowie sa znow widoczni po wylaczeniu occludera.
- Artefakty na ekranie nadal wystepuja: tekstury czasem nie renderuja sie w
  calosci, pojawiaja sie dziury/paski lub przebijajace tlo.
- Samo zwiekszenie kolejki plane'ow nie zamknelo problemu. Nastepny etap
  powinien sprawdzic aktualne wartosci `pdrop` w scenach z artefaktami:
  - jesli `pdrop>0`, nadal tracimy kolumny floor/ceiling i trzeba ograniczyc
    albo przebudowac kolejke plane'ow,
  - jesli `pdrop=0`, bardziej prawdopodobny jest blad kolejnosci rysowania,
    clipowania albo zbyt dalekie odejscie od oryginalnego `r_plane` /
    `r_segs` / `drawsegs`.

## Etap 18: Naprawa Occludera Sprite'ow (Bug Sentinela 8-bit)

Status: zrobiony w buildzie, wymaga testu runtime na sprzecie.

Cel:

- Przywrocic zaslanianie wrogow za pelnymi (jednostronnymi) scianami bez
  przenoszenia pelnych `drawsegs/openings` i bez ryzyka pamieciowego.

Znaleziona przyczyna regresji z Etapu 16:

- Pod `FLOOR_CEILING_CLIP_8BIT=1` typ `floor_ceiling_clip_t` to `uint8_t`, a
  konwencja uzywa `FLOOR_CEILING_CLIP_OFFSET=1`, zeby sentinel "brak clipu" byl
  nieujemny (`doomtype.h`).
- Stary occluder w `R_DrawSprite()` ustawial `cliptop[x] = -1`, co w `uint8_t`
  daje `255`. W `R_DrawMaskedColumn()` warunek `if (yl <= mceilingclip[x])`
  byl wtedy zawsze prawdziwy, `yl` rosl do `256 > yh`, i sprite NIE byl
  rysowany w kazdej kolumnie bez sciany. To powodowalo masowe znikanie
  wrogow, a nie tylko "zbyt agresywny" occluder.
- Drugi blad: kod zaslanial cala kolumne, ignorujac faktyczne pasmo
  `[top, bottom]` zajmowane przez sciane.

Wynik:

- `R_DrawSprite()` w sciezce `JASZCZURHAL_PORT && DOOM_SPRITE_OCCLUSION` uzywa
  teraz poprawnych wartosci domyslnych: `clipbot=viewheight+OFFSET`,
  `cliptop=OFFSET-1`.
- Occluder wycina tylko pasmo zajmowane przez najblizsza pelna sciane. Bo
  pojedyncze okno `(ceiling, floor)` utrzymuje jedna ciagla strone, kod
  preferuje pokazanie sprite'a nad sciana (sciany jednostronne siegaja
  podlogi); fallback zachowuje wieksza widoczna strone.
- Dodano margines glebokosci: sciana musi byc wyraznie blizej niz sprite
  (`scale > spr->scale + (spr->scale >> 4)`), zeby nie ucinac sprite'a
  stojacego tuz przy scianie.
- `DOOM_SPRITE_OCCLUSION` przelaczone z `0` na `1` w `CMakeLists.txt`.
- Occluder rejestruje tylko sciany jednostronne (`midtexture`, `r_segs.c`),
  wiec poprawia najczestszy przypadek "wrog za pelna sciana". Nie obejmuje
  occlusion przez sciany dwustronne / kroki portali - to nadal zostaje dla
  ewentualnej przyszlej sciezki `drawsegs/openings`.

Weryfikacja:

- `cmake --build .build/cmake --target firmware` przechodzi bez `warning:` i
  `error:`.
- `cmake --build .build/cmake --target firmware_compile_db` przechodzi.
- Rozmiar: `Sketch uses 308500 bytes (7%)`, global variables
  `169464 bytes (64%)`, zostaje `92680 bytes` SRAM. Wzrost ~2,5 KB to tablice
  occludera (`s_occluder_scale/top/bottom`, `clipbot/cliptop`); zone 64 KB bez
  zmian, brak ryzyka `Z_Init`.

Do sprawdzenia runtime:

- Czy wrogowie sa widoczni w otwartej przestrzeni (occluder nie powinien ich
  juz masowo ucinac) - to potwierdzi naprawe bledu sentinela.
- Czy wrogowie chowajacy sie za pelnymi scianami przestaja przez nie przebijac.
- Log `[render]`: pole `occ=columns/clipped` - `clipped>0` znaczy, ze occluder
  realnie tnie kolumny sprite'ow; przy znikajacych wrogach sprawdzic, czy
  `clipped` nie jest podejrzanie bliskie sumie kolumn sprite'ow.

## Aktualny Stan Renderera

Ostatni potwierdzony stan runtime:

- Gra startuje, rendering scian/podlog/sprite'ow dziala czesciowo.
- Wrogowie sa widoczni po cofnieciu domyslnego occludera.
- Core1 wykonuje async TFT flush, a core0 czeka na bariere przy poczatku
  kolejnej ramki.
- Nadal sa artefakty scian/podlog: niepelne tekstury, paski, dziury i
  przebijajace tlo.
- Problem wrogow widocznych za scianami pozostaje nierozwiazany, ale prosty
  occluder per-X powodowal gorsza regresje niz sam problem.

Najbardziej sensowny kolejny kierunek:

- Zebrac nowy log z artefaktami, szczegolnie `planes`, `pdrop`, `masked`,
  `spr`, `occ` i `flush`.
- Jesli `pdrop=0`, zaczac przenosic wiekszy, oryginalny fragment renderera:
  najpierw clipowanie/organizacje visplane'ow i drawsegow, zamiast kolejnych
  heurystyk per-kolumna.

## Oczekiwane Konflikty

1. `main()` kontra `.ino`/`setup()`/`loop()`.
2. Pico SDK `pico_scanvideo` kontra HAL TFT.
3. `pico_audio_i2s` kontra HAL DMA PWM audio.
4. TinyUSB host kontra pierwsza sciezka inputu przez HAL GPIO/przyciski.
5. Bezposrednie flash programming kontra HAL storage/raw flash.
6. `pico/sem.h` i `pico/multicore.h` kontra model `app_task1`.
7. Bardzo ciasne zalozenia pamieciowe Dooma kontra runtime Arduino-Pico.
8. C/C++ granice: Doom jest glownie C, HAL ma sporo C++ driverow.
9. Stare symlinki w wygenerowanym szkicu kontra etapowe wypinanie plikow z
   source-setu.
