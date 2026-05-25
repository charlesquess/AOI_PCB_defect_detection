#include "utils/config.hpp"
#include "utils/logger.hpp"
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <sstream>
#include <cctype>

/* 字符串 → CameraBackend 枚举 */
static CameraBackend parse_backend(const std::string& s) {
    std::string upper;
    upper.reserve(s.size());
    for (char c : s) upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    if (upper == "DSHOW")  return CameraBackend::DShow;
    if (upper == "MSMF")   return CameraBackend::MSMF;
    if (upper == "VFW")    return CameraBackend::VFW;
    if (upper == "OPENCV") return CameraBackend::OpenCV;
    return CameraBackend::AutoDetect;
}

/* 字符串 → LogLevel */
static LogLevel parse_log_level(const std::string& s) {
    std::string lower;
    lower.reserve(s.size());
    for (char c : s) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (lower == "debug") return LogLevel::Debug;
    if (lower == "info")  return LogLevel::Info;
    if (lower == "warn")  return LogLevel::Warn;
    if (lower == "error") return LogLevel::Error;
    if (lower == "fatal") return LogLevel::Fatal;
    return LogLevel::Info;
}

/* 配置管理器内部实现 (PIMPL) */
class Config::Impl {
public:
    bool load(const std::string& filepath) {
        try {
            YAML::Node root = YAML::LoadFile(filepath);

            /* ── camera ── */
            if (root["camera"]) {
                auto n = root["camera"];
                auto& cam = app_config_.camera;
                cam.device_id     = n["device_id"].as<int>(cam.device_id);
                cam.video_path    = n["video_path"].as<std::string>(cam.video_path);
                cam.loop_video    = n["loop_video"].as<bool>(cam.loop_video);
                cam.width         = n["width"].as<int>(cam.width);
                cam.height        = n["height"].as<int>(cam.height);
                cam.fps           = n["fps"].as<double>(cam.fps);
                cam.exposure      = n["exposure"].as<double>(cam.exposure);
                cam.gain          = n["gain"].as<double>(cam.gain);
                if (n["backend"]) cam.backend = parse_backend(n["backend"].as<std::string>());
                cam.timeout_ms    = n["timeout_ms"].as<int>(cam.timeout_ms);
                cam.retry_count   = n["retry_count"].as<int>(cam.retry_count);
                cam.auto_reconnect = n["auto_reconnect"].as<bool>(cam.auto_reconnect);
                cam.roi_x         = n["roi_x"].as<int>(cam.roi_x);
                cam.roi_y         = n["roi_y"].as<int>(cam.roi_y);
                cam.roi_w         = n["roi_w"].as<int>(cam.roi_w);
                cam.roi_h         = n["roi_h"].as<int>(cam.roi_h);
            }

            /* ── model ── */
            if (root["model"]) {
                auto n = root["model"];
                app_config_.model_path     = n["path"].as<std::string>(app_config_.model_path);
                app_config_.use_gpu        = n["use_gpu"].as<bool>(app_config_.use_gpu);
                app_config_.gpu_device_id  = n["gpu_device_id"].as<int>(app_config_.gpu_device_id);
            }

            /* ── detection ── */
            if (root["detection"]) {
                auto n = root["detection"];
                app_config_.confidence_threshold = n["confidence_threshold"].as<float>(app_config_.confidence_threshold);
                app_config_.nms_threshold        = n["nms_threshold"].as<float>(app_config_.nms_threshold);
            }

            /* ── serial ── */
            if (root["serial"]) {
                auto n = root["serial"];
                auto& ser = app_config_.serial;
                ser.port       = n["port"].as<std::string>(ser.port);
                ser.baudrate   = n["baudrate"].as<int>(ser.baudrate);
                ser.data_bits  = n["data_bits"].as<int>(ser.data_bits);
                if (n["parity"]) {
                    std::string p = n["parity"].as<std::string>();
                    ser.parity = p.empty() ? 'N' : p[0];
                }
                ser.stop_bits  = n["stop_bits"].as<int>(ser.stop_bits);
            }

            /* ── ui ── */
            if (root["ui"]) {
                auto n = root["ui"];
                auto& ui = app_config_.ui;
                ui.window_width  = n["window_width"].as<int>(ui.window_width);
                ui.window_height = n["window_height"].as<int>(ui.window_height);
                ui.show_fps      = n["show_fps"].as<bool>(ui.show_fps);
            }

            /* ── logging ── */
            if (root["logging"]) {
                auto n = root["logging"];
                if (n["level"]) Logger::instance().set_level(parse_log_level(n["level"].as<std::string>()));
                if (n["file"])  Logger::instance().set_log_file(n["file"].as<std::string>());
            }

            loaded_ = true;
            LOG_INFO("配置文件已加载: " + filepath);
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("加载配置文件失败: " + std::string(e.what()));
            return false;
        }
    }

    bool save(const std::string& filepath) const {
        try {
            YAML::Emitter out;
            out << YAML::BeginMap;

            out << YAML::Key << "camera";
            out << YAML::Value << YAML::BeginMap;
            auto& cam = app_config_.camera;
            out << YAML::Key << "device_id"       << YAML::Value << cam.device_id;
            out << YAML::Key << "video_path"      << YAML::Value << cam.video_path;
            out << YAML::Key << "loop_video"      << YAML::Value << cam.loop_video;
            out << YAML::Key << "width"           << YAML::Value << cam.width;
            out << YAML::Key << "height"          << YAML::Value << cam.height;
            out << YAML::Key << "fps"             << YAML::Value << cam.fps;
            out << YAML::Key << "exposure"        << YAML::Value << cam.exposure;
            out << YAML::Key << "gain"            << YAML::Value << cam.gain;
            out << YAML::Key << "timeout_ms"      << YAML::Value << cam.timeout_ms;
            out << YAML::Key << "retry_count"     << YAML::Value << cam.retry_count;
            out << YAML::Key << "auto_reconnect"  << YAML::Value << cam.auto_reconnect;
            out << YAML::Key << "roi_x"           << YAML::Value << cam.roi_x;
            out << YAML::Key << "roi_y"           << YAML::Value << cam.roi_y;
            out << YAML::Key << "roi_w"           << YAML::Value << cam.roi_w;
            out << YAML::Key << "roi_h"           << YAML::Value << cam.roi_h;
            out << YAML::EndMap;

            out << YAML::Key << "model";
            out << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "path"           << YAML::Value << app_config_.model_path;
            out << YAML::Key << "use_gpu"        << YAML::Value << app_config_.use_gpu;
            out << YAML::Key << "gpu_device_id"  << YAML::Value << app_config_.gpu_device_id;
            out << YAML::EndMap;

            out << YAML::Key << "detection";
            out << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "confidence_threshold" << YAML::Value << app_config_.confidence_threshold;
            out << YAML::Key << "nms_threshold"        << YAML::Value << app_config_.nms_threshold;
            out << YAML::EndMap;

            out << YAML::Key << "serial";
            out << YAML::Value << YAML::BeginMap;
            auto& ser = app_config_.serial;
            out << YAML::Key << "port"      << YAML::Value << ser.port;
            out << YAML::Key << "baudrate"  << YAML::Value << ser.baudrate;
            out << YAML::Key << "data_bits" << YAML::Value << ser.data_bits;
            out << YAML::Key << "parity"    << YAML::Value << std::string(1, ser.parity);
            out << YAML::Key << "stop_bits" << YAML::Value << ser.stop_bits;
            out << YAML::EndMap;

            out << YAML::Key << "ui";
            out << YAML::Value << YAML::BeginMap;
            auto& ui = app_config_.ui;
            out << YAML::Key << "window_width"  << YAML::Value << ui.window_width;
            out << YAML::Key << "window_height" << YAML::Value << ui.window_height;
            out << YAML::Key << "show_fps"      << YAML::Value << ui.show_fps;
            out << YAML::EndMap;

            out << YAML::EndMap;

            std::ofstream file(filepath);
            if (!file.is_open()) return false;
            file << out.c_str();
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("保存配置文件失败: " + std::string(e.what()));
            return false;
        }
    }

    AppConfig app_config() const { return app_config_; }
    void set_app_config(const AppConfig& cfg) { app_config_ = cfg; }

    std::string dump() const {
        std::ostringstream oss;
        auto& cam = app_config_.camera;
        oss << "Config{camera["
            << cam.width << "x" << cam.height
            << " @" << cam.fps << "fps"
            << " dev=" << cam.device_id
            << " roi=" << cam.roi_x << "," << cam.roi_y << "," << cam.roi_w << "x" << cam.roi_h
            << "], model[" << app_config_.model_path
            << " gpu=" << (app_config_.use_gpu ? "on" : "off")
            << "], det[conf=" << app_config_.confidence_threshold
            << " nms=" << app_config_.nms_threshold
            << "], serial[" << app_config_.serial.port
            << " @" << app_config_.serial.baudrate
            << "], ui[" << app_config_.ui.window_width << "x" << app_config_.ui.window_height
            << "]}";
        return oss.str();
    }

private:
    AppConfig app_config_;
    bool loaded_ = false;
};

Config::Config() : impl_(std::make_unique<Impl>()) {}
Config::~Config() = default;
bool Config::load(const std::string& fp) { return impl_->load(fp); }
bool Config::save(const std::string& fp) const { return impl_->save(fp); }
AppConfig Config::app_config() const { return impl_->app_config(); }
void Config::set_app_config(const AppConfig& c) { impl_->set_app_config(c); }
std::string Config::dump() const { return impl_->dump(); }
