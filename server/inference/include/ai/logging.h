#pragma once
#include <string>
#include <sstream>

namespace ai {

enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3 };

// Parse a level string ("debug"/"info"/"warn"/"error") into a LogLevel.
LogLevel logLevelFromString(const std::string& s);

// Global logger. Defaults to Info on stderr. Not thread-safe for reconfig at
// runtime, but logs from multiple threads write atomically line-by-line under
// a mutex. Designed to be cheap in the hot path.
class Logger {
public:
    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    void setLevel(LogLevel l) { level_ = l; }
    void setJson(bool j) { json_ = j; }
    LogLevel level() const { return level_; }

    void log(LogLevel l, const std::string& component, const std::string& msg);
    void shutdown();

private:
    Logger() = default;
    LogLevel level_ = LogLevel::Info;
    bool json_ = false;
};

// Stream-style logger: LOG(Info) << "msg" << val;
class LogStream {
public:
    LogStream(LogLevel l, const char* component) : level_(l), component_(component) {}
    ~LogStream() { Logger::instance().log(level_, component_, ss_.str()); }

    template <typename T>
    LogStream& operator<<(const T& v) { ss_ << v; return *this; }

private:
    LogLevel level_;
    std::string component_;
    std::ostringstream ss_;
};

} // namespace ai

#define LOG_DEBUG ::ai::LogStream(::ai::LogLevel::Debug, "app")
#define LOG_INFO  ::ai::LogStream(::ai::LogLevel::Info,  "app")
#define LOG_WARN  ::ai::LogStream(::ai::LogLevel::Warn,  "app")
#define LOG_ERROR ::ai::LogStream(::ai::LogLevel::Error, "app")
