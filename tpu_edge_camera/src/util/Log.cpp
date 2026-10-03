#include "util/Log.hpp"

#include <iostream>

namespace logutil {

void info(const std::string& msg) {
    std::cout << "[INFO] " << msg << '\n';
}

void warn(const std::string& msg) {
    std::cerr << "[WARN] " << msg << '\n';
}

void error(const std::string& msg) {
    std::cerr << "[ERROR] " << msg << '\n';
}

void debug(const std::string& msg) {
    std::cout << "[DEBUG] " << msg << '\n';
}

} // namespace logutil
