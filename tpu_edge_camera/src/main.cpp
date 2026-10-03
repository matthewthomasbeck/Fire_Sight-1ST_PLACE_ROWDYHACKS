// Edge camera main loop: sensors, fusion, uplink, HUD.

#include "config/Config.hpp"
#include "hardware/CameraDriver.hpp"
#include "hardware/GpsDriver.hpp"
#include "hardware/LidarDriver.hpp"
#include "hardware/ThermalDriver.hpp"
#include "net/Auth0M2M.hpp"
#include "net/UplinkClient.hpp"
#include "util/Log.hpp"
#include "vision/DisplayHud.hpp"
#include "vision/LidarMap.hpp"
#include "vision/PredatorFusion.hpp"

#include <chrono>
#include <thread>

namespace {

void sleep_for_fps(int fps) {
    if (fps < 1) {
        fps = 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1000 / fps));
}

} // namespace

int main() {
    const Config cfg = Config::load_from_env();
    logutil::info("tpu_edge_camera starting");
    logutil::info("stream_id=" + cfg.stream_id + " gps_id=" + cfg.gps_id);
    logutil::info("ec2=" + cfg.ec2_base_url);

    CameraDriver camera;
    ThermalDriver thermal;
    LidarDriver lidar;
    GpsDriver gps;
    PredatorFusion fusion;
    LidarMap lidar_map;
    DisplayHud hud;
    Auth0M2M auth;
    UplinkClient uplink;

    auth.init(cfg);
    uplink.init(cfg, &auth);

    if (!camera.init()) {
        logutil::error("camera init failed");
    }
    if (!thermal.init()) {
        logutil::error("thermal init failed");
    }
    if (!lidar.init()) {
        logutil::error("lidar init failed");
    }
    if (!gps.init(cfg.gps_serial_path, cfg.gps_baud)) {
        logutil::error("gps init failed");
    }

    fusion.init();
    lidar_map.init();

    if (cfg.hud_enabled) {
        hud.enabled = true;
        hud.init(CameraDriver::width, CameraDriver::height);
    }

    if (cfg.uplink_enabled) {
        uplink.connect_or_healthcheck();
    }

    RgbFrame fused;
    LidarMapPayload lidar_payload;
    HudOverlay overlay;

    while (!hud.poll_quit()) {
        camera.refresh();
        thermal.refresh();
        lidar.refresh();
        gps.refresh(0.05);

        fusion.fuse(camera, thermal, fused);
        lidar_map.snapshot(lidar, lidar_payload);

        overlay.lat = gps.last_fix.latitude;
        overlay.lon = gps.last_fix.longitude;
        overlay.gps_valid = gps.last_fix.valid;
        overlay.link_status = uplink.last_ok() ? "uplink ok" : "uplink down";

        if (cfg.hud_enabled) {
            hud.draw(fused, &lidar_payload, overlay);
        }

        if (cfg.uplink_enabled) {
            std::vector<std::uint8_t> jpeg;
            if (fusion.encode_jpeg(fused, jpeg)) {
                uplink.post_frame(cfg.stream_id, jpeg);
            }

            if (gps.last_fix.valid) {
                uplink.post_gps(cfg.gps_id, gps.last_fix.latitude, gps.last_fix.longitude);
            }

            std::string lidar_json;
            if (lidar_map.to_json(lidar_payload, cfg.stream_id, lidar_json)) {
                uplink.post_lidar_json(lidar_json);
            }
        }

        sleep_for_fps(cfg.target_fps);
        break; // single pass until drivers are filled
    }

    uplink.shutdown();
    auth.shutdown();
    hud.shutdown();
    lidar_map.shutdown();
    fusion.shutdown();
    gps.shutdown();
    lidar.shutdown();
    thermal.shutdown();
    camera.shutdown();

    logutil::info("tpu_edge_camera exit");
    return 0;
}
