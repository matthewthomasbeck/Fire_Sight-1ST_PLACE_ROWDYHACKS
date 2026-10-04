#include "vision/DisplayHud.hpp"

#include "util/Log.hpp"

#include <cstdlib>
#include <string>

#if defined(__linux__)
#include <fcntl.h>
#include <linux/fb.h>
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
    const char* path = std::getenv("HUD_FB_DEVICE");
    if (path == nullptr || path[0] == '\0') {
        path = "/dev/fb0";
    }

    fb_fd_ = open(path, O_RDWR);
    if (fb_fd_ < 0) {
        logutil::warn(std::string("DisplayHud: open ") + path + " failed");
        enabled = false;
        return true;
    }

    fb_var_screeninfo vinfo{};
    fb_fix_screeninfo finfo{};
    if (ioctl(fb_fd_, FBIOGET_VSCREENINFO, &vinfo) < 0
        || ioctl(fb_fd_, FBIOGET_FSCREENINFO, &finfo) < 0) {
        logutil::warn("DisplayHud: fb ioctl failed");
        close(fb_fd_);
        fb_fd_ = -1;
        enabled = false;
        return true;
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
        );
        close(fb_fd_);
        fb_fd_ = -1;
        enabled = false;
        return true;
    }

    fb_map_len_ = finfo.smem_len;
    fb_mem_ = static_cast<std::uint8_t*>(
        mmap(nullptr, fb_map_len_, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd_, 0)
    );
    if (fb_mem_ == MAP_FAILED) {
        logutil::warn("DisplayHud: mmap failed");
        fb_mem_ = nullptr;
        close(fb_fd_);
        fb_fd_ = -1;
        enabled = false;
        return true;
    }

    logutil::info(
        std::string("DisplayHud fb ") + path + " "
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

    for (int y = 0; y < fb_h_; ++y) {
        const int sy = (y * src_h) / fb_h_;
        std::uint8_t* row = fb_mem_ + static_cast<std::size_t>(y) * fb_line_bytes_;
        for (int x = 0; x < fb_w_; ++x) {
            const int sx = (x * src_w) / fb_w_;
            const int si = (sy * src_w + sx) * 3;
            const std::uint32_t px = pack_pixel(
                fb_bpp_,
                red_off_,
                green_off_,
                blue_off_,
                src[si],
                src[si + 1],
                src[si + 2]
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
