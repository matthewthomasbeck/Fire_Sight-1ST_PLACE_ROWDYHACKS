/************************************************************/
/*************** IMPORT / CREATE DEPENDENCIES ***************/
/************************************************************/


/*************** IMPORT DEPENDENCIES ***************/

/***** import custom modules *****/

#include "hardware/CameraDriver.hpp"
#include "util/Log.hpp"

/***** import libraries *****/

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#if defined(__linux__) && defined(HAS_LIBCAMERA)
#include <libcamera/libcamera.h>
#include <libcamera/logging.h>
#include <sys/mman.h>
#include <unistd.h>
#endif


/*************** CREATE DEPENDENCIES ***************/

/***** capture constants *****/

static constexpr unsigned k_capture_w = 640;
static constexpr unsigned k_capture_h = 480;
static constexpr int k_first_frame_timeout_ms = 8000;
static constexpr int k_frame_timeout_ms = 50;
static constexpr int k_miss_limit_before_demo = 60; // ~3s of stalls at 50ms

#if defined(__linux__) && defined(HAS_LIBCAMERA)

using namespace libcamera;

/***** live capture state *****/

static std::unique_ptr<CameraManager> g_cm;
static std::shared_ptr<Camera> g_camera;
static std::unique_ptr<CameraConfiguration> g_config;
static std::unique_ptr<FrameBufferAllocator> g_allocator;
static Stream* g_stream = nullptr;
static std::vector<std::unique_ptr<Request>> g_requests;

struct MappedBuffer {
    void* mmap_ptr = nullptr;
    size_t mmap_size = 0;
    const uint8_t* sample = nullptr; // mmap_ptr + plane0.offset
};

static std::map<FrameBuffer*, MappedBuffer> g_mapped;
static PixelFormat g_pixel_format;
static unsigned g_stride = 0;
static unsigned g_capture_w = k_capture_w;
static unsigned g_capture_h = k_capture_h;
static bool g_live_ready = false;
static std::mutex g_shutdown_mutex;
static int g_refresh_misses = 0;
static std::string g_fail_reason;

/***** latest frame shared between camera thread and refresh() *****/

static std::mutex g_frame_mutex;
static std::condition_variable g_frame_cv;
static uint8_t g_latest_r[CameraDriver::pixel_count]{};
static uint8_t g_latest_g[CameraDriver::pixel_count]{};
static uint8_t g_latest_b[CameraDriver::pixel_count]{};
static uint8_t g_latest_min = 0;
static uint8_t g_latest_max = 255;
static int g_latest_index = 0;
static bool g_have_frame = false;

/***** camera-thread convert scratch *****/

static uint8_t g_scratch_r[CameraDriver::pixel_count];
static uint8_t g_scratch_g[CameraDriver::pixel_count];
static uint8_t g_scratch_b[CameraDriver::pixel_count];

#endif




/****************************************************/
/****************** CAMERA DRIVER *******************/
/****************************************************/


/*************** DEMO FRAME (fallback) ***************/

static void refresh_demo(CameraDriver& self) {

    /***** demo fake IR blob setup (color, not grayscale) *****/

    const float cx = (CameraDriver::width - 1) * 0.5f
        + std::cos(self.frame_index * 0.07f) * 40.0f;
    const float cy = (CameraDriver::height - 1) * 0.5f
        + std::sin(self.frame_index * 0.09f) * 28.0f;
    const float sigma = 18.0f;
    self.v_min = 255;
    self.v_max = 0;

    for (int y = 0; y < CameraDriver::height; ++y) {

        for (int x = 0; x < CameraDriver::width; ++x) {

            const float dx = static_cast<float>(x) - cx;
            const float dy = static_cast<float>(y) - cy;
            const float gauss =
                std::exp(-(dx * dx + dy * dy) / (2.0f * sigma * sigma));
            const float nx = static_cast<float>(x)
                / static_cast<float>(CameraDriver::width - 1);
            const float ny = static_cast<float>(y)
                / static_cast<float>(CameraDriver::height - 1);

            const float warm = 0.15f + 0.85f * gauss;
            const uint8_t rr = static_cast<uint8_t>(
                std::min(255.0f, (40.0f + 200.0f * warm + 40.0f * nx) + 0.5f)
            );
            const uint8_t gg = static_cast<uint8_t>(
                std::min(255.0f, (30.0f + 120.0f * warm + 60.0f * ny) + 0.5f)
            );
            const uint8_t bb = static_cast<uint8_t>(
                std::min(255.0f, (50.0f + 80.0f * (1.0f - warm) + 90.0f * nx) + 0.5f)
            );

            const int i = y * CameraDriver::width + x;
            self.r[i] = rr;
            self.g[i] = gg;
            self.b[i] = bb;

            const uint8_t lum = static_cast<uint8_t>((77u * rr + 150u * gg + 29u * bb) >> 8);
            self.v_min = std::min(self.v_min, lum);
            self.v_max = std::max(self.v_max, lum);
        }
    }

    ++self.frame_index;
}


#if defined(__linux__) && defined(HAS_LIBCAMERA)


/*************** ERROR HELPERS ***************/

static void set_fail(const char* reason) {

    g_fail_reason = reason ? reason : "unknown";
}


static void copy_fail_to(CameraDriver& self) {

    std::snprintf(

        self.last_error,
        sizeof(self.last_error),
        "%s",
        g_fail_reason.empty() ? "live init failed" : g_fail_reason.c_str()
    );
}


static void silence_libcamera_logs() {

    // Kill noisy ISP / pipeline chatter that otherwise paints over the TUI.
    setenv("LIBCAMERA_LOG_LEVELS", "*:FATAL", 1);
    logSetTarget(LoggingTargetNone);
}


/*************** CAMERA PICK (prefer OV5647 IR) ***************/

static std::shared_ptr<Camera> pick_camera(CameraManager& cm) {

    const auto cameras = cm.cameras();

    if (cameras.empty()) {

        return nullptr;
    }

    for (const std::shared_ptr<Camera>& cam : cameras) {

        const std::string& id = cam->id();
        std::string lower = id;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (lower.find("ov5647") != std::string::npos) {

            return cm.get(id);
        }
    }

    return cm.get(cameras[0]->id());
}


/*************** PIXEL HELPERS (keep ISP color) ***************/

static void sample_rgb(

    const uint8_t* src,
    unsigned sx,
    unsigned sy,
    uint8_t* out_r,
    uint8_t* out_g,
    uint8_t* out_b
) {

    const size_t row = static_cast<size_t>(sy) * g_stride;

    /***** YUV family → approximate BT.601 color *****/

    if (g_pixel_format == formats::NV12
        || g_pixel_format == formats::NV21) {

        const uint8_t y = src[row + sx];
        const uint8_t* uv_base = src + static_cast<size_t>(g_stride) * g_capture_h;
        const size_t uv_row = static_cast<size_t>(sy / 2) * g_stride;
        const size_t uv_i = uv_row + static_cast<size_t>(sx & ~1u);
        uint8_t u = 128;
        uint8_t v = 128;

        if (g_pixel_format == formats::NV12) {

            u = uv_base[uv_i];
            v = uv_base[uv_i + 1];

        } else {

            v = uv_base[uv_i];
            u = uv_base[uv_i + 1];
        }

        const int c = static_cast<int>(y) - 16;
        const int d = static_cast<int>(u) - 128;
        const int e = static_cast<int>(v) - 128;
        int rr = (298 * c + 409 * e + 128) >> 8;
        int gg = (298 * c - 100 * d - 208 * e + 128) >> 8;
        int bb = (298 * c + 516 * d + 128) >> 8;
        *out_r = static_cast<uint8_t>(std::clamp(rr, 0, 255));
        *out_g = static_cast<uint8_t>(std::clamp(gg, 0, 255));
        *out_b = static_cast<uint8_t>(std::clamp(bb, 0, 255));
        return;
    }

    if (g_pixel_format == formats::YUV420
        || g_pixel_format == formats::YVU420) {

        // Contiguous 420: Y, then U plane, then V (or swapped for YVU).
        const uint8_t y = src[row + sx];
        const size_t y_size = static_cast<size_t>(g_stride) * g_capture_h;
        const size_t chroma_stride = g_stride / 2;
        const size_t chroma_h = (g_capture_h + 1) / 2;
        const size_t u_off = y_size;
        const size_t v_off = y_size + chroma_stride * chroma_h;
        const size_t ci = static_cast<size_t>(sy / 2) * chroma_stride
            + static_cast<size_t>(sx / 2);
        uint8_t u = 128;
        uint8_t v = 128;

        if (g_pixel_format == formats::YUV420) {

            u = src[u_off + ci];
            v = src[v_off + ci];

        } else {

            v = src[u_off + ci];
            u = src[v_off + ci];
        }

        const int c = static_cast<int>(y) - 16;
        const int d = static_cast<int>(u) - 128;
        const int e = static_cast<int>(v) - 128;
        int rr = (298 * c + 409 * e + 128) >> 8;
        int gg = (298 * c - 100 * d - 208 * e + 128) >> 8;
        int bb = (298 * c + 516 * d + 128) >> 8;
        *out_r = static_cast<uint8_t>(std::clamp(rr, 0, 255));
        *out_g = static_cast<uint8_t>(std::clamp(gg, 0, 255));
        *out_b = static_cast<uint8_t>(std::clamp(bb, 0, 255));
        return;
    }

    if (g_pixel_format == formats::YUYV) {

        const uint8_t* px = src + row + static_cast<size_t>(sx) * 2;
        const uint8_t y = px[0];
        // Pair shares U/V on even x.
        const uint8_t* pair = src + row + static_cast<size_t>(sx & ~1u) * 2;
        const uint8_t u = pair[1];
        const uint8_t v = pair[3];
        const int c = static_cast<int>(y) - 16;
        const int d = static_cast<int>(u) - 128;
        const int e = static_cast<int>(v) - 128;
        int rr = (298 * c + 409 * e + 128) >> 8;
        int gg = (298 * c - 100 * d - 208 * e + 128) >> 8;
        int bb = (298 * c + 516 * d + 128) >> 8;
        *out_r = static_cast<uint8_t>(std::clamp(rr, 0, 255));
        *out_g = static_cast<uint8_t>(std::clamp(gg, 0, 255));
        *out_b = static_cast<uint8_t>(std::clamp(bb, 0, 255));
        return;
    }

    /***** packed RGB family (Pi viewfinder is often XBGR8888) *****/

    unsigned bpp = 4;
    unsigned r_off = 0;
    unsigned g_off = 1;
    unsigned b_off = 2;

    if (g_pixel_format == formats::RGB888) {

        bpp = 3;
        r_off = 0;
        g_off = 1;
        b_off = 2;

    } else if (g_pixel_format == formats::BGR888) {

        bpp = 3;
        r_off = 2;
        g_off = 1;
        b_off = 0;

    } else if (g_pixel_format == formats::XBGR8888
        || g_pixel_format == formats::ABGR8888) {

        bpp = 4;
        r_off = 0;
        g_off = 1;
        b_off = 2;

    } else if (g_pixel_format == formats::XRGB8888
        || g_pixel_format == formats::ARGB8888) {

        bpp = 4;
        r_off = 2;
        g_off = 1;
        b_off = 0;

    } else {

        const uint8_t y = src[row + sx];
        *out_r = y;
        *out_g = y;
        *out_b = y;
        return;
    }

    const uint8_t* px = src + row + static_cast<size_t>(sx) * bpp;
    *out_r = px[r_off];
    *out_g = px[g_off];
    *out_b = px[b_off];
}


static void downscale_to_rgb(

    const uint8_t* src,
    uint8_t* out_r,
    uint8_t* out_g,
    uint8_t* out_b,
    uint8_t* out_min,
    uint8_t* out_max
) {

    *out_min = 255;
    *out_max = 0;

    for (int y = 0; y < CameraDriver::height; ++y) {

        const unsigned sy = static_cast<unsigned>(
            (static_cast<uint64_t>(y) * g_capture_h)
            / static_cast<unsigned>(CameraDriver::height)
        );

        for (int x = 0; x < CameraDriver::width; ++x) {

            const unsigned sx = static_cast<unsigned>(
                (static_cast<uint64_t>(x) * g_capture_w)
                / static_cast<unsigned>(CameraDriver::width)
            );
            const int i = y * CameraDriver::width + x;
            sample_rgb(src, sx, sy, &out_r[i], &out_g[i], &out_b[i]);
            const uint8_t lum = static_cast<uint8_t>(
                (77u * out_r[i] + 150u * out_g[i] + 29u * out_b[i]) >> 8
            );
            *out_min = std::min(*out_min, lum);
            *out_max = std::max(*out_max, lum);
        }
    }
}


/*************** REQUEST COMPLETED (camera thread) ***************/

static void request_complete(Request* request) {

    Camera* camera = nullptr;

    {
        std::lock_guard<std::mutex> shutdown_lock(g_shutdown_mutex);

        if (request == nullptr
            || request->status() == Request::RequestCancelled
            || !g_live_ready
            || !g_camera) {

            return;
        }

        camera = g_camera.get();

        for (auto const& buffer_pair : request->buffers()) {

            FrameBuffer* buffer = buffer_pair.second;

            if (buffer->metadata().status != FrameMetadata::FrameSuccess) {

                continue;
            }

            auto it = g_mapped.find(buffer);

            if (it == g_mapped.end() || it->second.sample == nullptr) {

                continue;
            }

            const uint8_t* src = it->second.sample;
            uint8_t vmin = 0;
            uint8_t vmax = 255;
            downscale_to_rgb(

                src,
                g_scratch_r,
                g_scratch_g,
                g_scratch_b,
                &vmin,
                &vmax
            );

            {
                std::lock_guard<std::mutex> lock(g_frame_mutex);
                std::memcpy(g_latest_r, g_scratch_r, sizeof(g_latest_r));
                std::memcpy(g_latest_g, g_scratch_g, sizeof(g_latest_g));
                std::memcpy(g_latest_b, g_scratch_b, sizeof(g_latest_b));
                g_latest_min = vmin;
                g_latest_max = vmax;
                ++g_latest_index;
                g_have_frame = true;
            }

            g_frame_cv.notify_one();
        }

        request->reuse(Request::ReuseBuffers);
    }

    if (camera != nullptr) {

        camera->queueRequest(request);
    }
}


/*************** TEAR DOWN LIVE PATH ***************/

static void close_live() {

    {
        std::lock_guard<std::mutex> shutdown_lock(g_shutdown_mutex);
        g_live_ready = false;

        if (g_camera) {

            g_camera->requestCompleted.disconnect(request_complete);
        }
    }

    if (g_camera) {

        g_camera->stop();
    }

    {
        std::lock_guard<std::mutex> lock(g_frame_mutex);
        g_have_frame = false;
    }

    for (auto& entry : g_mapped) {

        if (entry.second.mmap_ptr != nullptr) {

            munmap(entry.second.mmap_ptr, entry.second.mmap_size);
        }
    }

    g_mapped.clear();
    g_requests.clear();

    if (g_allocator && g_stream) {

        g_allocator->free(g_stream);
    }

    g_allocator.reset();
    g_config.reset();
    g_stream = nullptr;

    if (g_camera) {

        g_camera->release();
        g_camera.reset();
    }

    if (g_cm) {

        g_cm->stop();
        g_cm.reset();
    }

    g_refresh_misses = 0;
}


/*************** BUFFER MAP (respect plane.offset) ***************/

static bool map_buffers(

    const std::vector<std::unique_ptr<FrameBuffer>>& buffers
) {

    for (const std::unique_ptr<FrameBuffer>& buffer : buffers) {

        const std::vector<FrameBuffer::Plane>& planes = buffer->planes();

        if (planes.empty()) {

            set_fail("camera buffer has no planes");
            return false;
        }

        const int fd = planes[0].fd.get();
        size_t map_size = 0;

        for (const FrameBuffer::Plane& plane : planes) {

            if (plane.fd.get() != fd) {

                continue;
            }

            const size_t end =
                static_cast<size_t>(plane.offset) + plane.length;
            map_size = std::max(map_size, end);
        }

        if (map_size == 0) {

            set_fail("camera buffer map size is zero");
            return false;
        }

        void* memory = mmap(

            nullptr,
            map_size,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            fd,
            0
        );

        if (memory == MAP_FAILED) {

            set_fail("mmap of camera buffer failed");
            return false;
        }

        MappedBuffer mapped;
        mapped.mmap_ptr = memory;
        mapped.mmap_size = map_size;
        mapped.sample =
            static_cast<const uint8_t*>(memory) + planes[0].offset;
        g_mapped[buffer.get()] = mapped;
    }

    return true;
}


/*************** INITIALIZE LIVE FRAME FUNCTION ***************/

static bool try_init_live() {

    close_live();
    g_fail_reason.clear();
    silence_libcamera_logs();

    g_cm = std::make_unique<CameraManager>();

    if (g_cm->start() != 0) {

        set_fail("CameraManager::start failed");
        close_live();
        return false;
    }

    if (g_cm->cameras().empty()) {

        set_fail("no libcamera cameras found");
        close_live();
        return false;
    }

    g_camera = pick_camera(*g_cm);

    if (!g_camera) {

        set_fail("failed to open camera");
        close_live();
        return false;
    }

    if (g_camera->acquire() != 0) {

        set_fail("camera acquire failed (in use?)");
        close_live();
        return false;
    }

    g_config = g_camera->generateConfiguration({ StreamRole::Viewfinder });

    if (!g_config || g_config->empty()) {

        set_fail("generateConfiguration failed");
        close_live();
        return false;
    }

    StreamConfiguration& stream_config = g_config->at(0);

    // Prefer a larger viewfinder + packed RGB so color matches rpicam-hello.
    stream_config.size.width = k_capture_w;
    stream_config.size.height = k_capture_h;
    stream_config.pixelFormat = formats::XBGR8888;

    CameraConfiguration::Status status = g_config->validate();

    if (status == CameraConfiguration::Invalid) {

        g_config = g_camera->generateConfiguration({ StreamRole::Viewfinder });

        if (!g_config || g_config->empty()) {

            set_fail("stream configuration invalid");
            close_live();
            return false;
        }

        StreamConfiguration& retry = g_config->at(0);
        retry.size.width = k_capture_w;
        retry.size.height = k_capture_h;

        if (g_config->validate() == CameraConfiguration::Invalid) {

            g_config = g_camera->generateConfiguration({ StreamRole::Viewfinder });

            if (!g_config || g_config->empty()
                || g_config->validate() == CameraConfiguration::Invalid) {

                set_fail("stream configuration invalid");
                close_live();
                return false;
            }
        }
    }

    if (g_camera->configure(g_config.get()) < 0) {

        set_fail("camera configure failed");
        close_live();
        return false;
    }

    StreamConfiguration& applied = g_config->at(0);
    g_pixel_format = applied.pixelFormat;
    g_stride = applied.stride;
    g_capture_w = applied.size.width;
    g_capture_h = applied.size.height;
    g_stream = applied.stream();

    g_allocator = std::make_unique<FrameBufferAllocator>(g_camera);

    if (g_allocator->allocate(g_stream) < 0) {

        set_fail("framebuffer allocate failed");
        close_live();
        return false;
    }

    const std::vector<std::unique_ptr<FrameBuffer>>& buffers =
        g_allocator->buffers(g_stream);

    if (buffers.empty()) {

        set_fail("no framebuffers allocated");
        close_live();
        return false;
    }

    if (!map_buffers(buffers)) {

        close_live();
        return false;
    }

    for (const std::unique_ptr<FrameBuffer>& buffer : buffers) {

        std::unique_ptr<Request> request = g_camera->createRequest();

        if (!request || request->addBuffer(g_stream, buffer.get()) < 0) {

            set_fail("create/add request buffer failed");
            close_live();
            return false;
        }

        g_requests.push_back(std::move(request));
    }

    g_camera->requestCompleted.connect(request_complete);

    {
        std::lock_guard<std::mutex> lock(g_frame_mutex);
        g_have_frame = false;
        g_latest_index = 0;
    }

    {
        std::lock_guard<std::mutex> shutdown_lock(g_shutdown_mutex);
        g_live_ready = true;
    }

    if (g_camera->start() < 0) {

        set_fail("camera start failed");
        close_live();
        return false;
    }

    for (std::unique_ptr<Request>& request : g_requests) {

        if (g_camera->queueRequest(request.get()) < 0) {

            set_fail("queueRequest failed");
            close_live();
            return false;
        }
    }

    {
        std::unique_lock<std::mutex> lock(g_frame_mutex);
        g_frame_cv.wait_for(

            lock,
            std::chrono::milliseconds(k_first_frame_timeout_ms),
            [] { return g_have_frame; }
        );

        if (!g_have_frame) {

            lock.unlock();
            set_fail("timed out waiting for first frame");
            close_live();
            return false;
        }
    }

    g_fail_reason.clear();
    return true;
}


/*************** REFRESH LIVE FRAME FUNCTION ***************/

static bool refresh_live(CameraDriver& self) {

    if (!g_live_ready) {

        return false;
    }

    std::unique_lock<std::mutex> lock(g_frame_mutex);

    if (!g_have_frame) {

        g_frame_cv.wait_for(

            lock,
            std::chrono::milliseconds(k_frame_timeout_ms),
            [] { return g_have_frame; }
        );
    }

    if (!g_have_frame) {

        return false;
    }

    std::memcpy(self.r, g_latest_r, sizeof(self.r));
    std::memcpy(self.g, g_latest_g, sizeof(self.g));
    std::memcpy(self.b, g_latest_b, sizeof(self.b));
    self.v_min = g_latest_min;
    self.v_max = g_latest_max;
    self.frame_index = g_latest_index;
    return true;
}

#endif // HAS_LIBCAMERA


/*************** INITIALIZE CAMERA DRIVER FUNCTION ***************/

bool CameraDriver::init() {

    frame_index = 0;
    last_error[0] = '\0';

#if defined(__linux__) && defined(HAS_LIBCAMERA)
    if (try_init_live()) {

        demo_mode = false;
        ready = true;
        last_error[0] = '\0';
        logutil::info("CameraDriver live libcamera IR");
        return refresh();
    }

    copy_fail_to(*this);
#else
    std::snprintf(

        last_error,
        sizeof(last_error),
        "libcamera not built in"
    );
#endif

    logutil::warn(
        std::string("CameraDriver falling back to demo mode: ") + last_error
    );
    demo_mode = true;
    ready = true;
    refresh_demo(*this);
    return true;
}


/*************** REFRESH CAMERA DRIVER FUNCTION ***************/

bool CameraDriver::refresh() {

    if (!ready) {

        return false;
    }

#if defined(__linux__) && defined(HAS_LIBCAMERA)
    if (!demo_mode) {

        if (refresh_live(*this)) {

            g_refresh_misses = 0;
            return true;
        }

        ++g_refresh_misses;

        if (g_refresh_misses < k_miss_limit_before_demo) {

            return true;
        }

        set_fail("live stream stalled");
        copy_fail_to(*this);
        demo_mode = true;
        close_live();
    }
#endif

    refresh_demo(*this);
    return true;
}


/*************** SHUT DOWN CAMERA DRIVER FUNCTION ***************/

void CameraDriver::shutdown() {

    ready = false;
    demo_mode = true;
    frame_index = 0;
    last_error[0] = '\0';

#if defined(__linux__) && defined(HAS_LIBCAMERA)
    close_live();
    g_fail_reason.clear();
#endif
}
