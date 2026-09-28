#include "Logger.h"
#include <memory.h>
#include <fstream>
#include <iostream>
#include <cstdlib>

namespace {
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
    std::cout << "[INFO]: " << message << "\n";
}

void Logger::EnterLog(LogLevel logLevel, std::string message) {
    // dropped before the lock/allocation/flush below
    if (logLevel == LogLevel::Debug && !DebugLoggingEnabled()) return;

    std::lock_guard<std::recursive_mutex> guard(m_ResultLock);
    m_Logs.push_back(new Log(logLevel, message));
    FlushLogs();
    std::cout << "[" << static_cast<int>(logLevel) << "]: " << message << "\n";
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

void Logger::FlushLogs()
{
    if (m_FilePath == "") return;

    std::lock_guard<std::recursive_mutex> guard(m_ResultLock);

    // opened once and kept open for the Logger's lifetime, not reopened on every call
    if (!m_LogFile.is_open()) {
        m_LogFile.open(m_FilePath, std::ios::out | std::ios::app);
        if (!m_LogFile.is_open()) return;
    }

    // front-to-back (not m_Logs.back()+pop) so the file is in chronological order
    for (Log* p_Log : m_Logs) {
        m_LogFile << "[" + p_Log->GetLogLevelString() + "]: " + p_Log->GetMessage() + "\n";
        delete p_Log;
    }
    m_Logs.clear();
}
