#include "vision/CameraFusion.hpp"

#include "util/Log.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "util/stb_image_write.h"

namespace {

float g_smoothed[ThermalDriver::pixel_count]{};
bool g_smooth_ready = false;
float g_ambient = 20.0f;
bool g_ambient_ready = false;

void ensure_frame(RgbFrame& out) {
    out.width = CameraFusion::out_width;
    out.height = CameraFusion::out_height;
    out.rgb.resize(static_cast<std::size_t>(
        CameraFusion::out_width * CameraFusion::out_height * 3
    ));
}

void delta_to_rgb(
    float delta,
    float dead_c,
    float cold_span_c,
    float warm_span_c,
    unsigned* r,
    unsigned* g,
    unsigned* b,
    bool fusion_deadband_black
) {
    if (delta <= dead_c && delta >= -dead_c) {
        if (fusion_deadband_black) {
            *r = 0;
            *g = 0;
            *b = 0;
        } else {
            *r = 16;
            *g = 16;
            *b = 20;
        }
        return;
    }

    if (delta < 0.0f) {
        float u = (-delta - dead_c) / cold_span_c;
        u = std::clamp(u, 0.0f, 1.0f);
        if (u < 0.50f) {
            const float t = u / 0.50f;
            *r = 0;
            *g = static_cast<unsigned>(40.0f * t);
            *b = static_cast<unsigned>(120.0f + 135.0f * t);
        } else if (u < 0.80f) {
            const float t = (u - 0.50f) / 0.30f;
            *r = static_cast<unsigned>(180.0f * t);
            *g = static_cast<unsigned>(40.0f + 180.0f * t);
            *b = 255;
        } else {
            const float t = (u - 0.80f) / 0.20f;
            *r = static_cast<unsigned>(180.0f + 75.0f * t);
            *g = static_cast<unsigned>(220.0f + 35.0f * t);
            *b = 255;
        }
        return;
    }

    float u = (delta - dead_c) / warm_span_c;
    u = std::clamp(u, 0.0f, 1.0f);
    if (u < 0.33f) {
        const float t = u / 0.33f;
        *r = 0;
        *g = static_cast<unsigned>(80.0f + 175.0f * t);
        *b = 0;
    } else if (u < 0.66f) {
        const float t = (u - 0.33f) / 0.33f;
        *r = static_cast<unsigned>(255.0f * t);
        *g = 255;
        *b = 0;
    } else {
        const float t = (u - 0.66f) / 0.34f;
        *r = 255;
        *g = static_cast<unsigned>(255.0f * (1.0f - t));
        *b = 0;
    }
}

float estimate_ambient(const float* pixels, float percentile) {
    float scratch[ThermalDriver::pixel_count];
    std::memcpy(scratch, pixels, sizeof(scratch));
    const int n = ThermalDriver::pixel_count;
    int idx = static_cast<int>(percentile * static_cast<float>(n - 1));
    idx = std::clamp(idx, 0, n - 1);
    std::nth_element(scratch, scratch + idx, scratch + n);
    return scratch[idx];
}

void update_smoothed(const ThermalDriver& thermal, float ema_alpha) {
    if (!g_smooth_ready) {
        std::memcpy(g_smoothed, thermal.pixels, sizeof(g_smoothed));
        g_smooth_ready = true;
        return;
    }
    for (int i = 0; i < ThermalDriver::pixel_count; ++i) {
        g_smoothed[i] = ema_alpha * thermal.pixels[i]
            + (1.0f - ema_alpha) * g_smoothed[i];
    }
}

float update_ambient(float sample, float ambient_ema_alpha) {
    if (!g_ambient_ready) {
        g_ambient = sample;
        g_ambient_ready = true;
        return g_ambient;
    }
    g_ambient = ambient_ema_alpha * sample + (1.0f - ambient_ema_alpha) * g_ambient;
    return g_ambient;
}

float sample_thermal(float x, float y) {
    const int w = ThermalDriver::width;
    const int h = ThermalDriver::height;
    x = std::clamp(x, 0.0f, static_cast<float>(w - 1));
    y = std::clamp(y, 0.0f, static_cast<float>(h - 1));

    const int x0 = static_cast<int>(x);
    const int y0 = static_cast<int>(y);
    const int x1 = std::min(x0 + 1, w - 1);
    const int y1 = std::min(y0 + 1, h - 1);
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);

    const float v00 = g_smoothed[y0 * w + x0];
    const float v10 = g_smoothed[y0 * w + x1];
    const float v01 = g_smoothed[y1 * w + x0];
    const float v11 = g_smoothed[y1 * w + x1];
    const float v0 = v00 * (1.0f - fx) + v10 * fx;
    const float v1 = v01 * (1.0f - fx) + v11 * fx;
    return v0 * (1.0f - fy) + v1 * fy;
}

void sample_ir_rgb(
    const CameraDriver& camera,
    float x,
    float y,
    unsigned* out_r,
    unsigned* out_g,
    unsigned* out_b
) {
    const int w = CameraDriver::width;
    const int h = CameraDriver::height;
    x = std::clamp(x, 0.0f, static_cast<float>(w - 1));
    y = std::clamp(y, 0.0f, static_cast<float>(h - 1));

    const int x0 = static_cast<int>(x);
    const int y0 = static_cast<int>(y);
    const int x1 = std::min(x0 + 1, w - 1);
    const int y1 = std::min(y0 + 1, h - 1);
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);
    const float ifx = 1.0f - fx;
    const float ify = 1.0f - fy;

    const int i00 = y0 * w + x0;
    const int i10 = y0 * w + x1;
    const int i01 = y1 * w + x0;
    const int i11 = y1 * w + x1;

    const float r =
        (static_cast<float>(camera.r[i00]) * ifx
            + static_cast<float>(camera.r[i10]) * fx) * ify
        + (static_cast<float>(camera.r[i01]) * ifx
            + static_cast<float>(camera.r[i11]) * fx) * fy;
    const float g =
        (static_cast<float>(camera.g[i00]) * ifx
            + static_cast<float>(camera.g[i10]) * fx) * ify
        + (static_cast<float>(camera.g[i01]) * ifx
            + static_cast<float>(camera.g[i11]) * fx) * fy;
    const float b =
        (static_cast<float>(camera.b[i00]) * ifx
            + static_cast<float>(camera.b[i10]) * fx) * ify
        + (static_cast<float>(camera.b[i01]) * ifx
            + static_cast<float>(camera.b[i11]) * fx) * fy;

    *out_r = static_cast<unsigned>(std::min(255.0f, r + 0.5f));
    *out_g = static_cast<unsigned>(std::min(255.0f, g + 0.5f));
    *out_b = static_cast<unsigned>(std::min(255.0f, b + 0.5f));
}

struct JpegBuf {
    std::vector<std::uint8_t>* out;
};

void jpeg_write_cb(void* context, void* data, int size) {
    auto* buf = static_cast<JpegBuf*>(context);
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    buf->out->insert(buf->out->end(), bytes, bytes + size);
}

} // namespace

bool CameraFusion::init() {
    g_smooth_ready = false;
    g_ambient_ready = false;
    g_ambient = 20.0f;
    return true;
}

void CameraFusion::shutdown() {
    g_smooth_ready = false;
    g_ambient_ready = false;
}

bool CameraFusion::render_thermal(const ThermalDriver& thermal, RgbFrame& out) {
    update_smoothed(thermal, ema_alpha);
    const float ambient = update_ambient(
        estimate_ambient(g_smoothed, ambient_percentile),
        ambient_ema_alpha
    );

    ensure_frame(out);

    const float sx = static_cast<float>(ThermalDriver::width)
        / static_cast<float>(out_width);
    const float sy = static_cast<float>(ThermalDriver::height)
        / static_cast<float>(out_height);

    for (int y = 0; y < out_height; ++y) {
        for (int x = 0; x < out_width; ++x) {
            const float fx = (static_cast<float>(x) + 0.5f) * sx - 0.5f;
            const float fy = (static_cast<float>(y) + 0.5f) * sy - 0.5f;
            const float temp = sample_thermal(fx, fy);

            unsigned r = 0;
            unsigned g = 0;
            unsigned b = 0;
            delta_to_rgb(
                temp - ambient, dead_c, cold_span_c, warm_span_c,
                &r, &g, &b, false
            );

            const std::size_t i = static_cast<std::size_t>((y * out_width + x) * 3);
            out.rgb[i + 0] = static_cast<std::uint8_t>(r);
            out.rgb[i + 1] = static_cast<std::uint8_t>(g);
            out.rgb[i + 2] = static_cast<std::uint8_t>(b);
        }
    }
    return true;
}

bool CameraFusion::render_ir(const CameraDriver& camera, RgbFrame& out) {
    ensure_frame(out);

    const float ir_w = static_cast<float>(CameraDriver::width);
    const float ir_h = static_cast<float>(CameraDriver::height);

    for (int y = 0; y < out_height; ++y) {
        const float v = (static_cast<float>(y) + 0.5f)
            / static_cast<float>(out_height);
        const float ir_sy = v * ir_h - 0.5f;

        for (int x = 0; x < out_width; ++x) {
            const float u = (static_cast<float>(x) + 0.5f)
                / static_cast<float>(out_width);
            const float ir_sx = u * ir_w - 0.5f;

            unsigned r = 0;
            unsigned g = 0;
            unsigned b = 0;
            sample_ir_rgb(camera, ir_sx, ir_sy, &r, &g, &b);

            const std::size_t i = static_cast<std::size_t>((y * out_width + x) * 3);
            out.rgb[i + 0] = static_cast<std::uint8_t>(r);
            out.rgb[i + 1] = static_cast<std::uint8_t>(g);
            out.rgb[i + 2] = static_cast<std::uint8_t>(b);
        }
    }
    return true;
}

bool CameraFusion::render_fusion(
    const CameraDriver& camera,
    const ThermalDriver& thermal,
    RgbFrame& out
) {
    update_smoothed(thermal, ema_alpha);
    const float ambient = update_ambient(
        estimate_ambient(g_smoothed, ambient_percentile),
        ambient_ema_alpha
    );

    ensure_frame(out);

    const float ir_w = static_cast<float>(CameraDriver::width);
    const float ir_h = static_cast<float>(CameraDriver::height);
    const float th_w = static_cast<float>(ThermalDriver::width);
    const float th_h = static_cast<float>(ThermalDriver::height);

    for (int y = 0; y < out_height; ++y) {
        const float v = (static_cast<float>(y) + 0.5f)
            / static_cast<float>(out_height);
        const float ir_sy = v * ir_h - 0.5f;
        const float th_sy = v * th_h - 0.5f;

        for (int x = 0; x < out_width; ++x) {
            const float u = (static_cast<float>(x) + 0.5f)
                / static_cast<float>(out_width);
            const float ir_sx = u * ir_w - 0.5f;

            // Mirror thermal + x-shift registration (FusionScreen).
            const float u_th = u - (thermal_x_shift_px / th_w);
            const float th_sx_src = u_th * th_w - 0.5f;
            const float th_sx = (th_w - 1.0f) - th_sx_src;

            unsigned ir_r = 0;
            unsigned ir_g = 0;
            unsigned ir_b = 0;
            sample_ir_rgb(camera, ir_sx, ir_sy, &ir_r, &ir_g, &ir_b);

            const float temp = sample_thermal(th_sx, th_sy);
            unsigned th_r = 0;
            unsigned th_g = 0;
            unsigned th_b = 0;
            delta_to_rgb(
                temp - ambient, dead_c, cold_span_c, warm_span_c,
                &th_r, &th_g, &th_b, true
            );

            float a = thermal_alpha;
            if (th_r == 0 && th_g == 0 && th_b == 0) {
                a = 0.0f;
            }
            const float ia = 1.0f - a;
            const unsigned out_r = static_cast<unsigned>(
                ia * static_cast<float>(ir_r) + a * static_cast<float>(th_r) + 0.5f
            );
            const unsigned out_g = static_cast<unsigned>(
                ia * static_cast<float>(ir_g) + a * static_cast<float>(th_g) + 0.5f
            );
            const unsigned out_b = static_cast<unsigned>(
                ia * static_cast<float>(ir_b) + a * static_cast<float>(th_b) + 0.5f
            );

            const std::size_t i = static_cast<std::size_t>((y * out_width + x) * 3);
            out.rgb[i + 0] = static_cast<std::uint8_t>(out_r);
            out.rgb[i + 1] = static_cast<std::uint8_t>(out_g);
            out.rgb[i + 2] = static_cast<std::uint8_t>(out_b);
        }
    }
    return true;
}

bool CameraFusion::encode_jpeg(
    const RgbFrame& frame,
    std::vector<std::uint8_t>& jpeg_out,
    int quality
) {
    if (frame.width <= 0 || frame.height <= 0 || frame.rgb.empty()) {
        return false;
    }
    quality = std::clamp(quality, 1, 100);
    jpeg_out.clear();
    JpegBuf buf{&jpeg_out};
    const int ok = stbi_write_jpg_to_func(
        jpeg_write_cb,
        &buf,
        frame.width,
        frame.height,
        3,
        frame.rgb.data(),
        quality
    );
    if (!ok || jpeg_out.empty()) {
        logutil::error("CameraFusion::encode_jpeg failed");
        jpeg_out.clear();
        return false;
    }
    return true;
}
