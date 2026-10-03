#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "config/Config.hpp"
#include "net/Auth0M2M.hpp"

// HTTP JSON uplink to EC2 (Robot_Dog internet.py role, FireSight ingest paths).

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

    bool http_post_json(const std::string& url, const std::string& json_body);
};
