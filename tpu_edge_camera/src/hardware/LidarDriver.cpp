#include "hardware/LidarDriver.hpp"

#include "util/Log.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#if defined(__linux__)
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace {

static constexpr uint8_t k_header = 0x54;
static constexpr uint8_t k_verlen = 0x2C;
static constexpr int k_rx_chunk = 4096;
static constexpr int k_parse_buf = 8192;
static constexpr auto k_bin_ttl = std::chrono::milliseconds(250);

// CRC-8 table (poly 0x4D, init 0x00) from LDRobot LD19 manual.
static const uint8_t k_crc_table[256] = {
    0x00, 0x4d, 0x9a, 0xd7, 0x79, 0x34, 0xe3, 0xae, 0xf2, 0xbf, 0x68, 0x25,
    0x8b, 0xc6, 0x11, 0x5c, 0xa9, 0xe4, 0x33, 0x7e, 0xd0, 0x9d, 0x4a, 0x07,
    0x5b, 0x16, 0xc1, 0x8c, 0x22, 0x6f, 0xb8, 0xf5, 0x1f, 0x52, 0x85, 0xc8,
    0x66, 0x2b, 0xfc, 0xb1, 0xed, 0xa0, 0x77, 0x3a, 0x94, 0xd9, 0x0e, 0x43,
    0xb6, 0xfb, 0x2c, 0x61, 0xcf, 0x82, 0x55, 0x18, 0x44, 0x09, 0xde, 0x93,
    0x3d, 0x70, 0xa7, 0xea, 0x3e, 0x73, 0xa4, 0xe9, 0x47, 0x0a, 0xdd, 0x90,
    0xcc, 0x81, 0x56, 0x1b, 0xb5, 0xf8, 0x2f, 0x62, 0x97, 0xda, 0x0d, 0x40,
    0xee, 0xa3, 0x74, 0x39, 0x65, 0x28, 0xff, 0xb2, 0x1c, 0x51, 0x86, 0xcb,
    0x21, 0x6c, 0xbb, 0xf6, 0x58, 0x15, 0xc2, 0x8f, 0xd3, 0x9e, 0x49, 0x04,
    0xaa, 0xe7, 0x30, 0x7d, 0x88, 0xc5, 0x12, 0x5f, 0xf1, 0xbc, 0x6b, 0x26,
    0x7a, 0x37, 0xe0, 0xad, 0x03, 0x4e, 0x99, 0xd4, 0x7c, 0x31, 0xe6, 0xab,
    0x05, 0x48, 0x9f, 0xd2, 0x8e, 0xc3, 0x14, 0x59, 0xf7, 0xba, 0x6d, 0x20,
    0xd5, 0x98, 0x4f, 0x02, 0xac, 0xe1, 0x36, 0x7b, 0x27, 0x6a, 0xbd, 0xf0,
    0x5e, 0x13, 0xc4, 0x89, 0x63, 0x2e, 0xf9, 0xb4, 0x1a, 0x57, 0x80, 0xcd,
    0x91, 0xdc, 0x0b, 0x46, 0xe8, 0xa5, 0x72, 0x3f, 0xca, 0x87, 0x50, 0x1d,
    0xb3, 0xfe, 0x29, 0x64, 0x38, 0x75, 0xa2, 0xef, 0x41, 0x0c, 0xdb, 0x96,
    0x42, 0x0f, 0xd8, 0x95, 0x3b, 0x76, 0xa1, 0xec, 0xb0, 0xfd, 0x2a, 0x67,
    0xc9, 0x84, 0x53, 0x1e, 0xeb, 0xa6, 0x71, 0x3c, 0x92, 0xdf, 0x08, 0x45,
    0x19, 0x54, 0x83, 0xce, 0x60, 0x2d, 0xfa, 0xb7, 0x5d, 0x10, 0xc7, 0x8a,
    0x24, 0x69, 0xbe, 0xf3, 0xaf, 0xe2, 0x35, 0x78, 0xd6, 0x9b, 0x4c, 0x01,
    0xf4, 0xb9, 0x6e, 0x23, 0x8d, 0xc0, 0x17, 0x5a, 0x06, 0x4b, 0x9c, 0xd1,
    0x7f, 0x32, 0xe5, 0xa8
};

using SteadyClock = std::chrono::steady_clock;
using SteadyTime = SteadyClock::time_point;

#if defined(__linux__)
static int g_fd = -1;
static uint8_t g_rx[k_parse_buf]{};
static int g_rx_len = 0;
static bool g_live_ready = false;
static int g_empty_ticks = 0;
static SteadyTime g_bin_stamp[LidarDriver::bin_count]{};

speed_t baud_to_flag(int baud) {
    switch (baud) {
    case 115200:
        return B115200;
    case 230400:
        return B230400;
    case 460800:
        return B460800;
    default:
        return B230400;
    }
}

uint8_t cal_crc8(const uint8_t* p, int len) {
    uint8_t crc = 0;
    for (int i = 0; i < len; ++i) {
        crc = k_crc_table[(crc ^ p[i]) & 0xff];
    }
    return crc;
}

uint16_t read_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

void clear_bins(LidarDriver& self) {
    std::memset(self.distance_mm, 0, sizeof(self.distance_mm));
    std::memset(self.intensity, 0, sizeof(self.intensity));
    self.valid_bins = 0;
    for (int i = 0; i < LidarDriver::bin_count; ++i) {
        g_bin_stamp[i] = SteadyTime{};
    }
}

void recount_valid(LidarDriver& self) {
    int n = 0;
    for (int i = 0; i < LidarDriver::bin_count; ++i) {
        if (self.distance_mm[i] != 0) {
            ++n;
        }
    }
    self.valid_bins = n;
}

void expire_stale_bins(LidarDriver& self, SteadyTime now) {
    for (int i = 0; i < LidarDriver::bin_count; ++i) {
        if (self.distance_mm[i] == 0) {
            continue;
        }
        if (now - g_bin_stamp[i] > k_bin_ttl) {
            self.distance_mm[i] = 0;
            self.intensity[i] = 0;
            g_bin_stamp[i] = SteadyTime{};
        }
    }
    recount_valid(self);
}

void store_point(
    LidarDriver& self,
    float angle_deg,
    uint16_t dist_mm,
    uint8_t intens,
    SteadyTime now
) {
    if (dist_mm == 0) {
        return;
    }

    while (angle_deg < 0.0f) {
        angle_deg += 360.0f;
    }
    while (angle_deg >= 360.0f) {
        angle_deg -= 360.0f;
    }

    int bin = static_cast<int>(angle_deg * 2.0f + 0.5f) % LidarDriver::bin_count;
    if (bin < 0) {
        bin = 0;
    }

    self.distance_mm[bin] = dist_mm;
    self.intensity[bin] = intens;
    g_bin_stamp[bin] = now;
}

bool parse_frame(LidarDriver& self, const uint8_t* pkt, SteadyTime now) {
    if (pkt[0] != k_header || pkt[1] != k_verlen) {
        return false;
    }
    if (cal_crc8(pkt, LidarDriver::packet_bytes - 1) != pkt[LidarDriver::packet_bytes - 1]) {
        ++self.packets_bad;
        return false;
    }

    const uint16_t speed = read_u16(pkt + 2);
    float start_angle = static_cast<float>(read_u16(pkt + 4)) / 100.0f;
    float end_angle = static_cast<float>(read_u16(pkt + 42)) / 100.0f;
    if (end_angle < start_angle) {
        end_angle += 360.0f;
    }

    const float step =
        (end_angle - start_angle)
        / static_cast<float>(LidarDriver::points_per_packet - 1);
    const uint8_t* points = pkt + 6;

    for (int i = 0; i < LidarDriver::points_per_packet; ++i) {
        const uint16_t dist = read_u16(points + i * 3);
        const uint8_t intens = points[i * 3 + 2];
        store_point(
            self,
            start_angle + step * static_cast<float>(i),
            dist,
            intens,
            now
        );
    }

    self.speed_dps = static_cast<float>(speed);
    ++self.packets_ok;
    return true;
}

void consume_rx(LidarDriver& self) {
    if (g_fd < 0) {
        return;
    }

    for (;;) {
        uint8_t chunk[k_rx_chunk];
        const ssize_t n = ::read(g_fd, chunk, sizeof(chunk));
        if (n <= 0) {
            break;
        }

        if (g_rx_len + static_cast<int>(n) > k_parse_buf) {
            const int keep = k_parse_buf / 2;
            std::memmove(g_rx, g_rx + (g_rx_len - keep), static_cast<size_t>(keep));
            g_rx_len = keep;
        }

        std::memcpy(g_rx + g_rx_len, chunk, static_cast<size_t>(n));
        g_rx_len += static_cast<int>(n);
    }

    const SteadyTime now = SteadyClock::now();
    int i = 0;
    while (i + LidarDriver::packet_bytes <= g_rx_len) {
        if (g_rx[i] != k_header || g_rx[i + 1] != k_verlen) {
            ++i;
            continue;
        }
        parse_frame(self, g_rx + i, now);
        i += LidarDriver::packet_bytes;
    }

    if (i > 0) {
        const int left = g_rx_len - i;
        std::memmove(g_rx, g_rx + i, static_cast<size_t>(left));
        g_rx_len = left;
    }

    expire_stale_bins(self, now);
}

bool configure_port(int fd, int baud) {
    termios tty{};
    if (tcgetattr(fd, &tty) != 0) {
        return false;
    }

    cfmakeraw(&tty);
    const speed_t speed = baud_to_flag(baud);
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= static_cast<tcflag_t>(~CSIZE);
    tty.c_cflag |= CS8;
    tty.c_cflag &= static_cast<tcflag_t>(~PARENB);
    tty.c_cflag &= static_cast<tcflag_t>(~CSTOPB);
    tty.c_cflag &= static_cast<tcflag_t>(~CRTSCTS);
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        return false;
    }
    tcflush(fd, TCIOFLUSH);
    return true;
}

bool open_port(LidarDriver& self, const char* path) {
    const int fd = ::open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        return false;
    }
    if (!configure_port(fd, self.baud)) {
        ::close(fd);
        return false;
    }
    tcflush(fd, TCIOFLUSH);
    g_fd = fd;
    g_rx_len = 0;
    std::snprintf(self.port_path, sizeof(self.port_path), "%s", path);
    return true;
}

bool try_open_port(LidarDriver& self, const std::string& preferred) {
    if (!preferred.empty() && open_port(self, preferred.c_str())) {
        return true;
    }

    static const char* const k_fallbacks[] = {
        "/dev/serial0",
        "/dev/ttyAMA0",
        "/dev/ttyUSB0",
        "/dev/ttyS0",
    };
    for (const char* path : k_fallbacks) {
        if (!preferred.empty() && preferred == path) {
            continue;
        }
        if (open_port(self, path)) {
            return true;
        }
    }

    std::snprintf(
        self.last_error,
        sizeof(self.last_error),
        "no UART (tried %s + serial0/ttyAMA0/ttyUSB0)",
        preferred.empty() ? "?" : preferred.c_str()
    );
    return false;
}
#endif

void clear_bins_demo(LidarDriver& self) {
    std::memset(self.distance_mm, 0, sizeof(self.distance_mm));
    std::memset(self.intensity, 0, sizeof(self.intensity));
    self.valid_bins = 0;
}

void store_point_demo(
    LidarDriver& self,
    float angle_deg,
    uint16_t dist_mm,
    uint8_t intens
) {
    if (dist_mm == 0) {
        return;
    }
    while (angle_deg < 0.0f) {
        angle_deg += 360.0f;
    }
    while (angle_deg >= 360.0f) {
        angle_deg -= 360.0f;
    }
    int bin = static_cast<int>(angle_deg * 2.0f + 0.5f) % LidarDriver::bin_count;
    if (bin < 0) {
        bin = 0;
    }
    self.distance_mm[bin] = dist_mm;
    self.intensity[bin] = intens;
}

void recount_valid_demo(LidarDriver& self) {
    int n = 0;
    for (int i = 0; i < LidarDriver::bin_count; ++i) {
        if (self.distance_mm[i] != 0) {
            ++n;
        }
    }
    self.valid_bins = n;
}

void refresh_demo(LidarDriver& self) {
    clear_bins_demo(self);
    self.speed_dps = 3600.0f;
    const float t = static_cast<float>(self.frame_index) * 0.05f;

    for (int bin = 0; bin < LidarDriver::bin_count; ++bin) {
        const float deg = static_cast<float>(bin) * 0.5f;
        const float a = deg * 0.01745329252f;
        float dist = 2500.0f + 400.0f * std::sin(3.0f * a + t);
        if (deg > 40.0f && deg < 80.0f) {
            dist = 800.0f + 100.0f * std::sin(t * 4.0f);
        }
        if (deg > 200.0f && deg < 230.0f) {
            dist = 1500.0f;
        }
        store_point_demo(
            self,
            deg,
            static_cast<uint16_t>(dist),
            static_cast<uint8_t>(120 + (bin % 80))
        );
    }

    recount_valid_demo(self);
    ++self.frame_index;
}

} // namespace

bool LidarDriver::init(const std::string& serial_path, int serial_baud) {
    baud = serial_baud > 0 ? serial_baud : baud_default;
    frame_index = 0;
    packets_ok = 0;
    packets_bad = 0;
    speed_dps = 0.0f;
    port_path[0] = '\0';
    last_error[0] = '\0';

#if !defined(__linux__)
    ready = true;
    demo_mode = true;
    std::snprintf(last_error, sizeof(last_error), "Lidar serial only on Linux");
    logutil::warn(std::string("LidarDriver demo_mode: ") + last_error);
    refresh_demo(*this);
    return true;
#else
    g_live_ready = false;
    g_rx_len = 0;
    g_empty_ticks = 0;
    clear_bins(*this);

    if (g_fd >= 0) {
        ::close(g_fd);
        g_fd = -1;
    }

    if (try_open_port(*this, serial_path)) {
        demo_mode = false;
        ready = true;
        g_live_ready = true;
        logutil::info(
            "LidarDriver live path=" + std::string(port_path)
            + " baud=" + std::to_string(baud)
        );
        usleep(50 * 1000);
        return refresh();
    }

    demo_mode = true;
    ready = true;
    logutil::warn(std::string("LidarDriver demo_mode: ") + last_error);
    refresh_demo(*this);
    return true;
#endif
}

bool LidarDriver::refresh() {
    if (!ready) {
        return false;
    }

#if defined(__linux__)
    if (!demo_mode && g_live_ready && g_fd >= 0) {
        const int ok_before = packets_ok;
        consume_rx(*this);
        ++frame_index;

        if (packets_ok > ok_before) {
            g_empty_ticks = 0;
            last_error[0] = '\0';
            return true;
        }

        ++g_empty_ticks;
        if (g_empty_ticks > 60) {
            std::snprintf(
                last_error,
                sizeof(last_error),
                "no LD19 frames on %s (enable_uart?)",
                port_path[0] ? port_path : "?"
            );
            logutil::warn(std::string("LidarDriver falling back to demo: ") + last_error);
            demo_mode = true;
            g_live_ready = false;
            g_empty_ticks = 0;
            if (g_fd >= 0) {
                ::close(g_fd);
                g_fd = -1;
            }
        } else {
            return true;
        }
    }
#endif

    refresh_demo(*this);
    return true;
}

void LidarDriver::shutdown() {
#if defined(__linux__)
    g_live_ready = false;
    g_rx_len = 0;
    g_empty_ticks = 0;
    clear_bins(*this);
    if (g_fd >= 0) {
        ::close(g_fd);
        g_fd = -1;
    }
#else
    clear_bins_demo(*this);
#endif
    ready = false;
    demo_mode = true;
    frame_index = 0;
    packets_ok = 0;
    packets_bad = 0;
    port_path[0] = '\0';
}
