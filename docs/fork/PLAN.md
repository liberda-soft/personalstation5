# Plan forka wydajnościowego

*Wersja robocza, 19 października 2026. Zastępuje plan „od zera” z `plan-emulatora-ps5.md`.*

## Skąd startujemy

Bazą jest [KytyPS5](https://github.com/KytyPS5/KytyPS5) (GPL-2.0), a nie stary [Kyty](https://github.com/InoriRus/Kyty) z 2022 roku. KytyPS5 ma już prawie wszystko z faz 0–4 starego planu:

- loader ELF z linkerem po NID;
- jądro HLE: wątki, pamięć, pliki;
- parser AGC/PM4;
- rekompilator RDNA2 → SPIR-V;
- renderer Vulkan;
- dźwięk, pad i zapisy.

Pisanie tego od nowa nie ma sensu. Dlatego **fork KytyPS5 nastawiony na wydajność** na naszym sprzęcie.

Stan upstreamu (19.10.2026):

- **Kompatybilność:** 372 zgłoszenia na liście kompatybilności, z czego 197 to „in-game”.
- **Wydajność:** 60 FPS osiągają głównie gry 2D i indie. Gry 3D na Unreal Engine często chodzą w 1–10 FPS.
  - Demon's Souls: około 30 FPS na RTX 4080.
  - Astro's Playroom: 5–8 FPS na Ryzenie 5 5600 ([#1113](https://github.com/KytyPS5/KytyPS5/issues/1113)).
- **Tempo prac:** około 12 commitów dziennie, prawie wszystkie od jednego maintainera. W ostatnich 30 dniach rekompilator shaderów był zmieniany 1161 razy.
- **Polityka PR-ów:** upstream automatycznie zamyka PR-y ogólne lub architektoniczne, które nie naprawiają konkretnej gry. Odrzucił też asynchroniczną kompilację pipeline'ów oraz DLSS/FSR. Prace wydajnościowe muszą więc żyć w naszym forku.

## Cel

Grywalne gry 3D na **RTX 3080 (10 GB) + Ryzen 5 5600**:

- najpierw stabilne 30 FPS, potem 60 FPS w wybranych tytułach;
- bez przycięć przy kompilacji shaderów;
- zawsze mierzalnie lepiej niż oficjalny build w tej samej scenie.

## Zasady

1. **Najpierw pomiar.** Każda zmiana wydajności ma przełącznik do testu A/B albo porównanie dwóch buildów, plus wyniki z `--bench-log` (patrz [BENCHMARK.md](BENCHMARK.md)).
2. **Upstream mergujemy co tydzień** (`git merge upstream/main`, bez rebase).
   - Nasze zmiany trzymamy w nowych plikach i małych hakach.
   - Bez dobrego powodu nie ruszamy rekompilatora shaderów, bo tam jest najwięcej konfliktów.
3. **Poprawki konkretnych gier wysyłamy upstream.** Ogólne zmiany wydajności zostają u nas.
4. **Zasady prawne ze starego planu obowiązują dalej:**
   - zero kodu i dokumentów z wycieków SDK;
   - żadnych gier, firmware'u ani kluczy w repo;
   - tylko własne zrzuty;
   - w nazwie bez „PS5” i „PlayStation”.
   - Upstream wymaga też ujawniania użycia AI w PR-ach.

## Co wynika z naszego sprzętu

| Ograniczenie | Skutek | Co robimy |
|---|---|---|
| **10 GB VRAM** (PS5 ma 16 GB wspólnej pamięci) | Upstream liczy progi czyszczenia tekstur od stałych 8 GiB. Na karcie 10 GB GC zaczyna przy ~0,25 GiB i pobiera obrazy z GPU od ~3,7 GiB. Kart 16–24 GB, na których testują twórcy, to nie dotyczy. | Zrobione: `--vram-gc Proportional`. Dalej: tablica stron BDA (768 MiB VRAM), progi GC bufora. |
| **6 rdzeni** (PS5 ma 8) | Cały wątek GPU emulatora działa synchronicznie na jednym rdzeniu: kompilacja, nagrywanie i czekanie. Wątki gościa mogą mieć wyższy priorytet niż wątki emulatora. | Etapy 1 i 3. |
| **Ampere** (RTX 30) | Frame generation DLSS wymaga RTX 40, ale DLSS SR działa. Jest znany błąd shadera `CvtPkrtz` na RTX 3090 ([#1575](https://github.com/KytyPS5/KytyPS5/issues/1575)). | Skalowanie w etapie 4. #1575 sprawdzić na 3080. |

## Etapy

### Etap 0: Fundament (ten tydzień)

- [x] Fork lokalnie (`upstream` = KytyPS5), build x86_64 na Macu, testy regresyjne.
- [x] `--bench-log <folder>` (czasy klatek i liczniki do CSV) oraz `tools/bench/compare.py`.
- [x] `--vram-gc Proportional|Legacy`: progi GC tekstur jako ułamek budżetu VRAM, z testem `vram_gc_policy`.
- [x] Kod hosta kompilowany dla x86-64-v3 (AVX2/FMA/BMI2) na Windows i Linuksie.
- [ ] Repo na GitHubie i CI dla Windows, żeby artefakty trafiały na PC testowy.
- [ ] Nazwa projektu.
- [ ] Baseline: 2–3 posiadane gry, w każdej ta sama scena, `Legacy` vs `Proportional`.

### Etap 1: Szybkie wygrane (2–4 tygodnie, niskie ryzyko)

1. **Priorytety wątków (Windows):**
   - wątki GPU, schedulera i prezentacji powyżej wątków gościa;
   - wątki gościa najwyżej +1;
   - wyłączenie power throttlingu.
2. **Wątek GPU budzony sygnałem zamiast pollingu co 1 ms** (`graphicsRun.cpp:481`, `common/threads.cpp:345`).
3. **`tls_get_addr` bez blokady:** cache w `thread_local` (`runtimeLinker.cpp:1579`).
4. **Drobne poprawki synchronizacji:**
   - szybkie wyjście z `KernelDispatchPendingSignal…`, gdy maska jest zerowa;
   - `usleep(0)` ma robić yield.
5. **Zapis pipeline cache w tle,** bez przycięcia co 5 minut. Usunięcie `printf` przy każdej nowej permutacji.
6. **Mniej VRAM na stałe:** mniejsza lub rzadka tablica stron BDA, progi GC bufora.
7. **Dekodowanie audio i wideo:**
   - FFmpeg z wątkami (`thread_count`);
   - cache `SwrContext` w AJM;
   - osobny lock na instancję AJM.

### Etap 2: Przycięcia przy kompilacji shaderów (1–2 miesiące)

- ID shaderów z hasha zawartości zamiast licznika sesji.
- Dyskowy cache SPIR-V i kluczy pipeline'ów, wstępne kompilowanie (prewarm) przy starcie gry.
- Kompilacja pipeline'ów na 3–4 wątkach roboczych.
- Opcjonalnie: pomijanie draw do czasu gotowości albo szybkie linkowanie przez graphics pipeline library.
- Więcej stanu jako dynamic state (EDS3), czyli mniej permutacji.
- Wzorce do podejrzenia:
  - Jetsku/KytyPS5: background precompile, „first pipelines fast”;
  - AnyPS5: GPL fast-link, pipeline journal;
  - SharpEmu: cache per hash SPIR-V.

### Etap 3: Wątek GPU i synchronizacja (2–3 miesiące)

- **Odczyty z GPU bez pełnych synchronizacji.** Dziś każdy odczyt pamięci zapisanej przez GPU czeka na całą kolejkę ([#1178](https://github.com/KytyPS5/KytyPS5/issues/1178)).
- **Tańsze śledzenie zapisów CPU:**
  - VEH rejestrowany jako pierwszy;
  - większe bloki przy zdejmowaniu ochrony;
  - spinlock nie trzymany przez `VirtualProtect`;
  - strony zapisywane co klatkę obsługiwane hashem zamiast faultów.
- **Podział pracy:**
  - parsowanie PM4 oddzielone od nagrywania komend Vulkan;
  - łączenie submitów (Jetsku zmierzył +5 do +12%);
  - mniej pracy CPU na każdy draw (dirty tracking stanu, cache bindingów);
  - mniej globalnych barier.
- **Mutex gościa:** szybka ścieżka przez CAS i krótki adaptacyjny spin.

### Etap 4: Skalowanie obrazu (równolegle od etapu 2)

- Rozdzielczość renderowania i FSR 1.
- DLSS SR działa na RTX 30. Najpierw trzeba sprawdzić, czy licencja NVIDIA Streamline pozwala na dystrybucję z binarką GPL.
- Async compute na osobnej kolejce compute w NVIDII.

### Etap 5: Gry docelowe

- Lista 3–5 gier, które posiadasz, i na nich mierzymy postęp.
- Profile ustawień per gra.
- Poprawki kompatybilności wysyłane upstream.

## Jak pracujemy

```
Mac (kod, build x86_64, testy) ──push──► GitHub Actions (build Windows) ──artefakt──► PC 3080/5600
        ▲                                                                              │
        └────────────── wyniki --bench-log (CSV) + log emulatora ◄─────────────────────┘
```

## Ryzyka

| Ryzyko | Co robimy |
|---|---|
| Upstream zmienia się codziennie, więc będą konflikty | Merge co tydzień; zmiany w nowych plikach; nie przepisujemy gorących plików. |
| Testować możemy tylko na jednym PC | Pomiary z powtórzeniami (2 przebiegi na konfigurację), zawsze ta sama scena. |
| Konsola: zrzut gier wymaga odblokowanego firmware'u | Sprawdzić wersję firmware Twojego PS5 przed planowaniem listy gier. |
| Licencje (DLSS/Streamline a GPL) | Sprawdzić przed etapem 4. |
