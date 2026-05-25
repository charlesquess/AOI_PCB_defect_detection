#pragma once

#include <string>
#include <vector>
#include <opencv2/core/mat.hpp>
#include "utils/types.hpp"

/* 界面管理模块 - 基于 OpenCV highgui 的显示和交互 */
class UIManager {
public:
    UIManager();
    ~UIManager();

    bool init_window(const std::string& title, int width = 1280, int height = 720);  /* 初始化显示窗口 */
    void close_window();     /* 关闭窗口 */

    void show_frame(const cv::Mat& frame);                                /* 显示原始图像 */
    void show_results(const cv::Mat& frame, const std::vector<BoundingBox>& boxes);  /* 显示带检测框的图像 */

    void draw_overlay(cv::Mat& image, const InspectionSummary& summary);   /* 绘制统计信息叠加层 */
    void draw_model_info(cv::Mat& image);                                  /* 绘制模型信息 (版本/日期/阈值) */
    void draw_info_panel(cv::Mat& image, const std::string& info);         /* 绘制底部信息栏 */

    int wait_key(int delay_ms = 1);  /* 等待键盘输入，返回按键 ASCII 码 */

    bool window_closed() const;  /* 窗口是否已关闭 */

private:
    std::string window_title_;  /* 窗口标题 */
    bool initialized_;          /* 窗口是否已初始化 */
};
