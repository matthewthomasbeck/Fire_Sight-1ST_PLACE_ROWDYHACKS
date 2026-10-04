#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "vision/CameraFusion.hpp"
#include "vision/LidarMap.hpp"

// Direct framebuffer HUD (/dev/fb0). No OpenCV, no X11.
// Layout: width-filling fusion (cropped) + top-left lidar square (side = 66% min edge).

struct HudOverlay {
    double lat = 0.0;
    double lon = 0.0;
    bool gps_valid = false;
    std::string gemini_advice;
    std::string link_status;
};

struct DisplayHud {
    bool enabled = true;

    // width/height ignored; panel size comes from the framebuffer.
    bool init(int width, int height);
    void shutdown();

    bool draw(
        const RgbFrame& frame,
        const LidarMapPayload* lidar_or_null,
        const HudOverlay& overlay
    );

    bool poll_quit();

private:
    int fb_fd_ = -1;
    std::uint8_t* fb_mem_ = nullptr;
    std::size_t fb_map_len_ = 0;
    int fb_w_ = 0;
    int fb_h_ = 0;
    int fb_bpp_ = 0;
    int fb_line_bytes_ = 0;
    int red_off_ = 16;
    int green_off_ = 8;
    int blue_off_ = 0;
    bool quit_requested_ = false;

    // Last lidar inset; fusion never paints this rect, so no flash.
    int lidar_side_ = 0;
    std::vector<std::uint8_t> lidar_cache_;
};
