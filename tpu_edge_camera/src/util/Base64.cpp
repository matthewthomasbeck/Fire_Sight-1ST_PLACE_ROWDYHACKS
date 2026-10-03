#include "util/Base64.hpp"

namespace base64 {

namespace {

constexpr char k_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

} // namespace

std::string encode(const std::uint8_t* data, std::size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);

    std::size_t i = 0;
    while (i + 2 < len) {
        const std::uint32_t n =
            (static_cast<std::uint32_t>(data[i]) << 16)
            | (static_cast<std::uint32_t>(data[i + 1]) << 8)
            | static_cast<std::uint32_t>(data[i + 2]);
        out.push_back(k_table[(n >> 18) & 63]);
        out.push_back(k_table[(n >> 12) & 63]);
        out.push_back(k_table[(n >> 6) & 63]);
        out.push_back(k_table[n & 63]);
        i += 3;
    }

    if (i < len) {
        std::uint32_t n = static_cast<std::uint32_t>(data[i]) << 16;
        out.push_back(k_table[(n >> 18) & 63]);
        if (i + 1 < len) {
            n |= static_cast<std::uint32_t>(data[i + 1]) << 8;
            out.push_back(k_table[(n >> 12) & 63]);
            out.push_back(k_table[(n >> 6) & 63]);
            out.push_back('=');
        } else {
            out.push_back(k_table[(n >> 12) & 63]);
            out.push_back('=');
            out.push_back('=');
        }
    }

    return out;
}

std::string encode(const std::vector<std::uint8_t>& bytes) {
    return encode(bytes.data(), bytes.size());
}

} // namespace base64
