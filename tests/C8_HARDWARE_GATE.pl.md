# C8: test gamepada Bluetooth na sprzęcie

[English](C8_HARDWARE_GATE.md) · [O projekcie](../README.pl.md)

Uruchom test na `rp2350-arm:pico2w` z 8BitDo Zero 2 w trybie Android D-input.
Testy hostowe i kompilacja firmware'u nie zastępują tej próby sprzętowej.

## Przygotowanie

1. Zbuduj i wgraj domyślny firmware projektu oraz dane WHX.
2. Podłącz dziewięć przycisków GPIO aktywnych stanem niskim według
   [opisu połączeń i sterowania](../hardware_layout.pl.md).
3. Uruchom trwały monitor szeregowy i zapisuj cały log.
4. Przy `BT_AUTOMATIC_PAIRING=1` zacznij bez zapisanego parowania i potwierdź
   komunikat o oknie 120 sekund po starcie. Uruchom Zero 2 przez `B+Start`
   i naciśnij `Select`, aby sparować. Przed zaliczeniem C8 powtórz próbę
   z flagą `0`, trzymając fizyczne `Menu+Back` przez trzy sekundy.
5. Przed każdym ponownym połączeniem trzymaj jeden przycisk pada aż do
   nawiązania połączenia. Doom ma pozostać bezczynny do puszczenia przycisku
   i otrzymania neutralnego raportu.

## Wymagane próby

- Przejdź przez menu i rozpocznij grę padem.
- Sprawdź ruch przód/tył, obrót lewo/prawo, strzał, użycie, menu/pauzę,
  zatwierdzenie, powrót, oba kierunki ruchu bokiem przez `Y` + D-pad i przez
  `L`/`R`, według [opisu sterowania](../hardware_layout.pl.md).
- Powtórz tę samą sekwencję tylko przyciskami GPIO, z połączonym padem
  i po jego rozłączeniu.
- Graj bez przerwy przez 30 minut. Potwierdź sygnalizację obrażeń diodą płytki.
- W jednym uruchomieniu firmware'u wykonaj 20 cykli wyłączenia i włączenia
  kontrolera z ponownym połączeniem.
- Rozłącz pad podczas trzymania każdego przypisanego przycisku, także obu
  skrótów ruchu bokiem i obu przycisków barkowych. Każda próba musi zwolnić
  wejście bez zablokowanego ruchu, obrotu, strzału, użycia lub klawisza menu.
- Zmierz czas od raportu do zdarzenia dla co najmniej 200 zmian. Wymagane:
  p95 nie większe niż 29 ms, maksimum nie większe niż 58 ms.
- Porównaj ruch bokiem przez `Y` + D-pad oraz `L`/`R` i zapisz ostateczny
  wybór pod kątem wygody.

C8 wymaga logu szeregowego, danych opóźnienia, dokładnego commita firmware'u,
identyfikacji płytki i kontrolera, czasu gry, liczby ponownych połączeń,
wyników rozłączania dla każdego przycisku i końcowego przypisania sterowania.
