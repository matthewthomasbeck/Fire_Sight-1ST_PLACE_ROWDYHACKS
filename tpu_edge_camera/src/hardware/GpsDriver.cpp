#include "hardware/GpsDriver.hpp"

#include "util/Log.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(__linux__)
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace {

#if defined(__linux__)

speed_t baud_to_flag(int baud) {
    switch (baud) {
    case 4800:
        return B4800;
    case 9600:
        return B9600;
    case 19200:
        return B19200;
    case 38400:
        return B38400;
    case 57600:
        return B57600;
    case 115200:
        return B115200;
    default:
        return B9600;
    }
}

bool configure_port(int fd, int baud) {
    termios tio{};
    if (tcgetattr(fd, &tio) != 0) {
        return false;
    }

    cfmakeraw(&tio);
    const speed_t speed = baud_to_flag(baud);
    cfsetispeed(&tio, speed);
    cfsetospeed(&tio, speed);

    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= static_cast<tcflag_t>(~PARENB);
    tio.c_cflag &= static_cast<tcflag_t>(~CSTOPB);
    tio.c_cflag &= static_cast<tcflag_t>(~CSIZE);
    tio.c_cflag |= CS8;
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        return false;
    }
    tcflush(fd, TCIOFLUSH);
    return true;
}

#endif

std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : s) {
        if (ch == ',') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    out.push_back(cur);
    return out;
}

double nmea_to_degrees(const std::string& raw, const std::string& hemi) {
    if (raw.empty() || hemi.empty()) {
        return 0.0;
    }
    const double v = std::strtod(raw.c_str(), nullptr);
    const double abs_v = std::fabs(v);
    const int deg = static_cast<int>(abs_v / 100.0);
    const double minutes = abs_v - static_cast<double>(deg) * 100.0;
    double dec = static_cast<double>(deg) + minutes / 60.0;
    if (hemi[0] == 'S' || hemi[0] == 'W') {
        dec = -dec;
    }
    return dec;
}

// Match Vending_Machine_Robot gps.py: any talker ending in RMC ($GPRMC, $GNRMC, ...).
bool is_rmc_talker(const std::string& talker) {
    if (talker.size() < 6 || talker[0] != '$') {
        return false;
    }
    const size_t n = talker.size();
    return talker[n - 3] == 'R' && talker[n - 2] == 'M' && talker[n - 1] == 'C';
}

} // namespace

bool GpsDriver::init(const std::string& serial_path, int serial_baud) {
    port_path = serial_path;
    baud = serial_baud;
    last_error[0] = '\0';
    last_fix = {};
    line_buf_.clear();

#if !defined(__linux__)
    ready = true;
    demo_mode = true;
    std::snprintf(last_error, sizeof(last_error), "GPS serial only on Linux");
    logutil::warn(std::string("GpsDriver demo_mode: ") + last_error);
    return true;
#else
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }

    fd_ = ::open(port_path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) {
        ready = true;
        demo_mode = true;
        std::snprintf(
            last_error,
            sizeof(last_error),
            "open %s failed errno=%d",
            port_path.c_str(),
            errno
        );
        logutil::warn(std::string("GpsDriver demo_mode: ") + last_error);
        return true;
    }

    if (!configure_port(fd_, baud)) {
        std::snprintf(
            last_error,
            sizeof(last_error),
            "termios %s failed errno=%d",
            port_path.c_str(),
            errno
        );
        ::close(fd_);
        fd_ = -1;
        ready = true;
        demo_mode = true;
        logutil::warn(std::string("GpsDriver demo_mode: ") + last_error);
        return true;
    }

    ready = true;
    demo_mode = false;
    logutil::info(
        "GpsDriver live path=" + port_path + " baud=" + std::to_string(baud)
    );
    return true;
#endif
}

bool GpsDriver::refresh(double max_seconds) {
    if (!ready) {
        return false;
    }
    if (demo_mode || fd_ < 0) {
        return last_fix.valid;
    }

#if !defined(__linux__)
    return last_fix.valid;
#else
    if (max_seconds < 0.0) {
        max_seconds = 0.0;
    }
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
              std::chrono::duration<double>(max_seconds)
          );

    char chunk[256];
    while (std::chrono::steady_clock::now() <= deadline) {
        pollfd pfd{};
        pfd.fd = fd_;
        pfd.events = POLLIN;
        const auto remaining = deadline - std::chrono::steady_clock::now();
        int wait_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count()
        );
        if (wait_ms < 0) {
            wait_ms = 0;
        }

        const int pr = ::poll(&pfd, 1, wait_ms);
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::snprintf(last_error, sizeof(last_error), "poll errno=%d", errno);
            return last_fix.valid;
        }
        if (pr == 0) {
            break;
        }

        const ssize_t n = ::read(fd_, chunk, sizeof(chunk));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
            }
            std::snprintf(last_error, sizeof(last_error), "read errno=%d", errno);
            return last_fix.valid;
        }
        if (n == 0) {
            break;
        }

        bytes_read += static_cast<std::uint64_t>(n);
        line_buf_.append(chunk, static_cast<size_t>(n));

        // Cap runaway buffer if the module spews binary noise.
        if (line_buf_.size() > 4096) {
            line_buf_.erase(0, line_buf_.size() - 1024);
        }

        for (;;) {
            const auto nl = line_buf_.find('\n');
            if (nl == std::string::npos) {
                break;
            }
            std::string line = line_buf_.substr(0, nl);
            line_buf_.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) {
                continue;
            }
            ++lines_seen;

            GpsFix parsed;
            if (parse_gprmc(line, parsed)) {
                last_rmc_status = parsed.valid ? 'A' : 'V';
                if (parsed.valid) {
                    ++rmc_valid;
                    last_fix = parsed;
                } else {
                    ++rmc_void;
                    // Void RMC: keep last lat/lon, clear valid (HUD shows UNKNOWN).
                    last_fix.valid = false;
                    if (!parsed.utc_time.empty()) {
                        last_fix.utc_time = parsed.utc_time;
                    }
                    if (!parsed.date.empty()) {
                        last_fix.date = parsed.date;
                    }
                }
                last_error[0] = '\0';
            } else {
                // Remember non-RMC talker for diagnostics (e.g. GGA-only stream).
                const auto dollar = line.find('$');
                if (dollar != std::string::npos) {
                    const auto comma = line.find(',', dollar);
                    if (comma != std::string::npos && comma > dollar) {
                        last_nmea_tag = line.substr(dollar, comma - dollar);
                    }
                }
            }
        }
    }

    return last_fix.valid;
#endif
}

void GpsDriver::shutdown() {
#if defined(__linux__)
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
#endif
    ready = false;
    line_buf_.clear();
}

bool GpsDriver::parse_gprmc(const std::string& line, GpsFix& out) {
    out = {};

    std::string s = line;
    // Accept leading junk before '$'.
    const auto dollar = s.find('$');
    if (dollar == std::string::npos) {
        return false;
    }
    s = s.substr(dollar);

    const auto star = s.find('*');
    if (star != std::string::npos) {
        s = s.substr(0, star);
    }

    const auto fields = split_csv(s);
    // $xxRMC,time,status,lat,N/S,lon,E/W,speed,course,date,...
    if (fields.size() < 10) {
        return false;
    }

    const std::string& talker = fields[0];
    if (!is_rmc_talker(talker)) {
        return false;
    }

    const std::string& status = fields[2];
    if (status != "A") {
        out.valid = false;
        out.utc_time = fields[1];
        out.date = fields[9];
        return true; // parsed void fix; caller may keep prior valid coords
    }

    // Require lat/lon + hemispheres (same as gps.py rejecting None coords).
    if (fields[3].empty() || fields[4].empty() || fields[5].empty() || fields[6].empty()) {
        out.valid = false;
        out.utc_time = fields[1];
        out.date = fields[9];
        return true;
    }

    out.utc_time = fields[1];
    out.date = fields[9];
    out.latitude = nmea_to_degrees(fields[3], fields[4]);
    out.longitude = nmea_to_degrees(fields[5], fields[6]);
    out.speed_knots = fields[7].empty() ? 0.0 : std::strtod(fields[7].c_str(), nullptr);
    out.course_deg = fields[8].empty() ? 0.0 : std::strtod(fields[8].c_str(), nullptr);
    out.valid = true;
    return true;
}
