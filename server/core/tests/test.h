#pragma once
// Minimal test harness: TEST(name) { CHECK(cond); CHECK_EQ(a, b); }
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace test {

struct Case { const char* name; std::function<void()> fn; };
std::vector<Case>& registry();
struct Failure { std::string msg; };

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

} // namespace test

#define TEST(name)                                                  \
    static void name();                                             \
    static test::Registrar name##_reg(#name, name);                 \
    static void name()

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            std::ostringstream os_;                                                  \
            os_ << __FILE__ << ":" << __LINE__ << ": CHECK(" #cond ") failed";       \
            throw test::Failure{os_.str()};                                          \
        }                                                                            \
    } while (0)

#define CHECK_EQ(a, b)                                                                          \
    do {                                                                                        \
        auto va_ = (a);                                                                         \
        auto vb_ = (b);                                                                         \
        if (!(va_ == vb_)) {                                                                    \
            std::ostringstream os_;                                                             \
            os_ << __FILE__ << ":" << __LINE__ << ": " #a " == " #b " failed: [" << va_          \
                << "] vs [" << vb_ << "]";                                                      \
            throw test::Failure{os_.str()};                                                     \
        }                                                                                       \
    } while (0)

#define CHECK_THROWS(expr, type)                                                     \
    do {                                                                             \
        bool thrown_ = false;                                                        \
        try { expr; } catch (const type&) { thrown_ = true; }                        \
        if (!thrown_) throw test::Failure{std::string(__FILE__) + ":" +              \
                                          std::to_string(__LINE__) + ": expected " #type}; \
    } while (0)
