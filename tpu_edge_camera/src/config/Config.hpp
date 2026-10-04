#pragma once

#include <string>

// Edge box runtime config (stream/gps ids, EC2 URL, device paths).

struct Config {
    std::string stream_id = "stream_1";
    std::string gps_id = "gps_1";
    std::string lidar_id = "lidar_1";

    std::string ec2_base_url = "http://100.103.164.9:8080";

    std::string auth0_domain;
    std::string auth0_m2m_client_id;
    std::string auth0_m2m_client_secret;
    std::string auth0_edge_audience = "https://rowdyhacks.edge.api";

    std::string gps_serial_path = "/dev/ttyAMA4";
    int gps_baud = 9600;

    std::string lidar_serial_path = "/dev/serial0";
    int lidar_baud = 230400;

    int target_fps = 20;
    bool uplink_enabled = true;
    bool hud_enabled = true;

    static Config load_from_env();

    std::string health_url() const;
    std::string ingest_frame_url() const;
    std::string ingest_gps_url() const;
    std::string ingest_lidar_url() const;
};
