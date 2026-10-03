#pragma once

#include <cstdint>
#include <vector>

#include "hardware/CameraDriver.hpp"
#include "hardware/ThermalDriver.hpp"

// IR + thermal fusion. Port math from C_Hardware_Testing FusionScreen.cpp.

struct RgbFrame {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgb; // width*height*3, RGB packed
};

struct PredatorFusion {
    float thermal_alpha = 0.225f;
    float thermal_x_shift_px = -2.5f;
    float ema_alpha = 0.7f;
    float ambient_ema_alpha = 0.08f;
    float dead_c = 0.40f;

    bool init();
    void shutdown();

    bool fuse(const CameraDriver& camera, const ThermalDriver& thermal, RgbFrame& out);
    bool encode_jpeg(const RgbFrame& frame, std::vector<std::uint8_t>& jpeg_out, int quality = 80);
};
