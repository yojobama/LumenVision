#pragma once
#ifdef LUMEN_WITH_RGA

// librga's headers need NULL defined before inclusion (they do not include <cstddef>); keep this include first.
#include <cstddef>
#include <im2d.h>

// Keeps the calling thread's RGA jobs on the RK3588's two RGA3 cores. The buffers handed to RGA here are plain heap memory, which on a 16 GB
// board can sit above 4 GB; the RGA2 core cannot address that and fails such a job ("RGA_MMU unsupported memory larger than 4G", seen
// every hour or so in a 12 h soak), while the scheduler otherwise picks any core. librga keeps the setting per thread, and setting it is a
// plain store, so it is applied before every job.
inline void RgaUseRga3Cores()
{
	imconfig(IM_CONFIG_SCHEDULER_CORE, IM_SCHEDULER_RGA3_CORE0 | IM_SCHEDULER_RGA3_CORE1);
}

#endif // LUMEN_WITH_RGA
