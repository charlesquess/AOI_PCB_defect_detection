#include "utils/tracker.hpp"
#include <algorithm>

Tracker::Tracker(float iou_threshold, int max_history)
    : iou_threshold_(iou_threshold), max_history_(max_history) {}

float Tracker::compute_iou(const BoundingBox& a, const BoundingBox& b) const {
    float inter_x = std::max(a.x, b.x);
    float inter_y = std::max(a.y, b.y);
    float inter_w = std::min(a.x + a.width, b.x + b.width) - inter_x;
    float inter_h = std::min(a.y + a.height, b.y + b.height) - inter_y;
    if (inter_w <= 0 || inter_h <= 0) return 0.0f;

    float inter = inter_w * inter_h;
    float union_ = a.width * a.height + b.width * b.height - inter;
    return inter / union_;
}

std::pair<std::vector<BoundingBox>, std::vector<BoundingBox>>
Tracker::track(const std::vector<BoundingBox>& current) {
    std::vector<BoundingBox> new_objects;     // 首次出现 → 需计数
    std::vector<BoundingBox> tracked_objects; // 已有目标 → 不计入统计

    if (history_.empty()) {
        // 首帧全部标记为新目标
        new_objects = current;
    } else {
        const auto& prev = history_.back();
        for (const auto& cur : current) {
            bool matched = false;
            for (const auto& p : prev) {
                if (compute_iou(cur, p) > iou_threshold_) {
                    matched = true;
                    break;
                }
            }
            if (matched)
                tracked_objects.push_back(cur);
            else
                new_objects.push_back(cur);
        }
    }

    // 更新历史
    history_.push_back(current);
    if (static_cast<int>(history_.size()) > max_history_) {
        history_.pop_front();
    }

    return {new_objects, tracked_objects};
}

void Tracker::reset() {
    history_.clear();
}
