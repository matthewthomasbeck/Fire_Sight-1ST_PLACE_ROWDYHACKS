#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "hardware/LidarDriver.hpp"

// Polar scan payload for dispatch /ingest/lidar.

struct LidarMapPayload {
    int bin_count = LidarDriver::bin_count;
    std::vector<std::uint16_t> distance_mm;
    std::vector<std::uint8_t> intensity;
    int valid_bins = 0;
    float speed_dps = 0.0f;
};

struct LidarMap {
    bool init();
    void shutdown();

    bool snapshot(const LidarDriver& lidar, LidarMapPayload& out);
    bool to_json(const LidarMapPayload& map, const std::string& stream_id, std::string& json_out);
    bool render_hud(const LidarMapPayload& map, int size_px, std::vector<std::uint8_t>& rgb_out);
};
