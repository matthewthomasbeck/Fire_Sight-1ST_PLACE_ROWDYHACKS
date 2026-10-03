#include "vision/LidarMap.hpp"

#include "util/Log.hpp"

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
    const LidarMapPayload& /*map*/,
    const std::string& /*stream_id*/,
    std::string& json_out
) {
    logutil::warn("LidarMap::to_json not implemented");
    json_out.clear();
    return false;
}

bool LidarMap::render_hud(
    const LidarMapPayload& /*map*/,
    int /*size_px*/,
    std::vector<std::uint8_t>& rgb_out
) {
    logutil::warn("LidarMap::render_hud not implemented");
    rgb_out.clear();
    return false;
}
