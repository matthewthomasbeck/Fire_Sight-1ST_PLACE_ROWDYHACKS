#pragma once

#include <cstdint>

// Arducam IR (OV5647 CSI) via libcamera. Port of C_Hardware_Testing CameraDriver.
// Captures ISP color frames (like rpicam-hello), downscales to 320x240 RGB.
// Falls back to animated demo if the camera is missing.

struct CameraDriver {
    static constexpr int width = 320;
    static constexpr int height = 240;
    static constexpr int pixel_count = width * height;

    uint8_t r[pixel_count]{};
    uint8_t g[pixel_count]{};
    uint8_t b[pixel_count]{};
    uint8_t v_min = 0;
    uint8_t v_max = 255;
    bool ready = false;
    bool demo_mode = true;
    int frame_index = 0;
    char last_error[128]{};

    bool init();
    bool refresh();
    void shutdown();
};
