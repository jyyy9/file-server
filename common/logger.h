#ifndef FILESERVER_COMMON_LOGGER_H_
#define FILESERVER_COMMON_LOGGER_H_

#include <string>
#include <sstream>
#include <fstream>
#include <iostream>
#include <mutex>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <thread>

#include "common/common.h"

namespace fileserver {

// ── 日志级别 ─────────────────────────────────────────────────────
enum class LogLevel {
    kDebug   = 0,   // 调试信息
    kInfo    = 1,   // 一般信息
    kWarning = 2,   // 警告
    kError   = 3,   // 错误
    kFatal   = 4,   // 致命错误
};

// ── 日志单例 ─────────────────────────────────────────────────────
// 线程安全，支持同时输出到控制台和文件
class Logger {
public:
    // 获取单例实例
    static Logger& Instance();

    // 设置日志级别，低于此级别的消息将不输出
    void SetLevel(LogLevel level) { level_ = level; }

    // 设置日志输出文件
    void SetLogFile(const std::string& filepath);

    // 输出一条日志记录
    // level: 日志级别
    // file:  源文件名
    // line:  行号
    // func:  函数名
    // message: 日志内容
    void Log(LogLevel level, const char* file, int line,
             const char* func, const std::string& message);

private:
    Logger() = default;
    ~Logger();

    FILESERVER_DISALLOW_COPY_AND_MOVE(Logger);

    // 日志级别转字符串
    const char* LevelToStr(LogLevel level) const;

    LogLevel level_{LogLevel::kDebug};   // 当前日志级别
    std::mutex mutex_;                    // 线程安全锁
    std::ofstream file_stream_;           // 文件输出流
    bool use_file_{false};                // 是否启用文件输出
};

// ── 日志宏（便捷接口）────────────────────────────────────────────
#define LOG_DEBUG(msg)    fileserver::Logger::Instance().Log( \
    fileserver::LogLevel::kDebug, __FILE__, __LINE__, __func__, msg)

#define LOG_INFO(msg)     fileserver::Logger::Instance().Log( \
    fileserver::LogLevel::kInfo, __FILE__, __LINE__, __func__, msg)

#define LOG_WARNING(msg)  fileserver::Logger::Instance().Log( \
    fileserver::LogLevel::kWarning, __FILE__, __LINE__, __func__, msg)

#define LOG_ERROR(msg)    fileserver::Logger::Instance().Log( \
    fileserver::LogLevel::kError, __FILE__, __LINE__, __func__, msg)

#define LOG_FATAL(msg)    fileserver::Logger::Instance().Log( \
    fileserver::LogLevel::kFatal, __FILE__, __LINE__, __func__, msg)

// ── 流式日志宏 ───────────────────────────────────────────────────
// 用法: LOG_STREAM(Info) << "message: " << value;
#define LOG_STREAM(level) \
    fileserver::LogStream(fileserver::LogLevel::k##level, __FILE__, __LINE__, __func__).Stream()

// 流式日志辅助类，析构时自动输出拼接好的日志
class LogStream {
public:
    LogStream(LogLevel level, const char* file, int line, const char* func)
        : level_(level), file_(file), line_(line), func_(func) {}

    ~LogStream() {
        Logger::Instance().Log(level_, file_, line_, func_, ss_.str());
    }

    std::ostream& Stream() { return ss_; }

private:
    LogLevel level_;
    const char* file_;
    int line_;
    const char* func_;
    std::ostringstream ss_;
};

}  // namespace fileserver

#endif  // FILESERVER_COMMON_LOGGER_H_