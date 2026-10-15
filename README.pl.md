# DoomConsole

[English](README.md)

DoomConsole uruchamia Dooma na płytkach RP2040 i RP2350 z wyświetlaczem TFT,
dźwiękiem PWM i przyciskami GPIO. Wersja dla Pico 2 W obsługuje też gamepad
Bluetooth Classic HID i zapisuje parowanie. Firmware korzysta z
[JaszczurHAL](https://github.com/jaszczurtd/JaszczurHAL) oraz oficjalnego Pico SDK.

| Montaż | Płytka | Wyświetlacz |
| --- | --- | --- |
| `pico2w` (domyślny) | Raspberry Pi Pico 2 W | ILI9341, 320×240; wariant ST7796S, 480×320 |
| `rp2040-plus` | Waveshare RP2040-Plus, 4 MiB flash | ILI9341, 320×240 |

Ustawienie `DOOM_HIGHRES_SCENE=0` przywraca klasyczną scenę 320×200.
Dane gry są osobnym obrazem WHX we flash; samo wgranie firmware'u ich nie
instaluje. Obecny układ pamięci wymaga 4 MiB flash. Renderer jest nadal
rozwijany, a projekt PCB nie jest gotowy.

## Pierwsze uruchomienie

Zacznij od [kompilacji i wgrywania](docs/BUILDING.pl.md), a sprzęt podłącz
według [opisu połączeń i sterowania](hardware_layout.pl.md).

- [Wydajność, diagnostyka i dalsze prace](port_issues.pl.md)
- [C8: testy gamepada na sprzęcie](tests/C8_HARDWARE_GATE.pl.md)
- [C9: testy zapisu parowania na sprzęcie](tests/C9_HARDWARE_GATE.pl.md)

Przebieg kompilacji opisuje [CMakeLists.txt](CMakeLists.txt). Połączenia są w
[device_tree.json](device_tree.json), opcje kompilacji w
[hal_project_config.h](hal_project_config.h), a ustawienia portu w
[doom_main_config.h](doom_main_config.h). Aktywny kod platformy znajduje się w
`src/jaszczurhal/`, kod gry w `src/doom/`, a konwerter danych w `src/whd_gen/`.
Katalog `_unused/` zawiera kod i dokumenty poza aktywnym portem.

## Nagrania

Nagrania pokazują wcześniejsze wersje; ich rozdzielczość może różnić się od domyślnej.

- [RP2040, ILI9341, 320×200](https://www.youtube.com/watch?v=PHE-ADc1qWk)
- [RP2350, ILI9341, 320×200](https://www.youtube.com/watch?v=I1niNYLehJY)
- [RP2350, ST7796S, 480×320](https://www.youtube.com/watch?v=YNoUZEpIq5A)

## Pochodzenie i licencje

Port bazuje na projekcie Grahama Sandersona
[kilograham/rp2040-doom](https://github.com/kilograham/rp2040-doom), wywodzącym się z
[Chocolate Doom](https://github.com/chocolate-doom/chocolate-doom). Zachowuje
format skompresowanych danych gry i rozwiązania renderera, a obsługę platformy
zapewnia JaszczurHAL.

Zasady opisują [COPYING.md](COPYING.md) i informacje w poszczególnych plikach,
w tym [licencja ADPCM](src/adpcm-xq/license.txt). Przy rozpowszechnianiu
zmienionego kodu zachowaj informacje o autorach pierwotnych projektów.
Rozpowszechnianie danych gry podlega osobnym warunkom licencyjnym.
