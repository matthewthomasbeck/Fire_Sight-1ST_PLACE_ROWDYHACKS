#include "vision/DisplayHud.hpp"

#include "util/Log.hpp"

bool DisplayHud::init(int /*width*/, int /*height*/) {
    logutil::warn("DisplayHud::init not implemented");
    return true;
}

void DisplayHud::shutdown() {}

bool DisplayHud::draw(
    const RgbFrame& /*predator*/,
    const LidarMapPayload* /*lidar_or_null*/,
    const HudOverlay& /*overlay*/
) {
    return true;
}

bool DisplayHud::poll_quit() {
    return false;
}
