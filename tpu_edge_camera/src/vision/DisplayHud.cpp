#include "vision/DisplayHud.hpp"

#include "util/Log.hpp"

#include <algorithm>
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
    std::uint8_t r,
    std::uint8_t g,
    std::uint8_t b
) {
    if (bpp == 16) {
        return static_cast<std::uint32_t>(
            ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        );
    }
    return (static_cast<std::uint32_t>(r) << red_off)
        | (static_cast<std::uint32_t>(g) << green_off)
        | (static_cast<std::uint32_t>(b) << blue_off);
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

// 5x7 glyphs, MSB = left. Covers LAT/LON/UNKNOWN + digits/sign/dot/colon/space.
constexpr int k_font_w = 5;
constexpr int k_font_h = 7;

const std::uint8_t* glyph5x7(char ch) {
    static const std::uint8_t g_space[7] = {0, 0, 0, 0, 0, 0, 0};
    static const std::uint8_t g_colon[7] = {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00};
    static const std::uint8_t g_dot[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C};
    static const std::uint8_t g_minus[7] = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
    static const std::uint8_t g_plus[7] = {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00};
    static const std::uint8_t g_0[7] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
    static const std::uint8_t g_1[7] = {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E};
    static const std::uint8_t g_2[7] = {0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F};
    static const std::uint8_t g_3[7] = {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E};
    static const std::uint8_t g_4[7] = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
    static const std::uint8_t g_5[7] = {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E};
    static const std::uint8_t g_6[7] = {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E};
    static const std::uint8_t g_7[7] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
    static const std::uint8_t g_8[7] = {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
    static const std::uint8_t g_9[7] = {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C};
    static const std::uint8_t g_A[7] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    static const std::uint8_t g_K[7] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
    static const std::uint8_t g_L[7] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
    static const std::uint8_t g_N[7] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
    static const std::uint8_t g_O[7] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    static const std::uint8_t g_T[7] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    static const std::uint8_t g_U[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    static const std::uint8_t g_W[7] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A};

    switch (ch) {
    case ' ':
        return g_space;
    case ':':
        return g_colon;
    case '.':
        return g_dot;
    case '-':
        return g_minus;
    case '+':
        return g_plus;
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

void put_fb_rgb(
    std::uint8_t* fb_mem,
    int fb_w,
    int fb_h,
    int line_bytes,
    int bpp,
    int red_off,
    int green_off,
    int blue_off,
    int x,
    int y,
    std::uint8_t r,
    std::uint8_t g,
    std::uint8_t b
) {
    if (x < 0 || y < 0 || x >= fb_w || y >= fb_h || fb_mem == nullptr) {
        return;
    }
    const int bytes_pp = bpp / 8;
    std::uint8_t* row = fb_mem + static_cast<std::size_t>(y) * line_bytes;
    const std::uint32_t px = pack_pixel(bpp, red_off, green_off, blue_off, r, g, b);
    if (bytes_pp == 2) {
        *reinterpret_cast<std::uint16_t*>(row + x * 2) = static_cast<std::uint16_t>(px);
    } else {
        *reinterpret_cast<std::uint32_t*>(row + x * 4) = px;
    }
}

// Take over a text VT so /dev/fb0 actually paints the DSI panel.
// From SSH this needs root (or tty group) and the desktop must be stopped.
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
                + "). From SSH run: sudo ./compile_and_run   "
                  "and stop the desktop (sudo systemctl stop lightdm)"
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
            + " (desktop may own the panel)"
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
    int blue_off
) {
    const int bytes_pp = bpp / 8;
    for (int y = 0; y < h; ++y) {
        std::uint8_t* row = mem + static_cast<std::size_t>(y) * line_bytes;
        for (int x = 0; x < w; ++x) {
            const int band = (x * 3) / std::max(1, w);
            const std::uint8_t r = band == 0 ? 255 : 0;
            const std::uint8_t g = band == 1 ? 255 : 0;
            const std::uint8_t b = band == 2 ? 255 : 0;
            const std::uint32_t px = pack_pixel(
                bpp, red_off, green_off, blue_off, r, g, b
            );
            if (bytes_pp == 2) {
                *reinterpret_cast<std::uint16_t*>(row + x * 2) =
                    static_cast<std::uint16_t>(px);
            } else {
                *reinterpret_cast<std::uint32_t*>(row + x * 4) = px;
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
    const RgbFrame& frame,
    int skip_side
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
        std::uint8_t* row = fb_mem + static_cast<std::size_t>(y) * line_bytes;
        const int sy_img = y - origin_y;
        for (int x = 0; x < fb_w; ++x) {
            // Leave the lidar instrument rect alone so it never flashes under video.
            if (x < skip_side && y < skip_side) {
                continue;
            }

            const int sx_img = x - origin_x;
            std::uint8_t r = 0;
            std::uint8_t g = 0;
            std::uint8_t b = 0;
            if (sx_img >= 0 && sy_img >= 0 && sx_img < dst_w && sy_img < dst_h) {
                // Vertical flip (upright) + horizontal flip (undo mirror from mount).
                const int sx = (src_w - 1) - (sx_img * src_w) / dst_w;
                const int sy = (src_h - 1) - (sy_img * src_h) / dst_h;
                const int si = (sy * src_w + sx) * 3;
                r = src[si];
                g = src[si + 1];
                b = src[si + 2];
            }
            const std::uint32_t px = pack_pixel(
                bpp, red_off, green_off, blue_off, r, g, b
            );
            if (bytes_pp == 2) {
                *reinterpret_cast<std::uint16_t*>(row + x * 2) =
                    static_cast<std::uint16_t>(px);
            } else {
                *reinterpret_cast<std::uint32_t*>(row + x * 4) = px;
            }
        }
    }
}

void draw_char_scaled(
    std::uint8_t* fb_mem,
    int fb_w,
    int fb_h,
    int line_bytes,
    int bpp,
    int red_off,
    int green_off,
    int blue_off,
    int x0,
    int y0,
    char ch,
    int scale
) {
    const std::uint8_t* g = glyph5x7(ch);
    for (int row = 0; row < k_font_h; ++row) {
        const std::uint8_t bits = g[row];
        for (int col = 0; col < k_font_w; ++col) {
            if (((bits >> (k_font_w - 1 - col)) & 1) == 0) {
                continue;
            }
            for (int dy = 0; dy < scale; ++dy) {
                for (int dx = 0; dx < scale; ++dx) {
                    const int px = x0 + col * scale + dx;
                    const int py = y0 + row * scale + dy;
                    // Dark outline for contrast on bright fusion.
                    for (int oy = -1; oy <= 1; ++oy) {
                        for (int ox = -1; ox <= 1; ++ox) {
                            if (ox == 0 && oy == 0) {
                                continue;
                            }
                            put_fb_rgb(
                                fb_mem, fb_w, fb_h, line_bytes, bpp,
                                red_off, green_off, blue_off,
                                px + ox, py + oy, 0, 0, 0
                            );
                        }
                    }
                    put_fb_rgb(
                        fb_mem, fb_w, fb_h, line_bytes, bpp,
                        red_off, green_off, blue_off,
                        px, py, 255, 255, 255
                    );
                }
            }
        }
    }
}

void draw_text_scaled(
    std::uint8_t* fb_mem,
    int fb_w,
    int fb_h,
    int line_bytes,
    int bpp,
    int red_off,
    int green_off,
    int blue_off,
    int x0,
    int y0,
    const char* text,
    int scale
) {
    if (text == nullptr) {
        return;
    }
    const int advance = (k_font_w + 1) * scale;
    int x = x0;
    for (const char* p = text; *p != '\0'; ++p) {
        draw_char_scaled(
            fb_mem, fb_w, fb_h, line_bytes, bpp,
            red_off, green_off, blue_off, x, y0, *p, scale
        );
        x += advance;
    }
}

int text_pixel_width(const char* text, int scale) {
    if (text == nullptr || text[0] == '\0') {
        return 0;
    }
    const int n = static_cast<int>(std::strlen(text));
    return n * (k_font_w + 1) * scale - scale;
}

void draw_gps_overlay(
    std::uint8_t* fb_mem,
    int fb_w,
    int fb_h,
    int line_bytes,
    int bpp,
    int red_off,
    int green_off,
    int blue_off,
    const HudOverlay& overlay
) {
    char lat_line[48];
    char lon_line[48];
    if (overlay.gps_valid) {
        std::snprintf(lat_line, sizeof(lat_line), "LAT: %.7f", overlay.lat);
        std::snprintf(lon_line, sizeof(lon_line), "LON: %.7f", overlay.lon);
    } else {
        std::snprintf(lat_line, sizeof(lat_line), "LAT: UNKNOWN");
        std::snprintf(lon_line, sizeof(lon_line), "LON: UNKNOWN");
    }

    // Large type; scale with panel width so 800x480 stays readable.
    const int scale = std::clamp(fb_w / 160, 3, 5);
    const int pad = 10;
    const int line_h = k_font_h * scale + scale * 2;
    const int y0 = pad;
    const int y1 = pad + line_h;

    const int w0 = text_pixel_width(lat_line, scale);
    const int w1 = text_pixel_width(lon_line, scale);
    const int x0 = std::max(pad, fb_w - pad - w0);
    const int x1 = std::max(pad, fb_w - pad - w1);

    draw_text_scaled(
        fb_mem, fb_w, fb_h, line_bytes, bpp,
        red_off, green_off, blue_off, x0, y0, lat_line, scale
    );
    draw_text_scaled(
        fb_mem, fb_w, fb_h, line_bytes, bpp,
        red_off, green_off, blue_off, x1, y1, lon_line, scale
    );
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
                red_off, green_off, blue_off,
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
            red_off, green_off, blue_off, i, 0, 180, 180, 190
        );
        put_fb_rgb(
            fb_mem, fb_w, fb_h, line_bytes, bpp,
            red_off, green_off, blue_off, i, side - 1, 180, 180, 190
        );
        put_fb_rgb(
            fb_mem, fb_w, fb_h, line_bytes, bpp,
            red_off, green_off, blue_off, 0, i, 180, 180, 190
        );
        put_fb_rgb(
            fb_mem, fb_w, fb_h, line_bytes, bpp,
            red_off, green_off, blue_off, side - 1, i, 180, 180, 190
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
            "User needs video group + console VT. "
            "From SSH: sudo systemctl stop lightdm; sudo ./compile_and_run"
        );
        release_vt_graphics();
        enabled = false;
        return true;
    }

    // RGB test bars so a blank panel is distinguishable from "not drawing".
    draw_test_bars(
        fb_mem_, fb_w_, fb_h_, fb_line_bytes_, fb_bpp_,
        red_off_, green_off_, blue_off_
    );
    lidar_side_ = lidar_square_side(fb_w_, fb_h_);
    lidar_cache_.clear();
    logutil::info(
        std::string("DisplayHud fb ") + opened + " "
        + std::to_string(fb_w_) + "x" + std::to_string(fb_h_)
        + " bpp=" + std::to_string(fb_bpp_)
        + " lidar_square=" + std::to_string(lidar_side_)
        + "px (test bars drawn; if panel stays dark/desktop, stop the DE)"
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

    // Refresh cached lidar inset when a new scan arrives.
    if (lidar_or_null != nullptr) {
        LidarMap mapper;
        std::vector<std::uint8_t> rendered;
        if (mapper.render_hud(*lidar_or_null, lidar_side_, rendered)
            && static_cast<int>(rendered.size()) >= lidar_side_ * lidar_side_ * 3) {
            lidar_cache_.swap(rendered);
        }
    }

    // Fusion never paints the lidar rect; cache is redrawn every frame on top.
    blit_fusion(
        fb_mem_, fb_w_, fb_h_, fb_line_bytes_, fb_bpp_,
        red_off_, green_off_, blue_off_, frame, lidar_side_
    );

    if (!lidar_cache_.empty()) {
        blit_lidar_cache(
            fb_mem_, fb_w_, fb_h_, fb_line_bytes_, fb_bpp_,
            red_off_, green_off_, blue_off_, lidar_side_, lidar_cache_
        );
    } else {
        // Placeholder until the first scan so the reserved rect is not empty.
        for (int y = 0; y < lidar_side_; ++y) {
            for (int x = 0; x < lidar_side_; ++x) {
                put_fb_rgb(
                    fb_mem_, fb_w_, fb_h_, fb_line_bytes_, fb_bpp_,
                    red_off_, green_off_, blue_off_,
                    x, y, 12, 12, 16
                );
            }
        }
    }

    draw_gps_overlay(
        fb_mem_, fb_w_, fb_h_, fb_line_bytes_, fb_bpp_,
        red_off_, green_off_, blue_off_, overlay
    );

    poll_stdin_quit(quit_requested_);
    return true;
#endif
}

bool DisplayHud::poll_quit() {
    return quit_requested_;
}
