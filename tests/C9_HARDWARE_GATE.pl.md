# C9: test trwałego zapisu parowania na sprzęcie

[English](C9_HARDWARE_GATE.md) · [O projekcie](../README.pl.md)

Uruchom test na `rp2350-arm:pico2w` z 8BitDo Zero 2 w trybie Android D-input.
Obecna konsola nie ma fizycznych przycisków, więc użyj konfiguracji
`BT_AUTOMATIC_PAIRING=1` i zapisuj pełny log szeregowy. Nie publikuj pełnego
adresu Bluetooth ani klucza połączenia.

## Przygotowanie

1. Zbuduj i wgraj domyślny firmware, potem dane WHX. Potwierdź, że kompilacja
   i skrypt wgrywający rezerwują ostatnie 8192 bajty flash dla KV. Gdy inna
   płytka RP jest w BOOTSEL, wybierz Pico 2 W opcją `--serial` skryptu.
2. Zacznij bez zapisanego parowania, z wyłączonym Zero 2. Przy powtarzaniu
   testu najpierw wykonaj opisany poniżej krok kasowania parowania.
3. Zrestartuj konsolę i potwierdź dokładnie jedno ograniczone czasowo okno
   parowania z powodu pustego zapisu. Jeszcze nie włączaj parowania pada.
4. Zapisz rewizję firmware'u, płytkę, tryb kontrolera i czasy. Potwierdź
   działanie diody płytki przy starcie i podczas gry. Kontroler oznaczaj
   wyłącznie krótką własną etykietą.

## Wymagane próby

1. W otwartym oknie 120 sekund sparuj Zero 2 uruchomiony przez `B+Start`
   i `Select`.
2. Po otrzymaniu neutralnego stanu wejść przejdź przez menu i wykonaj akcję
   w grze. Potwierdź zapis zaakceptowanego urządzenia dopiero po nawiązaniu
   użytecznego połączenia HID.
3. Wyłącz i włącz kontroler. Potwierdź ponowne połączenie bez nowego okna
   parowania i bez zablokowanych wejść.
4. Wybierz **Quit Game**. Wywołuje to `I_Quit()` i restart przez watchdog.
   Potwierdź automatyczne połączenie po starcie bez parowania.
5. Odłącz zasilanie Pico 2 W na co najmniej pięć sekund. Podłącz je ponownie
   i potwierdź to samo zachowanie po zimnym starcie.
6. Przy zamkniętym parowaniu udostępnij XY-BT i Zero 2 w innym trybie.
   Żadne z nich nie może zmienić zapisanej tożsamości ani wywołać pętli
   łączenia i rozłączania. Wróć do Android D-input i potwierdź ponowne połączenie.
7. Na połączonym padzie przytrzymaj `L+R`, dodaj `Select` i trzymaj wszystkie
   trzy przez pięć sekund. Skrót pomija `Start`, którego długie przytrzymanie
   wyłącza Zero 2. Potwierdź ogólny komunikat `persisted bond erased`,
   rozłączenie, brak ponownego łączenia i brak okna parowania w tym uruchomieniu.
   Wyłącz pad i zrestartuj konsolę: ma otworzyć jedno okno dla pustego zapisu.
   Włącz stary pad zwyczajnie, bez `Select`; usunięty zapis nie może pozwolić
   mu na ponowne połączenie.
8. Włącz parowanie Android D-input przez `Select`. Potwierdź sterowanie
   oraz kolejne ponowne połączenie.

## Materiały i warunki zaliczenia

- Zachowaj pełny log, ale przed udostępnieniem usuń pełne adresy urządzeń,
  jeśli wypisał je któryś komponent zewnętrzny.
- Odrzuć wynik, jeśli udostępniany log zawiera sześciobajtowy adres Bluetooth,
  bajty klucza połączenia, zapis parowania lub inne sekrety.
- Zapisz wyniki ponownych połączeń, restartu watchdog, zimnego startu,
  odrzuconego trybu kontrolera i kasowania parowania. Potwierdź, że dane WHX
  pozostały czytelne po wszystkich zapisach KV.
- Osobno uruchom test utraty zasilania RP KV oraz test OTA+KV+LittleFS.
  Te testy niszczą własne obszary pamięci aplikacji; nie uruchamiaj ich
  na pamięci przechowującej zachowane parowanie Dooma.

C9 nie można zaliczyć na podstawie samych testów hostowych lub kompilacji.
Wymaga powodzenia wszystkich kroków na sprzęcie oraz materiałów bez danych
identyfikujących Bluetooth i bez kluczy.
