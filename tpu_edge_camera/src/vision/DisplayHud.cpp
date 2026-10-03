#include "vision/DisplayHud.hpp"

#include "util/Log.hpp"

#include <string>

#if defined(HAS_OPENCV)
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#endif

namespace {

#if defined(HAS_OPENCV)
constexpr const char* kWindowName = "tpu_edge_camera";
#endif

} // namespace

bool DisplayHud::init(int width, int height) {
    if (!enabled) {
        return true;
    }

#if !defined(HAS_OPENCV)
    (void)width;
    (void)height;
    logutil::warn("DisplayHud: built without OpenCV; HUD disabled");
    enabled = false;
    return true;
#else
    window_w_ = width > 0 ? width : 800;
    window_h_ = height > 0 ? height : 480;

    try {
        cv::namedWindow(kWindowName, cv::WINDOW_NORMAL);
        cv::resizeWindow(kWindowName, window_w_, window_h_);
        cv::setWindowProperty(
            kWindowName,
            cv::WND_PROP_FULLSCREEN,
            cv::WINDOW_FULLSCREEN
        );
        cv::waitKey(1);
    } catch (const cv::Exception& e) {
        logutil::warn(std::string("DisplayHud init failed: ") + e.what());
        enabled = false;
        return true;
    }

    logutil::info(
        "DisplayHud fullscreen " + std::to_string(window_w_)
        + "x" + std::to_string(window_h_)
    );
    return true;
#endif
}

void DisplayHud::shutdown() {
#if defined(HAS_OPENCV)
    if (enabled) {
        try {
            cv::destroyWindow(kWindowName);
        } catch (const cv::Exception&) {
        }
    }
#endif
    enabled = false;
}

bool DisplayHud::draw(
    const RgbFrame& frame,
    const LidarMapPayload* /*lidar_or_null*/,
    const HudOverlay& overlay
) {
    if (!enabled) {
        return true;
    }

#if !defined(HAS_OPENCV)
    (void)frame;
    (void)overlay;
    return true;
#else
    if (frame.width <= 0 || frame.height <= 0
        || frame.rgb.size()
            < static_cast<size_t>(frame.width * frame.height * 3)) {
        return false;
    }

    try {
        cv::Mat rgb(
            frame.height,
            frame.width,
            CV_8UC3,
            const_cast<std::uint8_t*>(frame.rgb.data())
        );
        cv::Mat bgr;
        cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);

        cv::Mat display;
        cv::resize(
            bgr,
            display,
            cv::Size(window_w_, window_h_),
            0,
            0,
            cv::INTER_NEAREST
        );

        if (overlay.gps_valid) {
            const std::string gps =
                "GPS " + std::to_string(overlay.lat) + ", "
                + std::to_string(overlay.lon);
            cv::putText(
                display,
                gps,
                cv::Point(12, 28),
                cv::FONT_HERSHEY_SIMPLEX,
                0.7,
                cv::Scalar(220, 220, 220),
                2
            );
        }
        if (!overlay.link_status.empty()) {
            cv::putText(
                display,
                overlay.link_status,
                cv::Point(12, window_h_ - 16),
                cv::FONT_HERSHEY_SIMPLEX,
                0.6,
                cv::Scalar(180, 180, 180),
                1
            );
        }

        cv::imshow(kWindowName, display);
        const int key = cv::waitKey(1);
        if (key == 'q' || key == 'Q' || key == 27) {
            quit_requested_ = true;
        }
    } catch (const cv::Exception& e) {
        logutil::warn(std::string("DisplayHud draw failed: ") + e.what());
        return false;
    }

    return true;
#endif
}

bool DisplayHud::poll_quit() {
#if defined(HAS_OPENCV)
    return quit_requested_;
#else
    return false;
#endif
}
