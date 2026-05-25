#include "preprocess/preprocessor.hpp"
#include <opencv2/imgproc.hpp>
#include <opencv2/photo.hpp>

Preprocessor::Preprocessor() = default;
Preprocessor::~Preprocessor() = default;

/* 图像缩放 */
cv::Mat Preprocessor::resize(const cv::Mat& src, int width, int height) {
    cv::Mat dst;
    cv::resize(src, dst, cv::Size(width, height));
    return dst;
}

/* 等比例缩放 + 白边填充 (letterbox)，与 YOLOv5 训练预处理一致 */
cv::Mat Preprocessor::letterbox(const cv::Mat& src, int target_size, cv::Scalar color) {
    int h = src.rows, w = src.cols;
    float scale = std::min(static_cast<float>(target_size) / w,
                           static_cast<float>(target_size) / h);
    int new_w = static_cast<int>(w * scale);
    int new_h = static_cast<int>(h * scale);

    cv::Mat resized;
    cv::resize(src, resized, cv::Size(new_w, new_h));

    cv::Mat dst(target_size, target_size, src.type(), color);
    int dx = (target_size - new_w) / 2;
    int dy = (target_size - new_h) / 2;
    resized.copyTo(dst(cv::Rect(dx, dy, new_w, new_h)));

    return dst;
}

/* 图像归一化：先除 255 缩放到 [0,1]，再按均值和标准差标准化 */
cv::Mat Preprocessor::normalize(const cv::Mat& src, const double* mean, const double* std) {
    cv::Mat dst;
    src.convertTo(dst, CV_32F);
    cv::divide(dst, 255.0, dst);
    if (mean && std) {
        cv::Mat mean_mat(src.size(), CV_32FC3, cv::Scalar(mean[0], mean[1], mean[2]));
        cv::Mat std_mat(src.size(), CV_32FC3, cv::Scalar(std[0], std[1], std[2]));
        dst = (dst - mean_mat) / std_mat;
    }
    return dst;
}

/* 直方图均衡化：增强对比度，对彩色图转灰度处理后还原 */
cv::Mat Preprocessor::equalize_hist(const cv::Mat& src) {
    cv::Mat dst;
    if (src.channels() == 1) {
        cv::equalizeHist(src, dst);
    } else {
        cv::Mat gray;
        cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
        cv::equalizeHist(gray, gray);
        cv::cvtColor(gray, dst, cv::COLOR_GRAY2BGR);
    }
    return dst;
}

/* 非局部均值去噪 */
cv::Mat Preprocessor::denoise(const cv::Mat& src, double strength) {
    cv::Mat dst;
    cv::fastNlMeansDenoisingColored(src, dst, strength);
    return dst;
}

/* 仿射变换（用于图像校正、定位对齐） */
cv::Mat Preprocessor::warp_affine(const cv::Mat& src, const cv::Mat& M, cv::Size size) {
    cv::Mat dst;
    cv::warpAffine(src, dst, M, size);
    return dst;
}

/* 颜色空间转换，code 为 OpenCV 转换码如 cv::COLOR_BGR2GRAY */
cv::Mat Preprocessor::convert_color(const cv::Mat& src, int code) {
    cv::Mat dst;
    cv::cvtColor(src, dst, code);
    return dst;
}

/* 检测 PCB 主体轮廓，计算旋转角度并摆正为水平 */
cv::Mat Preprocessor::deskew(const cv::Mat& src, cv::Mat& transform_matrix) {
    cv::Mat gray, mask;
    cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
    cv::threshold(gray, mask, 230, 255, cv::THRESH_BINARY_INV);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    if (contours.empty()) {
        transform_matrix = cv::Mat::eye(2, 3, CV_32F);
        return src.clone();
    }

    // 找最大轮廓（PCB 主体）
    auto it = std::max_element(contours.begin(), contours.end(),
        [](auto& a, auto& b) { return cv::contourArea(a) < cv::contourArea(b); });

    // 最小外接矩形
    cv::RotatedRect rr = cv::minAreaRect(*it);
    float angle = rr.angle;
    cv::Size rect_size = rr.size;

    // OpenCV 的 angle: 水平矩形为 -90°, 向左旋转减小, 向右旋转增大
    if (angle < -45.0f) angle = 90.0f + angle;  // 统一到 [-45, 45]
    if (fabs(angle) < 1.0f) {  // 几乎水平，无需纠偏
        transform_matrix = cv::Mat::eye(2, 3, CV_32F);
        return src.clone();
    }

    // 计算旋转后的图像尺寸
    cv::Mat rot = cv::getRotationMatrix2D(rr.center, angle, 1.0);
    float rad = (float)(angle * CV_PI / 180.0);
    float cos_a = (float)fabs(cos(rad));
    float sin_a = (float)fabs(sin(rad));
    int new_w = (int)(rect_size.height * sin_a + rect_size.width * cos_a);
    int new_h = (int)(rect_size.height * cos_a + rect_size.width * sin_a);
    rot.at<double>(0, 2) += (new_w - src.cols) / 2.0;
    rot.at<double>(1, 2) += (new_h - src.rows) / 2.0;

    cv::Mat dst;
    cv::warpAffine(src, dst, rot, cv::Size(new_w, new_h), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar::all(255));

    transform_matrix = rot.clone();
    return dst;
}

/* 预处理流水线：输入多个预处理步骤的结果，目前为桩函数 */
std::vector<cv::Mat> Preprocessor::pipeline(const std::vector<cv::Mat>& stages) {
    return stages;
}
