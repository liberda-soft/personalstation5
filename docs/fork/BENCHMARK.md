# Benchmark na PC testowym (Windows, RTX 3080 + Ryzen 5 5600)

Każdą zmianę wydajności sprawdzamy tak samo:

1. Ta sama gra i ta sama scena.
2. Dwie konfiguracje (A i B).
3. Po dwa przebiegi na konfigurację.
4. Porównanie skryptem.

## 1. Przygotowanie

1. Pobierz build Windows z GitHub Actions: zakładka *Actions*, ostatni udany przebieg, artefakt `personalstation5-Windows-x64`.
2. Rozpakuj go, np. do `C:\emu\build-AAAA-MM-DD`.
3. Zaktualizuj sterownik NVIDIA.
4. Zamknij przeglądarkę, Discorda z nakładką i inne programy obciążające GPU.
5. Ustaw tryb zasilania Windows na „Najwyższa wydajność”.

## 2. Scena testowa

- Wczytaj zapis w miejscu, które da się powtórzyć: stoisz w miejscu z nieruchomą kamerą albo przechodzisz krótką, zawsze tę samą trasę.
- Przebieg trwa 60–90 sekund od momentu, gdy jesteś w scenie.
- Zanotuj, po ilu sekundach od startu emulatora zaczynasz pomiar (np. 45 s). Ta liczba to `--skip` w kroku 5.

## 3. Uruchomienie

Najpierw zrób **jeden przebieg rozgrzewkowy**, bez zapisywania wyników. Wtedy shadery trafiają do `_PipelineCache` i nie zaburzają pomiaru.

Potem uruchamiaj przebiegi na zmianę A, B, A, B, np. w PowerShellu:

```powershell
cd C:\emu\build-AAAA-MM-DD
.\personalstation5_emulator.exe --game "D:\Gry\NazwaGry" --vram-gc Legacy --bench-log "D:\bench\gra-legacy-1" *> "D:\bench\gra-legacy-1.log"
.\personalstation5_emulator.exe --game "D:\Gry\NazwaGry" --vram-gc Proportional --bench-log "D:\bench\gra-prop-1" *> "D:\bench\gra-prop-1.log"
.\personalstation5_emulator.exe --game "D:\Gry\NazwaGry" --vram-gc Legacy --bench-log "D:\bench\gra-legacy-2" *> "D:\bench\gra-legacy-2.log"
.\personalstation5_emulator.exe --game "D:\Gry\NazwaGry" --vram-gc Proportional --bench-log "D:\bench\gra-prop-2" *> "D:\bench\gra-prop-2.log"
```

Po każdym przebiegu zamknij okno emulatora. Pliki CSV są zapisywane co sekundę, więc nawet crash zostawia wyniki.

Każdy folder `--bench-log` zawiera dwa pliki:

- `frametimes.csv`: czas każdej klatki (flip gry);
- `counters.csv`: co sekundę FPS, VRAM, CPU, najbardziej obciążone wątki i liczniki emulatora (kompilacje shaderów, oczekiwania CPU na GPU, faulty śledzenia zapisów, usunięte tekstury, transfery).

## 4. Wysłanie wyników

Spakuj folder `D:\bench` razem z plikami `.log`, bo log zawiera m.in. linię `TextureCache: VRAM budget …`.

Dopisz:

- nazwę gry i opis sceny;
- od której sekundy liczyć (`--skip`);
- rozdzielczość okna;
- czy gra wyglądała poprawnie w obu konfiguracjach.

## 5. Porównanie

Na dowolnym komputerze z Pythonem 3:

```bash
python3 tools/bench/compare.py D:/bench/gra-legacy-1 D:/bench/gra-prop-1 D:/bench/gra-legacy-2 D:/bench/gra-prop-2 --skip 45 --duration 60
```

Pierwszy folder to punkt odniesienia. W pozostałych kolumnach widać zmianę procentową:

- avg FPS, 1% i 0.1% low;
- klatki dłuższe niż 50 i 100 ms;
- kompilacje shaderów;
- czekanie CPU na GPU;
- usunięte tekstury;
- szczyt VRAM i obciążenie CPU.

Różnice poniżej ~2–3% traktujemy jako szum.

## Uwagi

- Domyślnie gra jest ograniczona do 60 FPS przez emulowany vblank (`--vblank-frequency 60`). Jeśli obie konfiguracje mają 60 FPS, różnicę widać w 1% low, w obciążeniu CPU i w licznikach.
- Oficjalny build KytyPS5 nie ma `--bench-log`. A/B robimy więc w naszym buildzie, przełącznikami zmian (np. `--vram-gc Legacy` zachowuje się jak upstream). Zmiany bez przełącznika, np. flagi kompilatora, porównujemy dwoma buildami.
- Nakładka `--perf-overlay true` (F2, Shift+F2 dla szczegółów) pokazuje te same liczniki na żywo, ale sama trochę kosztuje. Do pomiarów jej nie włączamy.
