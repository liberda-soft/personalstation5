# personalstation5

**personalstation5** to fork [KytyPS5](https://github.com/KytyPS5/KytyPS5) (GPL-2.0) nastawiony na wydajność na PC z RTX 3080 i Ryzenem 5 5600. Repo: [liberda-soft/personalstation5](https://github.com/liberda-soft/personalstation5). Projekt nie jest powiązany z Sony Interactive Entertainment.

Ten katalog opisuje nasze zmiany. Reszta repo to kod KytyPS5.

Pliki, które widzisz po rozpakowaniu buildu:

- `personalstation5.exe`: launcher z listą gier;
- `personalstation5_emulator.exe`: sam emulator, uruchamiany z wiersza poleceń (np. do benchmarku);
- na macOS: `personalstation5.app`.

Nazwy ustawiają zmienne CMake `KYTY_PRODUCT_NAME`, `KYTY_LAUNCHER_OUTPUT_NAME` i `KYTY_EMULATOR_OUTPUT_NAME`. Targety CMake w kodzie dalej nazywają się `kyty_emulator` i `launcher`, żeby merge z upstreamem był prosty. Tytuł okna to `personalstation5 <hash> (liberda-soft/personalstation5)`.

- [PLAN.md](PLAN.md): cel, etapy i zasady.
- [BENCHMARK.md](BENCHMARK.md): jak mierzyć wydajność na PC testowym.

## Zmiany względem upstreamu

| Zmiana | Pliki | Przełącznik |
|---|---|---|
| Log benchmarku: czasy klatek i liczniki do CSV | `src/graphics/presentation/benchmarkLog.*`; hooki w `performanceOverlay.cpp` i `vulkanWindow.cpp`; `common/debugCounters.h` (liczniki działają też bez nakładki) | `--bench-log <folder>` |
| Progi GC tekstur jako ułamek budżetu VRAM (karty 10 GB) | `src/graphics/host_gpu/renderer/cache/vramGcPolicy.h`, `textureCache.cpp`, `tests/VramGcPolicyTests.cpp` | `--vram-gc Proportional\|Legacy` (domyślnie `Proportional`) |
| Kod hosta dla x86-64-v3 (AVX2/FMA/BMI2) na Windows i Linuksie | `CMakeLists.txt` | `-DKYTY_HOST_ARCH=` (pusty wyłącza) |
| Skrypt porównujący benchmarki | `tools/bench/compare.py` | — |
| Nazwy plików, aplikacji i tytułów: personalstation5 | `CMakeLists.txt`, `src/cmake_config.h.in`, `src/kytyGitVersion.h.in`, `src/launcher/*`, `src/main.cpp`, `.github/workflows/build.yml` | `-DKYTY_PRODUCT_NAME=…` itd. |

## Synchronizacja z upstreamem

```bash
git fetch upstream
git merge upstream/main
```

Push do `upstream` jest zablokowany: URL push to `DISABLED_no_push_to_upstream`.

## Build na Macu (Apple Silicon, x86_64 przez Rosettę)

Bez Qt, czyli bez launchera:

```bash
cmake -S . -B _Build/macos-noqt -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DKYTY_BUILD_LAUNCHER=OFF
cmake --build _Build/macos-noqt --target kyty_emulator kyty_tests --parallel
ctest --test-dir _Build/macos-noqt
```

Testy korzystające z Vulkana wymagają `libMoltenVK.dylib` w wersji x86_64 (szczegóły w głównym README). Bez niej kończą się błędem „could not load the Vulkan loader”.
