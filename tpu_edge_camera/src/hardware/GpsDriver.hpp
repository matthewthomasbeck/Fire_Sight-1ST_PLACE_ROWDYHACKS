#pragma once

#include <string>

// UART4 NMEA GPS (/dev/ttyAMA4 @ 9600). Port from Vending_Machine_Robot gps.py.

struct GpsFix {
    bool valid = false;
    double latitude = 0.0;
    double longitude = 0.0;
    double speed_knots = 0.0;
    double course_deg = 0.0;
    std::string utc_time;
    std::string date;
};

struct GpsDriver {
    std::string port_path = "/dev/ttyAMA4";
    int baud = 9600;
    bool ready = false;
    bool demo_mode = true;
    char last_error[128]{};

    GpsFix last_fix{};

    bool init(const std::string& serial_path, int serial_baud);
    bool refresh(double max_seconds = 0.2);
    void shutdown();

    static bool parse_gprmc(const std::string& line, GpsFix& out);
};
