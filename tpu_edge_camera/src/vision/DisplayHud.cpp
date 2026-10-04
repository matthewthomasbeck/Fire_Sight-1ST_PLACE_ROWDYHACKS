#include "vision/DisplayHud.hpp"

#include "util/Log.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(__linux__)
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <poll.h>
#endif

namespace {

#if defined(__linux__)

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
        // RGB565
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

void unblank_and_claim_graphics(const char* fb_path) {
    // Wake panel if blanked by console / DPMS helpers.
    write_sysfs("/sys/class/graphics/fb0/blank", "0");
    write_sysfs("/sys/class/graphics/fb1/blank", "0");

    // Prefer graphics mode on the active VT so the console font is not painted
    // over the mmap'd framebuffer. Harmless if this is not a local tty.
    const int tty = open("/dev/tty0", O_RDWR | O_NOCTTY);
    if (tty >= 0) {
        if (ioctl(tty, KDSETMODE, KD_GRAPHICS) != 0) {
            // Non-fatal: SSH / no VT ownership.
        }
        ::close(tty);
    }

    (void)fb_path;
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
    const auto paths = candidate_fb_paths();
    std::string opened;

    for (const std::string& path : paths) {
        fb_fd_ = open(path.c_str(), O_RDWR);
        if (fb_fd_ < 0) {
            logutil::warn("DisplayHud: open " + path + " failed");
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
            logutil::warn("DisplayHud: mmap failed on " + path);
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
            "If a desktop owns the panel, boot to console or set HUD_ENABLED=false. "
            "User needs video group: sudo usermod -aG video $USER"
        );
        enabled = false;
        return true;
    }

    unblank_and_claim_graphics(opened.c_str());

    // Fill black so a successful open is obvious even before first frame.
    std::memset(fb_mem_, 0, fb_map_len_);

    logutil::info(
        std::string("DisplayHud fb ") + opened + " "
        + std::to_string(fb_w_) + "x" + std::to_string(fb_h_)
        + " bpp=" + std::to_string(fb_bpp_)
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
    const int tty = open("/dev/tty0", O_RDWR | O_NOCTTY);
    if (tty >= 0) {
        (void)ioctl(tty, KDSETMODE, KD_TEXT);
        ::close(tty);
    }
#endif
    enabled = false;
}

bool DisplayHud::draw(
    const RgbFrame& frame,
    const LidarMapPayload* /*lidar_or_null*/,
    const HudOverlay& /*overlay*/
) {
    if (!enabled) {
        return true;
    }

#if !defined(__linux__)
    (void)frame;
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

    // Letterbox scale (contain) so 320x240 fills the panel without stretch.
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

    poll_stdin_quit(quit_requested_);
    return true;
#endif
}

bool DisplayHud::poll_quit() {
    return quit_requested_;
}
