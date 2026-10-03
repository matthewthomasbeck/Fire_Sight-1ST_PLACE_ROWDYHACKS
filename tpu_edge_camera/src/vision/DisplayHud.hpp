#pragma once

#include <string>

#include "vision/LidarMap.hpp"
#include "vision/PredatorFusion.hpp"

// Touchscreen HUD (OpenCV/SDL). Not notcurses.

struct HudOverlay {
    double lat = 0.0;
    double lon = 0.0;
    bool gps_valid = false;
    std::string gemini_advice;
    std::string link_status;
};

struct DisplayHud {
    bool enabled = true;

    bool init(int width, int height);
    void shutdown();

    bool draw(
        const RgbFrame& predator,
        const LidarMapPayload* lidar_or_null,
        const HudOverlay& overlay
    );

    bool poll_quit();
};
