#include "vision/PredatorFusion.hpp"

#include "util/Log.hpp"

bool PredatorFusion::init() {
    return true;
}

void PredatorFusion::shutdown() {}

bool PredatorFusion::fuse(
    const CameraDriver& /*camera*/,
    const ThermalDriver& /*thermal*/,
    RgbFrame& out
) {
    out.width = CameraDriver::width;
    out.height = CameraDriver::height;
    out.rgb.assign(static_cast<std::size_t>(out.width * out.height * 3), 0);
    return true;
}

bool PredatorFusion::encode_jpeg(
    const RgbFrame& /*frame*/,
    std::vector<std::uint8_t>& jpeg_out,
    int /*quality*/
) {
    logutil::warn("PredatorFusion::encode_jpeg not implemented");
    jpeg_out.clear();
    return false;
}
