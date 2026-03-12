#pragma once
#include <iostream>
#include <fstream>
#include <mutex>
#include <string>
#include <ctime>
#include <sstream>

enum class LogLevel {
    Info, 
    Warning, 
    Error
};

class Logger {
public:
    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    void log(LogLevel level, const std::string& message) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        std::string levelStr;
        switch(level) {
            case LogLevel::Info: levelStr = "INFO"; break;
            case LogLevel::Warning: levelStr = "WARNING"; break;
            case LogLevel::Error: levelStr = "ERROR"; break;
        }

        std::time_t now = std::time(nullptr);
        char timeStr[100];
        std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", std::localtime(&now));

        std::stringstream ss;
        ss << "[" << timeStr << "] [" << levelStr << "] " << message;
        
        std::cout << ss.str() << std::endl;
        
        if (m_file.is_open()) {
            m_file << ss.str() << std::endl;
            m_file.flush();
        }
    }

    void setLogFile(const std::string& filename) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_file.is_open()) {
            m_file.close();
        }
        m_file.open(filename, std::ios::app);
        if (!m_file.is_open()) {
            std::cerr << "Failed to open log file: " << filename << std::endl;
        }
    }

private:
    Logger() = default;
    ~Logger() {
        if (m_file.is_open()) {
            m_file.close();
        }
    }
    
    std::mutex m_mutex;
    std::ofstream m_file;
};

#define LOG_INFO(msg) Logger::instance().log(LogLevel::Info, msg)
#define LOG_WARNING(msg) Logger::instance().log(LogLevel::Warning, msg)
#define LOG_ERROR(msg) Logger::instance().log(LogLevel::Error, msg)