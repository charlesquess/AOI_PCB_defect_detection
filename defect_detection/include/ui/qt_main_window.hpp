#pragma once

#include <QMainWindow>
#include <QTabWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QLineEdit>
#include <QTextEdit>
#include <QTimer>
#include <QListWidget>
#include <QSlider>
#include <QSpinBox>
#include <QCheckBox>

#include <opencv2/core/mat.hpp>
#include "utils/types.hpp"
#include "utils/thread_queue.hpp"
#include "camera/camera.hpp"
#include "inference/inferencer.hpp"
#include "postprocess/postprocessor.hpp"
#include "preprocess/preprocessor.hpp"

struct DisplayFrame {
    QImage image;
    std::vector<BoundingBox> boxes;
    InspectionSummary summary;
    int pcb_count = 0;
    double fps = 0.0;
};

struct DebugSettings {
    std::string model_path = "models/model.onnx";
    int camera_id = 0;
    int camera_width = 640;
    int camera_height = 480;
    std::string video_path = "";
    bool loop_video = true;
    std::string serial_port = "COM6";
    int serial_baud = 9600;
    float conf_threshold = 0.6f;
    float nms_threshold = 0.45f;
    class Config* config = nullptr;   /* for yaml save */
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(ThreadSafeQueue<FrameTask>* raw_q,
                        ThreadSafeQueue<DisplayFrame>* display_q,
                        Postprocessor* postproc,
                        Camera* camera,
                        Inferencer* infer,
                        Preprocessor* prep,
                        DebugSettings* settings,
                        bool model_loaded = false,
                        QWidget* parent = nullptr);
    ~MainWindow();
    void setDisplayQueue(ThreadSafeQueue<QImage>* q) { display_qimage_queue_ = q; }

private slots:
    void updateFrame();
    void refreshLog();
    void onStartStop();
    void onScreenshot();
    void onConfThreshold(int);
    void onNmsThreshold(int);

    /* 调试：模型 */
    void onBrowseModel();
    void onApplyModel();

    /* 调试：相机 */
    void onTestCamera();

    /* 调试：串口 */
    void onTestSerial();

    /* 调试：模拟测试 */
    void onSimulateImage();

    /* 调试：保存配置 */
    void onSaveConfig();

private:
    void setupUI();
    void setupMainTab(QWidget* tab);
    void setupDebugTab(QWidget* tab);
    void drawDefectChart(QImage& chart, const std::vector<int>& counts);
    void appendLog(const QString& text);

    QTabWidget* tabs_;

    /* 主界面 */
    QLabel* camera_view_;
    QPushButton* btn_start_;
    QPushButton* btn_screenshot_;
    QComboBox* model_selector_;          /* 主界面模型切换 */
    QLabel* lbl_model_ver_;
    QLabel* lbl_model_date_;
    QLabel* lbl_model_thresholds_;
    QLabel* lbl_serial_state_;
    QLabel* stat_total_, *stat_pass_, *stat_fail_, *stat_yield_;
    QLabel* stat_fps_, *stat_pcb_, *stat_avg_time_;
    QLabel* chart_widget_;
    QListWidget* log_list_;

    /* 调试面板 */
    QLineEdit* edit_model_path_;
    QPushButton* btn_browse_model_;
    QPushButton* btn_apply_model_;
    QLabel* model_status_;

    QPushButton* btn_test_camera_;
    QLabel* camera_test_result_;
    QSpinBox* spin_cam_w_;
    QSpinBox* spin_cam_h_;

    QComboBox* combo_serial_port_;
    QSpinBox* spin_serial_baud_;
    QPushButton* btn_test_serial_;
    QLabel* serial_test_result_;

    QPushButton* btn_simulate_;
    QLabel* sim_image_view_;
    QLabel* sim_result_text_;

    QLineEdit* edit_video_path_;
    QPushButton* btn_browse_video_;
    QCheckBox* check_loop_video_;
    QLabel* help_serial_sim_;
    QSlider* slider_conf_;
    QSlider* slider_nms_;
    QLabel* label_conf_;
    QLabel* label_nms_;

    /* 数据源 */
    ThreadSafeQueue<FrameTask>* raw_queue_;
    ThreadSafeQueue<DisplayFrame>* display_queue_;
    ThreadSafeQueue<QImage>* display_qimage_queue_ = nullptr;
    Postprocessor* postprocessor_;
    Camera* camera_;
    Inferencer* inferencer_;
    Preprocessor* preprocessor_;
    DebugSettings* settings_;
    bool model_loaded_;

    QTimer* frame_timer_;
    QTimer* log_timer_;
    bool running_;
};
