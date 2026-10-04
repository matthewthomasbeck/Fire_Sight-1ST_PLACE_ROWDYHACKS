#include "vision/DisplayHud.hpp"

#include "util/Log.hpp"

#include <algorithm>
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
        if (ioctl(fd, VT_ACTIVATE, vt_no) != 0) {
            logutil::warn(
                std::string("DisplayHud: VT_ACTIVATE failed: ") + std::strerror(errno)
            );
        } else if (ioctl(fd, VT_WAITACTIVE, vt_no) != 0) {
            logutil::warn(
                std::string("DisplayHud: VT_WAITACTIVE failed: ") + std::strerror(errno)
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
        // Keep fd so we can still try fb; caller decides.
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
    // Always try; SSH without sudo often fails but fb mmap may still work
    // after the desktop is stopped.
    (void)claim_vt_graphics();
    if (env_truthy("HUD_SKIP_VT")) {
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

    // RGB test bars for ~0.4s so a blank panel is distinguishable from "not drawing".
    draw_test_bars(
        fb_mem_, fb_w_, fb_h_, fb_line_bytes_, fb_bpp_,
        red_off_, green_off_, blue_off_
    );
    logutil::info(
        std::string("DisplayHud fb ") + opened + " "
        + std::to_string(fb_w_) + "x" + std::to_string(fb_h_)
        + " bpp=" + std::to_string(fb_bpp_)
        + " (test bars drawn; if panel stays dark/desktop, stop the DE)"
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
    enabled = false;
}

bool DisplayHud::draw(
    const RgbFrame& frame,
    const LidarMapPayload* lidar_or_null,
    const HudOverlay& /*overlay*/
) {
    if (!enabled) {
        return true;
    }

#if !defined(__linux__)
    (void)frame;
    (void)lidar_or_null;
    return true;
#else
    if (fb_mem_ == nullptr || frame.width <= 0 || frame.height <= 0
        || frame.rgb.size()
            < static_cast<size_t>(frame.width * frame.height * 3)) {
        return false;
    }

    const int src_w = frame.width;
    const int src_h = frame.height;
    const std::uint8_t* src = frame.rgb.data();
    const int bytes_pp = fb_bpp_ / 8;

    const float scale = std::min(
        static_cast<float>(fb_w_) / static_cast<float>(src_w),
        static_cast<float>(fb_h_) / static_cast<float>(src_h)
    );
    const int dst_w = std::max(1, static_cast<int>(src_w * scale + 0.5f));
    const int dst_h = std::max(1, static_cast<int>(src_h * scale + 0.5f));
    const int origin_x = (fb_w_ - dst_w) / 2;
    const int origin_y = (fb_h_ - dst_h) / 2;

    for (int y = 0; y < fb_h_; ++y) {
        std::uint8_t* row = fb_mem_ + static_cast<std::size_t>(y) * fb_line_bytes_;
        const int sy_img = y - origin_y;
        for (int x = 0; x < fb_w_; ++x) {
            const int sx_img = x - origin_x;
            std::uint8_t r = 0;
            std::uint8_t g = 0;
            std::uint8_t b = 0;
            if (sx_img >= 0 && sy_img >= 0 && sx_img < dst_w && sy_img < dst_h) {
                const int sx = (sx_img * src_w) / dst_w;
                const int sy = (sy_img * src_h) / dst_h;
                const int si = (sy * src_w + sx) * 3;
                r = src[si];
                g = src[si + 1];
                b = src[si + 2];
            }
            const std::uint32_t px = pack_pixel(
                fb_bpp_, red_off_, green_off_, blue_off_, r, g, b
            );
            if (bytes_pp == 2) {
                *reinterpret_cast<std::uint16_t*>(row + x * 2) =
                    static_cast<std::uint16_t>(px);
            } else {
                *reinterpret_cast<std::uint32_t*>(row + x * 4) = px;
            }
        }
    }

    // Bottom-right polar inset from LD19 scan (same palette as LidarScreen).
    if (lidar_or_null != nullptr) {
        const int inset = std::clamp(std::min(fb_w_, fb_h_) / 3, 96, 220);
        std::vector<std::uint8_t> lidar_rgb;
        LidarMap mapper;
        if (mapper.render_hud(*lidar_or_null, inset, lidar_rgb)
            && static_cast<int>(lidar_rgb.size()) >= inset * inset * 3) {
            const int pad = 8;
            const int x0 = std::max(0, fb_w_ - inset - pad);
            const int y0 = std::max(0, fb_h_ - inset - pad);
            for (int y = 0; y < inset; ++y) {
                const int fy = y0 + y;
                if (fy < 0 || fy >= fb_h_) {
                    continue;
                }
                std::uint8_t* row =
                    fb_mem_ + static_cast<std::size_t>(fy) * fb_line_bytes_;
                for (int x = 0; x < inset; ++x) {
                    const int fx = x0 + x;
                    if (fx < 0 || fx >= fb_w_) {
                        continue;
                    }
                    const int si = (y * inset + x) * 3;
                    const std::uint8_t r = lidar_rgb[static_cast<size_t>(si)];
                    const std::uint8_t g = lidar_rgb[static_cast<size_t>(si + 1)];
                    const std::uint8_t b = lidar_rgb[static_cast<size_t>(si + 2)];
                    if (r == 0 && g == 0 && b == 0) {
                        continue; // keep fusion under outside-disk pixels
                    }
                    const std::uint32_t px = pack_pixel(
                        fb_bpp_, red_off_, green_off_, blue_off_, r, g, b
                    );
                    if (bytes_pp == 2) {
                        *reinterpret_cast<std::uint16_t*>(row + fx * 2) =
                            static_cast<std::uint16_t>(px);
                    } else {
                        *reinterpret_cast<std::uint32_t*>(row + fx * 4) = px;
                    }
                }
            }
        }
    }

    poll_stdin_quit(quit_requested_);
    return true;
#endif
}

bool DisplayHud::poll_quit() {
    return quit_requested_;
}
