#include "vision/DisplayHud.hpp"

#include "util/Log.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(__linux__)
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <poll.h>
#endif

namespace {

#if defined(__linux__)

int g_console_fd = -1;
int g_claimed_vt = 0;

inline std::uint32_t pack_pixel(
    int bpp,
    int red_off,
    int green_off,
    int blue_off,
    int red_len,
    int green_len,
    int blue_len,
    std::uint8_t r,
    std::uint8_t g,
    std::uint8_t b
) {
    (void)bpp;
    const int r_shift = std::max(0, 8 - std::max(1, red_len));
    const int g_shift = std::max(0, 8 - std::max(1, green_len));
    const int b_shift = std::max(0, 8 - std::max(1, blue_len));
    return (static_cast<std::uint32_t>(r >> r_shift) << red_off)
        | (static_cast<std::uint32_t>(g >> g_shift) << green_off)
        | (static_cast<std::uint32_t>(b >> b_shift) << blue_off);
}

void flush_fb(int fb_fd, std::uint8_t* mem, std::size_t len) {
    if (mem != nullptr && len > 0) {
        (void)msync(mem, len, MS_SYNC);
    }
    if (fb_fd < 0) {
        return;
    }
    fb_var_screeninfo vinfo{};
    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) != 0) {
        return;
    }
    vinfo.xoffset = 0;
    vinfo.yoffset = 0;
    (void)ioctl(fb_fd, FBIOPAN_DISPLAY, &vinfo);
}

void poll_stdin_quit(bool& quit_requested) {
    pollfd pfd{};
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    if (poll(&pfd, 1, 0) <= 0) {
        return;
    }
    char ch = 0;
    if (read(STDIN_FILENO, &ch, 1) == 1) {
        if (ch == 'q' || ch == 'Q' || ch == 27) {
            quit_requested = true;
        }
    }
}

void write_sysfs(const char* path, const char* value) {
    const int fd = open(path, O_WRONLY);
    if (fd < 0) {
        return;
    }
    const auto n = static_cast<ssize_t>(std::strlen(value));
    (void)::write(fd, value, static_cast<size_t>(n));
    ::close(fd);
}

bool env_truthy(const char* key) {
    const char* v = std::getenv(key);
    if (v == nullptr || v[0] == '\0') {
        return false;
    }
    return std::string(v) == "1" || std::string(v) == "true" || std::string(v) == "TRUE";
}

// 8x12 glyphs (row = bit row, MSB = left). Denser than 5x7 so scaled text is less blocky.
constexpr int k_font_w = 8;
constexpr int k_font_h = 12;

const std::uint8_t* glyph8x12(char ch) {
    static const std::uint8_t g_space[12] = {};
    static const std::uint8_t g_colon[12] = {
        0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00
    };
    static const std::uint8_t g_dot[12] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00
    };
    static const std::uint8_t g_0[12] = {
        0x3C, 0x66, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0x66, 0x3C, 0x00
    };
    static const std::uint8_t g_1[12] = {
        0x18, 0x38, 0x78, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00
    };
    static const std::uint8_t g_2[12] = {
        0x3C, 0x66, 0xC3, 0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0xFF, 0x00
    };
    static const std::uint8_t g_3[12] = {
        0x3C, 0x66, 0xC3, 0x03, 0x06, 0x1C, 0x06, 0x03, 0xC3, 0x66, 0x3C, 0x00
    };
    static const std::uint8_t g_4[12] = {
        0x06, 0x0E, 0x1E, 0x36, 0x66, 0xC6, 0xFF, 0x06, 0x06, 0x06, 0x06, 0x00
    };
    static const std::uint8_t g_5[12] = {
        0xFF, 0xC0, 0xC0, 0xC0, 0xFC, 0x06, 0x03, 0x03, 0xC3, 0x66, 0x3C, 0x00
    };
    static const std::uint8_t g_6[12] = {
        0x1C, 0x30, 0x60, 0xC0, 0xFC, 0xC6, 0xC3, 0xC3, 0xC3, 0x66, 0x3C, 0x00
    };
    static const std::uint8_t g_7[12] = {
        0xFF, 0x03, 0x06, 0x06, 0x0C, 0x0C, 0x18, 0x18, 0x30, 0x30, 0x30, 0x00
    };
    static const std::uint8_t g_8[12] = {
        0x3C, 0x66, 0xC3, 0xC3, 0x66, 0x3C, 0x66, 0xC3, 0xC3, 0x66, 0x3C, 0x00
    };
    static const std::uint8_t g_9[12] = {
        0x3C, 0x66, 0xC3, 0xC3, 0xC3, 0x67, 0x3F, 0x03, 0x06, 0x0C, 0x38, 0x00
    };
    static const std::uint8_t g_A[12] = {
        0x18, 0x3C, 0x66, 0xC3, 0xC3, 0xC3, 0xFF, 0xC3, 0xC3, 0xC3, 0xC3, 0x00
    };
    static const std::uint8_t g_E[12] = {
        0xFF, 0xC0, 0xC0, 0xC0, 0xFC, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xFF, 0x00
    };
    static const std::uint8_t g_K[12] = {
        0xC3, 0xC6, 0xCC, 0xD8, 0xF0, 0xF0, 0xD8, 0xCC, 0xC6, 0xC3, 0xC3, 0x00
    };
    static const std::uint8_t g_L[12] = {
        0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xFF, 0x00
    };
    static const std::uint8_t g_N[12] = {
        0xC3, 0xE3, 0xE3, 0xD3, 0xD3, 0xCB, 0xCB, 0xC7, 0xC7, 0xC3, 0xC3, 0x00
    };
    static const std::uint8_t g_O[12] = {
        0x3C, 0x66, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0x66, 0x3C, 0x00
    };
    static const std::uint8_t g_S[12] = {
        0x3C, 0x66, 0xC3, 0xC0, 0x60, 0x3C, 0x06, 0x03, 0xC3, 0x66, 0x3C, 0x00
    };
    static const std::uint8_t g_T[12] = {
        0xFF, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00
    };
    static const std::uint8_t g_U[12] = {
        0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0x66, 0x3C, 0x00
    };
    static const std::uint8_t g_W[12] = {
        0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xDB, 0xDB, 0xDB, 0xFF, 0x66, 0x66, 0x00
    };

    switch (ch) {
    case ' ':
        return g_space;
    case ':':
        return g_colon;
    case '.':
        return g_dot;
    case '0':
        return g_0;
    case '1':
        return g_1;
    case '2':
        return g_2;
    case '3':
        return g_3;
    case '4':
        return g_4;
    case '5':
        return g_5;
    case '6':
        return g_6;
    case '7':
        return g_7;
    case '8':
        return g_8;
    case '9':
        return g_9;
    case 'A':
    case 'a':
        return g_A;
    case 'E':
    case 'e':
        return g_E;
    case 'K':
    case 'k':
        return g_K;
    case 'L':
    case 'l':
        return g_L;
    case 'N':
    case 'n':
        return g_N;
    case 'O':
    case 'o':
        return g_O;
    case 'S':
    case 's':
        return g_S;
    case 'T':
    case 't':
        return g_T;
    case 'U':
    case 'u':
        return g_U;
    case 'W':
    case 'w':
        return g_W;
    default:
        return g_space;
    }
}

// Panel is mounted upside down: map logical HUD coords to a 180 deg rotation
// (x,y) -> (w-1-x, h-1-y). That is a rotation, not a mirror.
inline void panel_map(int fb_w, int fb_h, int x, int y, int* ox, int* oy) {
    *ox = fb_w - 1 - x;
    *oy = fb_h - 1 - y;
}

void put_fb_rgb(
    std::uint8_t* fb_mem,
    int fb_w,
    int fb_h,
    int line_bytes,
    int bpp,
    int red_off,
    int green_off,
    int blue_off,
    int red_len,
    int green_len,
    int blue_len,
    int x,
    int y,
    std::uint8_t r,
    std::uint8_t g,
    std::uint8_t b
) {
    if (x < 0 || y < 0 || x >= fb_w || y >= fb_h || fb_mem == nullptr) {
        return;
    }
    int ox = x;
    int oy = y;
    panel_map(fb_w, fb_h, x, y, &ox, &oy);
    const int bytes_pp = bpp / 8;
    std::uint8_t* row = fb_mem + static_cast<std::size_t>(oy) * line_bytes;
    const std::uint32_t px = pack_pixel(
        bpp, red_off, green_off, blue_off, red_len, green_len, blue_len, r, g, b
    );
    if (bytes_pp == 2) {
        *reinterpret_cast<std::uint16_t*>(row + ox * 2) = static_cast<std::uint16_t>(px);
    } else {
        *reinterpret_cast<std::uint32_t*>(row + ox * 4) = px;
    }
}

// Take over a text VT so /dev/fb0 actually paints the DSI panel.
// From SSH this needs root; compile_and_run stops the desktop first.
bool claim_vt_graphics() {
    int vt_no = 0;
    if (const char* env = std::getenv("HUD_VT")) {
        vt_no = std::atoi(env);
    }
    if (vt_no <= 0) {
        vt_no = 2; // VT2 is usually free while graphical session sits on VT1/7
    }

    char tty_path[32];
    std::snprintf(tty_path, sizeof(tty_path), "/dev/tty%d", vt_no);
    int fd = open(tty_path, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        fd = open("/dev/tty0", O_RDWR | O_NOCTTY);
        if (fd < 0) {
            logutil::warn(
                std::string("DisplayHud: cannot open console (")
                + std::strerror(errno)
                + "). From SSH run: sudo ./compile_and_run"
            );
            return false;
        }
        vt_no = 0;
    }

    if (vt_no > 0) {
        // Do not VT_WAITACTIVE: from SSH that can block forever and the panel
        // never gets test bars / video. Activate and continue.
        if (ioctl(fd, VT_ACTIVATE, vt_no) != 0) {
            logutil::warn(
                std::string("DisplayHud: VT_ACTIVATE failed: ") + std::strerror(errno)
            );
        } else {
            logutil::info("DisplayHud claimed VT" + std::to_string(vt_no));
            g_claimed_vt = vt_no;
        }
    }

    if (ioctl(fd, KDSETMODE, KD_GRAPHICS) != 0) {
        logutil::warn(
            std::string("DisplayHud: KDSETMODE GRAPHICS failed: ")
            + std::strerror(errno)
            + " (desktop may still own DRM; rerun sudo ./compile_and_run)"
        );
    } else {
        logutil::info("DisplayHud console in KD_GRAPHICS");
    }

    write_sysfs("/sys/class/graphics/fb0/blank", "0");
    write_sysfs("/sys/class/graphics/fb1/blank", "0");
    write_sysfs("/sys/class/graphics/fbcon/cursor_blink", "0");

    g_console_fd = fd;
    return true;
}

void release_vt_graphics() {
    if (g_console_fd >= 0) {
        (void)ioctl(g_console_fd, KDSETMODE, KD_TEXT);
        ::close(g_console_fd);
        g_console_fd = -1;
    }
    g_claimed_vt = 0;
}

std::vector<std::string> candidate_fb_paths() {
    std::vector<std::string> paths;
    const char* env = std::getenv("HUD_FB_DEVICE");
    if (env != nullptr && env[0] != '\0') {
        paths.emplace_back(env);
    }
    paths.emplace_back("/dev/fb0");
    paths.emplace_back("/dev/fb1");
    return paths;
}

void draw_test_bars(
    std::uint8_t* mem,
    int w,
    int h,
    int line_bytes,
    int bpp,
    int red_off,
    int green_off,
    int blue_off,
    int red_len,
    int green_len,
    int blue_len
) {
    const int bytes_pp = bpp / 8;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int band = (x * 3) / std::max(1, w);
            const std::uint8_t r = band == 0 ? 255 : 0;
            const std::uint8_t g = band == 1 ? 255 : 0;
            const std::uint8_t b = band == 2 ? 255 : 0;
            int ox = x;
            int oy = y;
            panel_map(w, h, x, y, &ox, &oy);
            std::uint8_t* row = mem + static_cast<std::size_t>(oy) * line_bytes;
            const std::uint32_t px = pack_pixel(
                bpp, red_off, green_off, blue_off,
                red_len, green_len, blue_len, r, g, b
            );
            if (bytes_pp == 2) {
                *reinterpret_cast<std::uint16_t*>(row + ox * 2) =
                    static_cast<std::uint16_t>(px);
            } else {
                *reinterpret_cast<std::uint32_t*>(row + ox * 4) = px;
            }
        }
    }
}

int lidar_square_side(int fb_w, int fb_h) {
    // 50% of the shorter panel edge.
    return std::max(1, std::min(fb_w, fb_h) / 2);
}

void blit_fusion(
    std::uint8_t* fb_mem,
    int fb_w,
    int fb_h,
    int line_bytes,
    int bpp,
    int red_off,
    int green_off,
    int blue_off,
    int red_len,
    int green_len,
    int blue_len,
    const RgbFrame& frame,
    int skip_side,
    int skip_gps_x,
    int skip_gps_y,
    int skip_gps_w,
    int skip_gps_h
) {
    const int src_w = frame.width;
    const int src_h = frame.height;
    const std::uint8_t* src = frame.rgb.data();
    const int bytes_pp = bpp / 8;

    // Fill panel width; crop top/bottom if the scaled height overflows.
    const float scale = static_cast<float>(fb_w) / static_cast<float>(src_w);
    const int dst_w = fb_w;
    const int dst_h = std::max(1, static_cast<int>(src_h * scale + 0.5f));
    const int origin_x = 0;
    const int origin_y = (fb_h - dst_h) / 2; // may be negative when cropping

    for (int y = 0; y < fb_h; ++y) {
        const int sy_img = y - origin_y;
        for (int x = 0; x < fb_w; ++x) {
            // Leave lidar + GPS overlay rects alone to avoid flash under overlays.
            if (x < skip_side && y < skip_side) {
                continue;
            }
            if (skip_gps_w > 0 && skip_gps_h > 0
                && x >= skip_gps_x && x < skip_gps_x + skip_gps_w
                && y >= skip_gps_y && y < skip_gps_y + skip_gps_h) {
                continue;
            }

            const int sx_img = x - origin_x;
            std::uint8_t r = 0;
            std::uint8_t g = 0;
            std::uint8_t b = 0;
            if (sx_img >= 0 && sy_img >= 0 && sx_img < dst_w && sy_img < dst_h) {
                // Fusion is 90 deg CCW-corrected; vertical flip only (no mirror).
                const int sx = (sx_img * src_w) / dst_w;
                const int sy = (src_h - 1) - (sy_img * src_h) / dst_h;
                const int si = (sy * src_w + sx) * 3;
                r = src[si];
                g = src[si + 1];
                b = src[si + 2];
            }
            int ox = x;
            int oy = y;
            panel_map(fb_w, fb_h, x, y, &ox, &oy);
            std::uint8_t* out_row =
                fb_mem + static_cast<std::size_t>(oy) * line_bytes;
            const std::uint32_t px = pack_pixel(
                bpp, red_off, green_off, blue_off,
                red_len, green_len, blue_len, r, g, b
            );
            if (bytes_pp == 2) {
                *reinterpret_cast<std::uint16_t*>(out_row + ox * 2) =
                    static_cast<std::uint16_t>(px);
            } else {
                *reinterpret_cast<std::uint32_t*>(out_row + ox * 4) = px;
            }
        }
    }
}

int text_pixel_width(const char* text, int glyph_w) {
    if (text == nullptr || text[0] == '\0') {
        return 0;
    }
    const int n = static_cast<int>(std::strlen(text));
    const int advance = glyph_w + std::max(1, glyph_w / 8);
    return n * advance - std::max(1, glyph_w / 8);
}

float glyph_coverage(char ch, int px, int py, int dst_w, int dst_h) {
    const std::uint8_t* g = glyph8x12(ch);
    if (dst_w < 1 || dst_h < 1) {
        return 0.0f;
    }
    const float v0 = static_cast<float>(py) * static_cast<float>(k_font_h)
        / static_cast<float>(dst_h);
    const float v1 = static_cast<float>(py + 1) * static_cast<float>(k_font_h)
        / static_cast<float>(dst_h);
    const float u0 = static_cast<float>(px) * static_cast<float>(k_font_w)
        / static_cast<float>(dst_w);
    const float u1 = static_cast<float>(px + 1) * static_cast<float>(k_font_w)
        / static_cast<float>(dst_w);
    const int row0 = std::max(0, static_cast<int>(v0));
    const int row1 = std::min(k_font_h, static_cast<int>(std::ceil(v1)));
    const int col0 = std::max(0, static_cast<int>(u0));
    const int col1 = std::min(k_font_w, static_cast<int>(std::ceil(u1)));

    float cover = 0.0f;
    float area = 0.0f;
    for (int row = row0; row < row1; ++row) {
        const float y_a = std::max(v0, static_cast<float>(row));
        const float y_b = std::min(v1, static_cast<float>(row + 1));
        const float yh = y_b - y_a;
        if (yh <= 0.0f) {
            continue;
        }
        const std::uint8_t bits = g[row];
        for (int col = col0; col < col1; ++col) {
            const float x_a = std::max(u0, static_cast<float>(col));
            const float x_b = std::min(u1, static_cast<float>(col + 1));
            const float xw = x_b - x_a;
            if (xw <= 0.0f) {
                continue;
            }
            const float a = xw * yh;
            area += a;
            if ((bits >> (7 - col)) & 1) {
                cover += a;
            }
        }
    }
    if (area <= 0.0f) {
        return 0.0f;
    }
    return std::clamp(cover / area, 0.0f, 1.0f);
}

// Blend black glyphs into an RGB buffer (video already filled).
void stamp_black_text_rgb(
    std::uint8_t* rgb,
    int buf_w,
    int buf_h,
    int x0,
    int y0,
    const char* text,
    int glyph_w,
    int glyph_h
) {
    if (text == nullptr || rgb == nullptr) {
        return;
    }
    const int advance = glyph_w + std::max(1, glyph_w / 8);
    int x = x0;
    for (const char* p = text; *p != '\0'; ++p) {
        for (int py = 0; py < glyph_h; ++py) {
            for (int px = 0; px < glyph_w; ++px) {
                const float t = glyph_coverage(*p, px, py, glyph_w, glyph_h);
                if (t < 0.08f) {
                    continue;
                }
                const int dx = x + px;
                const int dy = y0 + py;
                if (dx < 0 || dy < 0 || dx >= buf_w || dy >= buf_h) {
                    continue;
                }
                const int di = (dy * buf_w + dx) * 3;
                const float keep = 1.0f - t;
                rgb[static_cast<size_t>(di)] = static_cast<std::uint8_t>(
                    rgb[static_cast<size_t>(di)] * keep + 0.5f
                );
                rgb[static_cast<size_t>(di + 1)] = static_cast<std::uint8_t>(
                    rgb[static_cast<size_t>(di + 1)] * keep + 0.5f
                );
                rgb[static_cast<size_t>(di + 2)] = static_cast<std::uint8_t>(
                    rgb[static_cast<size_t>(di + 2)] * keep + 0.5f
                );
            }
        }
        x += advance;
    }
}

void sample_fusion_pixel(
    const RgbFrame& frame,
    int fb_w,
    int fb_h,
    int x,
    int y,
    std::uint8_t* r,
    std::uint8_t* g,
    std::uint8_t* b
) {
    const int src_w = frame.width;
    const int src_h = frame.height;
    const std::uint8_t* src = frame.rgb.data();
    const float scale = static_cast<float>(fb_w) / static_cast<float>(src_w);
    const int dst_w = fb_w;
    const int dst_h = std::max(1, static_cast<int>(src_h * scale + 0.5f));
    const int origin_y = (fb_h - dst_h) / 2;
    const int sx_img = x;
    const int sy_img = y - origin_y;
    if (sx_img < 0 || sy_img < 0 || sx_img >= dst_w || sy_img >= dst_h) {
        *r = 0;
        *g = 0;
        *b = 0;
        return;
    }
    const int sx = (sx_img * src_w) / dst_w;
    const int sy = (src_h - 1) - (sy_img * src_h) / dst_h;
    const int si = (sy * src_w + sx) * 3;
    *r = src[si];
    *g = src[si + 1];
    *b = src[si + 2];
}

void compose_gps_cache(
    const RgbFrame& frame,
    int fb_w,
    int fb_h,
    const HudOverlay& overlay,
    int& box_x,
    int& box_y,
    int& box_w,
    int& box_h,
    std::vector<std::uint8_t>& cache
) {
    char lat_line[48];
    char lon_line[48];
    if (overlay.gps_valid) {
        const char ns = overlay.lat >= 0.0 ? 'N' : 'S';
        const char ew = overlay.lon >= 0.0 ? 'E' : 'W';
        std::snprintf(
            lat_line,
            sizeof(lat_line),
            "LAT: %.7f %c",
            std::fabs(overlay.lat),
            ns
        );
        std::snprintf(
            lon_line,
            sizeof(lon_line),
            "LON: %.7f %c",
            std::fabs(overlay.lon),
            ew
        );
    } else {
        std::snprintf(lat_line, sizeof(lat_line), "LAT: UNKNOWN");
        std::snprintf(lon_line, sizeof(lon_line), "LON: UNKNOWN");
    }

    const int glyph_h = std::clamp((fb_h * 26) / 480, 18, 28);
    const int glyph_w = std::max(10, (glyph_h * k_font_w) / k_font_h);
    const int pad = 8;
    const int gap = std::max(4, glyph_h / 6);
    const int line_h = glyph_h + gap;
    const int block_h = line_h + glyph_h;
    const int w0 = text_pixel_width(lat_line, glyph_w);
    const int w1 = text_pixel_width(lon_line, glyph_w);
    const int block_w = std::max(w0, w1);

    box_x = std::max(0, fb_w - pad - block_w - 6);
    box_y = pad;
    box_w = std::min(fb_w - box_x, block_w + 12);
    box_h = std::min(fb_h - box_y, block_h + 8);
    if (box_w < 1 || box_h < 1) {
        cache.clear();
        return;
    }

    cache.assign(static_cast<size_t>(box_w * box_h * 3), 0);
    for (int y = 0; y < box_h; ++y) {
        for (int x = 0; x < box_w; ++x) {
            std::uint8_t r = 0;
            std::uint8_t g = 0;
            std::uint8_t b = 0;
            sample_fusion_pixel(frame, fb_w, fb_h, box_x + x, box_y + y, &r, &g, &b);
            const int di = (y * box_w + x) * 3;
            cache[static_cast<size_t>(di)] = r;
            cache[static_cast<size_t>(di + 1)] = g;
            cache[static_cast<size_t>(di + 2)] = b;
        }
    }

    const int x0 = (fb_w - pad - w0) - box_x;
    const int x1 = (fb_w - pad - w1) - box_x;
    const int y0 = 4;
    const int y1 = y0 + line_h;
    stamp_black_text_rgb(cache.data(), box_w, box_h, x0, y0, lat_line, glyph_w, glyph_h);
    stamp_black_text_rgb(cache.data(), box_w, box_h, x1, y1, lon_line, glyph_w, glyph_h);
}

void blit_rgb_box(
    std::uint8_t* fb_mem,
    int fb_w,
    int fb_h,
    int line_bytes,
    int bpp,
    int red_off,
    int green_off,
    int blue_off,
    int red_len,
    int green_len,
    int blue_len,
    int box_x,
    int box_y,
    int box_w,
    int box_h,
    const std::vector<std::uint8_t>& rgb
) {
    if (box_w < 1 || box_h < 1
        || static_cast<int>(rgb.size()) < box_w * box_h * 3) {
        return;
    }
    for (int y = 0; y < box_h; ++y) {
        for (int x = 0; x < box_w; ++x) {
            const int si = (y * box_w + x) * 3;
            put_fb_rgb(
                fb_mem, fb_w, fb_h, line_bytes, bpp,
                red_off, green_off, blue_off, red_len, green_len, blue_len,
                box_x + x,
                box_y + y,
                rgb[static_cast<size_t>(si)],
                rgb[static_cast<size_t>(si + 1)],
                rgb[static_cast<size_t>(si + 2)]
            );
        }
    }
}

void blit_lidar_cache(
    std::uint8_t* fb_mem,
    int fb_w,
    int fb_h,
    int line_bytes,
    int bpp,
    int red_off,
    int green_off,
    int blue_off,
    int red_len,
    int green_len,
    int blue_len,
    int side,
    const std::vector<std::uint8_t>& lidar_rgb
) {
    if (side <= 0
        || static_cast<int>(lidar_rgb.size()) < side * side * 3) {
        return;
    }

    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side; ++x) {
            const int si = (y * side + x) * 3;
            put_fb_rgb(
                fb_mem, fb_w, fb_h, line_bytes, bpp,
                red_off, green_off, blue_off, red_len, green_len, blue_len,
                x,
                y,
                lidar_rgb[static_cast<size_t>(si)],
                lidar_rgb[static_cast<size_t>(si + 1)],
                lidar_rgb[static_cast<size_t>(si + 2)]
            );
        }
    }

    for (int i = 0; i < side; ++i) {
        put_fb_rgb(
            fb_mem, fb_w, fb_h, line_bytes, bpp,
            red_off, green_off, blue_off, red_len, green_len, blue_len,
            i, 0, 180, 180, 190
        );
        put_fb_rgb(
            fb_mem, fb_w, fb_h, line_bytes, bpp,
            red_off, green_off, blue_off, red_len, green_len, blue_len,
            i, side - 1, 180, 180, 190
        );
        put_fb_rgb(
            fb_mem, fb_w, fb_h, line_bytes, bpp,
            red_off, green_off, blue_off, red_len, green_len, blue_len,
            0, i, 180, 180, 190
        );
        put_fb_rgb(
            fb_mem, fb_w, fb_h, line_bytes, bpp,
            red_off, green_off, blue_off, red_len, green_len, blue_len,
            side - 1, i, 180, 180, 190
        );
    }
}

#endif

} // namespace

bool DisplayHud::init(int /*width*/, int /*height*/) {
    if (!enabled) {
        return true;
    }

#if !defined(__linux__)
    logutil::warn("DisplayHud: framebuffer HUD is Linux-only; disabled");
    enabled = false;
    return true;
#else
    if (!env_truthy("HUD_SKIP_VT")) {
        (void)claim_vt_graphics();
    } else {
        logutil::info("DisplayHud: HUD_SKIP_VT set; skipped console claim");
    }

    const auto paths = candidate_fb_paths();
    std::string opened;

    for (const std::string& path : paths) {
        fb_fd_ = open(path.c_str(), O_RDWR);
        if (fb_fd_ < 0) {
            logutil::warn(
                "DisplayHud: open " + path + " failed: " + std::strerror(errno)
            );
            continue;
        }

        fb_var_screeninfo vinfo{};
        fb_fix_screeninfo finfo{};
        if (ioctl(fb_fd_, FBIOGET_VSCREENINFO, &vinfo) < 0
            || ioctl(fb_fd_, FBIOGET_FSCREENINFO, &finfo) < 0) {
            logutil::warn("DisplayHud: fb ioctl failed on " + path);
            close(fb_fd_);
            fb_fd_ = -1;
            continue;
        }

        fb_w_ = static_cast<int>(vinfo.xres);
        fb_h_ = static_cast<int>(vinfo.yres);
        fb_bpp_ = static_cast<int>(vinfo.bits_per_pixel);
        fb_line_bytes_ = static_cast<int>(finfo.line_length);
        red_off_ = static_cast<int>(vinfo.red.offset);
        green_off_ = static_cast<int>(vinfo.green.offset);
        blue_off_ = static_cast<int>(vinfo.blue.offset);
        red_len_ = static_cast<int>(vinfo.red.length);
        green_len_ = static_cast<int>(vinfo.green.length);
        blue_len_ = static_cast<int>(vinfo.blue.length);
        if (red_len_ <= 0) {
            red_len_ = fb_bpp_ == 16 ? 5 : 8;
        }
        if (green_len_ <= 0) {
            green_len_ = fb_bpp_ == 16 ? 6 : 8;
        }
        if (blue_len_ <= 0) {
            blue_len_ = fb_bpp_ == 16 ? 5 : 8;
        }

        if (fb_bpp_ != 16 && fb_bpp_ != 32) {
            logutil::warn(
                "DisplayHud: unsupported bpp=" + std::to_string(fb_bpp_)
                + " on " + path
            );
            close(fb_fd_);
            fb_fd_ = -1;
            continue;
        }

        if (fb_w_ <= 0 || fb_h_ <= 0) {
            logutil::warn("DisplayHud: zero-sized fb on " + path);
            close(fb_fd_);
            fb_fd_ = -1;
            continue;
        }

        // Force the visible pan to the primary buffer so mmap writes show up.
        vinfo.xoffset = 0;
        vinfo.yoffset = 0;
        if (ioctl(fb_fd_, FBIOPUT_VSCREENINFO, &vinfo) != 0) {
            logutil::warn(
                std::string("DisplayHud: FBIOPUT_VSCREENINFO failed: ")
                + std::strerror(errno)
            );
        }
        (void)ioctl(fb_fd_, FBIOPAN_DISPLAY, &vinfo);

        fb_map_len_ = finfo.smem_len;
        fb_mem_ = static_cast<std::uint8_t*>(
            mmap(nullptr, fb_map_len_, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd_, 0)
        );
        if (fb_mem_ == MAP_FAILED) {
            logutil::warn(
                "DisplayHud: mmap failed on " + path + ": " + std::strerror(errno)
            );
            fb_mem_ = nullptr;
            close(fb_fd_);
            fb_fd_ = -1;
            continue;
        }

        opened = path;
        break;
    }

    if (fb_mem_ == nullptr || fb_fd_ < 0) {
        logutil::warn(
            "DisplayHud: no usable framebuffer. "
            "From SSH: sudo ./compile_and_run "
            "(stops the desktop automatically)"
        );
        release_vt_graphics();
        enabled = false;
        return true;
    }

    // RGB test bars so a blank panel is distinguishable from "not drawing".
    draw_test_bars(
        fb_mem_, fb_w_, fb_h_, fb_line_bytes_, fb_bpp_,
        red_off_, green_off_, blue_off_, red_len_, green_len_, blue_len_
    );
    flush_fb(fb_fd_, fb_mem_, fb_map_len_);
    lidar_side_ = lidar_square_side(fb_w_, fb_h_);
    lidar_cache_.clear();
    gps_cache_.clear();
    gps_box_x_ = gps_box_y_ = gps_box_w_ = gps_box_h_ = 0;
    logutil::info(
        std::string("DisplayHud fb ") + opened + " "
        + std::to_string(fb_w_) + "x" + std::to_string(fb_h_)
        + " bpp=" + std::to_string(fb_bpp_)
        + " lidar_square=" + std::to_string(lidar_side_)
        + "px (test bars flushed; if panel still dark, desktop still owns DRM)"
    );
    return true;
#endif
}

void DisplayHud::shutdown() {
#if defined(__linux__)
    if (fb_mem_ != nullptr) {
        munmap(fb_mem_, fb_map_len_);
        fb_mem_ = nullptr;
    }
    if (fb_fd_ >= 0) {
        close(fb_fd_);
        fb_fd_ = -1;
    }
    release_vt_graphics();
#endif
    lidar_cache_.clear();
    lidar_side_ = 0;
    gps_cache_.clear();
    gps_box_x_ = gps_box_y_ = gps_box_w_ = gps_box_h_ = 0;
    enabled = false;
}

bool DisplayHud::draw(
    const RgbFrame& frame,
    const LidarMapPayload* lidar_or_null,
    const HudOverlay& overlay
) {
    if (!enabled) {
        return true;
    }

#if !defined(__linux__)
    (void)frame;
    (void)lidar_or_null;
    (void)overlay;
    return true;
#else
    if (fb_mem_ == nullptr || frame.width <= 0 || frame.height <= 0
        || frame.rgb.size()
            < static_cast<size_t>(frame.width * frame.height * 3)) {
        return false;
    }

    if (lidar_side_ <= 0) {
        lidar_side_ = lidar_square_side(fb_w_, fb_h_);
    }

    // Build GPS overlay offscreen (video + black text) before touching the FB.
    compose_gps_cache(
        frame,
        fb_w_,
        fb_h_,
        overlay,
        gps_box_x_,
        gps_box_y_,
        gps_box_w_,
        gps_box_h_,
        gps_cache_
    );

    // Refresh cached lidar inset when a new scan arrives.
    if (lidar_or_null != nullptr) {
        LidarMap mapper;
        std::vector<std::uint8_t> rendered;
        if (mapper.render_hud(*lidar_or_null, lidar_side_, rendered)
            && static_cast<int>(rendered.size()) >= lidar_side_ * lidar_side_ * 3) {
            lidar_cache_.swap(rendered);
        }
    }

    // Fusion skips lidar + GPS rects; overlays are copied as finished tiles.
    blit_fusion(
        fb_mem_, fb_w_, fb_h_, fb_line_bytes_, fb_bpp_,
        red_off_, green_off_, blue_off_, red_len_, green_len_, blue_len_,
        frame, lidar_side_,
        gps_box_x_, gps_box_y_, gps_box_w_, gps_box_h_
    );

    if (!lidar_cache_.empty()) {
        blit_lidar_cache(
            fb_mem_, fb_w_, fb_h_, fb_line_bytes_, fb_bpp_,
            red_off_, green_off_, blue_off_, red_len_, green_len_, blue_len_,
            lidar_side_, lidar_cache_
        );
    } else {
        for (int y = 0; y < lidar_side_; ++y) {
            for (int x = 0; x < lidar_side_; ++x) {
                put_fb_rgb(
                    fb_mem_, fb_w_, fb_h_, fb_line_bytes_, fb_bpp_,
                    red_off_, green_off_, blue_off_,
                    red_len_, green_len_, blue_len_,
                    x, y, 12, 12, 16
                );
            }
        }
    }

    blit_rgb_box(
        fb_mem_, fb_w_, fb_h_, fb_line_bytes_, fb_bpp_,
        red_off_, green_off_, blue_off_, red_len_, green_len_, blue_len_,
        gps_box_x_, gps_box_y_, gps_box_w_, gps_box_h_, gps_cache_
    );

    flush_fb(fb_fd_, fb_mem_, fb_map_len_);
    poll_stdin_quit(quit_requested_);
    return true;
#endif
}

bool DisplayHud::poll_quit() {
    return quit_requested_;
}
