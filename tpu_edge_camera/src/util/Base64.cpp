#include "util/Base64.hpp"

#include "util/Log.hpp"

namespace base64 {

std::string encode(const std::uint8_t* /*data*/, std::size_t /*len*/) {
    logutil::warn("base64::encode not implemented");
    return {};
}

std::string encode(const std::vector<std::uint8_t>& bytes) {
    return encode(bytes.data(), bytes.size());
}

} // namespace base64
