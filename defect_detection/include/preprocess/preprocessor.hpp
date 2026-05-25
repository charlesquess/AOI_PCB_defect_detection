#pragma once

#include <vector>
#include <opencv2/core/mat.hpp>

/* 图像预处理模块 - 提供各种预处理操作 */
class Preprocessor {
public:
    Preprocessor();
    ~Preprocessor();

    cv::Mat resize(const cv::Mat& src, int width, int height);     /* 缩放图像 */
    cv::Mat letterbox(const cv::Mat& src, int target_size, cv::Scalar color = cv::Scalar(114, 114, 114));  /* 等比例缩放+白边填充 */
    cv::Mat deskew(const cv::Mat& src, cv::Mat& transform_matrix);  /* 检测PCB角度并摆正 */
    cv::Mat normalize(const cv::Mat& src, const double* mean, const double* std);  /* 归一化 */
    cv::Mat equalize_hist(const cv::Mat& src);                     /* 直方图均衡化 */
    cv::Mat denoise(const cv::Mat& src, double strength = 1.0);    /* 去噪 */
    cv::Mat warp_affine(const cv::Mat& src, const cv::Mat& M, cv::Size size);  /* 仿射变换 */
    cv::Mat convert_color(const cv::Mat& src, int code);           /* 颜色空间转换 */

    std::vector<cv::Mat> pipeline(const std::vector<cv::Mat>& stages);  /* 预处理流水线 */
};
