#include "hardware/ThermalDriver.hpp"

#include "util/Log.hpp"

bool ThermalDriver::init() {
    logutil::warn("ThermalDriver::init not implemented");
    ready = true;
    demo_mode = true;
    return true;
}

bool ThermalDriver::refresh() {
    if (!ready) {
        return false;
    }
    ++frame_index;
    return true;
}

void ThermalDriver::shutdown() {
    ready = false;
}
