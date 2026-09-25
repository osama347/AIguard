#include "app/auth_service.h"
#include "app/errors.h"
#include "infra/crypto.h"
#include <cctype>
#include <chrono>
#include <thread>

namespace guard {

namespace {

void validateUsername(const std::string& username) {
    if (username.size() < 3 || username.size() > 32)
        throw ValidationError("username must be 3-32 characters");
    for (unsigned char c : username)
        if (!std::isalnum(c) && c != '_' && c != '.' && c != '-')
            throw ValidationError("username may only contain letters, digits, '.', '_' and '-'");
}

void validatePassword(const std::string& password) {
    if (password.size() < 8 || password.size() > 128)
        throw ValidationError("password must be 8-128 characters");
}

void validateCredentials(const std::string& username, const std::string& password) {
    validateUsername(username);
    validatePassword(password);
}

void validateRole(const std::string& role) {
    if (role != "admin" && role != "guard") throw ValidationError("role must be admin or guard");
}

} // namespace

AuthService::AuthService(UserRepository& users, EventRepository& events, int sessionHours)
    : users_(users), events_(events), sessionHours_(sessionHours) {}

LoginResult AuthService::openSession(const User& user) {
    const std::string token = crypto::randomHex(32);
    users_.createSession(crypto::sha256Hex(token), user.id, sessionHours_);
    users_.purgeExpiredSessions();
    return {token, user, sessionHours_};
}

LoginResult AuthService::setup(const std::string& username, const std::string& password) {
    if (!setupRequired()) throw ConflictError("setup has already been completed");
    validateCredentials(username, password);
    User u;
    u.username = username;
    u.passwordHash = crypto::hashPassword(password);
    u.role = "admin";
    u.fullName = "Administrator";
    users_.create(u);
    events_.audit(username, "setup", "user", username);
    return openSession(*users_.byUsername(username));
}

LoginResult AuthService::login(const std::string& username, const std::string& password) {
    auto user = users_.byUsername(username);
    if (!user || !crypto::verifyPassword(password, user->passwordHash)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));   // slow down guessing
        throw UnauthorizedError("invalid username or password");
    }
    if (!user->active) throw UnauthorizedError("this account is disabled; ask an administrator");
    users_.touchLogin(user->id);
    events_.audit(username, "login", "user", username);
    return openSession(*users_.get(user->id));
}

std::optional<User> AuthService::authenticate(const std::string& token) {
    if (token.size() != 64) return std::nullopt;
    return users_.sessionUser(crypto::sha256Hex(token));
}

void AuthService::logout(const std::string& token) {
    users_.deleteSession(crypto::sha256Hex(token));
}

// ---------------------------------------------------------------- user management

User AuthService::getUser(int64_t id) {
    auto u = users_.get(id);
    if (!u) throw NotFoundError("user " + std::to_string(id) + " not found");
    return *u;
}

User AuthService::createUser(const UserInput& in, const User& actor) {
    User u;
    u.username = in.username.value_or("");
    u.fullName = in.fullName.value_or("");
    u.role = in.role.value_or("guard");
    u.active = in.active.value_or(true);
    validateCredentials(u.username, in.password.value_or(""));
    validateRole(u.role);
    if (u.fullName.size() > 80) throw ValidationError("full name is too long");
    if (users_.byUsername(u.username)) throw ConflictError("username '" + u.username + "' is taken");
    u.passwordHash = crypto::hashPassword(*in.password);
    const int64_t id = users_.create(u);
    events_.audit(actor.username, "create", "user", u.username, "role=" + u.role);
    return getUser(id);
}

User AuthService::updateUser(int64_t id, const UserInput& in, const User& actor) {
    User u = getUser(id);
    const bool wasActiveAdmin = u.isAdmin() && u.active;
    if (in.username && *in.username != u.username) throw ValidationError("usernames cannot be changed");
    if (in.fullName) {
        if (in.fullName->size() > 80) throw ValidationError("full name is too long");
        u.fullName = *in.fullName;
    }
    if (in.role) { validateRole(*in.role); u.role = *in.role; }
    if (in.active) u.active = *in.active;

    if (u.id == actor.id && !u.active) throw ConflictError("you cannot disable your own account");
    if (u.id == actor.id && !u.isAdmin()) throw ConflictError("you cannot remove your own administrator role");
    if (wasActiveAdmin && !(u.isAdmin() && u.active) && users_.countActiveAdmins() <= 1)
        throw ConflictError("at least one active administrator must remain");

    users_.update(u);
    std::string details = "role=" + u.role + (u.active ? "" : " disabled");
    if (in.password) {
        validatePassword(*in.password);
        users_.setPassword(u.id, crypto::hashPassword(*in.password));
        details += " password reset";
    }
    if (!u.active || in.password) users_.deleteSessionsFor(u.id);   // takes effect immediately
    events_.audit(actor.username, "update", "user", u.username, details);
    return getUser(id);
}

void AuthService::deleteUser(int64_t id, const User& actor) {
    User u = getUser(id);
    if (u.id == actor.id) throw ConflictError("you cannot delete your own account");
    if (u.isAdmin() && u.active && users_.countActiveAdmins() <= 1)
        throw ConflictError("at least one active administrator must remain");
    users_.remove(id);   // sessions cascade
    events_.audit(actor.username, "delete", "user", u.username);
}

void AuthService::changeOwnPassword(const User& user, const std::string& current, const std::string& next) {
    User fresh = getUser(user.id);
    if (!crypto::verifyPassword(current, fresh.passwordHash)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        throw ValidationError("current password is wrong");
    }
    validatePassword(next);
    users_.setPassword(user.id, crypto::hashPassword(next));
    events_.audit(user.username, "change_password", "user", user.username);
}

} // namespace guard
