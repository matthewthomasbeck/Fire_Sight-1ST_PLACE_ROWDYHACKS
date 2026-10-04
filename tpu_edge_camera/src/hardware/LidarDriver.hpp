#pragma once

#include <cstdint>
#include <string>

// LD19 UART @ 230400 8N1. Port from C_Hardware_Testing LidarDriver.

struct LidarDriver {
    static constexpr int bin_count = 720;
    static constexpr int packet_bytes = 47;
    static constexpr int points_per_packet = 12;
    static constexpr int baud_default = 230400;

    uint16_t distance_mm[bin_count]{};
    uint8_t intensity[bin_count]{};

    float speed_dps = 0.0f;
    int valid_bins = 0;
    int packets_ok = 0;
    int packets_bad = 0;
    int frame_index = 0;
    bool ready = false;
    bool demo_mode = true;
    int baud = baud_default;
    char port_path[64]{};
    char last_error[128]{};

    bool init(const std::string& serial_path, int serial_baud);
    bool refresh();
    void shutdown();
};
