#pragma once

#include <deque>
#include <vector>
#include <opencv2/core/mat.hpp>

/* 行扫描缓存 — 将逐行输入的像素数据拼接成完整图像 */
class LineScanBuffer {
public:
    /* 构造
     * line_width:    每行像素数（相机的分辨率宽度）
     * total_lines:   拼成一张完整图需要多少行
     * overlap_lines: 相邻两张图之间的重叠行数（防漏检）
     */
    LineScanBuffer(int line_width, int total_lines, int overlap_lines = 0);

    /* 推入一行像素数据 (data 长度需 = line_width * channels) */
    void push_line(const uint8_t* data, int channels = 3);

    /* 推入一行 cv::Mat (1×line_width) */
    void push_line(const cv::Mat& line);

    /* 当前已缓存的行数 */
    int cached_lines() const;

    /* 是否已攒够 total_lines，可以取出一帧 */
    bool frame_ready() const;

    /* 取出当前完整的帧（返回后内部保留 overlap_lines 行，其余清除） */
    cv::Mat pop_frame();

    /* 重置缓存 */
    void reset();

    /* 设置触发信号：外部触发一拍取一帧 */
    void trigger();

private:
    int line_width_;
    int total_lines_;
    int overlap_lines_;
    std::deque<cv::Mat> buffer_;  /* 行缓存 */
    bool triggered_;
};
