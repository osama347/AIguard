#pragma once
#include "infra/repositories.h"
#include <optional>
#include <string>

namespace guard {

struct LoginResult {
    std::string token;          // bearer token; only its SHA-256 is stored
    User user;
    int expiresInHours = 0;
};

struct UserInput {
    std::optional<std::string> username, fullName, password, role;
    std::optional<bool> active;
};

// ---------------------------------------------------------------------------
// AuthService: no default credentials. The first admin is created through a
// one-time setup call (only while no user exists), then bearer-token sessions.
//
// Users are "admin" or "guard". Admins manage users; the system always keeps
// at least one active admin, and nobody can delete or disable themselves.
// Disabling a user or resetting their password ends their sessions.
// ---------------------------------------------------------------------------
class AuthService {
public:
    AuthService(UserRepository& users, EventRepository& events, int sessionHours);

    std::vector<User> listUsers() { return users_.list(); }
    User getUser(int64_t id);
    User createUser(const UserInput& in, const User& actor);
    User updateUser(int64_t id, const UserInput& in, const User& actor);
    void deleteUser(int64_t id, const User& actor);
    void changeOwnPassword(const User& user, const std::string& current, const std::string& next);

    bool setupRequired() { return users_.count() == 0; }
    LoginResult setup(const std::string& username, const std::string& password);
    LoginResult login(const std::string& username, const std::string& password);
    std::optional<User> authenticate(const std::string& token);
    void logout(const std::string& token);

private:
    LoginResult openSession(const User& user);

    UserRepository& users_;
    EventRepository& events_;
    int sessionHours_;
};

} // namespace guard
