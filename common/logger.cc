#include "common/logger.h"

namespace fileserver {

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

Logger::~Logger() {
    if (file_stream_.is_open()) {
        file_stream_.close();
    }
}

void Logger::SetLogFile(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_stream_.is_open()) {
        file_stream_.close();
    }
    file_stream_.open(filepath, std::ios::out | std::ios::app);
    use_file_ = file_stream_.is_open();
    if (!use_file_) {
        std::cerr << "[Logger] 无法打开日志文件: " << filepath << std::endl;
    }
}

void Logger::Log(LogLevel level, const char* file, int line,
                 const char* func, const std::string& message) {
    // 级别过滤：低于当前级别的日志不输出
    if (level < level_) return;

    // 构造时间戳（精确到毫秒）
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) % 1000;

    // 格式化日志行
    // 格式: 时间 [级别] [线程ID] [文件:行号 函数名] 消息
    std::ostringstream line_buf;
    line_buf << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S")
             << '.' << std::setw(3) << std::setfill('0') << ms.count()
             << " [" << LevelToStr(level) << "] "
             << "[" << std::this_thread::get_id() << "] "
             << "[" << file << ":" << line << " " << func << "] "
             << message << "\n";

    std::string output = line_buf.str();

    std::lock_guard<std::mutex> lock(mutex_);

    // 控制台输出：Error及以上输出到stderr，其余输出到stdout
    if (level >= LogLevel::kError) {
        std::cerr << output;
    } else {
        std::cout << output;
    }
    std::cout.flush();

    // 文件输出
    if (use_file_ && file_stream_.is_open()) {
        file_stream_ << output;
        file_stream_.flush();
    }
}

const char* Logger::LevelToStr(LogLevel level) const {
    switch (level) {
        case LogLevel::kDebug:   return "DEBUG";
        case LogLevel::kInfo:    return "INFO ";
        case LogLevel::kWarning: return "WARN ";
        case LogLevel::kError:   return "ERROR";
        case LogLevel::kFatal:   return "FATAL";
        default:                 return "?????";
    }
}

}  // namespace fileserver