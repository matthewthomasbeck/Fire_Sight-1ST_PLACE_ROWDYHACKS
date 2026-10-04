#include "net/UplinkClient.hpp"

#include "util/Base64.hpp"
#include "util/Log.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>

namespace {

struct ParsedUrl {
    std::string host;
    int port = 80;
    std::string path = "/";
};

bool parse_http_url(const std::string& url, ParsedUrl& out) {
    const std::string prefix = "http://";
    if (url.rfind(prefix, 0) != 0) {
        return false;
    }
    std::string rest = url.substr(prefix.size());
    const auto slash = rest.find('/');
    std::string hostport = slash == std::string::npos ? rest : rest.substr(0, slash);
    out.path = slash == std::string::npos ? "/" : rest.substr(slash);

    const auto colon = hostport.find(':');
    if (colon == std::string::npos) {
        out.host = hostport;
        out.port = 80;
    } else {
        out.host = hostport.substr(0, colon);
        out.port = std::atoi(hostport.substr(colon + 1).c_str());
    }
    return !out.host.empty() && out.port > 0;
}

bool connect_tcp(const std::string& host, int port, int& fd_out) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    const std::string port_str = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0 || res == nullptr) {
        return false;
    }

    int fd = -1;
    for (addrinfo* p = res; p != nullptr; p = p->ai_next) {
        fd = static_cast<int>(::socket(p->ai_family, p->ai_socktype, p->ai_protocol));
        if (fd < 0) {
            continue;
        }
        if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
            break;
        }
        ::close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        return false;
    }
    fd_out = fd;
    return true;
}

bool send_all(int fd, const char* data, std::size_t len) {
    std::size_t sent = 0;
    while (sent < len) {
        const ssize_t n = ::send(fd, data + sent, len - sent, 0);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool recv_http_response(int fd, int& status_out, std::string& response_out, bool keep_alive) {
    std::string resp;
    char buf[4096];
    status_out = 0;

    // Read until headers complete, then Content-Length body (keep-alive safe).
    while (resp.find("\r\n\r\n") == std::string::npos) {
        const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n < 0) {
            return false;
        }
        if (n == 0) {
            break;
        }
        resp.append(buf, static_cast<std::size_t>(n));
        if (resp.size() > 1024 * 1024) {
            return false;
        }
    }

    if (resp.rfind("HTTP/", 0) == 0) {
        const auto sp = resp.find(' ');
        if (sp != std::string::npos) {
            status_out = std::atoi(resp.c_str() + sp + 1);
        }
    }

    const auto hdr_end = resp.find("\r\n\r\n");
    if (hdr_end == std::string::npos) {
        response_out = resp;
        return status_out > 0;
    }

    const std::string headers = resp.substr(0, hdr_end);
    response_out = resp.substr(hdr_end + 4);

    std::size_t content_length = 0;
    bool have_cl = false;
    {
        std::string lower = headers;
        for (char& c : lower) {
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        const auto cl = lower.find("content-length:");
        if (cl != std::string::npos) {
            content_length = static_cast<std::size_t>(
                std::strtoul(lower.c_str() + cl + 15, nullptr, 10)
            );
            have_cl = true;
        }
    }

    if (have_cl) {
        while (response_out.size() < content_length) {
            const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
            if (n < 0) {
                return false;
            }
            if (n == 0) {
                break;
            }
            response_out.append(buf, static_cast<std::size_t>(n));
        }
        if (response_out.size() > content_length) {
            response_out.resize(content_length);
        }
    } else if (!keep_alive) {
        while (true) {
            const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
            if (n < 0) {
                return false;
            }
            if (n == 0) {
                break;
            }
            response_out.append(buf, static_cast<std::size_t>(n));
        }
    }

    return status_out > 0;
}

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

} // namespace

void UplinkClient::close_socket_locked() {
    if (sock_fd_ >= 0) {
        ::close(sock_fd_);
        sock_fd_ = -1;
    }
    sock_host_.clear();
    sock_port_ = 0;
}

bool UplinkClient::ensure_socket_locked(const std::string& host, int port) {
    if (sock_fd_ >= 0 && sock_host_ == host && sock_port_ == port) {
        return true;
    }
    close_socket_locked();
    int fd = -1;
    if (!connect_tcp(host, port, fd)) {
        return false;
    }
    sock_fd_ = fd;
    sock_host_ = host;
    sock_port_ = port;
    return true;
}

bool UplinkClient::init(const Config& cfg, Auth0M2M* auth_or_null) {
    cfg_ = cfg;
    auth_ = auth_or_null;
    last_ok_ = false;
    last_error_.clear();
    frame_mode_ = 0;
    logutil::info("UplinkClient base=" + cfg_.ec2_base_url);
    return true;
}

void UplinkClient::shutdown() {
    auth_ = nullptr;
    std::lock_guard<std::mutex> lock(sock_mu_);
    close_socket_locked();
}

bool UplinkClient::http_exchange_once(
    const std::string& url,
    const std::string& method,
    const std::string& body,
    const std::string& content_type,
    const std::string& bearer,
    bool keep_alive,
    int& status_out,
    std::string& response_out
) {
    ParsedUrl parsed;
    if (!parse_http_url(url, parsed)) {
        return false;
    }

    std::ostringstream req;
    req << method << ' ' << parsed.path << " HTTP/1.1\r\n";
    req << "Host: " << parsed.host << ':' << parsed.port << "\r\n";
    req << "Connection: " << (keep_alive ? "keep-alive" : "close") << "\r\n";
    if (!content_type.empty()) {
        req << "Content-Type: " << content_type << "\r\n";
    }
    if (!bearer.empty()) {
        req << "Authorization: Bearer " << bearer << "\r\n";
    }
    req << "Content-Length: " << body.size() << "\r\n\r\n";
    const std::string header = req.str();

    if (keep_alive) {
        std::lock_guard<std::mutex> lock(sock_mu_);
        for (int attempt = 0; attempt < 2; ++attempt) {
            if (!ensure_socket_locked(parsed.host, parsed.port)) {
                last_error_ = "connect failed";
                return false;
            }
            if (!send_all(sock_fd_, header.data(), header.size())) {
                close_socket_locked();
                continue;
            }
            if (!body.empty() && !send_all(sock_fd_, body.data(), body.size())) {
                close_socket_locked();
                continue;
            }
            if (!recv_http_response(sock_fd_, status_out, response_out, true)) {
                close_socket_locked();
                continue;
            }
            return true;
        }
        return false;
    }

    int fd = -1;
    if (!connect_tcp(parsed.host, parsed.port, fd)) {
        return false;
    }
    const bool ok_send = send_all(fd, header.data(), header.size())
        && (body.empty() || send_all(fd, body.data(), body.size()));
    bool ok_recv = false;
    if (ok_send) {
        ok_recv = recv_http_response(fd, status_out, response_out, false);
    }
    ::close(fd);
    return ok_send && ok_recv;
}

bool UplinkClient::http_post_json(const std::string& url, const std::string& json_body) {
    std::string bearer;
    if (auth_ != nullptr && auth_->enabled()) {
        auth_->get_access_token(bearer);
    }

    int status = 0;
    std::string response;
    if (!http_exchange_once(
            url, "POST", json_body, "application/json", bearer, false, status, response
        )) {
        last_ok_ = false;
        last_error_ = "http exchange failed";
        return false;
    }
    if (status < 200 || status >= 300) {
        last_ok_ = false;
        last_error_ = "http status " + std::to_string(status);
        logutil::warn("UplinkClient POST " + url + " -> " + last_error_);
        return false;
    }
    last_ok_ = true;
    last_error_.clear();
    return true;
}

bool UplinkClient::http_post_jpeg(
    const std::string& url,
    const std::vector<std::uint8_t>& jpeg_bytes
) {
    ParsedUrl parsed;
    if (!parse_http_url(url, parsed)) {
        last_ok_ = false;
        last_error_ = "bad jpeg url";
        return false;
    }

    std::string bearer;
    if (auth_ != nullptr && auth_->enabled()) {
        auth_->get_access_token(bearer);
    }

    std::ostringstream req;
    req << "POST " << parsed.path << " HTTP/1.1\r\n";
    req << "Host: " << parsed.host << ':' << parsed.port << "\r\n";
    req << "Connection: keep-alive\r\n";
    req << "Content-Type: image/jpeg\r\n";
    if (!bearer.empty()) {
        req << "Authorization: Bearer " << bearer << "\r\n";
    }
    req << "Content-Length: " << jpeg_bytes.size() << "\r\n\r\n";
    const std::string header = req.str();

    int status = 0;
    std::string response;
    {
        std::lock_guard<std::mutex> lock(sock_mu_);
        for (int attempt = 0; attempt < 2; ++attempt) {
            if (!ensure_socket_locked(parsed.host, parsed.port)) {
                last_ok_ = false;
                last_error_ = "connect failed";
                return false;
            }
            if (!send_all(sock_fd_, header.data(), header.size())) {
                close_socket_locked();
                continue;
            }
            if (!jpeg_bytes.empty()
                && !send_all(
                    sock_fd_,
                    reinterpret_cast<const char*>(jpeg_bytes.data()),
                    jpeg_bytes.size()
                )) {
                close_socket_locked();
                continue;
            }
            if (!recv_http_response(sock_fd_, status, response, true)) {
                close_socket_locked();
                continue;
            }

            if (status < 200 || status >= 300) {
                last_ok_ = false;
                last_error_ = "http status " + std::to_string(status);
                return false;
            }
            last_ok_ = true;
            last_error_.clear();
            return true;
        }
    }

    last_ok_ = false;
    last_error_ = "jpeg post failed";
    return false;
}

bool UplinkClient::connect_or_healthcheck() {
    int status = 0;
    std::string response;
    if (!http_exchange_once(
            cfg_.health_url(), "GET", "", "", "", false, status, response
        )) {
        last_ok_ = false;
        last_error_ = "health unreachable";
        logutil::error("UplinkClient health check failed: " + cfg_.health_url());
        return false;
    }
    if (status != 200) {
        last_ok_ = false;
        last_error_ = "health status " + std::to_string(status);
        return false;
    }
    last_ok_ = true;
    last_error_.clear();
    logutil::info("UplinkClient health ok");
    return true;
}

bool UplinkClient::post_frame(
    const std::string& stream_id,
    const std::vector<std::uint8_t>& jpeg_bytes
) {
    if (jpeg_bytes.empty()) {
        last_ok_ = false;
        last_error_ = "empty jpeg";
        return false;
    }

    const std::string binary_url =
        cfg_.ec2_base_url + "/api/v1/ingest/frame/jpeg?stream_id=" + stream_id;

    auto post_json = [&]() -> bool {
        const std::string b64 = base64::encode(jpeg_bytes);
        std::ostringstream body;
        body << "{\"stream_id\":\"" << json_escape(stream_id)
             << "\",\"frame\":\"" << b64 << "\"}";
        return http_post_json(cfg_.ingest_frame_url(), body.str());
    };

    // Stick to the mode that works. Re-probing binary every frame (then falling
    // back to huge JSON) was capping the website around ~4 fps.
    if (frame_mode_ == 2) {
        return post_json();
    }
    if (frame_mode_ == 1) {
        if (http_post_jpeg(binary_url, jpeg_bytes)) {
            return true;
        }
        logutil::warn("binary jpeg ingest dropped; will re-probe next frame");
        frame_mode_ = 0;
        return false;
    }

    if (http_post_jpeg(binary_url, jpeg_bytes)) {
        if (frame_mode_ != 1) {
            logutil::info("UplinkClient using binary JPEG ingest (keep-alive)");
        }
        frame_mode_ = 1;
        return true;
    }

    logutil::warn(
        "binary jpeg ingest unavailable (" + last_error_
        + "); using JSON base64. Redeploy ec2_cloud_backend for /ingest/frame/jpeg"
    );
    if (post_json()) {
        frame_mode_ = 2;
        return true;
    }
    return false;
}

bool UplinkClient::post_gps(const std::string& gps_id, double lat, double lon) {
    std::ostringstream body;
    body << "{\"gps_id\":\"" << json_escape(gps_id)
         << "\",\"lat\":" << lat << ",\"lon\":" << lon << '}';
    return http_post_json(cfg_.ingest_gps_url(), body.str());
}

bool UplinkClient::post_lidar_json(const std::string& json_body) {
    return http_post_json(cfg_.ingest_lidar_url(), json_body);
}
