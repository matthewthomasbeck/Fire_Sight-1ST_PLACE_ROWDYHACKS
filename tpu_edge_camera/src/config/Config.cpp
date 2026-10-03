#include "config/Config.hpp"

#include <cstdlib>

namespace {

std::string env_or(const char* key, const std::string& fallback) {
    const char* v = std::getenv(key);
    if (v == nullptr || v[0] == '\0') {
        return fallback;
    }
    return std::string(v);
}

int env_int_or(const char* key, int fallback) {
    const char* v = std::getenv(key);
    if (v == nullptr || v[0] == '\0') {
        return fallback;
    }
    return std::atoi(v);
}

bool env_bool_or(const char* key, bool fallback) {
    const char* v = std::getenv(key);
    if (v == nullptr || v[0] == '\0') {
        return fallback;
    }
    const std::string s(v);
    return s == "1" || s == "true" || s == "TRUE" || s == "yes";
}

} // namespace

Config Config::load_from_env() {
    Config c;
    c.stream_id = env_or("STREAM_ID", c.stream_id);
    c.gps_id = env_or("GPS_ID", c.gps_id);
    c.ec2_base_url = env_or("EC2_BASE_URL", c.ec2_base_url);

    c.auth0_domain = env_or("AUTH0_DOMAIN", "");
    c.auth0_m2m_client_id = env_or("AUTH0_M2M_CLIENT_ID", "");
    c.auth0_m2m_client_secret = env_or("AUTH0_M2M_CLIENT_SECRET", "");
    c.auth0_edge_audience = env_or("AUTH0_EDGE_AUDIENCE", c.auth0_edge_audience);

    c.gps_serial_path = env_or("GPS_SERIAL_PATH", c.gps_serial_path);
    c.gps_baud = env_int_or("GPS_BAUD", c.gps_baud);
    c.lidar_serial_path = env_or("LIDAR_SERIAL_PATH", c.lidar_serial_path);
    c.lidar_baud = env_int_or("LIDAR_BAUD", c.lidar_baud);

    c.target_fps = env_int_or("TARGET_FPS", c.target_fps);
    c.uplink_enabled = env_bool_or("UPLINK_ENABLED", c.uplink_enabled);
    c.hud_enabled = env_bool_or("HUD_ENABLED", c.hud_enabled);
    return c;
}

std::string Config::health_url() const {
    return ec2_base_url + "/health";
}

std::string Config::ingest_frame_url() const {
    return ec2_base_url + "/api/v1/ingest/frame";
}

std::string Config::ingest_gps_url() const {
    return ec2_base_url + "/api/v1/ingest/gps";
}

std::string Config::ingest_lidar_url() const {
    return ec2_base_url + "/api/v1/ingest/lidar";
}
