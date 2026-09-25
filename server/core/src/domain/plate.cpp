#include "domain/plate.h"
#include <cctype>

namespace guard {

std::string normalizePlate(const std::string& plate) {
    std::string out;
    out.reserve(plate.size());
    for (unsigned char c : plate) {
        if (std::isalnum(c)) out.push_back(static_cast<char>(std::toupper(c)));
    }
    return out;
}

namespace {
constexpr size_t kMinLetters = 2, kMaxLetters = 3, kMinDigits = 1, kMaxDigits = 4;
bool isLetter(char c) { return c >= 'A' && c <= 'Z'; }
bool isDigit(char c)  { return c >= '0' && c <= '9'; }
} // namespace

bool isPlausiblePlate(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && isLetter(s[i])) ++i;
    const size_t letters = i;
    while (i < s.size() && isDigit(s[i])) ++i;
    const size_t digits = s.size() - letters;
    return i == s.size() && letters >= kMinLetters && letters <= kMaxLetters &&
           digits >= kMinDigits && digits <= kMaxDigits;
}

} // namespace guard
