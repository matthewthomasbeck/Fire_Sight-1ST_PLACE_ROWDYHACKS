#include "hardware/ThermalDriver.hpp"

#include "mlx90640/MLX90640_API.h"
#include "mlx90640/MLX90640_I2C_Driver.h"
#include "util/Log.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

static constexpr uint8_t k_mlx_addr = 0x33;
static constexpr uint8_t k_refresh_rate = 0b101; // 16 Hz
static constexpr float k_ta_shift = 8.0f;
static constexpr float k_emissivity = 0.95f;

static paramsMLX90640 g_params{};
static uint16_t g_ee_data[832]{};
static uint16_t g_frame_data[834]{};
static bool g_live_ready = false;

static void refresh_demo(ThermalDriver& self) {
    const float ambient = 20.0f;
    const float hotspot = 36.0f;
    const float cx = (ThermalDriver::width - 1) * 0.5f
        + std::cos(self.frame_index * 0.08f) * 10.0f;
    const float cy = (ThermalDriver::height - 1) * 0.5f
        + std::sin(self.frame_index * 0.08f) * 7.0f;
    const float sigma = 3.5f;
    self.t_min = 999.0f;
    self.t_max = -999.0f;

    for (int y = 0; y < ThermalDriver::height; ++y) {
        for (int x = 0; x < ThermalDriver::width; ++x) {
            const float dx = static_cast<float>(x) - cx;
            const float dy = static_cast<float>(y) - cy;
            const float gauss =
                std::exp(-(dx * dx + dy * dy) / (2.0f * sigma * sigma));
            const float noise =
                0.4f * std::sin(static_cast<float>(x * 3 + y * 5 + self.frame_index));
            float temp = ambient + (hotspot - ambient) * gauss + noise;
            self.pixels[y * ThermalDriver::width + x] = temp;
            self.t_min = std::min(self.t_min, temp);
            self.t_max = std::max(self.t_max, temp);
        }
    }
    ++self.frame_index;
}

static bool try_init_live() {
    MLX90640_I2CInit();

    if (MLX90640_DumpEE(k_mlx_addr, g_ee_data) != 0) {
        return false;
    }
    if (MLX90640_ExtractParameters(g_ee_data, &g_params) != 0) {
        return false;
    }

    MLX90640_SetRefreshRate(k_mlx_addr, k_refresh_rate);
    MLX90640_SetChessMode(k_mlx_addr);
    MLX90640_SetResolution(k_mlx_addr, 0x03);
    g_live_ready = true;
    return true;
}

static bool refresh_live(ThermalDriver& self) {
    if (!g_live_ready) {
        return false;
    }

    const int status = MLX90640_GetFrameData(k_mlx_addr, g_frame_data);
    if (status < 0) {
        return false;
    }

    MLX90640_InterpolateOutliers(g_frame_data, g_ee_data);
    const float ta = MLX90640_GetTa(g_frame_data, &g_params);
    const float tr = ta - k_ta_shift;
    MLX90640_CalculateTo(g_frame_data, &g_params, k_emissivity, tr, self.pixels);
    MLX90640_BadPixelsCorrection(g_params.brokenPixels, self.pixels, 1, &g_params);
    MLX90640_BadPixelsCorrection(g_params.outlierPixels, self.pixels, 1, &g_params);

    self.t_min = 999.0f;
    self.t_max = -999.0f;
    for (int i = 0; i < ThermalDriver::pixel_count; ++i) {
        self.t_min = std::min(self.t_min, self.pixels[i]);
        self.t_max = std::max(self.t_max, self.pixels[i]);
    }
    ++self.frame_index;
    return true;
}

bool ThermalDriver::init() {
    frame_index = 0;
    g_live_ready = false;

    if (try_init_live()) {
        demo_mode = false;
        ready = true;
        logutil::info("ThermalDriver live MLX90640");
        return refresh();
    }

    logutil::warn("ThermalDriver falling back to demo mode");
    demo_mode = true;
    ready = true;
    refresh_demo(*this);
    return true;
}

bool ThermalDriver::refresh() {
    if (!ready) {
        return false;
    }

    if (!demo_mode) {
        if (refresh_live(*this)) {
            return true;
        }
        logutil::warn("ThermalDriver live read failed, demo fallback");
        demo_mode = true;
        g_live_ready = false;
    }

    refresh_demo(*this);
    return true;
}

void ThermalDriver::shutdown() {
    ready = false;
    demo_mode = true;
    frame_index = 0;
    g_live_ready = false;
}
