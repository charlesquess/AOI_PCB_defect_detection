#include <QApplication>
#include <QThread>
#include <QImage>
#include <QDir>
#include <QCoreApplication>
#include <QMetaObject>
#include <atomic>
#include <csignal>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#include "utils/logger.hpp"
#include "utils/config.hpp"
#include "utils/types.hpp"
#include "utils/timer.hpp"
#include "utils/tracker.hpp"
#include "utils/thread_queue.hpp"
#include "ui/qt_main_window.hpp"
#include "camera/camera.hpp"
#include "preprocess/preprocessor.hpp"
#include "inference/inferencer.hpp"
#include "postprocess/postprocessor.hpp"
#include "communication/serial_client.hpp"
#include "statistics/statistician.hpp"

static std::atomic<bool> g_running{true};
static std::atomic<bool> g_trigger_mode{false};
void signal_handler(int) { g_running = false; }

static bool create_dir(const std::string& path) {
#ifdef _WIN32
    return _mkdir(path.c_str()) == 0 || errno == EEXIST;
#else
    return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
#endif
}

static cv::Scalar defect_color(int class_id) {
    static cv::Scalar cols[] = {
        {0, 0, 255}, {0, 255, 0}, {255, 0, 0},
        {0, 255, 255}, {255, 0, 255}, {255, 255, 0}
    };
    return cols[class_id % 6];
}

static void save_defect(const cv::Mat& frame, const std::vector<BoundingBox>& defects,
                        int frame_id, int pcb_count, const std::string& model_ver) {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    auto ts = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    std::string dir = "logs/defects";
    create_dir(dir);

    cv::Mat annotated = frame.clone();
    for (auto& d : defects) {
        cv::Scalar color = defect_color(d.class_id);
        cv::Rect r((int)d.x, (int)d.y, (int)d.width, (int)d.height);
        cv::rectangle(annotated, r, color, 2);
        std::string label = std::to_string(d.class_id) + ":" + std::to_string((int)(d.confidence * 100)) + "%";
        int baseline = 0;
        cv::Size ts = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseline);
        cv::rectangle(annotated, cv::Point(r.x, r.y - ts.height - 4),
                      cv::Point(r.x + ts.width + 4, r.y), color, -1);
        cv::putText(annotated, label, cv::Point(r.x + 2, r.y - 2),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);
    }

    std::string prefix = dir + "/" + std::to_string(ts) + "_" + std::to_string(frame_id);
    cv::imwrite(prefix + ".jpg", annotated);

    std::ofstream json(prefix + ".json");
    if (json.is_open()) {
        json << "{\n";
        json << "  \"timestamp\": " << ts << ",\n";
        json << "  \"datetime\": \"" << std::put_time(std::localtime(&tt), "%Y-%m-%d %H:%M:%S") << "\",\n";
        json << "  \"frame_id\": " << frame_id << ",\n";
        json << "  \"pcb_count\": " << pcb_count << ",\n";
        json << "  \"model_version\": \"" << model_ver << "\",\n";
        json << "  \"defects\": [\n";
        for (size_t i = 0; i < defects.size(); ++i) {
            auto& d = defects[i];
            json << "    {\"class_id\": " << d.class_id
                 << ", \"confidence\": " << d.confidence
                 << ", \"x\": " << d.x << ", \"y\": " << d.y
                 << ", \"w\": " << d.width << ", \"h\": " << d.height
                 << "}";
            if (i < defects.size() - 1) json << ",";
            json << "\n";
        }
        json << "  ]\n}\n";
    }
}

/* 共享检测结果 — 推理线程写入，UI 线程读取叠加 */

/* 工具：cv::Mat → QImage */
static QImage mat_to_qimage(const cv::Mat& mat) {
    if (mat.empty()) return {};
    cv::Mat rgb;
    cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
    return QImage(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888).copy();
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    QApplication app(argc, argv);
    app.setApplicationName("PCB缺陷检测系统");
    QDir::setCurrent(QCoreApplication::applicationDirPath() + "/../..");

    Logger::instance().set_level(LogLevel::Info);
    Logger::instance().set_log_file("logs/defect_detection.log");
    LOG_INFO("=== PCB 缺陷检测系统 v1.0.0（Qt 版）===");

    /* ── 配置 ────────────────────────── */
    Config config;
    std::string config_path = "config/config.yaml";
    if (argc > 1) config_path = argv[1];
    if (!config.load(config_path)) { LOG_ERROR("加载配置文件失败"); return 1; }
    AppConfig app_cfg = config.app_config();
    LOG_INFO("配置文件已加载");

    /* ── 初始化模块 ────────────────────── */
    Camera camera(app_cfg.camera);
    bool camera_ok = camera.open();
    if (!camera_ok) LOG_WARN("相机未就绪,无相机模式(仅模拟测试可用)");

    /* ── 读 manifest.json（模型元信息） ───── */
    std::string model_version = "unknown";
    std::string training_date = "unknown";
    int manifest_num_classes = 0;
    int manifest_input_size = 0;
    {
        std::ifstream mf("models/manifest.json");
        if (mf.is_open()) {
            std::stringstream ss; ss << mf.rdbuf();
            std::string json = ss.str();
            auto get_str = [&](const std::string& key) -> std::string {
                auto p = json.find("\"" + key + "\"");
                if (p == std::string::npos) return "";
                auto q1 = json.find('"', p + key.size() + 3);
                if (q1 == std::string::npos) return "";
                auto q2 = json.find('"', q1 + 1);
                if (q2 == std::string::npos) return "";
                return json.substr(q1 + 1, q2 - q1 - 1);
            };
            auto get_int = [&](const std::string& key) -> int {
                auto p = json.find("\"" + key + "\"");
                if (p == std::string::npos) return 0;
                auto v = json.find(':', p + key.size() + 2);
                if (v == std::string::npos) return 0;
                auto e = json.find_first_of(",\n}", v + 1);
                if (e == std::string::npos) return 0;
                return std::stoi(json.substr(v + 1, e - v - 1));
            };
            model_version = get_str("model_version");
            if (model_version.empty()) model_version = "unknown";
            training_date = get_str("training_date");
            if (training_date.empty()) training_date = "unknown";
            manifest_num_classes = get_int("num_classes");
            manifest_input_size = get_int("input_width");

            const std::string MIN_VERSION = "1.0.0";
            if (model_version != "unknown" && model_version < MIN_VERSION) {
                LOG_ERROR("模型版本 " + model_version + " 低于最低要求 " + MIN_VERSION + "，拒绝加载");
                model_version = "incompatible";
            }

            LOG_INFO("manifest: 版本=" + model_version
                     + " 训练日期=" + training_date
                     + " 类别数=" + std::to_string(manifest_num_classes)
                     + " 输入=" + std::to_string(manifest_input_size));
        }
    }

    /* ── 初始化推理引擎 + 兼容性检查 ── */
    Preprocessor preprocessor;
    Inferencer inferencer;
    bool model_loaded = inferencer.load_model(app_cfg.model_path, app_cfg.use_gpu, app_cfg.gpu_device_id);
    SharedResultBox::instance().model_loaded = model_loaded;
    LOG_INFO(model_loaded ? "模型已加载" : "模型未加载，预览模式");

    if (model_loaded) {
        auto m_info = inferencer.model_info();
        int actual_input = (m_info.input_shape.size() >= 4 && m_info.input_shape[2] > 0)
                           ? (int)m_info.input_shape[2] : 0;
        int actual_classes = (m_info.output_shape.size() >= 3 && m_info.output_shape[2] > 5)
                             ? (int)(m_info.output_shape[2] - 5) : 0;
        if (manifest_input_size > 0 && actual_input > 0 && actual_input != manifest_input_size) {
            LOG_ERROR("ONNX 输入尺寸 (" + std::to_string(actual_input)
                      + ") 与 manifest (" + std::to_string(manifest_input_size) + ") 不匹配，拒绝加载");
            inferencer.unload_model(); model_loaded = false;
        } else if (manifest_num_classes > 0 && actual_classes > 0 && actual_classes != manifest_num_classes) {
            LOG_ERROR("ONNX 类别数 (" + std::to_string(actual_classes)
                      + ") 与 manifest (" + std::to_string(manifest_num_classes) + ") 不匹配，拒绝加载");
            inferencer.unload_model(); model_loaded = false;
        } else if (model_version == "incompatible") {
            inferencer.unload_model(); model_loaded = false;
        }
        SharedResultBox::instance().model_loaded = model_loaded;
    }

    {
        auto& shared = SharedResultBox::instance();
        std::lock_guard<std::mutex> lock(shared.mutex);
        shared.model_loaded = model_loaded;
        if (model_loaded) {
            shared.model_info.model_version = model_version;
            shared.model_info.training_date = training_date;
            shared.model_info.num_classes = manifest_num_classes > 0 ? manifest_num_classes : 6;
            shared.model_info.input_size = manifest_input_size > 0 ? manifest_input_size : 640;
        }
        shared.conf_threshold = app_cfg.confidence_threshold;
        shared.nms_threshold = app_cfg.nms_threshold;
    }

    Postprocessor postprocessor(app_cfg.confidence_threshold, app_cfg.nms_threshold);
    inferencer.set_confidence_threshold(app_cfg.confidence_threshold);
    inferencer.set_nms_threshold(app_cfg.nms_threshold);
    SerialClient serial(app_cfg.serial);
    Statistician statistician;
    Tracker tracker(0.3f, 3);
    serial.open();

    {
        auto& shared = SharedResultBox::instance();
        std::lock_guard<std::mutex> lock(shared.mutex);
        shared.serial_state = serial.is_open() ? SerialCommState::Idle : SerialCommState::Disconnected;
        shared.serial_state_str = serial.is_open() ? "空闲" : "串口未连接 - 仅 UI + 统计";
    }

    /* ── 触发模式初始检测 ──────────────────── */
    {
        auto resp = serial.receive(100);
        if (!resp.empty()) {
            std::string s(resp.begin(), resp.end());
            if (s.find("TRIG") != std::string::npos) {
                g_trigger_mode = true;
                LOG_INFO("检测到 TRIG 信号，进入触发模式");
            }
        }
    }

    DebugSettings debug_settings;
    debug_settings.model_path = app_cfg.model_path;
    debug_settings.camera_id = app_cfg.camera.device_id;
    debug_settings.camera_width = app_cfg.camera.width;
    debug_settings.camera_height = app_cfg.camera.height;
    debug_settings.video_path = app_cfg.camera.video_path;
    debug_settings.loop_video = app_cfg.camera.loop_video;
    debug_settings.serial_port = app_cfg.serial.port;
    debug_settings.serial_baud = app_cfg.serial.baudrate;
    debug_settings.conf_threshold = app_cfg.confidence_threshold;
    debug_settings.nms_threshold = app_cfg.nms_threshold;
    debug_settings.config = &config;

    /* ── 队列 ────────────────────────── */
    ThreadSafeQueue<FrameTask> raw_queue(3);
    ThreadSafeQueue<QImage> display_queue(3);  // 仅传 QImage，无框无统计

    MainWindow main_window(&raw_queue, nullptr, &postprocessor, &camera,
                           &inferencer, &preprocessor, &debug_settings,
                           SharedResultBox::instance().model_loaded);
    main_window.setDisplayQueue(&display_queue);
    main_window.show();

    /* ── 采集线程：推 raw_queue + 直接推 display_queue ── */
    std::thread capture_thread([&] {
        int frame_id = 0;
        while (g_running) {
            if (!camera_ok) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            /* 触发模式：等待 TRIG 信号 */
            if (g_trigger_mode) {
                {
                    auto& shared = SharedResultBox::instance();
                    std::lock_guard<std::mutex> lock(shared.mutex);
                    shared.serial_state = SerialCommState::WaitingTrig;
                    shared.serial_state_str = "等待 TRIG";
                }
                auto data = serial.receive(100);
                bool trig_received = false;
                if (!data.empty()) {
                    std::string s(data.begin(), data.end());
                    if (s.find("TRIG") != std::string::npos) {
                        trig_received = true;
                        LOG_INFO("状态机: 收到 TRIG, 进入推理");
                    } else if (s.find("RST") != std::string::npos) {
                        statistician.reset();
                        LOG_INFO("收到 RST, 统计已重置");
                    }
                }
                if (!trig_received) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
            }

            cv::Mat frame;
            if (!camera.capture_frame(frame)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            // 推 raw 队列走推理
            FrameTask task;
            task.frame_id = ++frame_id;
            task.frame = frame.clone();
            raw_queue.push(std::move(task), 100);

            // 同时直接推 display 队列（立即显示，无框）
            display_queue.push(mat_to_qimage(frame), 100);
        }
        LOG_INFO("采集线程退出");
    });

    /* ── 推理线程：更新共享结果 ── */
    double fps_counter = 0.0;
    auto fps_last = std::chrono::steady_clock::now();
    int fps_frames = 0;

    std::thread inference_thread([&] {
        while (g_running) {
            FrameTask task;
            if (!raw_queue.pop(task, 50)) continue;

            std::vector<BoundingBox> all_detections;
            std::vector<BoundingBox> new_defects;
            {
                {
                    auto& shared = SharedResultBox::instance();
                    std::lock_guard<std::mutex> lock(shared.mutex);
                    shared.serial_state = SerialCommState::Inferring;
                    shared.serial_state_str = "推理中";
                }
                // 锁定模型互斥锁，防止模型切换时正在推理
                std::lock_guard<std::mutex> model_lock(SharedResultBox::instance().model_mutex);
                if (SharedResultBox::instance().model_loaded) {
                    task.processed = task.frame;
                    auto results = inferencer.infer(task.frame);
                    if (!results.empty()) all_detections = results[0].detections;
                    auto [nd, td] = tracker.track(all_detections);
                    new_defects = nd;
                }
            }
            {
                auto& shared = SharedResultBox::instance();
                std::lock_guard<std::mutex> lock(shared.mutex);
                shared.serial_state = SerialCommState::Reporting;
                shared.serial_state_str = "回传结果";
            }
            if (!new_defects.empty()) {
                DetectionResult dr;
                dr.frame_id = task.frame_id;
                dr.detections = new_defects;
                statistician.record_detection(dr);
                if (serial.is_open()) {
                    serial.send("FAIL:" + std::to_string(new_defects.size()) + "\n");
                    LOG_INFO("状态机: 发送 FAIL:" + std::to_string(new_defects.size()));
                }
                save_defect(task.frame, new_defects, task.frame_id, SharedResultBox::instance().pcb_count, model_version);
                if (g_trigger_mode && serial.is_open()) { serial.send("DONE\n"); LOG_INFO("状态机: 发送 DONE"); }
            } else if (!all_detections.empty()) {
                if (serial.is_open()) {
                    serial.send("PASS\n");
                    LOG_INFO("状态机: 发送 PASS");
                }
                if (g_trigger_mode && serial.is_open()) { serial.send("DONE\n"); LOG_INFO("状态机: 发送 DONE"); }
            }
            {
                auto& shared = SharedResultBox::instance();
                std::lock_guard<std::mutex> lock(shared.mutex);
                shared.serial_state = SerialCommState::Idle;
                shared.serial_state_str = "空闲";
            }

            fps_frames++;
            auto now = std::chrono::steady_clock::now();
            double el = std::chrono::duration<double>(now - fps_last).count();
            if (el >= 1.0) { fps_counter = fps_frames / el; fps_frames = 0; fps_last = now; }

            // 更新共享结果（UI 读取后叠加到最新画面）
            {
                auto& sr = SharedResultBox::instance();
                std::lock_guard<std::mutex> lock(sr.mutex);
                sr.boxes = all_detections;
                sr.summary = statistician.summary();
                sr.pcb_count = static_cast<int>(statistician.summary().total_inspected);
                sr.fps = fps_counter;
            }
        }
        LOG_INFO("推理线程退出");
    });

    LOG_INFO("系统就绪");
    int ret = app.exec();

    g_running = false;
    raw_queue.close();
    display_queue.close();
    if (capture_thread.joinable()) capture_thread.join();
    if (inference_thread.joinable()) inference_thread.join();

    statistician.export_daily_csv("logs/stats");

    {
        auto s = statistician.summary();
        std::ofstream csv("logs/stats_report.csv");
        if (csv.is_open()) {
            csv << "model_version," << model_version << "\n";
            csv << "total_inspected," << s.total_inspected << "\n";
            csv << "pass_count," << s.pass_count << "\n";
            csv << "fail_count," << s.fail_count << "\n";
            csv << "yield_rate," << s.yield_rate << "\n";
            csv << "avg_inference_time_ms," << s.avg_inference_time_ms << "\n";
            auto dd = statistician.defect_distribution();
            for (size_t i = 0; i < dd.size(); ++i)
                csv << "defect_type_" << i << "," << dd[i] << "\n";
        }
    }
    serial.close();
    camera.close();
    LOG_INFO("系统已安全关闭");
    return ret;
}
