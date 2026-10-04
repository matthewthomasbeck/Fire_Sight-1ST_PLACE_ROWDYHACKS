// Predator fusion uplink + optional framebuffer HUD (IR + thermal).
// IR paces the loop; thermal is async so GetFrameData cannot stall fusion.

#include "config/Config.hpp"
#include "hardware/CameraDriver.hpp"
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
    logutil::info("tpu_edge_camera starting (IR + thermal fusion)");
    logutil::info("stream_id=" + cfg.stream_id);
    logutil::info("ec2=" + cfg.ec2_base_url);

    CameraDriver camera;
    ThermalDriver thermal;
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
                            + " ir=" + (camera.demo_mode ? "demo" : "live")
                            + " therm=" + (thermal.demo_mode ? "demo" : "live")
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
    auto last_loop = std::chrono::steady_clock::now();

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

        if (!fusion.render_fusion(camera, thermal, frame)) {
            logutil::warn("render_fusion failed");
            sleep_for_fps(cfg.target_fps);
            continue;
        }

        if (hud.enabled) {
            hud.draw(frame, nullptr, overlay);
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
    thermal.shutdown();
    camera.shutdown();
    return 0;
}
