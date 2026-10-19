#ifndef EMULATOR_SRC_GRAPHICS_PRESENTATION_BENCHMARKLOG_H_
#define EMULATOR_SRC_GRAPHICS_PRESENTATION_BENCHMARKLOG_H_

#include <chrono>

namespace Libs::Graphics {

struct GraphicContext;

// Writes every main-bus flip interval to frametimes.csv and, once per second, the debug counters,
// device memory and host CPU to counters.csv in Config::GetBenchmarkLogFolder(), so runs of
// different builds or settings can be compared with tools/bench/compare.py. Both files are
// flushed every second, so a crash loses at most the last second. Does nothing when no folder is
// configured.
void BenchmarkLogStart(const GraphicContext& graphics);
// Must run before the graphics subsystem is destroyed.
void BenchmarkLogStop();
void BenchmarkLogRecordFlip(std::chrono::steady_clock::time_point now) noexcept;

} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_PRESENTATION_BENCHMARKLOG_H_
