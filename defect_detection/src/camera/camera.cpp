#include "camera/camera.hpp"
#include "utils/logger.hpp"
#include "utils/timer.hpp"
#include <opencv2/videoio.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <chrono>
#include <thread>
#include <sstream>

/* ================================================================
 *  后端转换工具
 * ================================================================ */

/* 将相机后端枚举转换为 OpenCV 的 VideoCapture 后端常量 */
int Camera::backend_to_cv(CameraBackend backend) {
    switch (backend) {
        case CameraBackend::DShow: return cv::CAP_DSHOW;
        case CameraBackend::MSMF:  return cv::CAP_MSMF;
        case CameraBackend::VFW:   return cv::CAP_VFW;
        case CameraBackend::OpenCV:return cv::CAP_ANY;
        case CameraBackend::AutoDetect:
        default:                   return cv::CAP_ANY;
    }
}

/* 返回相机后端的中文可读名称 */
std::string Camera::backend_name(CameraBackend backend) {
    switch (backend) {
        case CameraBackend::AutoDetect: return "自动探测";
        case CameraBackend::DShow:      return "DirectShow";
        case CameraBackend::MSMF:       return "MediaFoundation";
        case CameraBackend::VFW:        return "VFW";
        case CameraBackend::OpenCV:     return "OpenCV";
        default:                        return "未知";
    }
}

/* ================================================================
 *  内部实现类 (PIMPL 模式)
 * ================================================================ */

class Camera::Impl {
public:
    explicit Impl(const CameraConfig& cfg)
        : config_(cfg),
          frame_count_(0),
          dropped_count_(0),
          last_valid_timestamp_(0),
          last_error_("") {}

    /* ── 打开 / 关闭 ────────────────────────────────────────────── */

    /* 打开相机或视频文件 */
    bool open() {
        if (is_open_) return true;

        if (!config_.video_path.empty()) {
            LOG_INFO("正在打开视频文件: " + config_.video_path);
            cap_.open(config_.video_path);
            if (!cap_.isOpened()) {
                last_error_ = "打开视频失败: " + config_.video_path;
                LOG_ERROR(last_error_);
                return false;
            }
            total_frames_ = static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_COUNT));
            is_open_ = true;
            is_video_ = true;
            LOG_INFO("视频已打开: " + config_.video_path +
                     " (" + std::to_string(total_frames_) + " 帧)");
            return true;
        }

        /* 自动选择最佳后端 */
        if (config_.backend == CameraBackend::AutoDetect)
            backend_ = probe_best_backend(config_.device_id);
        else
            backend_ = config_.backend;

        int cv_backend = Camera::backend_to_cv(backend_);
        LOG_INFO("正在打开相机 " + std::to_string(config_.device_id) +
                 "，后端: " + Camera::backend_name(backend_));

#ifdef _WIN32
        if (cv_backend == cv::CAP_ANY) cv_backend = cv::CAP_DSHOW;
        cap_.open(config_.device_id, cv_backend);
#else
        cap_.open(config_.device_id);
#endif

        if (!cap_.isOpened()) {
            last_error_ = "打开相机 " + std::to_string(config_.device_id) + " 失败";
            LOG_ERROR(last_error_);
            return false;
        }

        apply_config();           /* 应用用户配置的参数 */
        is_open_ = true;
        LOG_INFO("相机 " + std::to_string(config_.device_id) +
                 " 已打开 (" + Camera::backend_name(backend_) + ")");
        return true;
    }

    /* 关闭相机并释放资源 */
    void close() {
        if (is_open_) {
            cap_.release();
            is_open_ = false;
            LOG_INFO("相机 " + std::to_string(config_.device_id) + " 已关闭");
        }
    }

    bool is_opened() const { return is_open_; }

    /* 重新打开相机：先关闭，等待 200ms 后重试 */
    bool reopen() {
        close();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        return open();
    }

    /* ── 帧采集 ────────────────────────────────────────────────── */

    /* 采集一帧，使用配置的超时时间 */
    bool capture_frame(cv::Mat& frame) {
        return capture_frame(frame, config_.timeout_ms);
    }

    /* 采集一帧 */
    bool capture_frame(cv::Mat& frame, int timeout_ms) {
        if (!is_open_) return false;

        /* 视频模式：读取下一帧，到头自动循环 */
        if (is_video_) {
            bool ret = cap_.read(frame);
            if (!ret || frame.empty()) {
                if (config_.loop_video) {
                    cap_.set(cv::CAP_PROP_POS_FRAMES, 0);
                    ret = cap_.read(frame);
                }
            }
            if (ret && !frame.empty()) {
                return true;
            }
            return false;
        }

        /* 相机模式：带超时和自动重连 */
        if (!is_open_) {
            if (config_.auto_reconnect && reopen()) {
                LOG_WARN("相机自动重连成功");
            } else {
                last_error_ = "相机未打开";
                return false;
            }
        }

        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::milliseconds(timeout_ms);

        /* 在超时时间内循环尝试 grab + retrieve */
        while (std::chrono::steady_clock::now() < deadline) {
            if (cap_.grab()) {
                if (cap_.retrieve(frame)) {
                    if (frame.empty()) {
                        dropped_count_++;
                        continue;
                    }
                    /* 裁剪 ROI */
                    apply_roi(frame);

                    frame_count_++;
                    update_fps();
                    last_valid_timestamp_ = cv::getTickCount();
                    return true;
                }
            }
            /* 让出 CPU，避免忙等待 */
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        dropped_count_++;
        last_error_ = "帧读取超时 (" + std::to_string(timeout_ms) + " ms)";
        LOG_WARN(last_error_);

        /* 连续丢帧达到阈值时自动重连 */
        if (config_.auto_reconnect && dropped_count_ % config_.retry_count == 0) {
            LOG_WARN("尝试相机自动重连...");
            reopen();
        }

        return false;
    }

    /* 采集一帧并保存为图片文件 */
    bool save_snapshot(const std::string& path) {
        cv::Mat frame;
        if (!capture_frame(frame, 2000)) return false;
        return cv::imwrite(path, frame);
    }

    /* ── 参数设置 ─────────────────────────────────────────────── */

    bool set(int prop_id, double value) {
        return cap_.set(prop_id, value);
    }

    double get(int prop_id) const {
        return cap_.get(prop_id);
    }

    /* 设置曝光时间：< 0 为自动曝光 */
    bool set_exposure(double value_us) {
        config_.exposure = value_us;
        if (value_us < 0)
            return cap_.set(cv::CAP_PROP_EXPOSURE, cv::CAP_PROP_AUTO_EXPOSURE);
        return cap_.set(cv::CAP_PROP_EXPOSURE, value_us);
    }

    bool set_gain(double value_db) {
        config_.gain = value_db;
        return cap_.set(cv::CAP_PROP_GAIN, value_db);
    }

    bool set_resolution(int width, int height) {
        config_.width = width;
        config_.height = height;
        bool ok = true;
        ok &= cap_.set(cv::CAP_PROP_FRAME_WIDTH, width);
        ok &= cap_.set(cv::CAP_PROP_FRAME_HEIGHT, height);
        return ok;
    }

    bool set_fps(double fps) {
        config_.fps = fps;
        return cap_.set(cv::CAP_PROP_FPS, fps);
    }

    bool set_roi(int x, int y, int w, int h) {
        config_.roi_x = x;
        config_.roi_y = y;
        config_.roi_w = w;
        config_.roi_h = h;
        return true;
    }

    /* ── 状态查询 ─────────────────────────────────────────────── */

    CameraConfig config() const { return config_; }
    std::string last_error() const { return last_error_; }
    double measured_fps() const { return fps_; }
    int frame_count() const { return frame_count_; }
    int dropped_frame_count() const { return dropped_count_; }

    /* 帧健康检查：检测空帧、全黑、全白等异常情况 */
    bool frame_healthy(const cv::Mat& frame) const {
        if (frame.empty()) return false;
        if (frame.rows == 0 || frame.cols == 0) return false;
        if (frame.type() != CV_8UC3) return false;
        cv::Scalar mean = cv::mean(frame);
        if (mean[0] < 1 && mean[1] < 1 && mean[2] < 1) return false;   /* 全黑 */
        if (mean[0] > 254 && mean[1] > 254 && mean[2] > 254) return false;  /* 全白 */
        return true;
    }

    /* ── 内部辅助方法 ─────────────────────────────────────────── */

private:
    /* 将配置中的参数应用到 VideoCapture */
    void apply_config() {
        if (config_.width > 0 && config_.height > 0) {
            cap_.set(cv::CAP_PROP_FRAME_WIDTH, config_.width);
            cap_.set(cv::CAP_PROP_FRAME_HEIGHT, config_.height);
        }
        if (config_.fps > 0)
            cap_.set(cv::CAP_PROP_FPS, config_.fps);
        if (config_.exposure >= 0)
            cap_.set(cv::CAP_PROP_EXPOSURE, config_.exposure);
        else
            cap_.set(cv::CAP_PROP_AUTO_EXPOSURE, 1);
        if (config_.gain >= 0)
            cap_.set(cv::CAP_PROP_GAIN, config_.gain);
    }

    /* 如果配置了 ROI，裁剪图像到指定区域 */
    void apply_roi(cv::Mat& frame) {
        if (config_.roi_w > 0 && config_.roi_h > 0) {
            cv::Rect roi(config_.roi_x, config_.roi_y,
                         config_.roi_w, config_.roi_h);
            roi &= cv::Rect(0, 0, frame.cols, frame.rows);  /* 防止越界 */
            if (roi.area() > 0) frame = frame(roi);
        }
    }

    /* 每秒更新一次实测帧率 */
    void update_fps() {
        static auto last_time = std::chrono::steady_clock::now();
        static int cnt = 0;
        cnt++;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration<double>(now - last_time).count();
        if (elapsed >= 1.0) {
            fps_ = cnt / elapsed;
            cnt = 0;
            last_time = now;
        }
    }

    /* 自动探测最佳后端：先尝试 DirectShow，失败则尝试 MSMF */
    CameraBackend probe_best_backend(int device_id) {
#ifdef _WIN32
        cv::VideoCapture test;
        if (test.open(device_id, cv::CAP_DSHOW)) {
            test.release();
            return CameraBackend::DShow;
        }
        if (test.open(device_id, cv::CAP_MSMF)) {
            test.release();
            return CameraBackend::MSMF;
        }
#endif
        return CameraBackend::OpenCV;
    }

    CameraConfig config_;             /* 相机配置 */
    CameraBackend backend_;           /* 当前使用的后端 */
    cv::VideoCapture cap_;            /* OpenCV 视频采集器 */
    bool is_open_ = false;
    bool is_video_ = false;           /* 是否为视频文件模式 */
    int total_frames_ = 0;            /* 视频总帧数 */

    int64_t frame_count_;             /* 累计采集帧数 */
    int64_t dropped_count_;           /* 累计丢帧数 */
    int64_t last_valid_timestamp_;    /* 最后一帧有效时间戳 */
    double fps_ = 0.0;                /* 实测帧率 */
    std::string last_error_;          /* 最后错误信息 */
};

/* ================================================================
 *  Camera 公开 API — 委托给 Impl
 * ================================================================ */

Camera::Camera(const CameraConfig& config)
    : impl_(std::make_unique<Impl>(config)) {}
Camera::~Camera() { close(); }

bool   Camera::open()                         { return impl_->open(); }
void   Camera::close()                        { impl_->close(); }
bool   Camera::is_opened() const              { return impl_->is_opened(); }
bool   Camera::reopen()                       { return impl_->reopen(); }

bool   Camera::capture_frame(cv::Mat& f)       { return impl_->capture_frame(f); }
bool   Camera::capture_frame(cv::Mat& f, int t){ return impl_->capture_frame(f, t); }
bool   Camera::save_snapshot(const std::string& p) { return impl_->save_snapshot(p); }

bool   Camera::set(int p, double v)            { return impl_->set(p, v); }
double Camera::get(int p) const                { return impl_->get(p); }
bool   Camera::set_exposure(double v)          { return impl_->set_exposure(v); }
bool   Camera::set_gain(double v)              { return impl_->set_gain(v); }
bool   Camera::set_resolution(int w, int h)    { return impl_->set_resolution(w, h); }
bool   Camera::set_fps(double f)               { return impl_->set_fps(f); }
bool   Camera::set_roi(int x, int y, int w, int h) { return impl_->set_roi(x, y, w, h); }

CameraConfig Camera::config() const            { return impl_->config(); }
std::string  Camera::last_error() const         { return impl_->last_error(); }
double       Camera::measured_fps() const       { return impl_->measured_fps(); }
int          Camera::frame_count() const         { return impl_->frame_count(); }
int          Camera::dropped_frame_count() const { return impl_->dropped_frame_count(); }
bool         Camera::frame_healthy(const cv::Mat& f) const { return impl_->frame_healthy(f); }

/* ── 静态方法 ─────────────────────────────────────────────────── */

/* 枚举 0~9 号设备，返回可用的设备列表 */
std::vector<int> Camera::list_available_devices() {
    std::vector<int> devices;
    for (int i = 0; i < 10; ++i) {
        cv::VideoCapture cap;
#ifdef _WIN32
        if (cap.open(i, cv::CAP_DSHOW)) {
#else
        if (cap.open(i)) {
#endif
            devices.push_back(i);
            cap.release();
        }
    }
    return devices;
}

/* 探测指定设备在指定超时时间内是否能返回有效帧 */
bool Camera::probe_device(int device_id, CameraBackend backend, int timeout_ms) {
    int cv_backend = (backend == CameraBackend::AutoDetect)
        ? cv::CAP_DSHOW : Camera::backend_to_cv(backend);
    cv::VideoCapture cap;
    if (!cap.open(device_id, cv_backend)) return false;

    cv::Mat frame;
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(timeout_ms);
    bool ok = false;
    while (std::chrono::steady_clock::now() < deadline) {
        if (cap.grab() && cap.retrieve(frame)) {
            ok = !frame.empty();
            break;
        }
    }
    cap.release();
    return ok;
}
