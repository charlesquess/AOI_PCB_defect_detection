#include "postprocess/postprocessor.hpp"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

Postprocessor::Postprocessor(float conf_threshold, float nms_threshold)
    : conf_threshold_(conf_threshold), nms_threshold_(nms_threshold) {}

Postprocessor::~Postprocessor() = default;

void Postprocessor::set_confidence_threshold(float threshold) {
    conf_threshold_ = threshold;
}

void Postprocessor::set_nms_threshold(float threshold) {
    nms_threshold_ = threshold;
}

/* [已废弃] 解析模型原始输出 -> BoundingBox
 * 说明: YOLOv5 原始输出解析已统一在 Inferencer::infer_one_crop() 中完成。
 *       此方法不再被主流程调用，保留仅作接口兼容，请勿在新代码中使用。
 */
std::vector<BoundingBox> Postprocessor::process(const float* raw_output,
                                                 int num_detections,
                                                 int num_classes,
                                                 float scale_x, float scale_y) {
    (void)raw_output;
    (void)num_detections;
    (void)num_classes;
    (void)scale_x;
    (void)scale_y;
    return {};
}

/* 置信度过滤：只保留置信度 ≥ 阈值的框 */
std::vector<BoundingBox> Postprocessor::filter(const std::vector<BoundingBox>& boxes) const {
    std::vector<BoundingBox> result;
    for (const auto& b : boxes) {
        if (b.confidence >= conf_threshold_) {
            result.push_back(b);
        }
    }
    return result;
}

/* 非极大值抑制 (NMS)：先过滤置信度，再对剩余框做 NMS */
std::vector<BoundingBox> Postprocessor::nms(const std::vector<BoundingBox>& boxes) {
    if (boxes.empty()) return {};

    /* 先按置信度过滤 */
    std::vector<BoundingBox> filtered = filter(boxes);
    if (filtered.empty()) return {};

    /* 按置信度降序排序 */
    std::vector<BoundingBox> sorted = filtered;
    std::sort(sorted.begin(), sorted.end(),
              [](const BoundingBox& a, const BoundingBox& b) {
                  return a.confidence > b.confidence;
              });

    std::vector<BoundingBox> result;
    std::vector<bool> suppressed(sorted.size(), false);

    for (size_t i = 0; i < sorted.size(); ++i) {
        if (suppressed[i]) continue;
        result.push_back(sorted[i]);

        for (size_t j = i + 1; j < sorted.size(); ++j) {
            if (suppressed[j]) continue;

            /* 计算交集面积 */
            float inter_x = std::max(sorted[i].x, sorted[j].x);
            float inter_y = std::max(sorted[i].y, sorted[j].y);
            float inter_w = std::min(sorted[i].x + sorted[i].width, sorted[j].x + sorted[j].width) - inter_x;
            float inter_h = std::min(sorted[i].y + sorted[i].height, sorted[j].y + sorted[j].height) - inter_y;

            if (inter_w <= 0 || inter_h <= 0) continue;

            float inter_area = inter_w * inter_h;
            float area_i = sorted[i].width * sorted[i].height;
            float area_j = sorted[j].width * sorted[j].height;
            float iou = inter_area / (area_i + area_j - inter_area);

            if (iou > nms_threshold_) suppressed[j] = true;
        }
    }

    return result;
}

/* 在图像上绘制检测框和标签 */
void Postprocessor::draw_results(cv::Mat& image, const std::vector<BoundingBox>& boxes) {
    for (const auto& box : boxes) {
        cv::Rect rect(cv::Point(box.x, box.y),
                      cv::Point(box.x + box.width, box.y + box.height));
        /* 颜色：高置信度绿色，低置信度黄色 */
        cv::Scalar color(0, 255, 0);
        if (box.confidence < conf_threshold_ + 0.2f) color = cv::Scalar(0, 255, 255);
        cv::rectangle(image, rect, color, 2);

        std::string label = std::to_string(box.class_id) + ": " +
                            std::to_string(static_cast<int>(box.confidence * 100)) + "%";
        cv::putText(image, label, cv::Point(box.x, box.y - 5),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, color, 1);
    }
}
