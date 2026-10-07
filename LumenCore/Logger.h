#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <mutex>
#include <memory>
#include <atomic>
#include <cstdint>
#include <fstream>

enum class LogLevel {
    Info,
    Warning,
    Debug,
    Error,
    Wtf
};

class Log {
public:
    Log(LogLevel logLevel, std::string message)
        : m_LogLevel(logLevel), m_Message(message) {}
    LogLevel GetLogLevel() { return m_LogLevel; }
    std::string GetMessage() { return m_Message; }
    std::string GetLogLevelString() {
        switch (m_LogLevel) {
            case LogLevel::Info: return "INFO";
            case LogLevel::Warning: return "WARNING";
            case LogLevel::Debug: return "DEBUG";
            case LogLevel::Error: return "ERROR";
            case LogLevel::Wtf: return "WTF";
            default: return "UNKNOWN";
        }
    }
private:
    LogLevel m_LogLevel;
    std::string m_Message;
};

class Logger {
public:
	// How big a log file may grow before it is rotated (name -> name.1 -> name.2 ...), and how many rotated files are kept. Shared by every
	// Logger, adjustable at any time; the defaults keep a file under 10 MB and three older ones (about 40 MB per log in total).
	static void SetRotation(int64_t maxFileBytes, int filesKept);
	static int64_t GetMaxFileBytes();
	static int GetFilesKept();

	// Logging is a file, never stdout: a log file that has grown past the size limit before this process started (one written before rotation existed)
	// is cut down to its newest lines when the Logger first opens it.
    Logger();
    Logger(std::string filePath);
    ~Logger();
    void EnterLog(std::string message);
    void EnterLog(LogLevel logLevel, std::string message);
    void EnterLog(Log* p_Log);
    void ClearAllLogs();
    void FlushLogs();
private:
    // closes the file and shifts name -> name.1 -> ... once it is over the size limit; the caller holds m_ResultLock
    void RotateIfNeededLocked();
    void TrimOversizedFileLocked();

    std::string m_FilePath;
    bool m_Trimmed = false;
    std::recursive_mutex m_ResultLock;
    std::vector<Log*> m_Logs;
    // opened once (append mode) rather than on every FlushLogs() call
    std::ofstream m_LogFile;
};

