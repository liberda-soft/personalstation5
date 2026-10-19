#include "graphics/host_gpu/renderer/cache/vramGcPolicy.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

using Libs::Graphics::ComputeTextureGcLevels;
using Libs::Graphics::TextureGcLevels;
using Policy = Config::VramGcPolicy;

constexpr int64_t GiB = 1024ll * 1024 * 1024;
// GetTotalMemoryBudget() on 10, 16 and 24 GB cards.
constexpr int64_t BUDGET_10GB = GiB * 17 / 2;
constexpr int64_t BUDGET_16GB = 14 * GiB;
constexpr int64_t BUDGET_24GB = 22 * GiB;

void Check(bool value, const char* message) {
	if (!value) {
		std::fprintf(stderr, "VramGcPolicyTests: failed: %s\n", message);
		std::abort();
	}
}

bool Equal(const TextureGcLevels& levels, uint64_t trigger, uint64_t pressure, uint64_t critical) {
	return levels.trigger == trigger && levels.pressure == pressure && levels.critical == critical;
}

void TestLegacyMatchesUpstream() {
	Check(Equal(ComputeTextureGcLevels(BUDGET_10GB, Policy::Legacy), 268435456, 3972844749,
	            7408818586),
	      "legacy levels changed for a 10 GB card");
	Check(Equal(ComputeTextureGcLevels(BUDGET_16GB, Policy::Legacy), 3221225472, 9878424781,
	            13314398618),
	      "legacy levels changed for a 16 GB card");
	Check(Equal(ComputeTextureGcLevels(BUDGET_24GB, Policy::Legacy), 7516192768, 18468359373,
	            21904333210),
	      "legacy levels changed for a 24 GB card");
}

void TestProportionalLevels() {
	Check(Equal(ComputeTextureGcLevels(BUDGET_10GB, Policy::Proportional), 3650722201, 6388763852,
	            8031588843),
	      "proportional levels for a 10 GB card");
	Check(Equal(ComputeTextureGcLevels(BUDGET_16GB, Policy::Proportional), 6012954214,
	            10522669875, 13228499271),
	      "proportional levels for a 16 GB card");
	// The reason for the policy: a 10 GB card no longer collects from the first quarter GiB.
	Check(ComputeTextureGcLevels(BUDGET_10GB, Policy::Proportional).trigger >= 3 * GiB,
	      "a 10 GB card collects too early");
}

void TestLevelsAreOrdered() {
	for (int64_t budget = 2 * GiB; budget <= 48 * GiB; budget += GiB / 4) {
		for (const auto policy: {Policy::Proportional, Policy::Legacy}) {
			const auto levels = ComputeTextureGcLevels(budget, policy);
			Check(levels.trigger <= levels.pressure && levels.pressure <= levels.critical,
			      "levels are not ordered");
			if (budget >= 4 * GiB) {
				Check(levels.critical <= static_cast<uint64_t>(budget), "critical exceeds budget");
			}
		}
	}
	Check(Equal(ComputeTextureGcLevels(0, Policy::Proportional), 0, GiB + GiB / 2, 3 * GiB),
	      "an unknown budget keeps the minimum levels");
}

} // namespace

int main() {
	TestLegacyMatchesUpstream();
	TestProportionalLevels();
	TestLevelsAreOrdered();
	std::printf("VramGcPolicyTests: ok\n");
	return 0;
}
