#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Minimal self-contained crypto for credentials (no OpenSSL dependency):
// SHA-256 (FIPS 180-4), HMAC-SHA256 (RFC 2104), PBKDF2-HMAC-SHA256 (RFC 8018).
namespace guard::crypto {

std::vector<uint8_t> sha256(const uint8_t* data, std::size_t len);
std::string sha256Hex(const std::string& data);

std::vector<uint8_t> pbkdf2Sha256(const std::string& password, const std::vector<uint8_t>& salt,
                                  uint32_t iterations, std::size_t keyLen);

// Cryptographically secure random bytes from /dev/urandom.
std::vector<uint8_t> randomBytes(std::size_t n);
std::string randomHex(std::size_t bytes);

std::string toHex(const std::vector<uint8_t>& v);

// "pbkdf2-sha256$<iterations>$<salt hex>$<hash hex>"
std::string hashPassword(const std::string& password);
bool verifyPassword(const std::string& password, const std::string& encoded);

} // namespace guard::crypto
