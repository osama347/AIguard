#include "ai/logging.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <mutex>

namespace ai {

namespace {
std::mutex gLogMutex;

std::string levelName(LogLevel l) {
    switch (l) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "?";
}

std::string timestamp() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto t = system_clock::to_time_t(now);
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec, (int)ms);
    return buf;
}
}

LogLevel logLevelFromString(const std::string& s) {
    if (s == "debug") return LogLevel::Debug;
    if (s == "warn")  return LogLevel::Warn;
    if (s == "error") return LogLevel::Error;
    return LogLevel::Info;
}

void Logger::log(LogLevel l, const std::string& component, const std::string& msg) {
    if (static_cast<int>(l) < static_cast<int>(level_)) return;
    std::lock_guard<std::mutex> lock(gLogMutex);
    if (json_) {
        std::string lvl = levelName(l);
        // minimal JSON escaping for the message
        std::string esc = msg;
        // crude escape for quotes/backslash/newline
        std::string out;
        out.reserve(esc.size() + 8);
        for (char c : esc) {
            if (c == '"' || c == '\\') { out.push_back('\\'); out.push_back(c); }
            else if (c == '\n') { out += "\\n"; }
            else out.push_back(c);
        }
        std::printf("{\"ts\":\"%s\",\"level\":\"%s\",\"component\":\"%s\",\"msg\":\"%s\"}\n",
                    timestamp().c_str(), lvl.c_str(), component.c_str(), out.c_str());
    } else {
        std::printf("[%s] %-5s [%s] %s\n", timestamp().c_str(), levelName(l).c_str(),
                    component.c_str(), msg.c_str());
    }
    std::fflush(stdout);
}

void Logger::shutdown() {
    std::lock_guard<std::mutex> lock(gLogMutex);
    std::fflush(stdout);
    std::fflush(stderr);
}

} // namespace ai
