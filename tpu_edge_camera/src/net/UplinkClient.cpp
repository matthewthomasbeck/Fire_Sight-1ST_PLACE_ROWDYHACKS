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

bool http_exchange(
    const std::string& url,
    const std::string& method,
    const std::string& body,
    const std::string& content_type,
    const std::string& bearer,
    int& status_out,
    std::string& response_out
) {
    ParsedUrl parsed;
    if (!parse_http_url(url, parsed)) {
        return false;
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    const std::string port_str = std::to_string(parsed.port);
    if (getaddrinfo(parsed.host.c_str(), port_str.c_str(), &hints, &res) != 0 || res == nullptr) {
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

    std::ostringstream req;
    req << method << ' ' << parsed.path << " HTTP/1.1\r\n";
    req << "Host: " << parsed.host << ':' << parsed.port << "\r\n";
    req << "Connection: close\r\n";
    if (!content_type.empty()) {
        req << "Content-Type: " << content_type << "\r\n";
    }
    if (!bearer.empty()) {
        req << "Authorization: Bearer " << bearer << "\r\n";
    }
    req << "Content-Length: " << body.size() << "\r\n\r\n";
    req << body;
    const std::string raw = req.str();

    std::size_t sent = 0;
    while (sent < raw.size()) {
        const ssize_t n = ::send(fd, raw.data() + sent, raw.size() - sent, 0);
        if (n <= 0) {
            ::close(fd);
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }

    std::string resp;
    char buf[4096];
    while (true) {
        const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n < 0) {
            ::close(fd);
            return false;
        }
        if (n == 0) {
            break;
        }
        resp.append(buf, static_cast<std::size_t>(n));
    }
    ::close(fd);

    status_out = 0;
    if (resp.rfind("HTTP/", 0) == 0) {
        const auto sp = resp.find(' ');
        if (sp != std::string::npos) {
            status_out = std::atoi(resp.c_str() + sp + 1);
        }
    }
    const auto hdr_end = resp.find("\r\n\r\n");
    response_out = hdr_end == std::string::npos ? resp : resp.substr(hdr_end + 4);
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

bool UplinkClient::init(const Config& cfg, Auth0M2M* auth_or_null) {
    cfg_ = cfg;
    auth_ = auth_or_null;
    last_ok_ = false;
    last_error_.clear();
    logutil::info("UplinkClient base=" + cfg_.ec2_base_url);
    return true;
}

void UplinkClient::shutdown() {
    auth_ = nullptr;
}

bool UplinkClient::http_post_json(const std::string& url, const std::string& json_body) {
    std::string bearer;
    if (auth_ != nullptr && auth_->enabled()) {
        auth_->get_access_token(bearer);
    }

    int status = 0;
    std::string response;
    if (!http_exchange(url, "POST", json_body, "application/json", bearer, status, response)) {
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

bool UplinkClient::connect_or_healthcheck() {
    int status = 0;
    std::string response;
    if (!http_exchange(cfg_.health_url(), "GET", "", "", "", status, response)) {
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
    const std::string b64 = base64::encode(jpeg_bytes);
    std::ostringstream body;
    body << "{\"stream_id\":\"" << json_escape(stream_id)
         << "\",\"frame\":\"" << b64 << "\"}";
    return http_post_json(cfg_.ingest_frame_url(), body.str());
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
