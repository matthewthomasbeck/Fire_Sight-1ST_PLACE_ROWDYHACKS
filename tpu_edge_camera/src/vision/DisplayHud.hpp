#pragma once

#include <string>

#include "vision/CameraFusion.hpp"
#include "vision/LidarMap.hpp"

// Fullscreen touchscreen HUD via OpenCV (not notcurses).

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
        const RgbFrame& frame,
        const LidarMapPayload* lidar_or_null,
        const HudOverlay& overlay
    );

    bool poll_quit();

private:
    int window_w_ = 800;
    int window_h_ = 480;
    bool quit_requested_ = false;
};
