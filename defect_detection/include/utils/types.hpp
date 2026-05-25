#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <opencv2/core/types.hpp>

/* 缺陷类型枚举，对应训练集 CLASSES 标准（与 preprocess_dataset.py 保持一致） */
enum class DefectType {
    MissingHole,     /* 孔洞缺失 */
    MouseBite,       /* 鼠咬 */
    OpenCircuit,     /* 开路 */
    Short,           /* 短路 */
    Spur,            /* 毛刺 */
    SpuriousCopper   /* 多余铜箔/铜渣 */
};

/* 检测框结构，表示一个检测到的目标 */
struct BoundingBox {
    float x, y, width, height;  /* 框的左上角坐标和宽高 */
    float confidence;            /* 置信度 (0~1) */
    DefectType label;            /* 缺陷类型标签 */
    int class_id;                /* 类别 ID */
};

/* 单帧检测结果，包含所有检测框和性能指标 */
struct DetectionResult {
    int frame_id;                          /* 帧序号，用于追踪去重 */
    uint64_t timestamp_us;                 /* 采集时间戳 (微秒) */
    std::vector<BoundingBox> detections;   /* 当前帧的所有检测框 */
    double inference_time_ms;              /* 推理耗时 (毫秒) */
    int image_width;                       /* 原图宽度 */
    int image_height;                      /* 原图高度 */
};

/* 相机后端类型，用于选择 OpenCV VideoCapture 后端 */
enum class CameraBackend {
    AutoDetect,  /* 自动探测最佳后端 */
    DShow,       /* DirectShow (Windows) */
    MSMF,        /* Media Foundation (Windows) */
    VFW,         /* Video for Windows */
    OpenCV       /* OpenCV 默认 */
};

/* 相机配置项 */
struct CameraConfig {
    int device_id = 0;            /* 相机设备号 (视频模式无效) */
    std::string video_path = "";  /* 视频文件路径 (非空时替代相机) */
    bool loop_video = true;       /* 视频是否循环播放 */
    double exposure = -1;         /* 曝光时间 (μs)，-1 表示自动 */
    double gain = -1;             /* 增益 (dB)，-1 表示自动 */
    int width = 640;              /* 采集宽度 */
    int height = 480;             /* 采集高度 */
    double fps = 30;              /* 目标帧率 */
    CameraBackend backend = CameraBackend::DShow;  /* 视频后端 */
    int timeout_ms = 3000;        /* 帧读取超时 (毫秒) */
    int retry_count = 3;          /* 自动重连重试次数 */
    bool auto_reconnect = true;   /* 丢帧后自动重连 */
    int roi_x = 0, roi_y = 0, roi_w = 0, roi_h = 0;  /* ROI 区域，全 0 表示全图 */
};

/* UI 界面配置 */
struct UIConfig {
    int window_width = 1280;   /* 显示窗口宽度 */
    int window_height = 720;   /* 显示窗口高度 */
    bool show_fps = true;      /* 是否显示帧率 */
};

/* 串口通信状态机 */
enum class SerialCommState {
    Disconnected = 0,    /* 串口未连接 */
    Idle,                /* 空闲，等待触发 */
    WaitingTrig,         /* 等待 TRIG 信号（触发模式） */
    Inferring,           /* 推理中 */
    Reporting,           /* 回传结果 */
    Timeout              /* 超时 */
};

/* 串口通信配置 */
struct SerialConfig {
    std::string port = "COM6";  /* 串口号 */
    int baudrate = 9600;        /* 波特率 */
    int data_bits = 8;          /* 数据位 (5/6/7/8) */
    char parity = 'N';          /* 校验位: N=无, E=偶, O=奇 */
    int stop_bits = 1;          /* 停止位 (1/2) */
};

/* 全局应用配置，聚合所有模块的配置项 */
struct AppConfig {
    CameraConfig camera;                  /* 相机配置 */
    UIConfig ui;                          /* UI 配置 */
    SerialConfig serial;                  /* 串口配置 */
    std::string model_path = "models/model.onnx";  /* 模型路径 */
    float confidence_threshold = 0.6f;    /* 置信度阈值 */
    float nms_threshold = 0.45f;          /* NMS 的 IoU 阈值 */
    bool use_gpu = true;                  /* 是否启用 GPU 推理 */
    int gpu_device_id = 0;                /* GPU 设备号 */
};

/* 模型元信息，从 manifest.json 读取 */
struct ModelMetadata {
    std::string model_version = "unknown";
    std::string training_date = "unknown";
    int num_classes = 0;
    int input_size = 0;
};

/* 检测统计摘要，用于界面显示和报表导出 */
struct InspectionSummary {
    int total_inspected = 0;              /* 总检测数 */
    int pass_count = 0;                   /* 良品数 */
    int fail_count = 0;                   /* 缺陷数 */
    double yield_rate = 100.0;            /* 良率 (%) */
    std::vector<int> defect_counts;       /* 各类缺陷数量 */
    double avg_inference_time_ms = 0.0;   /* 平均推理耗时 */
};

/* 全局共享检测结果（推理线程↔UI 线程） */
struct SharedResultBox {
    std::vector<BoundingBox> boxes;
    InspectionSummary summary;
    int pcb_count = 0;
    double fps = 0.0;
    bool model_loaded = false;           /* 模型是否就绪，UI/推理线程共享 */
    ModelMetadata model_info;            /* 模型元信息 (版本/日期/类别/输入尺寸) */
    float conf_threshold = 0.6f;         /* 当前置信度阈值 (给 UI 显示) */
    float nms_threshold = 0.45f;         /* 当前 NMS IoU 阈值 (给 UI 显示) */
    SerialCommState serial_state = SerialCommState::Disconnected;  /* 串口通信状态 */
    std::string serial_state_str = "串口未连接";                    /* 状态机描述文本 */
    std::mutex model_mutex;              /* 模型切换时的互斥锁 */
    mutable std::mutex mutex;

    static SharedResultBox& instance() {
        static SharedResultBox inst;
        return inst;
    }
};
