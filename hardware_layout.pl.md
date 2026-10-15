# Połączenia i sterowanie

[English](hardware_layout.md) · [O projekcie](README.pl.md)

## Przyciski GPIO

Obsługa wejść w JaszczurHAL używa przycisków zwierających pin sygnałowy do GND.
Piny i polaryzacja pochodzą z węzłów przycisków w `device_tree.json` i są takie
same w obu montażach:

- tryb wejścia: `HAL_GPIO_INPUT_PULLUP` (`pull: up`),
- stan aktywny: niski (`activeLow`).

| Funkcja | GPIO | Klawisz Dooma | Węzeł |
| --- | ---: | --- | --- |
| Góra | 2 | `KEY_UPARROW` | `buttonUp` |
| Dół | 3 | `KEY_DOWNARROW` | `buttonDown` |
| Lewo | 4 | `KEY_LEFTARROW` | `buttonLeft` |
| Prawo | 5 | `KEY_RIGHTARROW` | `buttonRight` |
| Strzał | 7 | `KEY_RCTRL` | `buttonFire` |
| Użycie | 8 | Spacja | `buttonUse` |
| Menu | 9 | `KEY_ESCAPE` | `buttonMenu` |
| Zatwierdzenie | 10 | `KEY_ENTER` | `buttonAccept` |
| Powrót | 11 | `KEY_BACKSPACE` | `buttonBack` |

Implementacja: `src/jaszczurhal/doom_input_hal.c`.

## Gamepad Bluetooth

Montaż `pico2w` (RP2350 ARM) włącza obsługę gamepada Bluetooth Classic
w JaszczurHAL. Pierwszym obsługiwanym kontrolerem jest 8BitDo Zero 2 w trybie
Android D-input. Akcje GPIO i Bluetooth są łączone, więc przyciski fizyczne
pozostają dostępne przy podłączonym padzie.

Obecna konfiguracja bez przycisków ma `BT_AUTOMATIC_PAIRING=1`. Przy starcie
otwiera jedno okno parowania na 120 sekund, tylko gdy KV aplikacji nie zawiera
zaakceptowanego urządzenia. Uruchom Zero 2 przez `B+Start`, aby wybrać Android
D-input, a potem naciśnij `Select`, aby sparować. Zapisany pad łączy się ponownie
bez otwierania okna parowania. Przy fizycznych przyciskach można ustawić flagę
na `0` i otwierać to samo okno, trzymając GPIO `Menu+Back` przez trzy sekundy.

Na połączonym padzie przytrzymaj `L+R`, dodaj `Select` i trzymaj wszystkie trzy
przez pięć sekund, aby usunąć zapisaną tożsamość i klucz połączenia. Parowanie
pozostaje zamknięte do końca tego uruchomienia; wersja bez przycisków otwiera
nowe okno dla pustego zapisu po restarcie. Dziewięć przycisków GPIO nie ma
równoważnego skrótu kasowania; `Menu+Accept+Back` nie kasuje parowania
w obecnej implementacji wejść. Jeśli kasowanie pamięci się nie powiedzie,
automatyczne łączenie pozostaje zablokowane do ponowienia kasowania lub
jawnego rozpoczęcia parowania.

| Przycisk Zero 2 | Akcja w grze | Klawisz Dooma |
| --- | --- | --- |
| D-pad góra/dół | Ruch do przodu/do tyłu | Strzałka góra/dół |
| D-pad lewo/prawo | Obrót w lewo/w prawo | Strzałka lewo/prawo |
| A | Strzał | `KEY_RCTRL` |
| B | Użycie/otwarcie | Spacja |
| X | Zatwierdzenie/start gry | `KEY_ENTER` |
| Start | Menu/pauza przez menu | `KEY_ESCAPE` |
| Select | Powrót | `KEY_BACKSPACE` |
| Y + D-pad lewo/prawo | Ruch bokiem | `KEY_RALT` + strzałka lewo/prawo |
| L | Ruch bokiem w lewo | `,` (`key_strafeleft`) |
| R | Ruch bokiem w prawo | `.` (`key_straferight`) |

Dla każdego nowego połączenia adapter przyjmuje wejścia dopiero po otrzymaniu
neutralnego stanu przycisków i osi. Rozłączenie, przepełnienie kolejki oraz
błędy odpytywania zwalniają wszystkie akcje pochodzące z gamepada. Poprawny
zapis parowania pozwala połączyć się po restarcie watchdog, zimnym starcie
lub utracie zasilania bez ponownego otwierania parowania.

Implementacja: `src/jaszczurhal/doom_gamepad_input.c`.

## Wyświetlacz TFT

Oba montaże, Raspberry Pi Pico 2 W i Waveshare RP2040-Plus 4 MB, używają tych
samych połączeń 2,8-calowego TFT SPI z ILI9341. Wariant ST7796S na Pico 2 W
używa tych samych linii. Połączenia zapisano w `device_tree.json`.

| Sygnał wyświetlacza | GPIO | Uwagi |
| --- | ---: | --- |
| `SCK` / `CLK` | 18 | Zegar SPI0, domyślna magistrala HAL 0 |
| `MOSI` / `SDA` / `DIN` | 19 | SPI0 TX, dane z Pico do wyświetlacza |
| `MISO` / `SDO` | 16 | Opcjonalne przy samym wyświetlaniu na ILI9341 |
| `CS` | 17 | Węzeł `tft`, pin `cs` |
| `DC` / `D/C` / `RS` | 20 | Węzeł `tftDc` |
| `RST` / `RESET` | 21 | Węzeł `tftReset` |
| `VCC` | 3V3 | Logika 3,3 V |
| `GND` | GND | Wspólna masa |
| `LED` / `BL` | 3V3 | Podświetlenie stale włączone w obecnym firmware |

Firmware nie używa pinów karty SD ani kontrolera dotyku wyświetlacza.

## Dźwięk i dioda statusu

Dźwięk PWM wychodzi z GP6 (`audio` w `device_tree.json`) w obu montażach.
Podłącz go do odpowiedniego obwodu wyjścia audio; jest to sygnał PWM o poziomach
logicznych. Rozmiar bufora, rozdzielczość PWM i liczbę kanałów miksera określa
`doom_main_config.h`.

Gra używa diody płytki do sygnalizacji startu i otrzymanych obrażeń. Wariant
`BOOT_PROBE` przełącza ją co sekundę i wyświetla licznik działania na TFT.

## Dane WHD/WHX we flash

Dane gry są surowym obrazem flash czytanym przez XIP i wgrywanym osobno
od firmware'u.

| Obszar | Adres | Uwagi |
| --- | ---: | --- |
| Baza XIP firmware'u | `0x10000000` | `DOOM_FLASH_XIP_BASE` |
| Dane WHD/WHX | `0x10200000` | `DOOM_WHD_FLASH_ADDR` / `TINY_WAD_ADDR` |
| Koniec obszaru danych aplikacji | `0x103fe000` | Pierwszy bajt rezerwacji KV |
| Trwały zapis KV | `0x103fe000`–`0x10400000` | Dwa banki po 4 KiB; koniec wyłączny |
| Koniec fizycznej flash | `0x10400000` | `DOOM_FLASH_SIZE_BYTES=0x400000` |

Układ wymaga co najmniej 4 MiB fizycznej flash. Dołączony `doom1.whx` zajmuje
około 1,72 MiB, więc z obecnym firmware'em nie mieści się w 2 MiB zwykłego Pico.

Montaż `rp2040-plus` wybiera profil `rp2040-plus-4mb`. Profil wskazuje definicję
płytki w Pico SDK i sprawdza deklarację 4 MiB flash w konfiguracji kompilacji.
Dane WHD/WHX mogą zajmować obszar od `0x10200000` do początku rezerwacji
pamięci aplikacji, bez tego ostatniego bajtu. Skrypt sprawdza tę granicę
przed dostępem do urządzenia.

Przebieg pierwszego uruchomienia:

1. Przełącz płytkę w BOOTSEL.
2. Jeśli potrzebujesz logów startowych, uruchom trwały monitor szeregowy
   (`Ctrl+Shift+3`). Firmware otwiera USB CDC z ustawieniem `115200`, wyświetla
   diagnostykę przez 4 sekundy (`DOOM_BOOT_DIAG_HOLD_MS`) i wypisuje komunikaty
   `[boot]` oraz `[video]` przed wejściem do gry.
3. Uruchom `Project: Upload (UF2 / BOOTSEL)`, aby zbudować i wgrać UF2 montażu,
   np. `.build/rp2350-arm/pico2w/firmware.uf2`.
4. Ponownie przełącz płytkę w BOOTSEL i uruchom `Project: Upload WHX Payload`.
   Zadanie wgrywa `doom1.whx` pod `0x10200000`, weryfikuje dane i restartuje płytkę.

Kompilacja definiuje `WHD_SUPER_TINY=1`, więc oczekiwana sygnatura danych pod
`0x10200000` to `IWHX`. Polecenia konsolowe i kontrolę granic flash opisuje
[instrukcja kompilacji i wgrywania](docs/BUILDING.pl.md).

Gdy danych brakuje lub mają złą sygnaturę, ekran startowy pokazuje `Missing WHX`,
zamiast zatrzymywać się bez komunikatu na breakpoint Dooma.
