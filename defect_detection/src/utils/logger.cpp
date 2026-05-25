#include "utils/logger.hpp"
#include <iostream>
#include <fstream>
#include <ctime>
#include <iomanip>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#endif

static const size_t MAX_LOG_BUFFER = 200;

/* UTF-8 转当前终端编码 (Windows) */
static std::string utf8_to_console(const std::string& utf8) {
#ifdef _WIN32
    if (utf8.empty()) return utf8;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return utf8;
    std::wstring wstr(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wstr[0], wlen);
    UINT cp = GetConsoleOutputCP();
    int clen = WideCharToMultiByte(cp, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (clen <= 0) return utf8;
    std::string conv(clen, '\0');
    WideCharToMultiByte(cp, 0, wstr.c_str(), -1, &conv[0], clen, nullptr, nullptr);
    conv.resize(clen - 1);
    return conv;
#else
    return utf8;
#endif
}

/* 日志记录器内部实现 */
class Logger::Impl {
public:
    Impl() : level_(LogLevel::Info) {}

    void set_level(LogLevel level) { level_ = level; }

    void set_log_file(const std::string& filepath) {
        file_stream_.open(filepath, std::ios::app);
    }

    void log(LogLevel level, const std::string& message) {
        if (level < level_) return;

        std::string level_str;
        switch (level) {
            case LogLevel::Debug: level_str = "DEBUG"; break;
            case LogLevel::Info:  level_str = "INFO";  break;
            case LogLevel::Warn:  level_str = "WARN";  break;
            case LogLevel::Error: level_str = "ERROR"; break;
            case LogLevel::Fatal: level_str = "FATAL"; break;
        }

        auto now = std::time(nullptr);
        std::tm tm;
        localtime_s(&tm, &now);
        std::ostringstream oss;
        oss << "[" << std::put_time(&tm, "%H:%M:%S")
            << "] [" << level_str << "] " << message;
        std::string formatted = oss.str();

        /* 控制台 */
        std::cout << utf8_to_console(formatted) << std::endl;

        /* 文件 */
        if (file_stream_.is_open()) {
            file_stream_ << formatted << std::endl;
        }

        /* 环缓冲区 (UI 用) */
        {
            std::lock_guard<std::mutex> lock(buf_mutex_);
            LogEntry entry;
            entry.level = level;
            std::ostringstream ts;
            ts << std::put_time(&tm, "%H:%M:%S");
            entry.timestamp = ts.str();
            entry.message = message;
            log_buffer_.push_back(std::move(entry));
            if (log_buffer_.size() > MAX_LOG_BUFFER) {
                log_buffer_.pop_front();
            }
        }
    }

    std::vector<LogEntry> recent_logs(size_t count) const {
        std::lock_guard<std::mutex> lock(buf_mutex_);
        std::vector<LogEntry> result;
        size_t start = (log_buffer_.size() > count)
            ? log_buffer_.size() - count : 0;
        for (size_t i = start; i < log_buffer_.size(); ++i) {
            result.push_back(log_buffer_[i]);
        }
        return result;
    }

private:
    LogLevel level_;
    std::ofstream file_stream_;
    std::deque<LogEntry> log_buffer_;
    mutable std::mutex buf_mutex_;
};

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::set_level(LogLevel level) { impl_->set_level(level); }
void Logger::set_log_file(const std::string& fp) { impl_->set_log_file(fp); }
void Logger::debug(const std::string& m) { impl_->log(LogLevel::Debug, m); }
void Logger::info(const std::string& m)  { impl_->log(LogLevel::Info, m); }
void Logger::warn(const std::string& m)  { impl_->log(LogLevel::Warn, m); }
void Logger::error(const std::string& m) { impl_->log(LogLevel::Error, m); }
void Logger::fatal(const std::string& m) { impl_->log(LogLevel::Fatal, m); }
void Logger::log(LogLevel l, const std::string& m) { impl_->log(l, m); }
std::vector<LogEntry> Logger::recent_logs(size_t c) const { return impl_->recent_logs(c); }

Logger::Logger() : impl_(std::make_unique<Impl>()) {}
Logger::~Logger() = default;
