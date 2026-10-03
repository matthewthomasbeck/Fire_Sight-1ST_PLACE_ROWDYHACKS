#include "hardware/LidarDriver.hpp"

#include "util/Log.hpp"

#include <cstdio>

bool LidarDriver::init() {
    logutil::warn("LidarDriver::init not implemented");
    ready = true;
    demo_mode = true;
    std::snprintf(port_path, sizeof(port_path), "none");
    std::snprintf(last_error, sizeof(last_error), "not implemented");
    return true;
}

bool LidarDriver::refresh() {
    if (!ready) {
        return false;
    }
    ++frame_index;
    return true;
}

void LidarDriver::shutdown() {
    ready = false;
    port_path[0] = '\0';
}
