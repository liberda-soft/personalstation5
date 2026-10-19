#include "graphics/presentation/benchmarkLog.h"

#include "common/debugCounters.h"
#include "common/emulatorConfig.h"
#include "common/hostStats.h"
#include "graphics/host_gpu/graphicContext.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <magic_enum.hpp>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace Libs::Graphics {

namespace {

using Clock   = std::chrono::steady_clock;
using Counter = Common::DebugCounters::Counter;

constexpr auto   WRITE_INTERVAL = std::chrono::seconds(1);
constexpr size_t COUNTER_COUNT  = static_cast<size_t>(Counter::Count);
constexpr size_t THREAD_COLUMNS = 3;
constexpr double MIB            = 1024.0 * 1024.0;

struct Flip {
	double time_s   = 0.0; // Since the log started.
	float  frame_ms = 0.0f;
};

std::atomic<bool> g_recording {false};
std::mutex        g_flip_mutex;
std::vector<Flip> g_flips;
Clock::time_point g_start {};
Clock::time_point g_last_flip {};
std::mutex        g_writer_mutex;
std::jthread      g_writer;

template <typename T>
std::string Optional(const std::optional<T>& value, double scale = 1.0) {
	if (!value) {
		return {};
	}
	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "%.1f", static_cast<double>(*value) / scale);
	return buffer;
}

// Thread names come from the guest; keep them from splitting CSV columns.
std::string CsvName(std::string name) {
	std::replace_if(
	    name.begin(), name.end(), [](char c) { return c == ',' || c == '\n' || c == '"'; }, ' ');
	return name;
}

class Writer final {
public:
	Writer(const GraphicContext& graphics, const std::filesystem::path& folder)
	    : m_graphics(graphics) {
		std::error_code error;
		std::filesystem::create_directories(folder, error);
		m_frames.open(folder / "frametimes.csv", std::ios::out | std::ios::trunc);
		m_counters.open(folder / "counters.csv", std::ios::out | std::ios::trunc);
		if (!m_frames || !m_counters) {
			::printf("Benchmark log: could not write to %s\n", folder.string().c_str());
			return;
		}
		m_frames << "time_s,frame_ms\n";
		m_counters << "time_s,frames,fps,avg_frame_ms,p99_frame_ms,max_frame_ms,vram_used_mb,"
		              "vram_budget_mb,process_cpu_percent,system_cpu_percent,process_memory_mb";
		for (size_t i = 0; i < THREAD_COLUMNS; i++) {
			m_counters << ",thread" << i + 1 << "_name,thread" << i + 1 << "_cpu_percent";
		}
		for (size_t i = 0; i < COUNTER_COUNT; i++) {
			m_counters << ',' << magic_enum::enum_name(static_cast<Counter>(i));
			m_previous[i] = Common::DebugCounters::Get(static_cast<Counter>(i));
		}
		m_counters << '\n';
		m_frames.flush();
		m_counters.flush();
		m_last_write = Clock::now();
		m_ok         = true;
		::printf("Benchmark log: writing to %s\n", folder.string().c_str());
	}

	[[nodiscard]] bool Ok() const { return m_ok; }

	void Write(Clock::time_point now) {
		std::vector<Flip> flips;
		{
			std::scoped_lock lock(g_flip_mutex);
			flips.swap(g_flips);
		}
		const double seconds = std::chrono::duration<double>(now - m_last_write).count();
		m_last_write         = now;
		std::vector<float> ms;
		ms.reserve(flips.size());
		for (const auto& flip: flips) {
			char buffer[48];
			std::snprintf(buffer, sizeof(buffer), "%.4f,%.3f\n", flip.time_s, flip.frame_ms);
			m_frames << buffer;
			ms.push_back(flip.frame_ms);
		}

		double avg_ms = 0.0;
		double p99_ms = 0.0;
		double max_ms = 0.0;
		if (!ms.empty()) {
			for (const float value: ms) {
				avg_ms += value;
			}
			avg_ms /= static_cast<double>(ms.size());
			max_ms                = *std::max_element(ms.begin(), ms.end());
			const auto percentile = ms.begin() + static_cast<ptrdiff_t>(ms.size() * 99 / 100);
			std::nth_element(ms.begin(), percentile, ms.end());
			p99_ms = *percentile;
		}

		std::array<GraphicContext::HeapUsage, VK_MAX_MEMORY_HEAPS> heaps {};
		const auto              heap_count = m_graphics.GetHeapUsage(heaps);
		const bool              discrete   = m_graphics.GetPhysicalDeviceProperties().deviceType ==
		                                     vk::PhysicalDeviceType::eDiscreteGpu;
		std::optional<uint64_t> vram_used;
		std::optional<uint64_t> vram_budget;
		if (m_graphics.CanReportMemoryUsage()) {
			vram_used   = 0;
			vram_budget = 0;
			for (uint32_t i = 0; i < heap_count; i++) {
				if (!discrete || heaps[i].device_local) {
					*vram_used += heaps[i].usage;
					*vram_budget += heaps[i].budget;
				}
			}
		}
		const auto host = m_sampler.Sample();

		char head[160];
		std::snprintf(head, sizeof(head), "%.3f,%zu,%.2f,%.3f,%.3f,%.3f,",
		              std::chrono::duration<double>(now - g_start).count(), ms.size(),
		              seconds > 0.0 ? static_cast<double>(ms.size()) / seconds : 0.0, avg_ms,
		              p99_ms, max_ms);
		m_counters << head << Optional(vram_used, MIB) << ',' << Optional(vram_budget, MIB) << ','
		           << Optional(host.process_cpu_percent) << ',' << Optional(host.system_cpu_percent)
		           << ',' << Optional(host.process_memory, MIB);
		for (size_t i = 0; i < THREAD_COLUMNS; i++) {
			if (i < host.busiest_threads.size()) {
				const auto& thread = host.busiest_threads[i];
				char        cpu[16];
				std::snprintf(cpu, sizeof(cpu), "%.1f", thread.cpu_percent);
				m_counters << ',' << CsvName(thread.name) << ',' << cpu;
			} else {
				m_counters << ",,";
			}
		}
		for (size_t i = 0; i < COUNTER_COUNT; i++) {
			const auto value = Common::DebugCounters::Get(static_cast<Counter>(i));
			m_counters << ',' << value - m_previous[i];
			m_previous[i] = value;
		}
		m_counters << '\n';
		m_frames.flush();
		m_counters.flush();
	}

private:
	const GraphicContext&               m_graphics;
	std::ofstream                       m_frames;
	std::ofstream                       m_counters;
	Common::HostStatsSampler            m_sampler;
	std::array<uint64_t, COUNTER_COUNT> m_previous {};
	Clock::time_point                   m_last_write {};
	bool                                m_ok = false;
};

void WriterThread(const std::stop_token& stop, const GraphicContext* graphics,
                  std::filesystem::path folder) {
	Writer writer(*graphics, folder);
	if (!writer.Ok()) {
		g_recording.store(false, std::memory_order_relaxed);
		Common::DebugCounters::SetBenchmarkEnabled(false);
		return;
	}
	std::mutex                  mutex;
	std::condition_variable_any wake;
	auto                        next = Clock::now() + WRITE_INTERVAL;
	std::unique_lock            lock(mutex);
	while (!wake.wait_until(lock, stop, next, [] { return false; })) {
		if (stop.stop_requested()) {
			break;
		}
		writer.Write(Clock::now());
		next += WRITE_INTERVAL;
	}
	writer.Write(Clock::now());
}

} // namespace

void BenchmarkLogStart(const GraphicContext& graphics) {
	const auto& folder = Config::GetBenchmarkLogFolder();
	if (folder.empty()) {
		return;
	}
	std::scoped_lock lock(g_writer_mutex);
	if (g_writer.joinable()) {
		return;
	}
	{
		std::scoped_lock flip_lock(g_flip_mutex);
		g_flips.clear();
		g_flips.reserve(1024);
		g_start     = Clock::now();
		g_last_flip = {};
	}
	Common::DebugCounters::SetBenchmarkEnabled(true);
	g_recording.store(true, std::memory_order_relaxed);
	g_writer = std::jthread(WriterThread, &graphics, folder);
}

void BenchmarkLogStop() {
	std::scoped_lock lock(g_writer_mutex);
	if (!g_writer.joinable()) {
		return;
	}
	g_recording.store(false, std::memory_order_relaxed);
	g_writer.request_stop();
	g_writer.join();
	g_writer = {};
	Common::DebugCounters::SetBenchmarkEnabled(false);
}

void BenchmarkLogRecordFlip(Clock::time_point now) noexcept {
	if (!g_recording.load(std::memory_order_relaxed)) {
		return;
	}
	std::scoped_lock lock(g_flip_mutex);
	if (g_last_flip != Clock::time_point {}) {
		g_flips.push_back({std::chrono::duration<double>(now - g_start).count(),
		                   std::chrono::duration<float, std::milli>(now - g_last_flip).count()});
	}
	g_last_flip = now;
}

} // namespace Libs::Graphics
