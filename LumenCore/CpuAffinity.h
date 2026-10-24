#pragma once
#include <string>
#include <vector>

// Detects big.LITTLE performance cores (e.g. RK3588 A76) at runtime from each CPU's cpu_capacity (or max scaling frequency)
// in sysfs and pins threads to the cores within 10% of the fastest. A no-op on non-Linux or non-big.LITTLE systems.
namespace CpuAffinity
{
	// Cached after the first call - core topology doesn't change at runtime. Empty means
	// "not big.LITTLE (or couldn't tell)" - callers should treat that as "don't pin".
	const std::vector<int>& GetPerformanceCoreIds();

	// Pins the CALLING thread to the performance core set (no-op if none detected or non-Linux).
	// For the heavy pipeline threads (ISource::SourceThreadProc/ISink::ProcessingThreadLoop).
	void PinCurrentThreadToPerformanceCores();

	// Names the calling thread "<prefix>-<id>" (cut to the kernel's 15 characters) so top, htop and perf show which node it serves.
	void NameCurrentThread(const std::string& prefix, const std::string& id);

	// Starts OpenCV's worker pool from a thread pinned to the performance cores and sizes it to them, so the pool does not
	// inherit whichever thread (possibly a .NET one) first made a parallel call. Call once at startup.
	void InitialiseOpenCvThreads();
}
