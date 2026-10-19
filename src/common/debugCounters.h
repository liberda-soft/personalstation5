#ifndef KYTY_COMMON_DEBUG_COUNTERS_H_
#define KYTY_COMMON_DEBUG_COUNTERS_H_

#include <atomic>
#include <chrono>
#include <cstdint>

namespace Common::DebugCounters {

static_assert(std::atomic<uint64_t>::is_always_lock_free &&
              std::atomic<uint32_t>::is_always_lock_free);

// Collect events only while the panel is visible. Relaxed, lock-free atomics keep updates
// cheap and safe in signal handlers. Rates come from comparing two snapshots.
enum class Counter {
	// GPU work.
	Draws,
	DrawInstances,
	IndirectDraws,
	Dispatches,
	QueueSubmits,
	GpuWaits, // CPU blocked on a GPU timeline semaphore.
	GpuWaitNs,
	GpuFaults, // Guest writes to GPU-tracked memory.
	ShadersCompiled,
	ShaderCompileNs,
	PipelinesCreated,
	PipelineCreateNs,
	// Caches.
	TextureUploadBytes,
	TextureDownloadBytes,
	TextureEvictions,
	BufferUploadBytes,
	BufferDownloadBytes,
	BufferEvictions,
	// Presentation.
	FlipsRequested,
	FlipsRejected, // Guest flip queue was full.
	LateVblanks,   // The present thread fell behind the vblank rate.
	SwapchainRecreations,
	DroppedPresents,
	// Emulator.
	EmulatedInstructions, // Host CPU lacked an instruction the guest used.
	UnresolvedImportCalls,
	GuestFileReadBytes,
	GuestFileWriteBytes,
	AudioUnderruns,
	Count
};

enum class Peak { ShaderCompileNs, PipelineCreateNs, Count };

namespace Detail {
// Counters collect while the performance overlay or the benchmark log uses them.
constexpr uint32_t           OVERLAY_USER   = 1u << 0u;
constexpr uint32_t           BENCHMARK_USER = 1u << 1u;
inline std::atomic<uint32_t> g_users {0};
inline std::atomic<uint64_t> g_counters[static_cast<int>(Counter::Count)] {};
inline std::atomic<uint64_t> g_peaks[static_cast<int>(Peak::Count)] {};
} // namespace Detail

[[nodiscard]] inline bool Enabled() noexcept {
	return Detail::g_users.load(std::memory_order_relaxed) != 0;
}

// The performance overlay is shown; Enabled() also stays true while the benchmark log records.
[[nodiscard]] inline bool OverlayEnabled() noexcept {
	return (Detail::g_users.load(std::memory_order_relaxed) & Detail::OVERLAY_USER) != 0;
}

inline void SetEnabled(bool enabled) noexcept {
	if (enabled) {
		Detail::g_users.fetch_or(Detail::OVERLAY_USER, std::memory_order_relaxed);
	} else {
		Detail::g_users.fetch_and(~Detail::OVERLAY_USER, std::memory_order_relaxed);
	}
}

inline void SetBenchmarkEnabled(bool enabled) noexcept {
	if (enabled) {
		Detail::g_users.fetch_or(Detail::BENCHMARK_USER, std::memory_order_relaxed);
	} else {
		Detail::g_users.fetch_and(~Detail::BENCHMARK_USER, std::memory_order_relaxed);
	}
}

inline void Add(Counter counter, uint64_t value = 1) noexcept {
	if (!Enabled()) {
		return;
	}
	Detail::g_counters[static_cast<int>(counter)].fetch_add(value, std::memory_order_relaxed);
}

class ScopedTimer final {
public:
	ScopedTimer(Counter count, Counter duration, Peak peak = Peak::Count)
	    : m_count(count), m_duration(duration), m_peak(peak),
	      m_start(Enabled() ? std::chrono::steady_clock::now()
	                        : std::chrono::steady_clock::time_point {}) {}
	~ScopedTimer() {
		if (m_start == std::chrono::steady_clock::time_point {} || !Enabled()) {
			return;
		}
		const auto elapsed =
		    static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
		                              std::chrono::steady_clock::now() - m_start)
		                              .count());
		Add(m_count);
		Add(m_duration, elapsed);
		if (m_peak != Peak::Count) {
			auto& peak     = Detail::g_peaks[static_cast<int>(m_peak)];
			auto  previous = peak.load(std::memory_order_relaxed);
			while (previous < elapsed &&
			       !peak.compare_exchange_weak(previous, elapsed, std::memory_order_relaxed)) {
			}
		}
	}
	ScopedTimer(const ScopedTimer&)            = delete;
	ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
	Counter                               m_count;
	Counter                               m_duration;
	Peak                                  m_peak;
	std::chrono::steady_clock::time_point m_start;
};

[[nodiscard]] inline uint64_t Get(Counter counter) noexcept {
	return Detail::g_counters[static_cast<int>(counter)].load(std::memory_order_relaxed);
}

[[nodiscard]] inline uint64_t Get(Peak peak) noexcept {
	return Detail::g_peaks[static_cast<int>(peak)].load(std::memory_order_relaxed);
}

} // namespace Common::DebugCounters

#endif /* KYTY_COMMON_DEBUG_COUNTERS_H_ */
