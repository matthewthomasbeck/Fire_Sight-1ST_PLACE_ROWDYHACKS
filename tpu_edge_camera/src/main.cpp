// Thermal stream uplink + optional fullscreen HUD. IR / lidar / GPS filled later.

#include "config/Config.hpp"
#include "hardware/ThermalDriver.hpp"
#include "net/Auth0M2M.hpp"
#include "net/UplinkClient.hpp"
#include "util/Log.hpp"
#include "vision/CameraFusion.hpp"
#include "vision/DisplayHud.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <thread>
#include <vector>

namespace {

void sleep_for_fps(int fps) {
    if (fps < 1) {
        fps = 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1000 / fps));
}

int env_int_or(const char* key, int fallback) {
    const char* v = std::getenv(key);
    if (v == nullptr || v[0] == '\0') {
        return fallback;
    }
    return std::atoi(v);
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
    // Default 800x480 matches common 5" DSI panels; override with HUD_WIDTH/HEIGHT.
    if (!hud.init(env_int_or("HUD_WIDTH", 800), env_int_or("HUD_HEIGHT", 480))) {
        logutil::warn("HUD init failed; continuing headless");
        hud.enabled = false;
    }

    if (cfg.uplink_enabled && !uplink.connect_or_healthcheck()) {
        logutil::warn("continuing without initial health ok");
    }

    RgbFrame frame;
    HudOverlay overlay;
    int ok_posts = 0;
    int fail_posts = 0;

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

        if (cfg.uplink_enabled) {
            std::vector<std::uint8_t> jpeg;
            if (fusion.encode_jpeg(frame, jpeg, 80)) {
                if (uplink.post_frame(cfg.stream_id, jpeg)) {
                    ++ok_posts;
                    overlay.link_status = "uplink ok #" + std::to_string(ok_posts);
                    if (ok_posts == 1 || ok_posts % 30 == 0) {
                        logutil::info(
                            "posted frame #" + std::to_string(ok_posts)
                            + " jpeg_bytes=" + std::to_string(jpeg.size())
                            + (thermal.demo_mode ? " demo" : " live")
                        );
                    }
                } else {
                    ++fail_posts;
                    overlay.link_status = "uplink fail";
                    if (fail_posts <= 3 || fail_posts % 30 == 0) {
                        logutil::warn("post_frame failed: " + uplink.last_error());
                    }
                }
            }
        } else {
            overlay.link_status = thermal.demo_mode ? "demo" : "local";
        }

        if (hud.enabled) {
            hud.draw(frame, nullptr, overlay);
        }

        sleep_for_fps(cfg.target_fps);
    }

    hud.shutdown();
    uplink.shutdown();
    auth.shutdown();
    fusion.shutdown();
    thermal.shutdown();
    return 0;
}
