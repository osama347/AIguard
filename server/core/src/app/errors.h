#pragma once
#include <stdexcept>
#include <string>

// Application errors; the API layer maps each to an HTTP status.
namespace guard {

struct ValidationError : std::runtime_error {      // 400
    using std::runtime_error::runtime_error;
};
struct UnauthorizedError : std::runtime_error {    // 401
    using std::runtime_error::runtime_error;
};
struct ForbiddenError : std::runtime_error {       // 403
    using std::runtime_error::runtime_error;
};
struct NotFoundError : std::runtime_error {        // 404
    using std::runtime_error::runtime_error;
};
struct ConflictError : std::runtime_error {        // 409
    using std::runtime_error::runtime_error;
};
struct UnavailableError : std::runtime_error {     // 503
    using std::runtime_error::runtime_error;
};

} // namespace guard
