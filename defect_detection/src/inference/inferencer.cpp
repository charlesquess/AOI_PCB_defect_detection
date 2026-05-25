#include "inference/inferencer.hpp"
#include "utils/logger.hpp"
#include "utils/timer.hpp"
#include <opencv2/imgproc.hpp>
#include <opencv2/dnn.hpp>
#include <vector>
#include <string>
#include <numeric>
#include <optional>
#include <algorithm>
#include <future>
#include <mutex>

#include <onnxruntime_cxx_api.h>

/* 内部实现类 */
class Inferencer::Impl {
public:
    Impl() : env_(ORT_LOGGING_LEVEL_WARNING, "PCB-Defect-Detection") {}

    void set_confidence_threshold(float t) { conf_threshold_ = t; }
    void set_nms_threshold(float t) { nms_threshold_ = t; }

    bool load_model(const std::string& model_path, bool use_gpu, int gpu_id) {
        try {
            Ort::SessionOptions opts;
            opts.SetIntraOpNumThreads(1);
            opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            if (use_gpu) {
                LOG_INFO("启用 CUDA GPU 推理 (device=" + std::to_string(gpu_id) + ")");
                OrtCUDAProviderOptions cuda_opts;
                cuda_opts.device_id = gpu_id;
                // 禁用 cuDNN Frontend API (某些 cuDNN 9 构建不支持 CC 7.0 的 FE)
                cuda_opts.cudnn_conv_algo_search = OrtCudnnConvAlgoSearchHeuristic;
                // 使用默认流避免同步问题
                cuda_opts.do_copy_in_default_stream = 1;
                opts.AppendExecutionProvider_CUDA(cuda_opts);
            }

            std::wstring wp(model_path.begin(), model_path.end());
            session_ = std::make_unique<Ort::Session>(env_, wp.c_str(), opts);
            loaded_ = true;

            Ort::AllocatorWithDefaultOptions alloc;
            input_name_ = Ort::AllocatedStringPtr(session_->GetInputNameAllocated(0, alloc)).get();
            auto it = session_->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo();
            input_shape_ = it.GetShape();
            output_name_ = Ort::AllocatedStringPtr(session_->GetOutputNameAllocated(0, alloc)).get();
            auto ot = session_->GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo();
            output_shape_ = ot.GetShape();

            info_.input_name = input_name_; info_.output_name = output_name_;
            info_.input_shape = input_shape_; info_.output_shape = output_shape_;
            if (input_shape_.size() >= 4) {
                input_h_ = (input_shape_[2] > 0) ? (int)input_shape_[2] : 640;
                input_w_ = (input_shape_[3] > 0) ? (int)input_shape_[3] : 640;
            }
            LOG_INFO("模型加载成功: " + model_path);
            LOG_INFO("  输入: " + input_name_ + " " + shape_to_string(input_shape_));
            LOG_INFO("  输出: " + output_name_ + " " + shape_to_string(output_shape_));
            return true;
        } catch (const Ort::Exception& e) {
            LOG_ERROR("ONNX Runtime 错误: " + std::string(e.what()));
            return false;
        }
    }

    void unload_model() { if (loaded_) { session_.reset(); loaded_ = false; } }

    /* ================================================================ */
    /*  对外接口：滑窗推理 + 跨窗口 NMS 融合                              */
    /* ================================================================ */
    std::vector<DetectionResult> infer(const cv::Mat& image) {
        if (!loaded_) return {};
        Timer total_timer("推理总耗时");

        int W = image.cols, H = image.rows;
        std::vector<BoundingBox> all_boxes;

        /* 生成滑窗：小图直接一窗，大图按步长 320 切 */
        struct Win { int x, y; };
        std::vector<Win> windows;
        // 如果图像能放进一个窗口（双边 ≤ input size），不做滑窗
        bool single = (W <= input_w_ && H <= input_h_);
        if (single) {
            windows.push_back({0, 0});
        } else {
            int stride = input_w_ / 2;
            for (int y = 0; y < H; y += stride)
                for (int x = 0; x < W; x += stride)
                    windows.push_back({x, y});
            if (windows.empty()) windows.push_back({0, 0});
        }
        int n_win = (int)windows.size();
        if (n_win > 1)
            LOG_INFO("滑窗: " + std::to_string(n_win) + " 个窗口, 步长 " + std::to_string(input_w_/2) +
                     ", 图片 " + std::to_string(W) + "x" + std::to_string(H));

        /* 并行处理所有窗口 */
        std::mutex boxes_mutex;
        std::vector<std::string> window_logs;
        std::mutex log_mutex;
        int concurrency = std::min(n_win, 8);
        size_t chunk = (n_win + concurrency - 1) / concurrency;
        std::vector<std::future<void>> futures;

        auto process_chunk = [&](size_t begin, size_t end) {
            std::vector<BoundingBox> local_boxes;
            for (size_t i = begin; i < end; ++i) {
                auto& w = windows[i];
                int x1 = w.x, y1 = w.y;
                int x2 = std::min(w.x + input_w_, W);
                int y2 = std::min(w.y + input_h_, H);
                int cw = x2 - x1, ch = y2 - y1;
                if (cw <= 0 || ch <= 0) continue;

                Timer win_timer("  " + std::to_string(i));
                cv::Mat white(input_h_, input_w_, CV_8UC3, cv::Scalar::all(255));
                image(cv::Rect(x1, y1, cw, ch)).copyTo(white(cv::Rect(0, 0, cw, ch)));
                double crop_ms = win_timer.elapsed_ms();

                auto boxes = infer_one_crop(white, x1, y1);
                double infer_ms = win_timer.elapsed_ms() - crop_ms;

                // 收集日志，待最终有检出时才输出
                if (!boxes.empty()) {
                    std::lock_guard<std::mutex> lock(log_mutex);
                    window_logs.push_back("  窗口[" + std::to_string(i) + "] (" + std::to_string(x1) + "," +
                                          std::to_string(y1) + ") 裁剪=" + std::to_string((int)crop_ms) +
                                          "ms 推理=" + std::to_string((int)infer_ms) + "ms 检出=" +
                                          std::to_string(boxes.size()));
                }

                local_boxes.insert(local_boxes.end(), boxes.begin(), boxes.end());
            }
            std::lock_guard<std::mutex> lock(boxes_mutex);
            all_boxes.insert(all_boxes.end(), local_boxes.begin(), local_boxes.end());
        };

        Timer parallel_timer("并行阶段");
        for (size_t i = 0; i < windows.size(); i += chunk) {
            size_t end = std::min(i + chunk, windows.size());
            futures.push_back(std::async(std::launch::async, process_chunk, i, end));
        }
        for (auto& f : futures) f.get();

        /* 跨窗口 NMS 融合 + 置信度过滤 */
        Timer nms_timer("NMS融合");
        all_boxes = global_nms(all_boxes, nms_threshold_);

        /* 按配置阈值过滤 */
        size_t before = all_boxes.size();
        all_boxes.erase(std::remove_if(all_boxes.begin(), all_boxes.end(),
            [this](const BoundingBox& b) { return b.confidence < conf_threshold_; }),
            all_boxes.end());

        DetectionResult result;
        result.image_width = W;
        result.image_height = H;
        result.detections = all_boxes;
        result.inference_time_ms = total_timer.elapsed_ms();

        // 仅在最终有检出时才输出详细日志
        if (!all_boxes.empty()) {
            for (auto& msg : window_logs) LOG_INFO(msg);
            if (n_win > 1)
                LOG_INFO("并行阶段: " + std::to_string((int)parallel_timer.elapsed_ms()) + "ms");
            LOG_INFO("NMS融合: " + std::to_string((int)nms_timer.elapsed_ms()) + "ms, 融合前 " +
                     std::to_string(before) + " 融合后 " + std::to_string(all_boxes.size()) + " 个检测框");
            LOG_INFO("推理总耗时: " + std::to_string((int)result.inference_time_ms) + "ms");
        }
        return {result};
    }

    std::vector<DetectionResult> infer_batch(const std::vector<cv::Mat>& images) {
        std::vector<DetectionResult> batch;
        for (auto& img : images) {
            auto r = infer(img);
            batch.insert(batch.end(), r.begin(), r.end());
        }
        return batch;
    }

    ModelInfo model_info() const { return info_; }
    bool is_loaded() const { return loaded_; }

private:
    /* ================================================================ */
    /*  单窗口推理                                                       */
    /*  crop: input_w_ × input_h_, 已填充好的图像                          */
    /*  offset_x/y: 此窗口在原图中的左上角偏移                             */
    /* ================================================================ */
    std::vector<BoundingBox> infer_one_crop(const cv::Mat& crop, int offset_x, int offset_y) {
        cv::Mat blob;
        cv::dnn::blobFromImage(crop, blob, 1.0 / 255.0,
                               cv::Size(input_w_, input_h_),
                               cv::Scalar(), true, false);

        size_t num_el = (size_t)1 * 3 * input_h_ * input_w_;
        float* ptr = (float*)blob.data;
        std::vector<int64_t> dims = {1, 3, input_h_, input_w_};
        auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value in_tensor = Ort::Value::CreateTensor<float>(mem, ptr, num_el, dims.data(), dims.size());

        const char* in_name[] = {input_name_.c_str()};
        const char* out_name[] = {output_name_.c_str()};
        auto out = session_->Run(Ort::RunOptions{nullptr}, in_name, &in_tensor, 1, out_name, 1);

        float* raw = out[0].GetTensorMutableData<float>();
        auto info = out[0].GetTensorTypeAndShapeInfo();
        auto shape = info.GetShape();

        std::vector<BoundingBox> boxes;
        if (shape.size() != 3 || shape[1] <= 0) return boxes;

        int num = (int)shape[1], attr = (int)shape[2], nc = attr - 5;
        for (int i = 0; i < num; ++i) {
            float obj = raw[i * attr + 4];
            if (obj < 0.001f) continue;  // 近零 objectness 快速跳过

            float max_c = 0; int cid = 0;
            for (int c = 0; c < nc; ++c) {
                float s = raw[i * attr + 5 + c];
                if (s > max_c) { max_c = s; cid = c; }
            }
            float conf = obj * max_c;

            // 坐标：模型输出 [cx,cy,w,h] → 左上角 [x,y,w,h]
            // 再加窗口偏移转到原图坐标系
            float cx = raw[i * attr + 0];
            float cy = raw[i * attr + 1];
            float bw = raw[i * attr + 2];
            float bh = raw[i * attr + 3];
            BoundingBox bb;
            bb.x       = cx - bw * 0.5f + offset_x;
            bb.y       = cy - bh * 0.5f + offset_y;
            bb.width   = bw;
            bb.height  = bh;
            bb.confidence = conf;
            bb.class_id = cid;
            bb.label    = static_cast<DefectType>(cid);
            boxes.push_back(bb);
        }
        return boxes;
    }

    /* ================================================================ */
    /*  全局 NMS：合并来自不同窗口的重复检测                                 */
    /* ================================================================ */
    static float iou(const BoundingBox& a, const BoundingBox& b) {
        float ix = std::max(a.x, b.x), iy = std::max(a.y, b.y);
        float iw = std::min(a.x + a.width, b.x + b.width) - ix;
        float ih = std::min(a.y + a.height, b.y + b.height) - iy;
        if (iw <= 0 || ih <= 0) return 0;
        float inter = iw * ih;
        return inter / (a.width * a.height + b.width * b.height - inter);
    }

    static std::vector<BoundingBox> global_nms(std::vector<BoundingBox>& boxes, float thresh) {
        if (boxes.empty()) return {};
        std::sort(boxes.begin(), boxes.end(), [](auto& a, auto& b) { return a.confidence > b.confidence; });
        std::vector<BoundingBox> result;
        std::vector<bool> suppressed(boxes.size(), false);
        for (size_t i = 0; i < boxes.size(); ++i) {
            if (suppressed[i]) continue;
            result.push_back(boxes[i]);
            for (size_t j = i + 1; j < boxes.size(); ++j) {
                if (!suppressed[j] && boxes[i].class_id == boxes[j].class_id && iou(boxes[i], boxes[j]) > thresh)
                    suppressed[j] = true;
            }
        }
        return result;
    }

    static std::string shape_to_string(const std::vector<int64_t>& shape) {
        std::string s = "["; for (size_t i = 0; i < shape.size(); ++i) { if (i) s += ","; s += std::to_string(shape[i]); } s += "]"; return s;
    }

    Ort::Env env_{ORT_LOGGING_LEVEL_WARNING, "PCB-Defect-Detection"};
    std::unique_ptr<Ort::Session> session_;
    bool loaded_ = false;
    std::string input_name_, output_name_;
    std::vector<int64_t> input_shape_, output_shape_;
    int input_w_ = 640, input_h_ = 640;
    ModelInfo info_;
    float conf_threshold_ = 0.6f;
    float nms_threshold_ = 0.45f;
};

Inferencer::Inferencer() : impl_(std::make_unique<Impl>()) {}
Inferencer::~Inferencer() = default;
bool Inferencer::load_model(const std::string& p, bool g, int d) { return impl_->load_model(p, g, d); }
void Inferencer::unload_model() { impl_->unload_model(); }
std::vector<DetectionResult> Inferencer::infer(const cv::Mat& img) { return impl_->infer(img); }
std::vector<DetectionResult> Inferencer::infer_batch(const std::vector<cv::Mat>& imgs) { return impl_->infer_batch(imgs); }
void Inferencer::set_confidence_threshold(float t) { impl_->set_confidence_threshold(t); }
void Inferencer::set_nms_threshold(float t) { impl_->set_nms_threshold(t); }
ModelInfo Inferencer::model_info() const { return impl_->model_info(); }
bool Inferencer::is_loaded() const { return impl_->is_loaded(); }
