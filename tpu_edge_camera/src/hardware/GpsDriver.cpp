#include "hardware/GpsDriver.hpp"

#include "util/Log.hpp"

#include <cstdio>

bool GpsDriver::init(const std::string& serial_path, int serial_baud) {
    port_path = serial_path;
    baud = serial_baud;
    logutil::warn("GpsDriver::init not implemented path=" + port_path);
    ready = true;
    demo_mode = true;
    std::snprintf(last_error, sizeof(last_error), "not implemented");
    return true;
}

bool GpsDriver::refresh(double /*max_seconds*/) {
    if (!ready) {
        return false;
    }
    return last_fix.valid;
}

void GpsDriver::shutdown() {
    ready = false;
}

bool GpsDriver::parse_gprmc(const std::string& /*line*/, GpsFix& /*out*/) {
    return false;
}
