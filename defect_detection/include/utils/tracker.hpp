#pragma once

#include <vector>
#include <deque>
#include "types.hpp"

/* 简易追踪器 - 通过 IOU 判断目标是新出现的还是已存在的，防止重复计数 */
class Tracker {
public:
    Tracker(float iou_threshold = 0.3f, int max_history = 5);

    /* 判断当前帧的检测框是否为新目标，返回 (新目标框, 已追踪到的框) */
    std::pair<std::vector<BoundingBox>, std::vector<BoundingBox>>
    track(const std::vector<BoundingBox>& current_detections);

    /* 重置追踪状态 (换产线/新批次时调用) */
    void reset();

private:
    float iou_threshold_;
    int max_history_;
    std::deque<std::vector<BoundingBox>> history_;

    float compute_iou(const BoundingBox& a, const BoundingBox& b) const;
};
