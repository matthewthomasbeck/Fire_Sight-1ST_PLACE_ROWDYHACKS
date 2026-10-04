#pragma once

// MLX90640 32x24 celsius on I2C1. Port from C_Hardware_Testing ThermalDriver + mlx90640/.
// Live path uses a background reader so GetFrameData does not block the IR loop.

struct ThermalDriver {
    static constexpr int width = 32;
    static constexpr int height = 24;
    static constexpr int pixel_count = width * height;

    float pixels[pixel_count]{};
    float t_min = 0.0f;
    float t_max = 0.0f;
    bool ready = false;
    bool demo_mode = true;
    int frame_index = 0;

    bool init();
    bool refresh();
    void shutdown();
};
