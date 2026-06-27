# Kontekst dla agentów pracujących z JaszczurHAL

Ten dokument jest praktyczną mapą repozytorium dla przyszłych agentów. Ma
pomóc szybko ustalić, gdzie jest biblioteka, jak jest zbudowana, które pliki są
kanoniczne i jak bezpiecznie wprowadzać zmiany.

## Lokalizacja i metadane

- Lokalna ścieżka repozytorium w tym środowisku:
  `/home/jaszczurtd/development/projects_git/libraries/JaszczurHAL`
- Root git:
  `/home/jaszczurtd/development/projects_git/libraries/JaszczurHAL`
- Zdalne repo:
  `https://github.com/jaszczurtd/JaszczurHAL.git`
- Branch odczytany przy tworzeniu tego dokumentu: `main`
- HEAD odczytany przy tworzeniu tego dokumentu: `536b8a5`
- Stan roboczy przy tworzeniu tego dokumentu: czysty
- Aktualna wersja z `library.properties`: `1.7.0`
- Autor z README: Marcin "Jaszczur" Kielesinski
- Główny publiczny include:
  `#include <JaszczurHAL.h>`
- Root include dla konsumentów:
  `src/`

Zawsze sprawdź aktualny stan przed pracą:

```bash
pwd
git status --short
git branch --show-current
git rev-parse --short HEAD
```

## Czym jest JaszczurHAL

JaszczurHAL to biblioteka C/C++ dla embedded: warstwa abstrakcji sprzętu
(HAL), zestaw przenośnych driverów i narzędzia pomocnicze. Główna idea to
oddzielenie logiki aplikacji od konkretnego SDK/MCU.

Najważniejsze backendy:

- `HAL_TARGET_RP2040` - RP2040/RP2350 przez Earle Philhower Arduino-Pico.
  To najbardziej kompletny backend.
- `HAL_TARGET_STM32G474` - bare-metal STM32G474, bez Arduino, z własnym
  startem, linkerem, register-level backendami i rosnącym reuse driverów
  wspólnych.
- `HAL_TARGET_MOCK` - deterministyczny backend hostowy do testów.
- ESP32 jest opisany jako planowany, ale w aktualnym kodzie nie istnieje
  `HAL_TARGET_ESP32` ani folder `src/hal/impl/esp32_arduino/`.

Projekt używa modelu opt-in: moduły opcjonalne są kompilowane dopiero po
zdefiniowaniu odpowiednich `HAL_ENABLE_*`. Zależności między flagami są
propagowane w `src/hal/hal_config.h`.

## Najważniejsze pliki wejściowe

Czytaj te pliki przed większą zmianą:

- `README.md` - krótki opis biblioteki, struktura, szybki start.
- `doc/JaszczurHAL_API.md` - główny indeks referencji API i mapa modułów.
- `doc/features.md` - zwięzła macierz funkcji.
- `doc/HAL_FLAGS.txt` - podsumowanie flag `HAL_ENABLE_*`.
- `doc/lib_compilation.md` - buildy host/RP2040/STM32 i warianty FreeRTOS.
- `examples/README.md` - unified build przykładów i kontrakt aplikacji.
- `src/hal/hal_target.h` - jedyne kanoniczne miejsce wyboru targetu.
- `src/hal/hal_config.h` - flagi modułów, zależności, walidacje, assert.
- `src/hal_app_entry.cpp` i `src/hal/hal_app.h` - portable entry point.
- `runalltests.sh` - pełna lokalna bramka jakości.

Statusy i roadmapy:

- `doc/STM32G474_porting_progress.md` - status portu STM32G474.
- `doc/ESP32_porting_progress.md` - plan portu ESP32.
- `doc/future_ideas.md` - backlog architektoniczny.

Uwaga: statusowe dokumenty mogą wyprzedzać lub opóźniać kod. Przy sprzeczności
traktuj kod, CMake i testy jako źródło prawdy, a dokument statusowy jako
kontekst.

## Struktura repozytorium

```text
CMakeLists.txt              # host/mock test build
library.properties          # metadane Arduino library
runalltests.sh              # pełna bramka jakości
runmefirst.sh               # jednorazowe przygotowanie toolchainów
freertos_core_version.conf  # pin FreeRTOS-Kernel dla STM32G474
doc/                        # dokumentacja API, flagi, roadmapy, datasheety
examples/                   # portable przykłady app.c/app.cpp + config
rp2040_lib/                 # static library build dla Arduino-Pico RP2040
stm32_lib/                  # static library build, toolchain i linker STM32G474
scripts/                    # build helpers, analiza, narzędzia
src/                        # publiczne include i implementacje biblioteki
tests/                      # testy hostowe na Unity/CTest
third_party/                # opcjonalne dependency, np. FreeRTOS-Kernel
vscode-templates/           # gotowe konfiguracje VS Code dla projektów
build*/                     # artefakty lokalne, nie traktować jako źródła
```

## Publiczne include i granica API

- W aplikacjach preferuj:

  ```cpp
  #include <JaszczurHAL.h>
  ```

- `src/JaszczurHAL.h` jest głównym punktem wykrywania biblioteki przez
  `arduino-cli` i dołącza `hal/hal.h`.
- `src/hal/hal.h` jest HAL-only agregatorem. Dołącza publiczne nagłówki
  zależnie od aktywnych `HAL_ENABLE_*`.
- `src/tools.h` i `src/tools_c.h` są narzędziami pomocniczymi, nie główną
  granicą portowalności.
- `src/libConfig.h` jest kompatybilnościowym redirectem do `hal/hal_config.h`.

Projektowa granica przenośności to publiczne `src/hal/hal_*.h`. Aplikacje
powinny opierać logikę sprzętową na tej warstwie, nie na Arduino/Pico SDK/STM32
registerach bezpośrednio.

## Architektura źródeł

Najważniejszy układ pod `src/`:

```text
src/JaszczurHAL.h
src/hal_app_entry.cpp
src/tools.h, src/tools_c.h
src/hal/
  hal_*.h                  # publiczne API modułów
  hal_config.h/.cpp        # konfiguracja, flagi, runtime config
  hal_target.h             # target selection
  hal_*.cpp                # wspólne wrappery/facady tam, gdzie są potrzebne
  impl/
    .mock/                 # backend hostowy do testów
    rp2040/                # backend Arduino-Pico
    stm32g474/             # backend bare-metal STM32G474
    shared/
      compat/              # shimy zgodności, np. BSD sockets/debug format
      drivers/             # target-neutral drivery sprzętowe
      frameworks/          # większe stosy/enginy/bundled libs
src/utils/                 # tools, PID, watchdog, draw helpers, Unity
```

Zasada własności:

- Publiczne nagłówki i kontrakty są w `src/hal/hal_*.h`.
- Implementacje backend-specific są w `src/hal/impl/<target>/`.
- Kod współdzielony przez backendy jest w `src/hal/impl/shared/`.
- Kod w `impl/shared/` powinien zależeć od HAL API, a nie od Arduino,
  rejestrów STM32 ani makr targetu, chyba że jest to jawnie shim/compat.
- Facady publiczne zwykle zarządzają handle, walidacją, lockami i dispatch.
  Chip/protocol logic trafia do shared drivera.

Przykładowy wzorzec dla drivera przenośnego:

```text
src/hal/hal_digipot.h
src/hal/hal_digipot.cpp
src/hal/impl/shared/drivers/digipot/
tests/test_hal_digipot.cpp
examples/...
```

## Target selection

Kanoniczny wybór targetu jest w `src/hal/hal_target.h`. Konsument definiuje
dokładnie jedną flagę:

```c
#define HAL_TARGET_RP2040
#define HAL_TARGET_STM32G474
#define HAL_TARGET_MOCK
```

Jeśli żadna nie jest zdefiniowana, target jest auto-detekowany:

- Arduino/Pico macros -> `HAL_TARGET_RP2040`
- `STM32G474xx` / `STM32G4` -> `HAL_TARGET_STM32G474`
- kompilator hostowy bez ARM -> `HAL_TARGET_MOCK`
- bare-metal ARM bez rozpoznania -> compile-time error

Reszta kodu powinna używać:

- `HAL_TARGET_IS_RP2040`
- `HAL_TARGET_IS_STM32G474`
- `HAL_TARGET_IS_MOCK`
- `HAL_TARGET_NAME`
- `JH_STM32G474_HW` tylko do rozróżnienia realnego builda ARM STM32G474 od
  hostowego sanity builda tego backendu.

Nie dodawaj rozproszonych heurystyk typu `#ifdef ARDUINO` jako substytutu
target selection. Jeśli powstaje nowy backend, zacznij od rozszerzenia
`hal_target.h`, app entry, build systemu i mock/test coverage.

## Feature flags

Funkcje opcjonalne są włączane przez `HAL_ENABLE_*`, zwykle w
`hal_project_config.h` projektu lub przez `-D`. Preferowany model: `hal_project_config.h`.

`src/hal/hal_config.h` robi cztery ważne rzeczy:

- automatycznie dołącza `hal_project_config.h`, jeśli jest na include path,
- propaguje zależności, np. `HAL_ENABLE_KV` -> `HAL_ENABLE_EEPROM`,
- waliduje wymagane backendy, np. `HAL_ENABLE_RTC` wymaga `PCF8563` albo
  `DS3231`,
- pilnuje specjalnych przypadków FreeRTOS i target-specific flag.

Przykłady propagacji:

- `HAL_ENABLE_TIME`, `MQTT`, `UDP`, `TCP`, `OTA`, `WIREGUARD` -> `WIFI`
- `HAL_ENABLE_BSD_SOCKETS` -> `UDP`, `TCP`, `WIFI`
- `HAL_ENABLE_A7670` -> `CELLULAR_MODEM`, `UART`
- `HAL_ENABLE_SDLOGGER` -> `FAT`, `EEPROM`, `SPI`
- `HAL_ENABLE_EXTERNAL_ADC`, `BH1750`, `TSC2007` -> `I2C`
- `HAL_ENABLE_STMPE610` -> `I2C`, `SPI`
- `HAL_ENABLE_PCF8563` lub `DS3231` -> `RTC`, `I2C`
- `HAL_ENABLE_MCP9600` -> `THERMOCOUPLE`, `I2C`
- `HAL_ENABLE_MCP401X` lub `MAX5395` -> `DIGIPOT`, `I2C`
- `HAL_ENABLE_DACLESS` -> `DMA_PWM_AUDIO`, `PWM_FREQ`
- `HAL_ENABLE_DS18B20` -> `ONEWIRE`
- `HAL_ENABLE_ILI9341/ST7789/ST7735/ST7796S` -> `TFT`, `DISPLAY`, `SPI`
- `HAL_ENABLE_SSD1306` -> `DISPLAY`, `I2C`
- `HAL_ENABLE_MCP2515/MCP251XFD` -> `CAN`, `SPI`
- `HAL_ENABLE_STM32G474_FDCAN` -> `CAN` i tylko STM32G474

Kompletna lista jest w `doc/HAL_FLAGS.txt` i komentarzu w `hal_config.h`.

## Kontrakt aplikacji

Portable aplikacje używają kontraktu z `src/hal/hal_app.h`:

```c
void app_start(void);
void app_task0(void);
void app_task1(void); /* opcjonalne */
```

`HAL_PROVIDE_APP_ENTRY` powoduje, że `src/hal_app_entry.cpp` wystawia entry
point dla danego backendu:

- RP2040/Arduino-Pico: `setup()` -> `app_start()`, `loop()` -> `app_task0()`.
  `loop1()` jest emitowane tylko z `HAL_ENABLE_APP_TASK1`, bo na RP2040 samo
  istnienie `loop1()` uruchamia ścieżkę core 1.
- STM32G474 bare-metal: `main()` wykonuje `app_start()`, potem super-loop z
  `app_task0()` i opcjonalnie cooperative `app_task1()`.
- STM32G474 FreeRTOS: `main()` wykonuje `app_start()`, tworzy taski FreeRTOS
  dla `app_task0()` i opcjonalnie `app_task1()`, potem uruchamia scheduler.
- Mock: analogiczny hostowy loop, użyteczny dla demo, nie dla unit testów.

Przykłady w `examples/` nie mają ręcznego `main()` ani `.ino`. Build generuje
albo dopina boilerplate.

## Backend RP2040/RP2350

RP2040/RP2350 używa Arduino-Pico. W `library.properties` biblioteka deklaruje
`architectures=rp2040` i include `JaszczurHAL.h`.

Najważniejsze pliki:

- `src/hal/impl/rp2040/` - target backend
- `src/hal/impl/rp2040/drivers/rp2040/` - usługi SoC, fault/system
- `src/hal/impl/rp2040/frameworks/` - integracje Arduino-origin, np.
  `PubSubClient`, `arduino-wireguard-pico-w`
- `rp2040_lib/CMakeLists.txt`
- `rp2040_lib/toolchain_rp2040.cmake`
- `rp2040_lib/MEMORY_MAP.md`
- `scripts/build_rp2040_lib.sh`

Static library build:

```bash
./scripts/build_rp2040_lib.sh --clean
```

Z flagami:

```bash
./scripts/build_rp2040_lib.sh --clean \
  -D HAL_ENABLE_WIFI \
  -D HAL_ENABLE_MQTT \
  -D HAL_ENABLE_GPS
```

FreeRTOS na RP2040 wymaga trybu FreeRTOS SMP w Arduino-Pico, czyli `__FREERTOS`.
Dla helpera użyj:

```bash
./scripts/build_rp2040_lib.sh --clean --freertos
```

RP2040 memory/linker layout jest własnością Arduino-Pico. Szczegóły są w
`rp2040_lib/MEMORY_MAP.md`. Jeśli używasz `HAL_ENABLE_LITTLEFS`, finalny FQBN
/ board menu musi mieć niezerową partycję LittleFS.

## Backend STM32G474

STM32G474 jest bare-metal backendem z własnym toolchainem, startupem, linkerem
i register-level implementacjami. Nie zakłada Arduino.

Najważniejsze pliki:

- `src/hal/impl/stm32g474/`
- `src/hal/impl/stm32g474/port/`
- `src/hal/impl/stm32g474/drivers/stm32g474/`
- `src/hal/impl/stm32g474/freertos/`
- `stm32_lib/CMakeLists.txt`
- `stm32_lib/toolchain_stm32g474.cmake`
- `stm32_lib/STM32G474RETx_FLASH.ld`
- `stm32_lib/MEMORY_MAP.md`
- `stm32_lib/freertos_stm32g474.cmake`
- `scripts/build_stm32_lib.sh`

Static library build:

```bash
./scripts/build_stm32_lib.sh --clean
```

FreeRTOS:

```bash
./scripts/build_stm32_lib.sh --clean --freertos
```

STM32 FreeRTOS używa lokalnego `third_party/FreeRTOS-Kernel` albo ścieżki
`JH_FREERTOS_KERNEL_DIR`. Pin wersji jest w `freertos_core_version.conf`:
`FREERTOS_KERNEL_VERSION=V11.1.0+`, commit
`2e588af95bad29aef577373512883370c9408c1c`.

Według aktualnego `stm32_lib/CMakeLists.txt` domyślny profil STM32 włącza m.in.
`I2C`, `I2C_SLAVE`, `SPI`, `MCP2515`, `UART`, `DAC`, `DACLESS`, `PCNT`,
`MCP401X`, `MAX5395`, `MCP9600`, `MAX6675`, `BH1750`, `TSC2007`, `STMPE610`,
`IRSMALL_DECODER`, `EXTERNAL_ADC`, `DS18B20`, `DHT`, `GPS`, `HD44780`.
Inne moduły włączaj świadomie przez `EXTRA_HAL_DEFINES` lub
`hal_project_config.h`.

Moduły nadal opisane jako brakujące realnego backendu STM32G474:
`mqtt`, `ota`, `swserial`, `udp`, `wifi`, `wireguard`.

STM32 memory layout jest w `stm32_lib/MEMORY_MAP.md`. Domyślnie:

- flash: `0x08000000` - `0x08080000`, 512 KB,
- RAM: `0x20000000` - `0x20018000`, 96 KB,
- EEPROM/KV: ostatnie 4 KB flash,
- LittleFS: domyślnie 0 KB; CMake helpers rezerwują 64 KB, gdy
  `HAL_ENABLE_LITTLEFS` jest włączone bez jawnego rozmiaru.

## Backend MOCK i testy hostowe

Mock backend jest w `src/hal/impl/.mock/`. Służy do deterministycznych testów
hostowych, nie do pełnej walidacji prawdziwej konkurencji na MCU. FreeRTOS
POSIX testy mogą być dołączone przez `JH_ENABLE_FREERTOS_POSIX_TESTS=ON`.

Szybki build/test hostowy:

```bash
cmake -S . -B build_test
cmake --build build_test
ctest --test-dir build_test --output-on-failure
```

Z FreeRTOS POSIX:

```bash
cmake -S . -B build_test -DJH_ENABLE_FREERTOS_POSIX_TESTS=ON
cmake --build build_test
ctest --test-dir build_test --output-on-failure
```

Rootowy `CMakeLists.txt` buduje `hal_mock` z wieloma flagami `HAL_ENABLE_*`,
żeby testy objęły możliwie szeroką powierzchnię API. Rejestracja testów jest w
`tests/CMakeLists.txt`.

## Przykłady

Przykłady są w `examples/`. Każdy przykład ma zwykle:

```text
examples/NN_name/
  app.c lub app.cpp
  hal_project_config.h
  README.md opcjonalnie
```

Unified CMake examples build:

```bash
cmake -S examples -B build_examples_rp2040 -DJH_EXAMPLE_TARGET=rp2040
cmake --build build_examples_rp2040
```

```bash
cmake -S examples -B build_examples_stm32g474 \
  -DJH_EXAMPLE_TARGET=stm32g474 \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/stm32_lib/toolchain_stm32g474.cmake"
cmake --build build_examples_stm32g474
```

Pojedynczy target ma nazwę `<folder>_<backend>`, np.:

```bash
cmake --build build_examples_rp2040 --target 01_blink_rp2040
cmake --build build_examples_stm32g474 --target 01_blink_stm32g474
```

Przykłady WiFi używają Pico W FQBN. Przykład FreeRTOS `29_freertos_smoke` jest
dodawany tylko, gdy build examples ma aktywny FreeRTOS.

## Pełna bramka jakości

Pełen lokalny gate:

```bash
./runalltests.sh
```

Z jobami:

```bash
./runalltests.sh -j8
```

`runalltests.sh` wykonuje:

1. sprawdzenie narzędzi,
2. host unit tests z FreeRTOS POSIX,
3. Valgrind memcheck,
4. cppcheck,
5. clang-tidy dla host i STM32 compile database,
6. build static libraries STM32 i RP2040 plus macierz flag RP2040,
7. build przykładów RP2040 i STM32G474.

Pełny gate wymaga narzędzi z `runmefirst.sh`, `arduino-cli`, core
`rp2040:rp2040`, `arm-none-eabi-*`, Valgrind, clang-tidy i cppcheck. Może
czyścić katalogi `build*`.

Walidacja:

Po dodaniu funkcjonalności do JaszczurHAL, należy zawsze starać się dodawać testy pokrywające
nową funkcjonalność. Trzeba starać się by było to testowalne nie tylko na etapie unit-testów,
ale również przez mem-test valgrind.
Każdy etap dodawania funkcjonalności powinien uruchamiać pObowiązuje zasada: zero-tolerance dla 
warningów w nowym kodzie.

- Mała zmiana w shared driverze: host testy + konkretny test modułu.
- Zmiana publicznego API: testy hostowe, dokumentacja API, przykłady lub
  przynajmniej kompilacja affected examples.
- Zmiana target backendu: test hostowy, static lib build targetu, affected
  examples.
- Zmiana build systemu lub flag: `runalltests.sh`, jeśli toolchainy są
  dostępne.

## Jak dodawać lub zmieniać moduł

Minimalna ścieżka dla nowego modułu HAL:

1. Dodaj publiczny kontrakt w `src/hal/hal_<module>.h`.
2. Dodaj include w `src/hal/hal.h` pod właściwym `HAL_ENABLE_*`, jeśli moduł
   ma być częścią agregatora.
3. Dodaj flagę i zależności w `src/hal/hal_config.h`.
4. Dodaj implementację mock w `src/hal/impl/.mock/`.
5. Dodaj implementacje targetów w `src/hal/impl/rp2040/` i/lub
   `src/hal/impl/stm32g474/`, albo jasno raportuj unsupported.
6. Jeśli logika jest przenośna, umieść ją w `src/hal/impl/shared/drivers/` lub
   `src/hal/impl/shared/frameworks/`.
7. Zaktualizuj CMake, jeśli źródło nie jest objęte globem lub należy do
   ręcznej listy, np. rootowy `CMakeLists.txt` dla `hal_mock`.
8. Dodaj test w `tests/` i rejestrację w `tests/CMakeLists.txt`.
9. Dodaj lub zaktualizuj przykład w `examples/`, jeśli moduł jest user-facing.
10. Zaktualizuj `doc/HAL_FLAGS.txt`, `doc/features.md` i właściwy rozdział w
    `doc/api/`.

Nie włączaj nowych modułów domyślnie bez powodu. Ta biblioteka celowo trzyma
opcjonalną powierzchnię jako opt-in.

## Zasady implementacyjne specyficzne dla tego repo

- Zachowuj publiczne API, jeśli zmiana nie wymaga łamania kompatybilności.
- Preferuj małe, testowalne kroki. Nie mieszaj dużego refaktoru z build-system
  churn w jednej zmianie.
- W `impl/shared/` używaj HAL API i przenośnych helperów. Unikaj zależności od
  Arduino i rejestrów MCU.
- Target-specific pin/peripheral/IRQ/startup/linker logic należy do
  `impl/<target>/`, `rp2040_lib/` albo `stm32_lib/`.
- Inicjalizacja i teardown (`init`, `create`, `destroy`, `deinit`) są
  traktowane jako operacje single-core.
- Runtime powinien być thread-safe tam, gdzie moduł nie dokumentuje inaczej.
- Singletony i locki często używają defensywnego lazy init. Sprawdź istniejący
  wzorzec przed dodaniem własnego.
- `HAL_ASSERT()` jest domyślnie aktywny; opt-out to `HAL_DISABLE_ASSERTS`.
- Nie poprawiaj wygodnych helperów w `tools.*`, jeśli funkcjonalnie należą do
  modułu HAL.
- Nie edytuj artefaktów `build*` jako źródła prawdy.
- Nie zmieniaj vendored/bundled third-party kodu bez jasnej potrzeby i bez
  sprawdzenia licencji w sąsiednich plikach.
- przed dodawaniem nowych funkcjonalności, zawsze sprawdzaj czy ta funkcjonalność
  już istnieje w JaszczurHAL. Preferuj to, co jest już zaimplementowane.

## Thread safety i FreeRTOS

Thread safety jest jednym z głównych założeń projektu, ale nie każdy moduł jest
równie wielowątkowy:

- `init/create/destroy/deinit` traktuj jako single-core/single-owner.
- Normalny mock backend służy deterministycznym testom, nie modelowaniu
  realnej konkurencji.
- `JH_ENABLE_FREERTOS_POSIX_TESTS` daje hostowe smoke/regression testy ścieżek
  FreeRTOS.
- Na RP2040 `HAL_ENABLE_FREERTOS` jest poprawne tylko z Arduino-Pico FreeRTOS
  mode (`__FREERTOS`).
- Na STM32G474 `HAL_ENABLE_FREERTOS` wymaga lokalnego FreeRTOS-Kernel i
  `FreeRTOSConfig.h`.
- Nie ma publicznego `hal_rtos_*` wrappera. Aplikacje używają natywnego API
  FreeRTOS, a HAL wybiera RTOS-aware implementacje mutex/delay/idle.

## Storage i pamięć

RP2040:

- Finalny linker script i flash layout są własnością Arduino-Pico.
- `hal_eeprom` używa Arduino EEPROM albo zewnętrznego EEPROM zależnie od
  `HAL_EEPROM_TYPE`.
- `hal_littlefs` wymaga właściwego board menu/FQBN z partycją FS.

STM32G474:

- Linker script: `stm32_lib/STM32G474RETx_FLASH.ld`.
- EEPROM/KV domyślnie zajmuje ostatnie 4 KB flash.
- LittleFS wymaga niezerowego `HAL_STM32_FLASH_LITTLEFS_SIZE`.
- Jeśli zmieniasz rozmiary flash-backed storage, pilnuj zgodności definicji C
  i symboli linkera.

## Dokumentacja API

Główny indeks to `doc/JaszczurHAL_API.md`. Rozdziały split API:

- `doc/api/02_module_flags.md` - flagi i konfiguracja
- `doc/api/03_build_tests.md` - build dependencies i testy
- `doc/api/04_multicore_drivers_migration.md` - multicore, drivery, migracja
- `doc/api/05_gpio_adc_pwm.md` - GPIO/ADC/PWM/DACLESS
- `doc/api/06_timers_system.md` - timery, system, bits, math
- `doc/api/07_crypto.md` - crypto
- `doc/api/08_sync_serial.md` - sync, serial, framing, auth
- `doc/api/09_buses.md` - SPI/I2C/UART/OneWire
- `doc/api/10_can_display.md` - CAN i display
- `doc/api/11_sensors.md` - sensory i RTC/GPS
- `doc/api/12_modem.md` - modem AT i SimCom
- `doc/api/13_output_devices.md` - output devices
- `doc/api/14_storage.md` - EEPROM/KV/LittleFS/FAT/SD logger
- `doc/api/15_connectivity.md` - WiFi/UDP/TCP/BSD/MQTT/OTA/WireGuard/time
- `doc/api/16_utilities.md` - soft timer, PID, tools, watchdog
- `doc/api/17_cJSON.md` - cJSON
- `doc/api/18_LodePNG.md` - PNG
- `doc/api/19_JPEG.md` - JPEG

Po zmianie API lub flag aktualizuj dokumentację razem z kodem. Przyszły agent
ma prawo ufać dokumentacji tylko wtedy, gdy jest utrzymywana obok zmian.

## Szybka mapa modułów

Core/system:
`hal_gpio`, `hal_adc`, `hal_pwm`, `hal_pwm_freq`, `hal_dac`,
`hal_dacless`, `hal_dma_pwm_audio`, `hal_pcnt`, `hal_timer`,
`hal_soft_timer`, `hal_system`, `hal_sync`, `hal_bits`, `hal_math`,
`hal_pid_controller`, `hal_serial`.

Buses/connectivity:
`hal_uart`, `hal_swserial`, `hal_i2c`, `hal_i2c_slave`, `hal_spi`,
`hal_onewire`, `hal_can`, `hal_wifi`, `hal_udp`, `hal_tcp`, BSD sockets
compat headers, `hal_mqtt`, `hal_ota`, `hal_wireguard`, `hal_time`.

Storage:
`hal_eeprom`, `hal_kv`, `hal_littlefs`, `hal_sdlogger`, FatFs/SD framework.

Sensors/time:
`hal_rtc`, `hal_thermocouple`, `hal_ds18b20`, `hal_dht`, `hal_bh1750`,
`hal_external_adc`, `hal_gps`, `hal_tsc2007`, `hal_stmpe610`,
`hal_irsmall_decoder`.

Output/media:
`hal_display`, `hal_hd44780`, `hal_rgb_led`, `hal_digipot`,
`hal_pga2311`, cJSON, LodePNG, JPEG.

## Najczęstsze komendy

Host tests:

```bash
cmake -S . -B build_test -DJH_ENABLE_FREERTOS_POSIX_TESTS=ON
cmake --build build_test
ctest --test-dir build_test --output-on-failure
```

RP2040 static lib:

```bash
./scripts/build_rp2040_lib.sh --clean
```

STM32G474 static lib:

```bash
./scripts/build_stm32_lib.sh --clean
```

RP2040 examples:

```bash
cmake -S examples -B build_examples_rp2040 -DJH_EXAMPLE_TARGET=rp2040
cmake --build build_examples_rp2040
```

STM32G474 examples:

```bash
cmake -S examples -B build_examples_stm32g474 \
  -DJH_EXAMPLE_TARGET=stm32g474 \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/stm32_lib/toolchain_stm32g474.cmake"
cmake --build build_examples_stm32g474
```

Pełny gate:

```bash
./runalltests.sh -j8
```

## Dobre pierwsze kroki przy nowym zadaniu

1. Sprawdź `git status --short`.
2. Przeczytaj publiczny nagłówek modułu, którego dotyczy zmiana.
3. Przeczytaj implementację mock i implementacje targetów.
4. Znajdź testy przez `rg "nazwa_modulu|hal_<module>" tests src examples`.
5. Sprawdź flagi w `hal_config.h` i dokumentację w `doc/api/`.
6. Zmień możliwie najmniejszy zakres.
7. Uruchom najbliższy sensowny test.
8. Przy zmianach szerszych uruchom build targetu lub `runalltests.sh`.

## Rzeczy, których nie zakładać

- Nie zakładaj, że build katalogi są aktualne.
- Nie zakładaj, że statusowy dokument portingu jest bardziej aktualny niż kod.
- Nie zakładaj, że RP2040 FreeRTOS działa bez Arduino-Pico `os=freertos`.
- Nie zakładaj, że STM32 ma WiFi/UDP/TCP/WireGuard gotowe tylko dlatego, że
  publiczny nagłówek istnieje.
- Nie zakładaj, że shared driver może użyć Arduino `Wire`/`SPI`; powinien iść
  przez HAL albo jawny compatibility layer.
- Nie zakładaj, że dodanie `loop1()` na RP2040 jest neutralne.
