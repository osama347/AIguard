#pragma once
#include <string>

namespace guard {

// Canonical plate form used for storage and matching: uppercase A-Z / 0-9
// only ("abc-12 3" -> "ABC123"). The OCR model emits the same alphabet.
std::string normalizePlate(const std::string& plate);

// Pakistani plates are 2-3 letters followed by 1-4 digits (FB393, LEE9876,
// ABC1234). True if `normalized` has exactly that shape.
bool isPlausiblePlate(const std::string& normalized);

} // namespace guard
