#include "utils/line_scan.hpp"
#include <cstring>

LineScanBuffer::LineScanBuffer(int line_width, int total_lines, int overlap_lines)
    : line_width_(line_width), total_lines_(total_lines),
      overlap_lines_(overlap_lines), triggered_(false) {}

void LineScanBuffer::push_line(const uint8_t* data, int channels) {
    cv::Mat line(1, line_width_, CV_MAKETYPE(CV_8U, channels));
    std::memcpy(line.data, data, line_width_ * channels);
    buffer_.push_back(line);
}

void LineScanBuffer::push_line(const cv::Mat& line) {
    buffer_.push_back(line.clone());
}

int LineScanBuffer::cached_lines() const {
    return (int)buffer_.size();
}

bool LineScanBuffer::frame_ready() const {
    return (int)buffer_.size() >= total_lines_;
}

cv::Mat LineScanBuffer::pop_frame() {
    if (buffer_.empty()) return {};

    // 从 buffer 头部取 total_lines 行拼成图像
    int rows = std::min((int)buffer_.size(), total_lines_);
    int cols = line_width_;
    int ch = buffer_.front().channels();

    cv::Mat frame(rows, cols, CV_MAKETYPE(CV_8U, ch));
    for (int i = 0; i < rows; ++i) {
        buffer_[i].copyTo(frame.row(i));
    }

    // 移除用过的行，保留 overlap 行用于下一帧拼接
    int keep = std::min(overlap_lines_, (int)buffer_.size() - rows);
    if (keep > 0) {
        int remove = rows - keep;
        for (int i = 0; i < remove; ++i)
            buffer_.pop_front();
    } else {
        buffer_.clear();
    }

    return frame;
}

void LineScanBuffer::reset() {
    buffer_.clear();
    triggered_ = false;
}

void LineScanBuffer::trigger() {
    triggered_ = true;
}
