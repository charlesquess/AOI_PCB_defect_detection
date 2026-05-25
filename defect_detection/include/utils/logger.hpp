#pragma once

#include <string>
#include <memory>
#include <vector>
#include <deque>
#include <mutex>

enum class LogLevel {
    Debug,  /* 调试信息 */
    Info,   /* 常规信息 */
    Warn,   /* 警告 */
    Error,  /* 错误 */
    Fatal   /* 致命错误 */
};

/* 单条日志记录 */
struct LogEntry {
    LogLevel level;
    std::string timestamp;
    std::string message;
};

/* 日志记录器 - 单例模式，同时输出到控制台和文件 */
class Logger {
public:
    static Logger& instance();

    void set_level(LogLevel level);
    void set_log_file(const std::string& filepath);

    void debug(const std::string& message);
    void info(const std::string& message);
    void warn(const std::string& message);
    void error(const std::string& message);
    void fatal(const std::string& message);

    void log(LogLevel level, const std::string& message);

    /* UI 用：获取最近 N 条日志 */
    std::vector<LogEntry> recent_logs(size_t count = 50) const;

private:
    Logger();
    ~Logger();
    class Impl;
    std::unique_ptr<Impl> impl_;
};

#define LOG_DEBUG(msg)    Logger::instance().debug(msg)
#define LOG_INFO(msg)     Logger::instance().info(msg)
#define LOG_WARN(msg)     Logger::instance().warn(msg)
#define LOG_ERROR(msg)    Logger::instance().error(msg)
#define LOG_FATAL(msg)    Logger::instance().fatal(msg)
