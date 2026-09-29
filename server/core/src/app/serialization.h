#pragma once
#include "domain/access_policy.h"
#include "domain/model.h"
#include <nlohmann/json.hpp>

// JSON shapes of the public API (documented in api/openapi.yaml). Shared by
// the HTTP layer and the live event stream so both always agree.
namespace guard {

// "YYYY-MM-DD HH:MM:SS" (SQLite, UTC) -> "YYYY-MM-DDTHH:MM:SSZ"; "" -> null.
nlohmann::json isoTime(const std::string& sqliteTime);

nlohmann::json toJson(const Driver& d);
// auth_code is a credential, not a display fact: only included when includeAuthCode
// is true (admins). Regular drivers/guards get owner name/phone but not the code.
nlohmann::json toJson(const Vehicle& v, bool includeAuthCode = false);
nlohmann::json toJson(const Job& j, int queuePosition = -1);
nlohmann::json toJson(const Camera& c);
nlohmann::json toJson(const AccessEvent& e);
nlohmann::json toJson(const Alert& a);
nlohmann::json toJson(const User& u);
nlohmann::json toJson(const Decision& d, const Evidence& evidence);

} // namespace guard
