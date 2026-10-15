# Kompilacja i wgrywanie DoomConsole

[English](BUILDING.md) · [O projekcie](../README.pl.md)

Polecenia uruchamiaj z katalogu DoomConsole. JaszczurHAL powinien znajdować się
w `../libraries/JaszczurHAL`. Przygotuj jego narzędzia przez `runmefirst.sh`
na Linuksie lub `runmefirst.ps1` na Windows. Instalują one Pico SDK,
kompilatory, CMake, Ninja i picotool używane przez projekt.

## Wybór montażu i kompilacja

[device_tree.json](../device_tree.json) opisuje `pico2w` (`rp2350-arm`) i
`rp2040-plus` (`rp2040`). Manifest projektu domyślnie wybiera `pico2w`.
Zadanie `Project: Select board` zapisuje lokalny wybór w ignorowanym przez Git
pliku `.vscode/jaszczurhal.local.json`. Argument `--assembly` wybiera montaż
dla danego polecenia, niezależnie od lokalnego ustawienia.

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode config-dump --project . --assembly pico2w
../libraries/JaszczurHAL/vscode/entry/jh-vscode build --project . --assembly pico2w
../libraries/JaszczurHAL/vscode/entry/jh-vscode refresh-intellisense --project . --assembly pico2w
```

Na Windows użyj osobnego launchera:

```powershell
& ..\libraries\JaszczurHAL\vscode\entry\jh-vscode.cmd build --project . --assembly pico2w
```

Dla drugiego montażu zastąp `pico2w` przez `rp2040-plus`. Pliki wynikowe to
`.build/<target>/<assembly>/firmware.elf`, `firmware.bin` i `firmware.uf2`.
Mapa linkera pozostaje w katalogu kompilacji CMake; `config-dump` pokazuje
rozwiązaną konfigurację kompilacji.

## Wyświetlacz i opcje kompilacji

[hal_project_config.h](../hal_project_config.h) deklaruje warianty:

| Wariant | Montaże | Wynik |
| --- | --- | --- |
| Domyślny, bez `--variant` | Oba | Gra na ILI9341 |
| `ST7796S` | Tylko `pico2w` | Gra na ST7796S |
| `BOOT_PROBE` | Oba | Minimalny test uruchomienia aplikacji |

```bash
../libraries/JaszczurHAL/vscode/entry/jh-vscode build --project . --assembly pico2w --variant ST7796S
```

Pliki wariantu trafiają do podkatalogu `variants/<id>/` w katalogu montażu.
`BOOT_PROBE` sprawdza start aplikacji i diodę statusu; nie uruchamia gry.

`DOOM_HIGHRES_SCENE=1` daje scenę 320×240 na ILI9341 lub 480×320 na ST7796S;
`0` wybiera 320×200. `DOOM_DUAL_CORE_COLUMNS` i `DOOM_RENDER_ASYNC_PLANES`
domyślnie mają wartość `0`. `DOOM_VIDEO_SYNC_FLUSH=0` pozostawia asynchroniczny
przesył obrazu. Przed włączeniem eksperymentów renderera przeczytaj
[opis wydajności](../port_issues.pl.md).

Piny i żądaną częstotliwość SPI zmieniaj w `device_tree.json`. Domyślne żądanie
to 50 MHz; `JH_ILI9341_SPI_DEFAULT_HZ` i `JH_ST77XX_SPI_DEFAULT_HZ`
przekazują je sterownikom. [doom_main_config.h](../doom_main_config.h) określa
obrót, kolejność kolorów, bufory dźwięku, budżety pamięci i diagnostykę.
Domyślnie żąda podkręcenia do 300 MHz na RP2350 i 250 MHz na RP2040 przy
1,30 V (`DOOM_SYS_OVERCLOCK`, `DOOM_SYS_CLOCK_KHZ`, `DOOM_SYS_VREG_VOLTAGE`).
Rzeczywista częstotliwość SPI zależy od dzielników zegara. Po zmianie zegarów
sprawdź stabilność podłączonej płytki i wyświetlacza.

## Wgrywanie firmware'u

Użyj `Project: Upload` dla wybranego montażu albo
`Project: Upload (UF2 / BOOTSEL)`, aby skopiować jego UF2 na dysk BOOTSEL.
Domyślną metodą wgrywania na RP jest UF2; lokalne zmiany sprawdzisz przez
`config-dump`. Obraz pochodzi z katalogu montażu, a po wyborze wariantu —
z jego podkatalogu.

Dane gry wgrywa się osobno, według instrukcji poniżej. Raspberry Pi Pico
z 2 MiB flash nie mieści obecnego układu danych. Inna płytka wymaga montażu
z odpowiednimi połączeniami i wystarczającą pamięcią flash.

## Wgrywanie danych gry

Firmware czyta skompresowane dane WHD/WHX z flash; nie używa bezpośrednio
zwykłego WAD. Repozytorium zawiera `doom1.whx`, a źródła konwertera są w
`src/whd_gen/`. Obecny format `WHD_SUPER_TINY=1` używa sygnatury `IWHX`.

Przełącz docelową płytkę z 4 MiB flash w BOOTSEL, a następnie uruchom:

```bash
python3 scripts/upload_whx.py
```

```powershell
py -3 scripts/upload_whx.py
```

Skrypt wgrywa dane pod `0x10200000`, sprawdza zapisane bajty i restartuje
płytkę dopiero po powodzeniu obu operacji. Ostatnie 8192 bajty flash pozostawia
dla KV: `[0x103fe000, 0x10400000)`. Są to dwa niezależnie kasowane banki po
4 KiB, określone przez `HAL_RP_FLASH_EEPROM_SIZE`. Dane gry muszą kończyć się
przed tym obszarem.

Jeśli kilka płytek RP działa w BOOTSEL, użyj `--serial <flash-id>`. Skrypt
przekazuje wybór do wgrywania, weryfikacji i restartu. Kontrola `--flash-size`
korzysta z zadeklarowanej pojemności; nie odczytuje fizycznej pojemności płytki.

```bash
python3 scripts/upload_whx.py --dry-run
python3 scripts/upload_whx.py --no-reboot
```

`--dry-run` sprawdza plik, ścieżkę narzędzia i granice flash bez dostępu do USB.
`--no-reboot` pozostawia płytkę w BOOTSEL. Inny obraz podaj jako argument
pozycyjny. Po zmianie `DOOM_WHD_FLASH_ADDR` w firmware przekaż ten sam adres
przez `--address` albo zmienną środowiskową `DOOM_WHD_FLASH_ADDR` skryptu.
Skrypt nie analizuje `doom_main_config.h`.

Rezerwację pamięci odczytuje liczbowo z `hal_project_config.h`. Większą wartość
nadpisaną przy kompilacji przekaż także przez `--reserved-tail-size`;
wartości mniejsze od rezerwacji w nagłówku są odrzucane. Jeśli wyszukiwanie
narzędzi wymaga wskazania ścieżki, użyj `--picotool` lub `--jaszczurhal-root`.

## Edytor i testy hostowe

Dla wybranego montażu używaj `Project: Build`, `Project: Build (Debug)`,
`Project: Serial Monitor`, `Project: Debug Probe Monitor` oraz
`Project: Refresh IntelliSense`. `Project: Upload WHX Payload` uruchamia
opisany wyżej skrypt. Widok Run and Debug zawiera profile uruchomienia dla
obsługiwanych montaży i wariantów z sondą CMSIS-DAP. Instalator HAL na Windows
konfiguruje OpenOCD i narzędzia GNU Arm.

Manifest, zadania, ustawienia i profile debuggera są w repozytorium. Port
szeregowy i lokalny wybór montażu przechowuj w `.vscode/jaszczurhal.local.json`.
IntelliSense korzysta z bazy kompilacji tworzonej przez `jh-vscode`.
Po zmianie rejestru zadań HAL odtwórz pliki edytora i sprawdź je:

```bash
python3 scripts/sync_vscode_project.py
python3 scripts/sync_vscode_project.py --check
python3 -m unittest discover -s tests -p 'test_*.py'
```

Na Windows zastąp `python3` przez `py -3`. Testy hostowe obejmują konfigurację,
cache, obsługę wejść i menu, polecenia wgrywania oraz pliki edytora. Wymagają
kompilatora C dla hosta, CMake i sąsiedniego repozytorium HAL.
[Workflow Windows](../.github/workflows/windows-firmware.yml) dodatkowo buduje
firmware Pico 2 W i odświeża IntelliSense.

Działanie wejść, opóźnienia, ponowne łączenie i zapis parowania wymagają
osobnych testów [C8](../tests/C8_HARDWARE_GATE.pl.md) i
[C9](../tests/C9_HARDWARE_GATE.pl.md). Testy hostowe nie potwierdzają tych wyników.
