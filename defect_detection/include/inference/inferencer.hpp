#pragma once

#include <string>
#include <memory>
#include <vector>
#include <opencv2/core/mat.hpp>
#include "utils/types.hpp"

/* 模型信息结构体，描述 ONNX 模型的输入输出张量信息 */
struct ModelInfo {
    std::vector<int64_t> input_shape;   /* 输入张量形状 */
    std::vector<int64_t> output_shape;  /* 输出张量形状 */
    std::string input_name;             /* 输入节点名称 */
    std::string output_name;            /* 输出节点名称 */
};

/* ONNX Runtime 推理引擎模块 */
class Inferencer {
public:
    Inferencer();
    ~Inferencer();

    /* 加载 ONNX 模型，支持 CPU/GPU */
    bool load_model(const std::string& model_path, bool use_gpu = false, int gpu_id = 0);
    void unload_model();                     /* 卸载模型 */

    std::vector<DetectionResult> infer(const cv::Mat& image);              /* 单帧推理 */
    std::vector<DetectionResult> infer_batch(const std::vector<cv::Mat>& images);  /* 批量推理 */

    void set_confidence_threshold(float t);   /* 设置推理置信度阈值 */
    void set_nms_threshold(float t);          /* 设置 NMS IoU 阈值 */

    ModelInfo model_info() const;   /* 获取模型信息 */
    bool is_loaded() const;         /* 模型是否已加载 */

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
