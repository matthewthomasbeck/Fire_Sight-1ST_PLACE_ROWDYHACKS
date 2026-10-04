// Predator fusion uplink + optional framebuffer HUD (IR + thermal + lidar).
// IR paces the loop; thermal is async so GetFrameData cannot stall fusion.

#include "config/Config.hpp"
#include "hardware/CameraDriver.hpp"
#include "hardware/GpsDriver.hpp"
#include "hardware/LidarDriver.hpp"
#include "hardware/ThermalDriver.hpp"
#include "net/Auth0M2M.hpp"
#include "net/UplinkClient.hpp"
#include "util/Log.hpp"
#include "vision/CameraFusion.hpp"
#include "vision/DisplayHud.hpp"
#include "vision/LidarMap.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

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
    logutil::info("tpu_edge_camera starting (IR + thermal + lidar)");
    logutil::info("stream_id=" + cfg.stream_id);
    logutil::info("ec2=" + cfg.ec2_base_url);

    CameraDriver camera;
    ThermalDriver thermal;
    GpsDriver gps;
    LidarDriver lidar;
    LidarMap lidar_map;
    CameraFusion fusion;
    DisplayHud hud;
    Auth0M2M auth;
    UplinkClient uplink;

    auth.init(cfg);
    uplink.init(cfg, &auth);

    if (!camera.init()) {
        logutil::error("camera init failed");
        return 1;
    }
    if (!thermal.init()) {
        logutil::error("thermal init failed");
        return 1;
    }

    // Claim the panel before UART sensors so a serial hang cannot block HUD.
    hud.enabled = cfg.hud_enabled;
    if (!hud.init(0, 0)) {
        logutil::warn("HUD init failed; continuing headless");
        hud.enabled = false;
    }

    if (!gps.init(cfg.gps_serial_path, cfg.gps_baud)) {
        logutil::warn("gps init failed; continuing without GPS");
    }
    if (!lidar.init(cfg.lidar_serial_path, cfg.lidar_baud)) {
        logutil::warn("lidar init failed; continuing without lidar");
    }
    lidar_map.init();
    fusion.init();

    if (cfg.uplink_enabled && !uplink.connect_or_healthcheck()) {
        logutil::warn("continuing without initial health ok");
    }

    // Latest-frame uplink worker so HTTP never stalls the panel blit.
    std::mutex uplink_mu;
    std::condition_variable uplink_cv;
    std::vector<std::uint8_t> uplink_jpeg;
    bool uplink_has_job = false;
    std::atomic<bool> uplink_stop{false};
    int ok_posts = 0;
    int fail_posts = 0;

    std::mutex gps_mu;
    double gps_lat = 0.0;
    double gps_lon = 0.0;
    bool gps_has_fix = false;
    int ok_gps_posts = 0;
    int fail_gps_posts = 0;
    auto last_gps_post = std::chrono::steady_clock::now()
        - std::chrono::seconds(10);

    std::mutex lidar_mu;
    std::string lidar_json;
    bool lidar_has_scan = false;
    int ok_lidar_posts = 0;
    int fail_lidar_posts = 0;
    auto last_lidar_post = std::chrono::steady_clock::now()
        - std::chrono::seconds(10);

    std::thread uplink_worker;
    if (cfg.uplink_enabled) {
        uplink_worker = std::thread([&]() {
            while (true) {
                std::vector<std::uint8_t> job;
                {
                    std::unique_lock<std::mutex> lock(uplink_mu);
                    uplink_cv.wait(lock, [&]() {
                        return uplink_has_job || uplink_stop.load();
                    });
                    if (uplink_stop.load() && !uplink_has_job) {
                        break;
                    }
                    job.swap(uplink_jpeg);
                    uplink_has_job = false;
                }

                if (uplink.post_frame(cfg.stream_id, job)) {
                    ++ok_posts;
                    if (ok_posts == 1 || ok_posts % 30 == 0) {
                        logutil::info(
                            "posted frame #" + std::to_string(ok_posts)
                            + " jpeg_bytes=" + std::to_string(job.size())
                            + " ir=" + (camera.demo_mode ? "demo" : "live")
                            + " therm=" + (thermal.demo_mode ? "demo" : "live")
                            + " gps=" + (gps.demo_mode ? "demo" : "live")
                            + " lidar=" + (lidar.demo_mode ? "demo" : "live")
                        );
                    }
                } else {
                    ++fail_posts;
                    if (fail_posts <= 3 || fail_posts % 30 == 0) {
                        logutil::warn("post_frame failed: " + uplink.last_error());
                    }
                }

                // ~1 Hz GPS JSON alongside frame uplink (non-blocking for HUD).
                bool do_gps = false;
                double lat = 0.0;
                double lon = 0.0;
                {
                    std::lock_guard<std::mutex> lock(gps_mu);
                    const auto now = std::chrono::steady_clock::now();
                    if (gps_has_fix
                        && now - last_gps_post >= std::chrono::seconds(1)) {
                        lat = gps_lat;
                        lon = gps_lon;
                        last_gps_post = now;
                        do_gps = true;
                    }
                }
                if (do_gps) {
                    if (uplink.post_gps(cfg.gps_id, lat, lon)) {
                        ++ok_gps_posts;
                        if (ok_gps_posts == 1 || ok_gps_posts % 30 == 0) {
                            logutil::info(
                                "posted gps #" + std::to_string(ok_gps_posts)
                                + " id=" + cfg.gps_id
                                + " lat=" + std::to_string(lat)
                                + " lon=" + std::to_string(lon)
                            );
                        }
                    } else {
                        ++fail_gps_posts;
                        if (fail_gps_posts <= 3 || fail_gps_posts % 30 == 0) {
                            logutil::warn(
                                "post_gps failed: " + uplink.last_error()
                            );
                        }
                    }
                }

                // ~2 Hz lidar JSON (scan is larger than GPS; keep off the IR thread).
                bool do_lidar = false;
                std::string lidar_body;
                {
                    std::lock_guard<std::mutex> lock(lidar_mu);
                    const auto now = std::chrono::steady_clock::now();
                    if (lidar_has_scan
                        && now - last_lidar_post >= std::chrono::milliseconds(500)) {
                        lidar_body.swap(lidar_json);
                        lidar_has_scan = false;
                        last_lidar_post = now;
                        do_lidar = !lidar_body.empty();
                    }
                }
                if (do_lidar) {
                    if (uplink.post_lidar_json(lidar_body)) {
                        ++ok_lidar_posts;
                        if (ok_lidar_posts == 1 || ok_lidar_posts % 30 == 0) {
                            logutil::info(
                                "posted lidar #" + std::to_string(ok_lidar_posts)
                                + " id=" + cfg.lidar_id
                                + " bytes=" + std::to_string(lidar_body.size())
                            );
                        }
                    } else {
                        ++fail_lidar_posts;
                        if (fail_lidar_posts <= 3 || fail_lidar_posts % 30 == 0) {
                            logutil::warn(
                                "post_lidar failed: " + uplink.last_error()
                            );
                        }
                    }
                }
            }
        });
    }

    RgbFrame frame;
    HudOverlay overlay;
    LidarMapPayload lidar_payload;
    auto last_loop = std::chrono::steady_clock::now();
    auto last_lidar_json = std::chrono::steady_clock::now()
        - std::chrono::seconds(1);
    int gps_log_count = 0;
    int lidar_log_count = 0;

    while (true) {
        if (hud.poll_quit()) {
            logutil::info("HUD quit requested");
            break;
        }

        if (!camera.refresh()) {
            logutil::warn("camera refresh failed");
            sleep_for_fps(cfg.target_fps);
            continue;
        }
        // Non-blocking: copies latest thermal snapshot from async reader.
        if (!thermal.refresh()) {
            logutil::warn("thermal refresh failed");
            sleep_for_fps(cfg.target_fps);
            continue;
        }

        // Short drain so GPS cannot stall IR/fusion pacing.
        gps.refresh(0.05);
        {
            std::lock_guard<std::mutex> lock(gps_mu);
            gps_has_fix = gps.last_fix.valid;
            if (gps_has_fix) {
                gps_lat = gps.last_fix.latitude;
                gps_lon = gps.last_fix.longitude;
            }
        }
        overlay.gps_valid = gps.last_fix.valid;
        overlay.lat = gps.last_fix.latitude;
        overlay.lon = gps.last_fix.longitude;
        ++gps_log_count;
        if (gps.last_fix.valid) {
            if (gps.rmc_valid == 1 || gps_log_count % 100 == 0) {
                logutil::info(
                    "gps fix lat=" + std::to_string(gps.last_fix.latitude)
                    + " lon=" + std::to_string(gps.last_fix.longitude)
                );
            }
        } else if (gps_log_count == 1 || gps_log_count % 100 == 0) {
            // HUD UNKNOWN: separate "port dead" vs "UART data but no lock".
            logutil::warn(
                std::string("gps no fix")
                + (gps.demo_mode ? " demo_mode" : " live")
                + " bytes=" + std::to_string(gps.bytes_read)
                + " lines=" + std::to_string(gps.lines_seen)
                + " rmc_a=" + std::to_string(gps.rmc_valid)
                + " rmc_v=" + std::to_string(gps.rmc_void)
                + " last_status=" + std::string(1, gps.last_rmc_status)
                + (gps.last_nmea_tag.empty() ? "" : " last_tag=" + gps.last_nmea_tag)
                + (gps.last_error[0] ? std::string(" err=") + gps.last_error : "")
            );
        }

        // Drain UART only; heavy JSON/HUD work stays off the critical path.
        lidar.refresh();

        if (!fusion.render_fusion(camera, thermal, frame)) {
            logutil::warn("render_fusion failed");
            sleep_for_fps(cfg.target_fps);
            continue;
        }

        // Snapshot after fusion so the panel always gets a fresh IR frame first.
        const LidarMapPayload* lidar_for_hud = nullptr;
        const auto now_lidar = std::chrono::steady_clock::now();
        if (lidar_map.snapshot(lidar, lidar_payload)) {
            lidar_for_hud = &lidar_payload;
            ++lidar_log_count;
            if (lidar_log_count == 1 || lidar_log_count % 100 == 0) {
                logutil::info(
                    std::string("lidar ")
                    + (lidar.demo_mode ? "demo" : "live")
                    + " bins=" + std::to_string(lidar.valid_bins)
                    + " ok=" + std::to_string(lidar.packets_ok)
                    + " bad=" + std::to_string(lidar.packets_bad)
                    + (lidar.port_path[0] ? std::string(" path=") + lidar.port_path : "")
                );
            }
        }

        if (hud.enabled) {
            hud.draw(frame, lidar_for_hud, overlay);
        }

        // Build lidar JSON at ~2 Hz after HUD blit so uplink cannot stall video.
        if (cfg.uplink_enabled && lidar_for_hud != nullptr
            && now_lidar - last_lidar_json >= std::chrono::milliseconds(500)) {
            std::string json;
            if (lidar_map.to_json(lidar_payload, cfg.lidar_id, json)) {
                std::lock_guard<std::mutex> lock(lidar_mu);
                lidar_json.swap(json);
                lidar_has_scan = true;
            }
            last_lidar_json = now_lidar;
        }

        if (cfg.uplink_enabled) {
            std::vector<std::uint8_t> jpeg;
            if (fusion.encode_jpeg(frame, jpeg, 70)) {
                {
                    std::lock_guard<std::mutex> lock(uplink_mu);
                    // Drop stale frames: only the newest JPEG waits for the worker.
                    uplink_jpeg.swap(jpeg);
                    uplink_has_job = true;
                }
                uplink_cv.notify_one();
            }
        }

        // Pace to target_fps. Live IR no longer waits on 16 Hz thermal.
        const auto now = std::chrono::steady_clock::now();
        const auto min_period = std::chrono::milliseconds(
            1000 / (cfg.target_fps < 1 ? 1 : cfg.target_fps)
        );
        const auto elapsed = now - last_loop;
        if (elapsed < min_period) {
            std::this_thread::sleep_for(min_period - elapsed);
        }
        last_loop = std::chrono::steady_clock::now();
    }

    if (cfg.uplink_enabled) {
        uplink_stop.store(true);
        uplink_cv.notify_one();
        uplink_worker.join();
    }

    hud.shutdown();
    uplink.shutdown();
    auth.shutdown();
    fusion.shutdown();
    lidar_map.shutdown();
    lidar.shutdown();
    gps.shutdown();
    thermal.shutdown();
    camera.shutdown();
    return 0;
}
