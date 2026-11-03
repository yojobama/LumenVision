// Hunter I (board-media): edge cases of V4l2CameraBackend that need no camera: missing and non-capture device nodes, use while closed.
#ifdef __linux__
#include <catch2/catch_test_macros.hpp>
#include "V4l2CameraBackend.h"
#include "edge_media_util.h"

#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace edge_media;

// ---- fault injection for the cases below: an ioctl()/poll() of this executable that forwards to libc's, unless a test arms a failure ----
// LumenCore's V4l2CameraBackend calls ioctl() and poll() through the PLT, so these definitions (executable first in the lookup order) see
// its calls. They let a test make the kernel fail at an exact step of a real camera's streaming set-up, which a camera never does on demand.
namespace {
struct EdgeV4l2Fault {
	std::atomic<unsigned long> request{ 0 }; // fail this ioctl request ...
	std::atomic<int> skip{ 0 };              // ... after letting this many of them through ...
	std::atomic<int> failures{ 0 };          // ... this many times (EIO)
	std::atomic<bool> forcePollReady{ false }; // the next zero-timeout poll() on a POLLIN wait reports data ready
	std::atomic<int> requeueRejected{ 0 };   // VIDIOC_QBUF calls the kernel answered EINVAL (a buffer that is already queued)
};
EdgeV4l2Fault g_Fault;
void DisarmFaults()
{
	g_Fault.request = 0;
	g_Fault.skip = 0;
	g_Fault.failures = 0;
	g_Fault.forcePollReady = false;
}
}

// for test_edge_camera_hw.cpp: every VIDIOC_DQBUF fails (EIO) until this is called with 0
extern "C" int edge_v4l2_fail_dequeues(int count)
{
	if (count <= 0) {
		DisarmFaults();
		return 0;
	}
	g_Fault.request = VIDIOC_DQBUF;
	g_Fault.skip = 0;
	g_Fault.failures = count;
	return count;
}

extern "C" int ioctl(int fd, unsigned long request, ...) __THROW
{
	va_list args;
	va_start(args, request);
	void* argument = va_arg(args, void*);
	va_end(args);
	using Ioctl = int (*)(int, unsigned long, ...);
	static const Ioctl real = reinterpret_cast<Ioctl>(dlsym(RTLD_NEXT, "ioctl"));
	if (request != 0 && request == g_Fault.request.load()) {
		if (g_Fault.skip.load() > 0) g_Fault.skip--;
		else if (g_Fault.failures.load() > 0) {
			g_Fault.failures--;
			errno = EIO;
			return -1;
		}
	}
	const int result = real(fd, request, argument);
	if (result < 0 && request == VIDIOC_QBUF && errno == EINVAL) g_Fault.requeueRejected++;
	return result;
}

extern "C" int poll(struct pollfd* fds, nfds_t count, int timeout)
{
	using Poll = int (*)(struct pollfd*, nfds_t, int);
	static const Poll real = reinterpret_cast<Poll>(dlsym(RTLD_NEXT, "poll"));
	if (timeout == 0 && count == 1 && (fds[0].events & POLLIN) && g_Fault.forcePollReady.exchange(false)) {
		fds[0].revents = POLLIN;
		return 1;
	}
	return real(fds, count, timeout);
}

namespace {
// the first /dev/video node that opens as a capture device here (the Pi has two USB cameras attached; empty when none)
std::string EdgeFindCaptureNode()
{
	for (int i = 0; i < 16; i++) {
		const std::string path = "/dev/video" + std::to_string(i);
		V4l2CameraBackend probe;
		if (probe.Open(path)) return path;
	}
	return {};
}

// mappings of a /dev/video node in this process
int EdgeCountVideoMappings()
{
	std::ifstream maps("/proc/self/maps");
	std::string line;
	int count = 0;
	while (std::getline(maps, line))
		if (line.find("/dev/video") != std::string::npos) count++;
	return count;
}
}

TEST_CASE("I: V4l2CameraBackend refuses paths that are not V4L2 capture devices, without leaking an fd", "[edge][board-media][v4l2]")
{
	const FdCounts before = CountFds();
	for (const char* path : { "/dev/nonexistent-video-node", "/dev/null", "/dev/zero", "/proc/self/status", "/dev", "", "/dev/video-does-not-exist0" }) {
		V4l2CameraBackend backend;
		INFO(path);
		CHECK_FALSE(backend.Open(path));
		CHECK_FALSE(backend.IsOpened());
		// everything on a backend that failed to open is a clean no-op
		CHECK_FALSE(backend.Grab(false).success);
		CHECK(backend.EnumerateModes().empty());
		CHECK_FALSE(backend.SetMode(CameraMode{ 640, 480, 30.0, FrameFormat::MJPEG }));
		CHECK_FALSE(backend.SetExposure(100));
		CHECK_FALSE(backend.SetAutoExposure(true));
		CHECK_FALSE(backend.SetGain(1));
		CHECK_FALSE(backend.SetControl(V4L2_CID_BRIGHTNESS, 1));
		CHECK(backend.EnumerateControls().empty());
		CHECK_FALSE(backend.GetExposureRange().supported);
	}
	CHECK(CountFds().total == before.total);
}

// I4. StartStreaming mapped each buffer and, on a failure part-way (QUERYBUF, mmap, QBUF, STREAMON), only cleared its vector: the mappings were
// never munmapped and the kernel's buffers stayed allocated, so the next S_FMT failed with EBUSY. A camera that fails to start (another process
// has it, a USB hiccup) and is retried every few seconds leaked a set of buffers each time.
TEST_CASE("I4: a failed streaming start gives its buffer mappings back and the camera can be opened again", "[edge][board-media][v4l2][camera]")
{
	const std::string node = EdgeFindCaptureNode();
	if (node.empty()) SKIP("no camera attached");

	for (unsigned long request : { static_cast<unsigned long>(VIDIOC_STREAMON), static_cast<unsigned long>(VIDIOC_QBUF) }) {
		INFO("failing request " << request);
		const int mappingsBefore = EdgeCountVideoMappings();
		{
			V4l2CameraBackend backend;
			g_Fault.request = request;
			g_Fault.skip = request == VIDIOC_QBUF ? 2 : 0; // the third buffer's QBUF: two are mapped and queued by then
			g_Fault.failures = 1;
			CHECK_FALSE(backend.Open(node));
			DisarmFaults();
			CHECK_FALSE(backend.IsOpened());
			CHECK(EdgeCountVideoMappings() == mappingsBefore);
			// nothing is left allocated in the kernel: the same node opens and streams
			REQUIRE(backend.Open(node));
			CameraGrabResult grab;
			for (int attempt = 0; attempt < 20 && !grab.success; attempt++) grab = backend.Grab(true);
			CHECK(grab.success);
		}
		CHECK(EdgeCountVideoMappings() == mappingsBefore);
	}
}

// I5. Grab drains to the newest frame: it requeues the buffer it holds, then dequeues the next. When that second dequeue failed, the old code
// stopped draining but still decoded the buffer it had already handed back to the driver (which may be writing into it) and requeued it again
// at the end, which the kernel rejects (EINVAL: queued twice).
TEST_CASE("I5: a failed dequeue after a requeue neither decodes nor requeues the same buffer twice", "[edge][board-media][v4l2][camera]")
{
	const std::string node = EdgeFindCaptureNode();
	if (node.empty()) SKIP("no camera attached");

	V4l2CameraBackend backend;
	REQUIRE(backend.Open(node));
	for (int attempt = 0; attempt < 20 && !backend.Grab(true).success; attempt++) {} // streaming and delivering

	g_Fault.requeueRejected = 0;
	g_Fault.forcePollReady = true; // the peek for a newer frame says there is one
	g_Fault.request = VIDIOC_DQBUF;
	g_Fault.skip = 1;              // the first dequeue works, the one after the requeue fails
	g_Fault.failures = 1;
	const CameraGrabResult faulted = backend.Grab(true);
	DisarmFaults();
	CHECK_FALSE(faulted.success);
	CHECK(g_Fault.requeueRejected.load() == 0);

	// and the camera goes on delivering
	bool recovered = false;
	for (int attempt = 0; attempt < 20 && !recovered; attempt++) recovered = backend.Grab(true).success;
	CHECK(recovered);
	CHECK(g_Fault.requeueRejected.load() == 0);
}

#endif // __linux__
