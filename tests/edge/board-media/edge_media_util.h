#pragma once
// Helpers for hunter I's board-media edge tests (MPP JPEG decode, rkmpp JPEG encode, codec-stereo rkmpp_hwenc, V4L2).
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <string>
#include <thread>

namespace edge_media {

// open fds of this process, and those that are dma-bufs (MPP/DMA-heap buffers)
struct FdCounts {
	int total = 0;
	int dmabuf = 0;
	int mpp = 0; // /dev/mpp_service and similar
};

inline FdCounts CountFds()
{
	FdCounts counts;
	DIR* dir = opendir("/proc/self/fd");
	if (!dir) return counts;
	while (dirent* entry = readdir(dir)) {
		if (entry->d_name[0] == '.') continue;
		char path[64], target[256];
		std::snprintf(path, sizeof path, "/proc/self/fd/%s", entry->d_name);
		ssize_t n = readlink(path, target, sizeof target - 1);
		if (n < 0) continue;
		target[n] = 0;
		counts.total++;
		if (std::strstr(target, "dmabuf")) counts.dmabuf++;
		if (std::strstr(target, "mpp") || std::strstr(target, "rkvenc") || std::strstr(target, "vepu")) counts.mpp++;
	}
	closedir(dir);
	return counts;
}

// mappings of dma-buf memory in this process (an mmapped MPP buffer whose fd is already closed still shows here)
inline int CountDmabufMaps()
{
	std::ifstream maps("/proc/self/maps");
	std::string line;
	int n = 0;
	while (std::getline(maps, line))
		if (line.find("dmabuf") != std::string::npos) n++;
	return n;
}

inline long VmRssKb()
{
	std::ifstream status("/proc/self/status");
	std::string line;
	while (std::getline(status, line))
		if (line.rfind("VmRSS:", 0) == 0) return std::stol(line.substr(6));
	return -1;
}

// Runs fn in a forked child so a crash or a hang cannot take the test process (or the other hunter's run) down with it.
// fn returns a short text result, written back through a pipe. A child that does not finish within timeoutSec is killed.
struct IsolatedResult {
	bool finished = false;  // exited normally with status 0
	bool timedOut = false;
	bool crashed = false;   // killed by a signal (other than our timeout kill) or a non-zero exit (a sanitiser report exits 1)
	int exitCode = -1;
	int signal = 0;
	std::string text;
};

inline IsolatedResult RunIsolated(const std::function<std::string()>& fn, int timeoutSec)
{
	IsolatedResult result;
	int fds[2];
	if (pipe(fds) != 0) { result.text = "pipe failed"; return result; }
	std::fflush(stdout);
	std::fflush(stderr);
	pid_t pid = fork();
	if (pid < 0) { result.text = "fork failed"; close(fds[0]); close(fds[1]); return result; }
	if (pid == 0) {
		close(fds[0]);
		// nothing may escape the child: an exception would unwind into Catch2 and run the rest of the test suite in this process too
		std::string text;
		try {
			text = fn();
		} catch (const std::exception& e) {
			text = std::string("exception: ") + e.what();
		} catch (...) {
			text = "unknown exception";
		}
		(void)!write(fds[1], text.data(), text.size());
		close(fds[1]);
		std::fflush(stdout);
		std::fflush(stderr);
		_exit(0);
	}
	close(fds[1]);
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSec);
	int status = 0;
	for (;;) {
		pid_t r = waitpid(pid, &status, WNOHANG);
		if (r == pid) break;
		// keep the pipe drained so a large result cannot block the child
		pollfd p{ fds[0], POLLIN, 0 };
		if (poll(&p, 1, 0) > 0 && (p.revents & POLLIN)) {
			char buf[4096];
			ssize_t n = read(fds[0], buf, sizeof buf);
			if (n > 0) result.text.append(buf, static_cast<size_t>(n));
		}
		if (std::chrono::steady_clock::now() > deadline) {
			result.timedOut = true;
			kill(pid, SIGKILL);
			waitpid(pid, &status, 0);
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	char buf[4096];
	for (;;) {
		ssize_t n = read(fds[0], buf, sizeof buf);
		if (n <= 0) break;
		result.text.append(buf, static_cast<size_t>(n));
	}
	close(fds[0]);
	if (result.timedOut) return result;
	if (WIFEXITED(status)) {
		result.exitCode = WEXITSTATUS(status);
		result.finished = result.exitCode == 0;
		result.crashed = result.exitCode != 0;
	} else if (WIFSIGNALED(status)) {
		result.signal = WTERMSIG(status);
		result.crashed = true;
	}
	return result;
}

inline std::string Describe(const IsolatedResult& r)
{
	std::string s = r.timedOut ? "TIMED OUT" : r.finished ? "finished" : r.signal ? "killed by signal " + std::to_string(r.signal)
		: "exit code " + std::to_string(r.exitCode);
	return s + " | " + r.text;
}

} // namespace edge_media
