#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>

namespace guard {

// Minimal RFC 4648 base64 (with padding). Used to transport raw float
// embeddings as JSON strings so float32 values round-trip bit-exactly.
// Header-only: definitions are inline.

namespace detail {
constexpr char kB64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
} // namespace detail

inline std::string base64Encode(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = 0;
        int rem = 0;
        for (int j = 0; j < 3; ++j) {
            if (i + j < len) {
                n = (n << 8) | data[i + j];
                ++rem;
            } else {
                n <<= 8;
            }
        }
        out += detail::kB64Table[(n >> 18) & 0x3f];
        out += detail::kB64Table[(n >> 12) & 0x3f];
        out += rem > 1 ? detail::kB64Table[(n >> 6) & 0x3f] : '=';
        out += rem > 2 ? detail::kB64Table[n & 0x3f] : '=';
    }
    return out;
}

inline std::string base64Encode(const std::vector<uint8_t>& v) {
    return base64Encode(v.data(), v.size());
}

inline std::vector<uint8_t> base64Decode(const std::string& in) {
    std::vector<uint8_t> out;
    if (in.empty()) return out;
    auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };

    out.reserve((in.size() / 4) * 3);
    uint32_t acc = 0;
    int bits = 0;
    for (char c : in) {
        if (c == '=' || c == '\n' || c == '\r') continue;  // padding / whitespace
        int v = value(c);
        if (v < 0) continue;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((acc >> bits) & 0xff));
        }
    }
    return out;
}

// Convenience: encode a vector<float> as base64 over its raw bytes.
inline std::string embedToBase64(const std::vector<float>& emb) {
    if (emb.empty()) return "";
    std::vector<uint8_t> raw(emb.size() * sizeof(float));
    std::memcpy(raw.data(), emb.data(), raw.size());
    return base64Encode(raw);
}

inline std::vector<float> base64ToEmbed(const std::string& b64) {
    std::vector<uint8_t> raw = base64Decode(b64);
    if (raw.size() % sizeof(float) != 0) {
        raw.resize((raw.size() / sizeof(float)) * sizeof(float));
    }
    std::vector<float> out(raw.size() / sizeof(float));
    if (!raw.empty()) std::memcpy(out.data(), raw.data(), raw.size());
    return out;
}

} // namespace guard