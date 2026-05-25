#pragma once

#include <string>
#include <memory>
#include <opencv2/core/mat.hpp>
#include "utils/types.hpp"

/* 相机采集模块 - 支持 USB/工业相机，多后端自动探测 */
class Camera {
public:
    explicit Camera(const CameraConfig& config);
    ~Camera();

    /* ── 生命周期 ────────────────────────────────── */
    bool open();                   /* 打开相机 */
    void close();                  /* 关闭相机 */
    bool is_opened() const;        /* 相机是否已打开 */
    bool reopen();                 /* 重新打开（关闭后重试） */

    /* ── 帧采集 ──────────────────────────────────── */
    bool capture_frame(cv::Mat& frame);               /* 采集一帧（使用配置的超时时间） */
    bool capture_frame(cv::Mat& frame, int timeout_ms); /* 采集一帧（指定超时） */
    bool save_snapshot(const std::string& path);        /* 保存当前帧快照到文件 */

    /* ── 参数设置 ───────────────────────────────── */
    bool set(int prop_id, double value);     /* 设置任意 OpenCV 属性 */
    double get(int prop_id) const;           /* 获取任意 OpenCV 属性 */

    bool set_exposure(double value_us);      /* 设置曝光时间 (μs) */
    bool set_gain(double value_db);          /* 设置增益 (dB) */
    bool set_resolution(int width, int height);  /* 设置分辨率 */
    bool set_fps(double fps);                /* 设置帧率 */
    bool set_roi(int x, int y, int w, int h);  /* 设置感兴趣区域 */

    /* ── 状态查询 ───────────────────────────────── */
    CameraConfig config() const;              /* 获取当前配置 */
    std::string last_error() const;           /* 获取最后错误信息 */
    double measured_fps() const;              /* 实测帧率（滑动窗口） */
    int frame_count() const;                  /* 已采集帧数 */
    int dropped_frame_count() const;          /* 丢帧数 */
    bool frame_healthy(const cv::Mat& frame) const;  /* 帧健康检查（空/全黑/全白） */

    /* ── 静态工具方法 ───────────────────────────── */
    static std::vector<int> list_available_devices();  /* 枚举可用相机设备 */
    static bool probe_device(int device_id, CameraBackend backend = CameraBackend::AutoDetect, int timeout_ms = 2000);  /* 探测设备是否可访问 */
    static std::string backend_name(CameraBackend backend);  /* 后端名称 */
    static int  backend_to_cv(CameraBackend backend);        /* 后端枚举转 OpenCV 常量 */

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
