#pragma once

#include <vector>
#include <opencv2/core/mat.hpp>
#include "utils/types.hpp"

/* 后处理模块 - 将模型裸输出转换为检测框，支持 NMS 和结果绘制 */
class Postprocessor {
public:
    explicit Postprocessor(float conf_threshold = 0.6f, float nms_threshold = 0.45f);
    ~Postprocessor();

    void set_confidence_threshold(float threshold);   /* 设置置信度阈值 */
    void set_nms_threshold(float threshold);           /* 设置 NMS 的 IoU 阈值 */

    /* 解析模型原始输出，返回检测框列表 */
    std::vector<BoundingBox> process(const float* raw_output,
                                     int num_detections,
                                     int num_classes,
                                     float scale_x, float scale_y);

    std::vector<BoundingBox> nms(const std::vector<BoundingBox>& boxes);
    std::vector<BoundingBox> filter(const std::vector<BoundingBox>& boxes) const;  /* 非极大值抑制 */

    void draw_results(cv::Mat& image, const std::vector<BoundingBox>& boxes);  /* 在图像上绘制检测框 */

private:
    float conf_threshold_;  /* 置信度阈值 */
    float nms_threshold_;   /* NMS 阈值 */
};
