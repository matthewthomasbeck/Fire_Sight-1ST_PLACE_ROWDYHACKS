#include "hardware/ThermalDriver.hpp"

#include "mlx90640/MLX90640_API.h"
#include "mlx90640/MLX90640_I2C_Driver.h"
#include "util/Log.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>

static constexpr uint8_t k_mlx_addr = 0x33;
static constexpr uint8_t k_refresh_rate = 0b101; // 16 Hz
static constexpr float k_ta_shift = 8.0f;
static constexpr float k_emissivity = 0.95f;

static paramsMLX90640 g_params{};
static uint16_t g_ee_data[832]{};
static uint16_t g_frame_data[834]{};
static bool g_hw_ready = false;

// Background reader so MLX90640_GetFrameData never blocks the IR/fusion loop.
static std::thread g_reader;
static std::atomic<bool> g_reader_stop{false};
static std::mutex g_frame_mu;
static float g_latest[ThermalDriver::pixel_count]{};
static float g_latest_min = 0.0f;
static float g_latest_max = 0.0f;
static int g_latest_index = 0;
static bool g_have_frame = false;

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

static bool try_init_hw() {
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
    g_hw_ready = true;
    return true;
}

static bool read_one_live_frame(float* out_pixels, float* out_min, float* out_max) {
    if (!g_hw_ready) {
        return false;
    }

    const int status = MLX90640_GetFrameData(k_mlx_addr, g_frame_data);
    if (status < 0) {
        return false;
    }

    MLX90640_InterpolateOutliers(g_frame_data, g_ee_data);
    const float ta = MLX90640_GetTa(g_frame_data, &g_params);
    const float tr = ta - k_ta_shift;
    MLX90640_CalculateTo(g_frame_data, &g_params, k_emissivity, tr, out_pixels);
    MLX90640_BadPixelsCorrection(g_params.brokenPixels, out_pixels, 1, &g_params);
    MLX90640_BadPixelsCorrection(g_params.outlierPixels, out_pixels, 1, &g_params);

    float t_min = 999.0f;
    float t_max = -999.0f;
    for (int i = 0; i < ThermalDriver::pixel_count; ++i) {
        t_min = std::min(t_min, out_pixels[i]);
        t_max = std::max(t_max, out_pixels[i]);
    }
    *out_min = t_min;
    *out_max = t_max;
    return true;
}

static void reader_loop() {
    float scratch[ThermalDriver::pixel_count];
    int fail_streak = 0;

    while (!g_reader_stop.load()) {
        float t_min = 0.0f;
        float t_max = 0.0f;
        if (!read_one_live_frame(scratch, &t_min, &t_max)) {
            ++fail_streak;
            if (fail_streak >= 8) {
                logutil::warn("ThermalDriver reader stalled; stopping live thread");
                g_hw_ready = false;
                break;
            }
            continue;
        }
        fail_streak = 0;

        {
            std::lock_guard<std::mutex> lock(g_frame_mu);
            std::memcpy(g_latest, scratch, sizeof(g_latest));
            g_latest_min = t_min;
            g_latest_max = t_max;
            ++g_latest_index;
            g_have_frame = true;
        }
    }
}

static void stop_reader() {
    g_reader_stop.store(true);
    if (g_reader.joinable()) {
        g_reader.join();
    }
    g_reader_stop.store(false);
    g_hw_ready = false;
    std::lock_guard<std::mutex> lock(g_frame_mu);
    g_have_frame = false;
}

static bool start_reader() {
    stop_reader();
    if (!try_init_hw()) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_frame_mu);
        g_have_frame = false;
        g_latest_index = 0;
    }

    g_reader_stop.store(false);
    g_reader = std::thread(reader_loop);

    // Wait briefly for first frame so init can report live vs demo.
    for (int i = 0; i < 40; ++i) {
        {
            std::lock_guard<std::mutex> lock(g_frame_mu);
            if (g_have_frame) {
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    logutil::warn("ThermalDriver timed out waiting for first frame");
    stop_reader();
    return false;
}

bool ThermalDriver::init() {
    frame_index = 0;

    if (start_reader()) {
        demo_mode = false;
        ready = true;
        logutil::info("ThermalDriver live MLX90640 (async reader)");
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
        std::lock_guard<std::mutex> lock(g_frame_mu);
        if (g_have_frame) {
            std::memcpy(pixels, g_latest, sizeof(pixels));
            t_min = g_latest_min;
            t_max = g_latest_max;
            frame_index = g_latest_index;
            return true;
        }
        if (g_hw_ready) {
            // Reader still starting / between frames; keep last pixels.
            return true;
        }
        logutil::warn("ThermalDriver live reader died, demo fallback");
        demo_mode = true;
    }

    refresh_demo(*this);
    return true;
}

void ThermalDriver::shutdown() {
    ready = false;
    demo_mode = true;
    frame_index = 0;
    stop_reader();
}
