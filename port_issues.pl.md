# Wydajność i diagnostyka

[English](port_issues.md) · [O projekcie](README.pl.md)

Porównuj zmiany renderera na tym samym sprzęcie i w tej samej scenie.
Poniższe obserwacje historyczne nie są pomiarami bieżącej wersji.

## Obecne ścieżki renderowania

Aktywny port korzysta z oficjalnego Pico SDK przez JaszczurHAL. Kod platformy
jest w `src/jaszczurhal/`; dawny backend `src/pico` pozostaje poza aktywnym drzewem.

`hal_project_config.h` wybiera `DOOM_DUAL_CORE_COLUMNS=0`. Opcjonalna ścieżka
pakietowa po BSP rysuje lewą stronę na core0, a pracę dla prawej przekazuje
core1 po zakończeniu BSP. Przesyłanie kolumn w trakcie BSP pozostaje
eksperymentem wymagającym odizolowania danych.

Core1 obsługuje asynchroniczny przesył do TFT. RP2350 z ILI9341 używa dwóch
buforów obrazu indeksowanego; konfiguracje z jednym buforem czekają przed jego
ponownym użyciem. Na RP2350 działa też `DOOM_RENDER_ASYNC_HUD`, z barierą przed
następnym krokiem gry. `DOOM_RENDER_ASYNC_PLANES=0` pozostawia domyślną ścieżkę
płaszczyzn. `DOOM_VIDEO_SYNC_FLUSH=1` pomaga odróżnić problemy przesyłu od błędów
renderowania.

## Odczytywanie liczników

| Pole | Znaczenie |
| --- | --- |
| `fps` | Oszacowanie liczby klatek na sekundę |
| `tus=bsp/planes/masked/pspr/hud` | Czas etapów klatki w mikrosekundach |
| `cols`, `planes`, `masked` | Obciążenie rysowaniem ścian, płaszczyzn i elementów maskowanych |
| `pdrop`, `texfail`, `flatfail` | Odrzucenia z kolejki i błędy tekstur/płaszczyzn |
| `pcache=hits/misses/tall_uncached/tall_hits` | Skuteczność cache kolumn |
| `tall`, `tallh` | Szczegóły wysokich kolumn, jeśli występują w logu |
| `black=total/top/mid/bot` | Piksele o wartości znacznika niewypełnionego obszaru |
| `flush=done/waits/wait_ms` | Zadania TFT i oczekiwanie |
| `casync=queued/done/wait_ms` | Zadania pomocniczego renderowania kolumn i oczekiwanie |
| `ccol=queued/right/inline/core1` | Rozdział pracy nad kolumnami |
| `free_heap` | Pozostała pamięć sterty |

`DOOM_RENDER_SENTINEL_CLEAR` i `DOOM_RENDER_BLACK_DIAG` domyślnie mają wartość
`0` podczas gry w wysokiej rozdzielczości. Zerowe `black` przy wyłączonym
skanowaniu nie potwierdza braku artefaktów. Nawet włączony licznik wykrywa
piksele o kolorze znacznika, a nie wszystkie możliwe błędy renderowania.

## Cache kolumn i pamięć

Cache zdekodowanych krótkich kolumn używa łańcuchów haszujących do wyszukiwania
pełnego klucza i listy LRU do wyboru zastępowanego wpisu. Kolizje nie usuwają
kluczy, trafienia aktualizują LRU, a wpis trafia do cache po udanym dekodowaniu.
Każdy rdzeń ma własny indeks i miejsca na piksele, także w trybie kolejkowanym.
Wysokie kolumny mają osobny cache tymczasowy. Indeks usuwa pełne skanowanie
cache bez zmniejszania jego pojemności.

Testy hostowe porównują trafienia, chybienia i wybór miejsc z poprzednią
implementacją LRU, również przy kolizjach i niezależnych właścicielach:

```bash
python3 -m unittest discover -s tests -p 'test_*.py'
```

Syntetyczny pomiar wyszukiwania i zastępowania wpisów:

```bash
mkdir -p .build
cc -std=c17 -O2 -Isrc tests/doom_column_cache_test.c -o .build/column-cache-test
.build/column-cache-test --benchmark
```

Pomiar pomija dekodowanie, rysowanie i przesył do TFT. Wpływ na grę oceniaj przez
FPS i `tus=bsp` na urządzeniu. Powiększenie cache zużywa SRAM potrzebny także
obszarowi Doom zone i buforom HAL. Obserwuj `free_heap` i błędy alokacji przy
starcie; większy cache sam w sobie nie oznacza szybszej ani działającej wersji.

## Obserwacje historyczne

Pomiary pochodzą sprzed obecnego indeksu cache. Przy porównaniu zachowaj tę samą
płytkę, rozdzielczość, zegary, pozycję na mapie oraz konfigurację wejść i dźwięku.

- Po zmianach zegarów SPI i procesora obserwowano około 10–13 FPS. Dominowała
  praca nad ścianami/BSP; przesył TFT przestał być głównym ograniczeniem.
- Proste przeniesienie płaszczyzn na core1 dawało niewiele, bo core0 nadal
  czekał przed `R_DrawMasked()`. Pakietowe kolumny po BSP także dawały mały
  zysk z powodu oczekiwania i części pracy wykonywanej bezpośrednio.
- Przesyłanie prawej strony podczas BSP obniżyło obserwowane `tus=bsp` z około
  43 000 do 30 000 µs i dawało około 13 FPS w lekkich scenach, ale powodowało
  czarne artefakty i asercje `R_GetColumn: fd->real_id >= 0`.
- Podejrzewano współbieżny dostęp do stanu renderera, framedrawables, cache
  WAD/WHD/zone lub tekstur; przyczyna nie została potwierdzona. Osobne dekodery
  i cache dla rdzeni nie wystarczyły do stabilnego przesyłania kolumn.

## Kolejne eksperymenty

1. Zbierz punkt odniesienia dla domyślnej pracy na jednym rdzeniu, w lekkich
   i ciężkich widokach mapy. Osobno porównaj pakiety po BSP, zachowując zegary,
   rozdzielczość i flagi renderera. Żądany zegar TFT pochodzi z `frequencyHz`
   magistrali `spi` w `device_tree.json`, przez `JH_ILI9341_SPI_DEFAULT_HZ`
   i `JH_ST77XX_SPI_DEFAULT_HZ`; rzeczywista częstotliwość zależy od dzielników.
2. Zmierz dekodowanie tekstur przed powiększaniem cache. Śledź `tus=bsp`, `cols`,
   `pcache` i `free_heap`. Sprawdź, czy podział cache obniża trafienia i czy
   `tall_uncached` uzasadnia zmianę pojemności cache wysokich kolumn.
3. Odizoluj dane core1 przed przywróceniem przesyłania kolumn. Zacznij od małego
   zbioru zdekodowanych tekstur; zapisuj ID tekstury, `real_id` lub lump,
   wysokość, offset i x. Sprawdź leniwe ładowanie, alokacje zone i tworzenie
   tekstur złożonych. Preferuj gotowe wskaźniki lub dane przygotowane przez
   core0, osobne bufory robocze/cache i jawne bariery.
4. Ogranicz eksperyment do `x >= DOOM_COL_SPLIT_X` (połowa `SCREENWIDTH`,
   niezależnie od panelu). Ustal zachowanie przy pełnym FIFO przed dodaniem
   wykonania bezpośredniego; zachowaj kolejność i unikaj podwójnego rysowania.
   Odrzucenia diagnostyczne lub oczekiwanie muszą być widoczne.
5. Po ustabilizowaniu przesyłania porównaj pojemność cache, haszowanie, reguły
   dla powtarzanych tekstur i limity wysokich kolumn. Wróć do płaszczyzn oraz
   sprite'ów, gdy uzasadnią to czasy. Przy `texfail=0` i `pdrop=0` sprawdzaj
   kolejność płaszczyzn, clipping i synchronizację, zamiast zakładać błąd przesyłu.

Przyjmuj zmianę wydajności dopiero po dłuższych próbach na urządzeniu bez
fatalnych asercji i nowych artefaktów, z `texfail=0`, `flatfail=0` i `pdrop=0`,
niższym porównywalnym `tus=bsp` oraz trwałą poprawą FPS w różnych scenach.
Przebuduj firmware i odśwież IntelliSense według [instrukcji](docs/BUILDING.pl.md).
Zachowuj logi i konfigurację każdego porównania; benchmark hostowy nie zastępuje
pomiarów na urządzeniu.
