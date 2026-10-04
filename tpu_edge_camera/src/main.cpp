// Thermal stream uplink + optional framebuffer HUD. IR / lidar / GPS filled later.

#include "config/Config.hpp"
#include "hardware/ThermalDriver.hpp"
#include "net/Auth0M2M.hpp"
#include "net/UplinkClient.hpp"
#include "util/Log.hpp"
#include "vision/CameraFusion.hpp"
#include "vision/DisplayHud.hpp"

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
    logutil::info("tpu_edge_camera starting (thermal only)");
    logutil::info("stream_id=" + cfg.stream_id);
    logutil::info("ec2=" + cfg.ec2_base_url);

    ThermalDriver thermal;
    CameraFusion fusion;
    DisplayHud hud;
    Auth0M2M auth;
    UplinkClient uplink;

    auth.init(cfg);
    uplink.init(cfg, &auth);

    if (!thermal.init()) {
        logutil::error("thermal init failed");
        return 1;
    }
    fusion.init();

    hud.enabled = cfg.hud_enabled;
    if (!hud.init(0, 0)) {
        logutil::warn("HUD init failed; continuing headless");
        hud.enabled = false;
    }

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
                            + (thermal.demo_mode ? " demo" : " live")
                        );
                    }
                } else {
                    ++fail_posts;
                    if (fail_posts <= 3 || fail_posts % 30 == 0) {
                        logutil::warn("post_frame failed: " + uplink.last_error());
                    }
                }
            }
        });
    }

    RgbFrame frame;
    HudOverlay overlay;

    while (true) {
        if (hud.poll_quit()) {
            logutil::info("HUD quit requested");
            break;
        }

        if (!thermal.refresh()) {
            logutil::warn("thermal refresh failed");
            sleep_for_fps(cfg.target_fps);
            continue;
        }

        if (!fusion.render_thermal(thermal, frame)) {
            logutil::warn("render_thermal failed");
            sleep_for_fps(cfg.target_fps);
            continue;
        }

        if (hud.enabled) {
            hud.draw(frame, nullptr, overlay);
        }

        if (cfg.uplink_enabled) {
            std::vector<std::uint8_t> jpeg;
            if (fusion.encode_jpeg(frame, jpeg, 80)) {
                {
                    std::lock_guard<std::mutex> lock(uplink_mu);
                    uplink_jpeg.swap(jpeg);
                    uplink_has_job = true;
                }
                uplink_cv.notify_one();
            }
        }

        // Live MLX already blocks on frame data (~16 Hz). Extra sleep only for demo / headless.
        if (!hud.enabled || thermal.demo_mode) {
            sleep_for_fps(cfg.target_fps);
        }
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
    thermal.shutdown();
    return 0;
}
