#pragma once
#include <sstream>
#include <string>

namespace guard {

enum class LogLevel { Debug = 0, Info, Warn, Error };

void setLogLevel(const std::string& level);
void logLine(LogLevel level, const std::string& msg);

// LOG(Info) << "text" << value;  — one line, written atomically to stderr.
class LogStream {
public:
    explicit LogStream(LogLevel l) : level_(l) {}
    ~LogStream() { logLine(level_, ss_.str()); }
    template <typename T> LogStream& operator<<(const T& v) { ss_ << v; return *this; }
private:
    LogLevel level_;
    std::ostringstream ss_;
};

} // namespace guard

#define LOG(level) ::guard::LogStream(::guard::LogLevel::level)
