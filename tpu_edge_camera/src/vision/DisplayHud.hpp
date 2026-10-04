#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "vision/CameraFusion.hpp"
#include "vision/LidarMap.hpp"

// Direct framebuffer HUD (/dev/fb0). No OpenCV, no X11.
// Layout: width-filling fusion (cropped) + top-left lidar square (side = 50% min edge).
// Panel mount is upside down: all fb writes apply a 180 deg rotation (not a mirror).
// Fusion frame is 90 deg CCW-corrected for the CW camera mount.

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
    int red_off_ = 11;
    int green_off_ = 5;
    int blue_off_ = 0;
    int red_len_ = 5;
    int green_len_ = 6;
    int blue_len_ = 5;
    bool quit_requested_ = false;

    // Last lidar inset; fusion never paints this rect, so no flash.
    int lidar_side_ = 0;
    std::vector<std::uint8_t> lidar_cache_;

    // Top-right GPS plate; composited offscreen then copied in one blit.
    int gps_box_x_ = 0;
    int gps_box_y_ = 0;
    int gps_box_w_ = 0;
    int gps_box_h_ = 0;
    std::vector<std::uint8_t> gps_cache_;
};
