#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <mutex>
#include <memory>
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
    Logger();
    Logger(std::string filePath);
    ~Logger();
    void EnterLog(std::string message);
    void EnterLog(LogLevel logLevel, std::string message);
    void EnterLog(Log* p_Log);
    void ClearAllLogs();
    void FlushLogs();
private:
    std::string m_FilePath;
    std::recursive_mutex m_ResultLock;
    std::vector<Log*> m_Logs;
    // opened once (append mode) rather than on every FlushLogs() call
    std::ofstream m_LogFile;
};

