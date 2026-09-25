#include "common/log.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace guard {

namespace {
std::atomic<int> gLevel{static_cast<int>(LogLevel::Info)};
std::mutex gMutex;
const char* kNames[] = {"DEBUG", "INFO ", "WARN ", "ERROR"};
}

void setLogLevel(const std::string& level) {
    if (level == "debug") gLevel = 0;
    else if (level == "warn") gLevel = 2;
    else if (level == "error") gLevel = 3;
    else gLevel = 1;
}

void logLine(LogLevel level, const std::string& msg) {
    if (static_cast<int>(level) < gLevel) return;
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    char ts[32];
    std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);
    std::lock_guard<std::mutex> lk(gMutex);
    std::fprintf(stderr, "[%s] %s %s\n", ts, kNames[static_cast<int>(level)], msg.c_str());
}

} // namespace guard
