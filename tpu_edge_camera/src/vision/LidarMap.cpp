#include "vision/LidarMap.hpp"

#include "util/Log.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>

bool LidarMap::init() {
    return true;
}

void LidarMap::shutdown() {}

bool LidarMap::snapshot(const LidarDriver& lidar, LidarMapPayload& out) {
    out.bin_count = LidarDriver::bin_count;
    out.distance_mm.assign(
        lidar.distance_mm,
        lidar.distance_mm + LidarDriver::bin_count
    );
    out.intensity.assign(
        lidar.intensity,
        lidar.intensity + LidarDriver::bin_count
    );
    out.valid_bins = lidar.valid_bins;
    out.speed_dps = lidar.speed_dps;
    return true;
}

bool LidarMap::to_json(
    const LidarMapPayload& map,
    const std::string& lidar_id,
    std::string& json_out
) {
    if (map.bin_count <= 0
        || static_cast<int>(map.distance_mm.size()) < map.bin_count
        || static_cast<int>(map.intensity.size()) < map.bin_count) {
        logutil::warn("LidarMap::to_json incomplete scan");
        json_out.clear();
        return false;
    }

    std::ostringstream body;
    body << "{\"lidar_id\":\"";
    for (char ch : lidar_id) {
        if (ch == '"' || ch == '\\') {
            body << '\\';
        }
        body << ch;
    }
    body << "\",\"bin_count\":" << map.bin_count
         << ",\"valid_bins\":" << map.valid_bins
         << ",\"speed_dps\":" << map.speed_dps
         << ",\"distance_mm\":[";

    for (int i = 0; i < map.bin_count; ++i) {
        if (i > 0) {
            body << ',';
        }
        body << map.distance_mm[static_cast<size_t>(i)];
    }

    body << "],\"intensity\":[";
    for (int i = 0; i < map.bin_count; ++i) {
        if (i > 0) {
            body << ',';
        }
        body << static_cast<int>(map.intensity[static_cast<size_t>(i)]);
    }
    body << "]}";

    json_out = body.str();
    return true;
}

bool LidarMap::render_hud(
    const LidarMapPayload& map,
    int size_px,
    std::vector<std::uint8_t>& rgb_out
) {
    // Cheap polar point plot (O(bins)), not a filled atan2 raster.
    // Full-disk cell shading is too heavy for the IR/HUD loop on Pi.
    if (size_px < 8) {
        size_px = 8;
    }
    if (map.bin_count <= 0
        || static_cast<int>(map.distance_mm.size()) < map.bin_count) {
        rgb_out.clear();
        return false;
    }

    const int n = size_px * size_px * 3;
    rgb_out.assign(static_cast<size_t>(n), 0);

    const float range_mm = 4000.0f;
    const float cx = (size_px - 1) * 0.5f;
    const float cy = (size_px - 1) * 0.5f;
    const float radius = std::min(cx, cy);

    auto put_rgb = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
        if (x < 0 || y < 0 || x >= size_px || y >= size_px) {
            return;
        }
        const int di = (y * size_px + x) * 3;
        rgb_out[static_cast<size_t>(di)] = r;
        rgb_out[static_cast<size_t>(di + 1)] = g;
        rgb_out[static_cast<size_t>(di + 2)] = b;
    };

    for (int bin = 0; bin < map.bin_count; ++bin) {
        const uint16_t hit_mm = map.distance_mm[static_cast<size_t>(bin)];
        if (hit_mm == 0) {
            continue;
        }
        const uint8_t intens = (bin < static_cast<int>(map.intensity.size()))
            ? map.intensity[static_cast<size_t>(bin)]
            : static_cast<uint8_t>(128);
        const float deg = static_cast<float>(bin) * 0.5f;
        const float rad = deg * 0.01745329252f;
        const float rn = std::clamp(static_cast<float>(hit_mm) / range_mm, 0.0f, 1.0f);
        // 0 deg = up, clockwise (sin x, -cos y), matching LD19 / LidarScreen.
        const int x = static_cast<int>(cx + radius * rn * std::sin(rad) + 0.5f);
        const int y = static_cast<int>(cy - radius * rn * std::cos(rad) + 0.5f);

        const float nearness = 1.0f - rn;
        const float bright = 0.40f + 0.60f * (static_cast<float>(intens) / 255.0f);
        const uint8_t r = static_cast<uint8_t>(
            std::clamp((60.0f + 195.0f * nearness) * bright, 0.0f, 255.0f)
        );
        const uint8_t g = static_cast<uint8_t>(std::clamp(
            (80.0f + 100.0f * (1.0f - nearness) + 80.0f * nearness) * bright,
            0.0f,
            255.0f
        ));
        const uint8_t b = static_cast<uint8_t>(
            std::clamp((40.0f + 200.0f * (1.0f - nearness)) * bright, 0.0f, 255.0f)
        );
        put_rgb(x, y, r, g, b);
        put_rgb(x + 1, y, r, g, b);
        put_rgb(x, y + 1, r, g, b);
    }

    const int ox = static_cast<int>(cx + 0.5f);
    const int oy = static_cast<int>(cy + 0.5f);
    for (int d = -2; d <= 2; ++d) {
        put_rgb(ox + d, oy, 240, 240, 245);
        put_rgb(ox, oy + d, 240, 240, 245);
    }

    return true;
}
