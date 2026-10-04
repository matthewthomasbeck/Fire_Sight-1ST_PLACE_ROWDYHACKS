#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "config/Config.hpp"
#include "net/Auth0M2M.hpp"

// HTTP uplink to EC2. Prefers keep-alive + raw JPEG ingest.

struct UplinkClient {
    bool init(const Config& cfg, Auth0M2M* auth_or_null);
    void shutdown();

    bool connect_or_healthcheck();
    bool post_frame(const std::string& stream_id, const std::vector<std::uint8_t>& jpeg_bytes);
    bool post_gps(const std::string& gps_id, double lat, double lon);
    bool post_lidar_json(const std::string& json_body);

    bool last_ok() const { return last_ok_; }
    const std::string& last_error() const { return last_error_; }

private:
    Config cfg_{};
    Auth0M2M* auth_ = nullptr;
    bool last_ok_ = false;
    std::string last_error_;

    // Persistent TCP for frame posts (Connection: keep-alive).
    std::mutex sock_mu_;
    int sock_fd_ = -1;
    std::string sock_host_;
    int sock_port_ = 0;

    // 0 = unknown, 1 = binary jpeg, 2 = legacy JSON base64.
    int frame_mode_ = 0;

    void close_socket_locked();
    bool ensure_socket_locked(const std::string& host, int port);
    bool http_exchange_once(
        const std::string& url,
        const std::string& method,
        const std::string& body,
        const std::string& content_type,
        const std::string& bearer,
        bool keep_alive,
        int& status_out,
        std::string& response_out
    );
    bool http_post_json(const std::string& url, const std::string& json_body);
    bool http_post_jpeg(const std::string& url, const std::vector<std::uint8_t>& jpeg_bytes);
};
