#pragma once

#include <cstdint>
#include <vector>

#include "hardware/CameraDriver.hpp"
#include "hardware/ThermalDriver.hpp"

// Thermal / IR / predator fusion to RGB + JPEG for uplink / HUD.
// Fusion numbers match C_Hardware_Testing FusionScreen.cpp.

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
    // Thermal tint over IR (0 = IR only, 1 = thermal only).
    float thermal_alpha = 0.225f;
    // Horizontal registration in thermal pixels. Positive = overlay right.
    float thermal_x_shift_px = -2.5f;

    bool init();
    void shutdown();

    // False-color thermal at out_width x out_height.
    bool render_thermal(const ThermalDriver& thermal, RgbFrame& out);

    // IR RGB (bilinear) at out_width x out_height.
    bool render_ir(const CameraDriver& camera, RgbFrame& out);

    // Predator vision: IR base + thermal tint (FusionScreen math).
    bool render_fusion(
        const CameraDriver& camera,
        const ThermalDriver& thermal,
        RgbFrame& out
    );

    bool encode_jpeg(const RgbFrame& frame, std::vector<std::uint8_t>& jpeg_out, int quality = 80);
};
