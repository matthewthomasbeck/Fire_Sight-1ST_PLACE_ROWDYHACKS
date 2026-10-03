#include "hardware/CameraDriver.hpp"

#include "util/Log.hpp"

#include <cstdio>

bool CameraDriver::init() {
    logutil::warn("CameraDriver::init not implemented");
    ready = true;
    demo_mode = true;
    std::snprintf(last_error, sizeof(last_error), "not implemented");
    return true;
}

bool CameraDriver::refresh() {
    if (!ready) {
        return false;
    }
    ++frame_index;
    return true;
}

void CameraDriver::shutdown() {
    ready = false;
}
