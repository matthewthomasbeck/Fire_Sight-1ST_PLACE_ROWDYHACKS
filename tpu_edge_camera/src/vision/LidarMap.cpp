#include "vision/LidarMap.hpp"

#include "util/Log.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>

namespace {

void sample_bin(
    const LidarMapPayload& map,
    float angle_deg,
    uint16_t* dist_mm,
    uint8_t* intens
) {
    while (angle_deg < 0.0f) {
        angle_deg += 360.0f;
    }
    while (angle_deg >= 360.0f) {
        angle_deg -= 360.0f;
    }

    const float fbin = angle_deg * 2.0f;
    int b0 = static_cast<int>(fbin) % map.bin_count;
    int b1 = (b0 + 1) % map.bin_count;
    if (b0 < 0) {
        b0 = 0;
    }

    if (b0 >= static_cast<int>(map.distance_mm.size())
        || b1 >= static_cast<int>(map.distance_mm.size())) {
        *dist_mm = 0;
        *intens = 0;
        return;
    }

    const uint16_t d0 = map.distance_mm[static_cast<size_t>(b0)];
    const uint16_t d1 = map.distance_mm[static_cast<size_t>(b1)];
    if (d0 == 0 && d1 == 0) {
        *dist_mm = 0;
        *intens = 0;
        return;
    }
    if (d0 == 0) {
        *dist_mm = d1;
        *intens = map.intensity[static_cast<size_t>(b1)];
        return;
    }
    if (d1 == 0) {
        *dist_mm = d0;
        *intens = map.intensity[static_cast<size_t>(b0)];
        return;
    }

    const float t = fbin - static_cast<float>(static_cast<int>(fbin));
    *dist_mm = static_cast<uint16_t>(
        (1.0f - t) * static_cast<float>(d0) + t * static_cast<float>(d1) + 0.5f
    );
    *intens = static_cast<uint8_t>(
        (1.0f - t) * static_cast<float>(map.intensity[static_cast<size_t>(b0)])
            + t * static_cast<float>(map.intensity[static_cast<size_t>(b1)])
            + 0.5f
    );
}

void cell_to_rgb(
    float cell_r_mm,
    uint16_t hit_mm,
    uint8_t intens,
    float range_mm,
    uint8_t* r,
    uint8_t* g,
    uint8_t* b
) {
    if (hit_mm == 0) {
        *r = 12;
        *g = 12;
        *b = 16;
        return;
    }

    const float hit = static_cast<float>(hit_mm);
    const float bright = 0.40f + 0.60f * (static_cast<float>(intens) / 255.0f);
    const float nearness = std::clamp(1.0f - hit / range_mm, 0.0f, 1.0f);
    const float band = std::max(40.0f, range_mm * 0.02f);

    if (cell_r_mm > hit + band) {
        *r = 8;
        *g = 8;
        *b = 10;
        return;
    }

    if (std::fabs(cell_r_mm - hit) <= band) {
        *r = static_cast<uint8_t>(std::clamp((60.0f + 195.0f * nearness) * bright, 0.0f, 255.0f));
        *g = static_cast<uint8_t>(std::clamp(
            (80.0f + 100.0f * (1.0f - nearness) + 80.0f * nearness) * bright,
            0.0f,
            255.0f
        ));
        *b = static_cast<uint8_t>(
            std::clamp((40.0f + 200.0f * (1.0f - nearness)) * bright, 0.0f, 255.0f)
        );
        return;
    }

    const float u = std::clamp(cell_r_mm / hit, 0.0f, 1.0f);
    const float glow = (0.12f + 0.22f * nearness) * (1.0f - u * 0.5f) * bright;
    *r = static_cast<uint8_t>(std::clamp(30.0f + 90.0f * glow, 0.0f, 255.0f));
    *g = static_cast<uint8_t>(std::clamp(35.0f + 70.0f * glow, 0.0f, 255.0f));
    *b = static_cast<uint8_t>(std::clamp(50.0f + 110.0f * glow, 0.0f, 255.0f));
}

} // namespace

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

    for (int y = 0; y < size_px; ++y) {
        for (int x = 0; x < size_px; ++x) {
            const float nx = (static_cast<float>(x) - cx) / radius;
            const float ny = (static_cast<float>(y) - cy) / radius;
            const float rn = std::sqrt(nx * nx + ny * ny);
            const int di = (y * size_px + x) * 3;

            if (rn > 1.0f) {
                rgb_out[static_cast<size_t>(di)] = 0;
                rgb_out[static_cast<size_t>(di + 1)] = 0;
                rgb_out[static_cast<size_t>(di + 2)] = 0;
                continue;
            }

            float angle_deg = std::atan2(nx, -ny) * 57.2957795f;
            if (angle_deg < 0.0f) {
                angle_deg += 360.0f;
            }

            uint16_t hit_mm = 0;
            uint8_t intens = 0;
            sample_bin(map, angle_deg, &hit_mm, &intens);

            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;
            cell_to_rgb(rn * range_mm, hit_mm, intens, range_mm, &r, &g, &b);
            rgb_out[static_cast<size_t>(di)] = r;
            rgb_out[static_cast<size_t>(di + 1)] = g;
            rgb_out[static_cast<size_t>(di + 2)] = b;
        }
    }

    // Origin crosshair.
    const int ox = static_cast<int>(cx + 0.5f);
    const int oy = static_cast<int>(cy + 0.5f);
    for (int d = -2; d <= 2; ++d) {
        const int hx = ox + d;
        const int hy = oy;
        if (hx >= 0 && hx < size_px && hy >= 0 && hy < size_px) {
            const int di = (hy * size_px + hx) * 3;
            rgb_out[static_cast<size_t>(di)] = 240;
            rgb_out[static_cast<size_t>(di + 1)] = 240;
            rgb_out[static_cast<size_t>(di + 2)] = 245;
        }
        const int vx = ox;
        const int vy = oy + d;
        if (vx >= 0 && vx < size_px && vy >= 0 && vy < size_px) {
            const int di = (vy * size_px + vx) * 3;
            rgb_out[static_cast<size_t>(di)] = 240;
            rgb_out[static_cast<size_t>(di + 1)] = 240;
            rgb_out[static_cast<size_t>(di + 2)] = 245;
        }
    }

    return true;
}
