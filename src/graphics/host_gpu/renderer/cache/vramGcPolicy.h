#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_CACHE_VRAMGCPOLICY_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_CACHE_VRAMGCPOLICY_H_

#include "common/emulatorConfig.h"

#include <algorithm>
#include <cstdint>

namespace Libs::Graphics {

// Device memory use, compared against GraphicContext::GetDeviceMemoryUsage(), at which the
// texture cache starts collecting, starts downloading GPU-written images to evict them, and
// collects aggressively.
struct TextureGcLevels {
	uint64_t trigger  = 0;
	uint64_t pressure = 0;
	uint64_t critical = 0;
};

// Upstream derives the levels from a fixed 8 GiB working set. With the ~8.5 GiB budget of a
// 10 GB card that starts collecting at ~0.25 GiB and downloading GPU-written images at ~3.7 GiB,
// while a 16 GB card waits until 3 and 9.2 GiB. The proportional policy uses the same shares of
// the budget on every card instead.
[[nodiscard]] inline TextureGcLevels ComputeTextureGcLevels(int64_t              budget,
                                                            Config::VramGcPolicy policy) {
	constexpr int64_t GiB = 1024ll * 1024 * 1024;
	if (policy == Config::VramGcPolicy::Legacy) {
		const auto threshold = std::min<int64_t>(budget, 8 * GiB);
		return {
		    .trigger  = static_cast<uint64_t>(std::max<int64_t>((budget - threshold) / 2, 0)),
		    .pressure = static_cast<uint64_t>(std::max<int64_t>(
		        std::min(budget - 6 * threshold / 10, budget - GiB), GiB + GiB / 2)),
		    .critical = static_cast<uint64_t>(std::max<int64_t>(
		        std::min(budget - 2 * threshold / 10, budget - GiB / 2), 3 * GiB)),
		};
	}
	return {
	    .trigger  = static_cast<uint64_t>(std::max<int64_t>(budget * 4 / 10, 0)),
	    .pressure = static_cast<uint64_t>(
	        std::max<int64_t>(std::min(budget * 7 / 10, budget - GiB), GiB + GiB / 2)),
	    .critical = static_cast<uint64_t>(
	        std::max<int64_t>(std::min(budget * 88 / 100, budget - GiB / 2), 3 * GiB)),
	};
}

} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_CACHE_VRAMGCPOLICY_H_
