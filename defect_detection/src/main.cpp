#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <csignal>
#include <thread>
#include <atomic>
#include <ctime>
#ifdef _WIN32
#include <windows.h>
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
#include "camera/camera.hpp"
#include "preprocess/preprocessor.hpp"
#include "inference/inferencer.hpp"
#include "postprocess/postprocessor.hpp"
#include "communication/serial_client.hpp"
#include "statistics/statistician.hpp"
#include "ui/ui_manager.hpp"

/* 全局控制 */
static std::atomic<bool> g_running{true};
static std::atomic<bool> g_trigger_mode{false};

void signal_handler(int) {
    g_running = false;
}

/* 创建目录（跨平台） */
static bool create_dir(const std::string& path) {
#ifdef _WIN32
    return _mkdir(path.c_str()) == 0 || errno == EEXIST;
#else
    return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
#endif
}

/* 缺陷框颜色 (BGR) */
static cv::Scalar defect_color(int class_id) {
    static cv::Scalar cols[] = {
        {0, 0, 255}, {0, 255, 0}, {255, 0, 0},
        {0, 255, 255}, {255, 0, 255}, {255, 255, 0}
    };
    return cols[class_id % 6];
}

/* 保存缺陷截图（叠加框）与元数据 */
static void save_defect(const cv::Mat& frame, const std::vector<BoundingBox>& defects,
                        int frame_id, int pcb_count, const std::string& model_ver) {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    auto ts = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    std::string dir = "logs/defects";
    create_dir(dir);

    // 叠加检测框到图像
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

int main(int argc, char* argv[]) {
    /* 不管从哪里启动,自动切换到 exe 上级的上级(项目根目录) */
#ifdef _WIN32
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring exe_dir = std::wstring(path);
    auto pos = exe_dir.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        exe_dir = exe_dir.substr(0, pos) + L"\\..\\..";
        _wchdir(exe_dir.c_str());
    }
#endif

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    Logger::instance().set_level(LogLevel::Info);
    Logger::instance().set_log_file("logs/defect_detection.log");
    LOG_INFO("=== PCB 缺陷检测系统 v1.0.0（多线程流水线）===");

    /* ── 加载配置 ────────────────────────── */
    Config config;
    std::string config_path = "config/config.yaml";
    if (argc > 1) config_path = argv[1];
    if (!config.load(config_path)) {
        LOG_ERROR("加载配置文件失败: " + config_path);
        return 1;
    }
    AppConfig app_cfg = config.app_config();
    LOG_INFO("配置文件已加载: " + config_path);

    /* ── 初始化模块 ────────────────────────── */
    Camera camera(app_cfg.camera);
    bool camera_ok = camera.open();
    if (!camera_ok) { LOG_WARN("相机未就绪,无相机模式(仅预览)"); }

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

            /* 版本兼容性检查 */
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
    if (model_loaded) {
        LOG_INFO("模型已加载: " + app_cfg.model_path);
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
    } else {
        LOG_WARN("模型未加载，运行在预览模式（仅显示相机画面）");
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
    UIManager ui;

    if (!ui.init_window("PCB Defect Detection", app_cfg.ui.window_width, app_cfg.ui.window_height)) {
        LOG_ERROR("UI 初始化失败");
        return 1;
    }
    {
        auto& shared = SharedResultBox::instance();
        std::lock_guard<std::mutex> lock(shared.mutex);
        if (!serial.is_open()) {
            shared.serial_state = SerialCommState::Disconnected;
            shared.serial_state_str = "串口未连接 - 仅 UI + 统计";
        } else {
            shared.serial_state = SerialCommState::Idle;
            shared.serial_state_str = "空闲";
        }
    }

    /* ── 线程安全队列 ────────────────────── */
    ThreadSafeQueue<FrameTask> raw_queue(3);
    ThreadSafeQueue<FrameTask> result_queue(3);

    /* ── 全局共享变量 ────────────────────── */
    std::atomic<int> pcb_count{0};

    /* ── 触发模式初始握手 ──────────────────── */
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

    /* ── 线程 1: 采集 ────────────────────── */
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

            FrameTask task;
            task.frame_id = ++frame_id;
            task.timestamp_us = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());

            if (!camera.capture_frame(task.frame)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            if (!raw_queue.push(std::move(task), 100)) {
                /* 队列满，丢弃当前帧 */
            }
        }
        LOG_INFO("采集线程退出");
    });

    /* ── 线程 2: 推理 ────────────────────── */
    std::thread inference_thread([&] {
        double fps_counter = 0.0;
        auto last_time = std::chrono::steady_clock::now();
        int frame_count = 0;

        while (g_running) {
            FrameTask task;
            if (!raw_queue.pop(task, 50)) {
                continue;
            }

            Timer timer("pipeline");

            /* 预处理 */
            std::vector<BoundingBox> all_detections;
            std::vector<BoundingBox> new_defects;
            std::vector<BoundingBox> tracked_defects;

            if (model_loaded) {
                {
                    auto& shared = SharedResultBox::instance();
                    std::lock_guard<std::mutex> lock(shared.mutex);
                    shared.serial_state = SerialCommState::Inferring;
                    shared.serial_state_str = "推理中";
                }
                auto results = inferencer.infer(task.frame);
                if (!results.empty()) {
                    all_detections = results[0].detections;
                }
                std::tie(new_defects, tracked_defects) = tracker.track(all_detections);

                if (!new_defects.empty()) {
                    DetectionResult dr;
                    dr.frame_id = task.frame_id;
                    dr.detections = new_defects;
                    statistician.record_detection(dr);
                    pcb_count++;
                }

                /* 串口回传 */
                {
                    auto& shared = SharedResultBox::instance();
                    std::lock_guard<std::mutex> lock(shared.mutex);
                    shared.serial_state = SerialCommState::Reporting;
                    shared.serial_state_str = "回传结果";
                }
                if (!new_defects.empty()) {
                    if (serial.is_open()) {
                        serial.send("FAIL:" + std::to_string(new_defects.size()) + "\n");
                        LOG_INFO("状态机: 发送 FAIL:" + std::to_string(new_defects.size()));
                    }
                    save_defect(task.frame, new_defects, task.frame_id, pcb_count.load(), model_version);
                } else if (!all_detections.empty()) {
                    if (serial.is_open()) {
                        serial.send("PASS\n");
                        LOG_INFO("状态机: 发送 PASS");
                    }
                }
                if (g_trigger_mode && serial.is_open()) {
                    serial.send("DONE\n");
                    LOG_INFO("状态机: 发送 DONE");
                }
                {
                    auto& shared = SharedResultBox::instance();
                    std::lock_guard<std::mutex> lock(shared.mutex);
                    shared.serial_state = SerialCommState::Idle;
                    shared.serial_state_str = "空闲";
                }
            }

            /* 更新 FPS */
            frame_count++;
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - last_time).count();
            if (elapsed >= 1.0) {
                fps_counter = frame_count / elapsed;
                frame_count = 0;
                last_time = now;
            }

            task.results.clear();
            if (!all_detections.empty()) {
                DetectionResult dr;
                dr.frame_id = task.frame_id;
                dr.detections = all_detections;
                dr.inference_time_ms = timer.elapsed_ms();
                task.results.push_back(dr);
            }
            task.has_result = model_loaded;

            if (!result_queue.push(std::move(task), 100)) {
                /* 结果队列满，丢弃 */
            }
        }
        LOG_INFO("推理线程退出");
    });

    /* ── 线程 3: UI (主线程) ─────────────── */
    LOG_INFO("系统就绪。按 ESC 或 Q 退出。");

    double fps_ui = 0.0;
    auto ui_last_time = std::chrono::steady_clock::now();
    int ui_frame_count = 0;

    while (g_running) {
        FrameTask task;
        if (result_queue.pop(task, 10)) {
            ui_frame_count++;

            cv::Mat display = task.frame.clone();
            if (!task.results.empty()) {
                postprocessor.draw_results(display, task.results[0].detections);
            }

            ui.draw_model_info(display);
            if (!task.results.empty() && !task.results[0].detections.empty()) {
                auto summary = statistician.summary();
                ui.draw_overlay(display, summary);
            }

            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - ui_last_time).count();
            if (elapsed >= 1.0) {
                fps_ui = ui_frame_count / elapsed;
                ui_frame_count = 0;
                ui_last_time = now;
            }

            ui.draw_info_panel(display, "PCB#" + std::to_string(pcb_count) +
                               "  FPS: " + std::to_string(static_cast<int>(fps_ui)));

            ui.show_frame(display);
        }

        int key = ui.wait_key(1);
        if (key == 'q' || key == 'Q' || key == 27) {
            g_running = false;
        }
    }

    /* ── 清理退出 ────────────────────────── */
    LOG_INFO("正在关闭系统...");
    g_running = false;

    raw_queue.close();
    result_queue.close();

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
    ui.close_window();

    LOG_INFO("系统已安全关闭。");
    return 0;
}
