#include "net/UplinkClient.hpp"

#include "util/Log.hpp"

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

bool UplinkClient::connect_or_healthcheck() {
    logutil::warn("UplinkClient::connect_or_healthcheck not implemented");
    last_ok_ = false;
    last_error_ = "not implemented";
    return false;
}

bool UplinkClient::post_frame(
    const std::string& stream_id,
    const std::vector<std::uint8_t>& jpeg_bytes
) {
    (void)stream_id;
    (void)jpeg_bytes;
    logutil::warn("UplinkClient::post_frame not implemented");
    last_ok_ = false;
    last_error_ = "not implemented";
    return false;
}

bool UplinkClient::post_gps(const std::string& gps_id, double lat, double lon) {
    (void)gps_id;
    (void)lat;
    (void)lon;
    logutil::warn("UplinkClient::post_gps not implemented");
    last_ok_ = false;
    last_error_ = "not implemented";
    return false;
}

bool UplinkClient::post_lidar_json(const std::string& json_body) {
    (void)json_body;
    logutil::warn("UplinkClient::post_lidar_json not implemented");
    last_ok_ = false;
    last_error_ = "not implemented";
    return false;
}

bool UplinkClient::http_post_json(const std::string& url, const std::string& json_body) {
    (void)url;
    (void)json_body;
    return false;
}
