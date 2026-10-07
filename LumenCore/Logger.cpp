#include "Logger.h"
#include <memory.h>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <cstdlib>

namespace {
	std::atomic<int64_t> g_MaxFileBytes{ 10LL * 1024 * 1024 };
	std::atomic<int> g_FilesKept{ 3 };

	// Debug-level entries are dropped before any allocation, mutex or I/O, so per-frame debug logging is free in normal
	// operation. Set LUMEN_LOG_DEBUG=1 to enable them; checked once, not per call.
	bool DebugLoggingEnabled()
	{
		static const bool enabled = [] {
			const char* value = std::getenv("LUMEN_LOG_DEBUG");
			return value != nullptr && value[0] != '\0' && value[0] != '0';
		}();
		return enabled;
	}
}

void Logger::SetRotation(int64_t maxFileBytes, int filesKept)
{
    g_MaxFileBytes = maxFileBytes < 64 * 1024 ? 64 * 1024 : maxFileBytes;
    g_FilesKept = filesKept < 0 ? 0 : (filesKept > 20 ? 20 : filesKept);
}

int64_t Logger::GetMaxFileBytes() { return g_MaxFileBytes; }

int Logger::GetFilesKept() { return g_FilesKept; }

Logger::Logger() {
    EnterLog("Logger constructed");
}

Logger::Logger(std::string filePath) {
    m_FilePath = filePath;
    EnterLog("Logger constructed with file path: " + filePath);
}

Logger::~Logger() {
    ClearAllLogs();
}

void Logger::EnterLog(std::string message) {
    std::lock_guard<std::recursive_mutex> guard(m_ResultLock);
    m_Logs.push_back(new Log(LogLevel::Info, message));
    FlushLogs();
}

void Logger::EnterLog(LogLevel logLevel, std::string message) {
    // dropped before the lock/allocation/flush below
    if (logLevel == LogLevel::Debug && !DebugLoggingEnabled()) return;

    std::lock_guard<std::recursive_mutex> guard(m_ResultLock);
    m_Logs.push_back(new Log(logLevel, message));
    FlushLogs();
}

void Logger::EnterLog(Log* p_Log) {
    if (!p_Log) return;
    std::lock_guard<std::recursive_mutex> guard(m_ResultLock);
    m_Logs.push_back(new Log(p_Log->GetLogLevel(), p_Log->GetMessage()));
    if (m_Logs.size() > 100) {
        FlushLogs();
    }
}

void Logger::ClearAllLogs() {
    std::lock_guard<std::recursive_mutex> guard(m_ResultLock);
    for (auto p_Log : m_Logs) {
        delete p_Log;
    }
    m_Logs.clear();
}

void Logger::RotateIfNeededLocked()
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto size = fs::file_size(m_FilePath, ec);
    if (ec || static_cast<int64_t>(size) < g_MaxFileBytes) return;

    m_LogFile.close();
    const int kept = g_FilesKept;
    // name.<kept> is the oldest and goes; every other rotated file moves up one
    fs::remove(m_FilePath + "." + std::to_string(kept), ec);
    for (int i = kept - 1; i >= 1; i--) {
        fs::path from = m_FilePath + "." + std::to_string(i);
        if (fs::exists(from, ec)) fs::rename(from, m_FilePath + "." + std::to_string(i + 1), ec);
    }
    if (kept >= 1) fs::rename(m_FilePath, m_FilePath + ".1", ec);
    else fs::remove(m_FilePath, ec);
    // a fresh live file straight away, so there is always one to read
    m_LogFile.open(m_FilePath, std::ios::out | std::ios::app);
}

void Logger::TrimOversizedFileLocked()
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto size = fs::file_size(m_FilePath, ec);
    const int64_t limit = g_MaxFileBytes;
    if (ec || static_cast<int64_t>(size) <= limit) return;

    // keep the newest half of the limit, starting at a line boundary
    const int64_t keep = limit / 2;
    std::string tail;
    {
        std::ifstream in(m_FilePath, std::ios::binary);
        if (!in) return;
        in.seekg(static_cast<std::streamoff>(size) - keep);
        std::getline(in, tail); // the partial line at the cut
        tail.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    std::ofstream out(m_FilePath, std::ios::binary | std::ios::trunc);
    if (out) out << tail;
}

void Logger::FlushLogs()
{
    if (m_FilePath == "") return;

    std::lock_guard<std::recursive_mutex> guard(m_ResultLock);

    // opened once and kept open for the Logger's lifetime, not reopened on every call (rotation reopens it)
    if (!m_LogFile.is_open()) {
        if (!m_Trimmed) {
            TrimOversizedFileLocked();
            m_Trimmed = true;
        }
        m_LogFile.open(m_FilePath, std::ios::out | std::ios::app);
        if (!m_LogFile.is_open()) return;
    }

    // front-to-back (not m_Logs.back()+pop) so the file is in chronological order
    for (Log* p_Log : m_Logs) {
        m_LogFile << "[" + p_Log->GetLogLevelString() + "]: " + p_Log->GetMessage() + "\n";
        delete p_Log;
    }
    m_Logs.clear();
    // out to the file now (a crash must not lose the last lines), then rotate if that pushed it over the limit
    m_LogFile.flush();
    RotateIfNeededLocked();
}
