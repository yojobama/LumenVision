#pragma once
#include <vector>

// Detects big.LITTLE performance cores (e.g. RK3588 A76) at runtime from each CPU's max scaling frequency in
// sysfs and pins threads to the highest tier. A no-op on non-Linux or non-big.LITTLE systems.
namespace CpuAffinity
{
	// Cached after the first call - core topology doesn't change at runtime. Empty means
	// "not big.LITTLE (or couldn't tell)" - callers should treat that as "don't pin".
	const std::vector<int>& GetPerformanceCoreIds();

	// Pins the CALLING thread to the performance core set (no-op if none detected or non-Linux).
	// For the heavy pipeline threads (ISource::SourceThreadProc/ISink::ProcessingThreadLoop).
	void PinCurrentThreadToPerformanceCores();
}
