#pragma once

#include <cstdint>
#include <vector>

#include "hardware/ThermalDriver.hpp"

// Thermal (and later IR) to RGB + JPEG for uplink / HUD.

struct RgbFrame {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgb; // width*height*3
};

struct CameraFusion {
    static constexpr int out_width = 320;
    static constexpr int out_height = 240;

    float ema_alpha = 0.7f;
    float ambient_ema_alpha = 0.08f;
    float ambient_percentile = 0.20f;
    float dead_c = 0.40f;
    float cold_span_c = 3.0f;
    float warm_span_c = 4.0f;

    bool init();
    void shutdown();

    // False-color thermal at out_width x out_height.
    bool render_thermal(const ThermalDriver& thermal, RgbFrame& out);

    bool encode_jpeg(const RgbFrame& frame, std::vector<std::uint8_t>& jpeg_out, int quality = 80);
};
