#include "ui/qt_main_window.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QApplication>
#include <QFont>
#include <QPainter>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include <QScrollArea>
#include <QFrame>
#include <QStatusBar>
#include <QCheckBox>
#include <QMessageBox>

#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>

#include <thread>
#include <future>
#include "utils/logger.hpp"
#include "utils/config.hpp"
#ifdef _WIN32
#include <windows.h>
#endif

/* ===== 工具函数 ===== */
static const char* class_name(int id) {
    static const char* n[]={"missing_hole","mouse_bite","open_circuit","short","spur","spurious_copper"};
    return (id>=0&&id<6)?n[id]:"?";
}

static QImage mat_to_qimage(const cv::Mat& mat) {
    if (mat.empty()) return {};
    cv::Mat rgb; cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
    return QImage(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888).copy();
}

static void draw_boxes(QImage& img, const std::vector<BoundingBox>& boxes) {
    QPainter p(&img); p.setRenderHint(QPainter::Antialiasing);
    p.setFont(QFont("Microsoft YaHei", 11));
    QColor cols[6]={{255,80,80},{80,200,80},{80,80,255},{255,200,80},{200,80,255},{80,200,200}};
    for (auto& b : boxes) {
        QRect r((int)b.x,(int)b.y,(int)b.width,(int)b.height);
        QColor c=cols[b.class_id%6];
        p.setPen(QPen(c,2)); p.drawRect(r);
        QString lbl=QString("[%1] %2%").arg(class_name(b.class_id)).arg((int)(b.confidence*100));
        QFontMetrics fm(p.font());
        QRect bg(r.x(),r.y()-fm.height()-4,fm.horizontalAdvance(lbl)+12,fm.height()+4);
        p.fillRect(bg,c); p.setPen(Qt::white); p.drawText(bg,Qt::AlignCenter,lbl);
    }
    p.end();
}

bool g_first_frame = false;

/* ============================================================ */
/*  MainWindow                                                   */
/* ============================================================ */
MainWindow::MainWindow(ThreadSafeQueue<FrameTask>* rq,
                       ThreadSafeQueue<DisplayFrame>* dq, Postprocessor* pp,
                       Camera* cam, Inferencer* inf, Preprocessor* prep,
                       DebugSettings* s, bool ml, QWidget* parent)
    : QMainWindow(parent), raw_queue_(rq), display_queue_(dq),
      postprocessor_(pp), camera_(cam), inferencer_(inf),
      preprocessor_(prep), settings_(s), model_loaded_(ml), running_(true) {

    setWindowTitle("PCB 缺陷检测系统 v1.0");
    resize(1480, 900);
    setMinimumSize(1100, 700);

    setupUI();

    frame_timer_ = new QTimer(this);
    connect(frame_timer_, &QTimer::timeout, this, &MainWindow::updateFrame);
    frame_timer_->start(16);

    log_timer_ = new QTimer(this);
    connect(log_timer_, &QTimer::timeout, this, &MainWindow::refreshLog);
    log_timer_->start(500);
}

MainWindow::~MainWindow() = default;

/* ============================================================ */
/*  setupUI                                                      */
/* ============================================================ */
void MainWindow::setupUI() {
    setStyleSheet(
        "QMainWindow{background:#0f1923}"
        "QTabWidget::pane{background:#0f1923;border:1px solid #2a3a4a;border-top:none}"
        "QTabBar::tab{background:#1e2d3d;color:#889;padding:8px 20px;"
        "  border:1px solid #2a3a4a;border-bottom:none;border-radius:4px 4px 0 0;font:bold 13px 'Microsoft YaHei'}"
        "QTabBar::tab:selected{background:#141e2a;color:#eee}"
        "QLabel{color:#ccd;font:12px 'Microsoft YaHei'}"
        "QGroupBox{font:bold 13px 'Microsoft YaHei';border:1px solid #2a3a4a;"
        " border-radius:6px;margin-top:16px;padding-top:12px;color:#aac;background:#141e2a}"
        "QGroupBox::title{subcontrol-origin:margin;left:10px;padding:0 6px}"
        "QPushButton{font:12px 'Microsoft YaHei';background:#1e2d3d;color:#ccd;"
        " border:1px solid #3a4a5a;border-radius:4px;padding:6px 14px;min-height:20px}"
        "QPushButton:hover{background:#2a3d50}"
        "QPushButton:disabled{color:#556}"
        "QComboBox,QSpinBox,QLineEdit{background:#0d1620;color:#ccd;"
        " border:1px solid #2a3a4a;border-radius:3px;padding:4px 8px;font:12px 'Microsoft YaHei'}"
        "QSlider::groove:horizontal{height:6px;background:#2a3a4a;border-radius:3px}"
        "QSlider::handle:horizontal{background:#4a8;width:14px;margin:-4px 0;border-radius:7px}"
        "QSlider::sub-page:horizontal{background:#4a8;border-radius:3px}"
        "QListWidget{background:#0d1620;border:1px solid #2a3a4a;border-radius:4px;"
        " color:#889;font:11px Consolas}"
    );

    tabs_ = new QTabWidget(this);
    QWidget* mt = new QWidget(this);
    setupMainTab(mt);
    tabs_->addTab(mt, "主界面");
    QWidget* dt = new QWidget(this);
    setupDebugTab(dt);
    tabs_->addTab(dt, "调试");
    setCentralWidget(tabs_);
    statusBar()->showMessage("就绪");
}

/* ============================================================ */
/*  主界面                                                       */
/* ============================================================ */
void MainWindow::setupMainTab(QWidget* tab) {
    QHBoxLayout* hbox = new QHBoxLayout(tab);
    hbox->setContentsMargins(6,6,6,6); hbox->setSpacing(6);

    /* ── 左：画面 ── */
    QVBoxLayout* left = new QVBoxLayout(); left->setSpacing(4);
    camera_view_ = new QLabel(this);
    camera_view_->setMinimumSize(680, 520);
    camera_view_->setAlignment(Qt::AlignCenter);
    camera_view_->setStyleSheet("QLabel{background:#0a121c;border:none}");
    left->addWidget(camera_view_, 1);

    QHBoxLayout* tb = new QHBoxLayout(); tb->setSpacing(6);
    btn_start_ = new QPushButton("暂停", this);
    btn_start_->setStyleSheet("QPushButton{background:#1e3d2a;color:#4a8;font-weight:bold;border:1px solid #2a5a3a;padding:8px 18px}");
    connect(btn_start_, &QPushButton::clicked, this, &MainWindow::onStartStop);
    tb->addWidget(btn_start_);
    btn_screenshot_ = new QPushButton("截图", this);
    connect(btn_screenshot_, &QPushButton::clicked, this, &MainWindow::onScreenshot);
    tb->addWidget(btn_screenshot_);
    tb->addStretch();
    left->addLayout(tb);

    /* 日志 (在画面下方) */
    log_list_ = new QListWidget(this);
    log_list_->setMaximumHeight(120);
    log_list_->setWordWrap(true);
    log_list_->setStyleSheet("QListWidget{background:#0d1620;border:1px solid #2a3a4a;border-radius:3px;color:#667;font:10px Consolas}");
    left->addWidget(log_list_);

    hbox->addLayout(left, 2);

    /* ── 右：统计 + 模型切换 ── */
    QVBoxLayout* right = new QVBoxLayout(); right->setSpacing(4);

    /* 模型切换 */
    QGroupBox* gm = new QGroupBox("检测模型", this);
    QVBoxLayout* lm = new QVBoxLayout(gm); lm->setSpacing(4);
    model_selector_ = new QComboBox(this);
    model_selector_->setEditable(true);
    model_selector_->addItem("models/model.onnx");
    // 自动搜索 models/ 下的 onnx
    QDir dir("models");
    for (auto& f : dir.entryList({"*.onnx"}, QDir::Files, QDir::Name))
        if (!model_selector_->findText(f)) model_selector_->addItem("models/"+f);
    lm->addWidget(model_selector_);
    QPushButton* btn_apply = new QPushButton("应用模型", this);
    btn_apply->setStyleSheet("QPushButton{background:#2a4a3a;color:#4a8;font-weight:bold}");
    connect(btn_apply, &QPushButton::clicked, this, &MainWindow::onApplyModel);
    lm->addWidget(btn_apply);

    auto mk_info = [&](const QString& label) -> QLabel* {
        QLabel* l = new QLabel(label, this);
        l->setStyleSheet("QLabel{color:#889;font:11px 'Microsoft YaHei';padding:1px 4px}");
        return l;
    };
    lbl_model_ver_ = mk_info("版本: --");
    lbl_model_date_ = mk_info("训练: --");
    lbl_model_thresholds_ = mk_info("阈值: --");
    lm->addWidget(lbl_model_ver_);
    lm->addWidget(lbl_model_date_);
    lm->addWidget(lbl_model_thresholds_);
    right->addWidget(gm);

    /* 统计 */
    auto grp = [&](const QString& t) -> QGroupBox* {
        QGroupBox* g = new QGroupBox(t, this); g->setFixedWidth(230);
        g->setLayout(new QVBoxLayout()); g->layout()->setSpacing(2); return g;
    };
    auto row = [&](QGroupBox* g, const QString& n) -> QLabel* {
        QHBoxLayout* r = new QHBoxLayout();
        r->addWidget(new QLabel(n, this)); r->addStretch();
        QLabel* v = new QLabel("--", this); v->setStyleSheet("font-weight:bold;color:#eee");
        r->addWidget(v); ((QVBoxLayout*)g->layout())->addLayout(r); return v;
    };
    QGroupBox* gs = grp("检测统计");
    stat_total_=row(gs,"总检测"); stat_pass_=row(gs,"良品"); stat_fail_=row(gs,"缺陷");
    stat_yield_=row(gs,"良率"); stat_pcb_=row(gs,"PCB计数"); stat_fps_=row(gs,"帧率");
    stat_avg_time_=row(gs,"平均耗时");
    right->addWidget(gs);

    QGroupBox* gc = grp("缺陷分布");
    chart_widget_ = new QLabel(this); chart_widget_->setFixedHeight(160);
    chart_widget_->setStyleSheet("background:transparent");
    gc->layout()->addWidget(chart_widget_);
    right->addWidget(gc);

    /* 串口状态 */
    QGroupBox* gs_state = grp("通信状态");
    lbl_serial_state_ = new QLabel("--", this);
    lbl_serial_state_->setStyleSheet("QLabel{font:12px Consolas;color:#4af;padding:4px}");
    ((QVBoxLayout*)gs_state->layout())->addWidget(lbl_serial_state_);
    right->addWidget(gs_state);

    right->addStretch();
    hbox->addLayout(right, 1);
}

/* ============================================================ */
/*  调试面板                                                     */
/* ============================================================ */
void MainWindow::setupDebugTab(QWidget* tab) {
    QHBoxLayout* hbox = new QHBoxLayout(tab);
    hbox->setContentsMargins(10,10,10,10); hbox->setSpacing(10);

    /* ── 左栏 ── */
    QVBoxLayout* left = new QVBoxLayout(); left->setSpacing(6);

    auto mk = [&](const QString& t) -> QGroupBox* {
        QGroupBox* g = new QGroupBox(t, tab);
        g->setLayout(new QFormLayout());
        ((QFormLayout*)g->layout())->setSpacing(6);
        ((QFormLayout*)g->layout())->setLabelAlignment(Qt::AlignRight);
        return g;
    };

    /* 模型 */
    QGroupBox* g_model = mk("模型配置");
    edit_model_path_ = new QLineEdit(QString::fromStdString(settings_->model_path), tab);
    btn_browse_model_ = new QPushButton("浏览...", tab);
    connect(btn_browse_model_, &QPushButton::clicked, this, &MainWindow::onBrowseModel);
    QHBoxLayout* ml = new QHBoxLayout();
    ml->addWidget(edit_model_path_,1); ml->addWidget(btn_browse_model_);
    ((QFormLayout*)g_model->layout())->addRow("ONNX:", ml);

    btn_apply_model_ = new QPushButton("应用 & 重载模型", tab);
    btn_apply_model_->setStyleSheet("QPushButton{background:#2a4a3a;color:#4a8;font-weight:bold}");
    connect(btn_apply_model_, &QPushButton::clicked, this, &MainWindow::onApplyModel);
    ((QFormLayout*)g_model->layout())->addRow("", btn_apply_model_);

    model_status_ = new QLabel(model_loaded_?"✓ 已加载":"✗ 未加载", tab);
    model_status_->setStyleSheet(QString("font-weight:bold;color:%1").arg(model_loaded_?"#4a8":"#a44"));
    ((QFormLayout*)g_model->layout())->addRow("状态:", model_status_);

    label_conf_ = new QLabel("置信度: 0.60", this);
    slider_conf_ = new QSlider(Qt::Horizontal, this);
    slider_conf_->setRange(10,99); slider_conf_->setValue(60);
    connect(slider_conf_, &QSlider::valueChanged, this, &MainWindow::onConfThreshold);
    ((QFormLayout*)g_model->layout())->addRow(label_conf_, slider_conf_);

    label_nms_ = new QLabel("NMS: 0.45", this);
    slider_nms_ = new QSlider(Qt::Horizontal, this);
    slider_nms_->setRange(10,90); slider_nms_->setValue(45);
    connect(slider_nms_, &QSlider::valueChanged, this, &MainWindow::onNmsThreshold);
    ((QFormLayout*)g_model->layout())->addRow(label_nms_, slider_nms_);
    left->addWidget(g_model);

    /* 相机测试 */
    QGroupBox* g_cam = mk("相机测试");
    btn_test_camera_ = new QPushButton("检测相机", tab);
    btn_test_camera_->setStyleSheet("QPushButton{background:#1e3d5a;color:#4af;font-weight:bold}");
    connect(btn_test_camera_, &QPushButton::clicked, this, &MainWindow::onTestCamera);
    ((QFormLayout*)g_cam->layout())->addRow("", btn_test_camera_);
    camera_test_result_ = new QLabel("点击按钮检测", tab);
    camera_test_result_->setWordWrap(true);
    camera_test_result_->setStyleSheet("QLabel{background:#0d1620;border:1px solid #2a3a4a;border-radius:3px;padding:6px;color:#889;font:11px Consolas}");
    ((QFormLayout*)g_cam->layout())->addRow("结果:", camera_test_result_);
    left->addWidget(g_cam);

    /* 视频路径 */
    QGroupBox* g_video = mk("视频回放");
    edit_video_path_ = new QLineEdit(QString::fromStdString(settings_->video_path), tab);
    edit_video_path_->setPlaceholderText("留空则使用 USB 相机");
    btn_browse_video_ = new QPushButton("浏览...", tab);
    connect(btn_browse_video_, &QPushButton::clicked, this, [this]() {
        QString p = QFileDialog::getOpenFileName(this, "选择视频文件", "", "视频 (*.mp4 *.avi *.mov *.mkv);;所有文件 (*)");
        if (!p.isEmpty()) { edit_video_path_->setText(p); }
    });
    QHBoxLayout* vl = new QHBoxLayout();
    vl->addWidget(edit_video_path_, 1); vl->addWidget(btn_browse_video_);
    ((QFormLayout*)g_video->layout())->addRow("视频:", vl);
    check_loop_video_ = new QCheckBox("循环播放", tab);
    check_loop_video_->setChecked(settings_->loop_video);
    check_loop_video_->setStyleSheet("QCheckBox{color:#889;font:12px 'Microsoft YaHei'}");
    ((QFormLayout*)g_video->layout())->addRow("", check_loop_video_);
    left->addWidget(g_video);

    /* 保存到配置 */
    QPushButton* btn_save = new QPushButton("保存到 config.yaml", tab);
    btn_save->setStyleSheet("QPushButton{background:#2a3a5a;color:#8af;font:bold 12px 'Microsoft YaHei';border:1px solid #3a5a7a;border-radius:4px;padding:8px}");
    connect(btn_save, &QPushButton::clicked, this, &MainWindow::onSaveConfig);
    left->addWidget(btn_save);

    /* 串口测试 */
    QGroupBox* g_ser = mk("串口测试");
    combo_serial_port_ = new QComboBox(tab);
    combo_serial_port_->addItems({"COM1","COM3","COM4","COM5","COM6"});
    spin_serial_baud_ = new QSpinBox(tab);
    spin_serial_baud_->setRange(1200,921600); spin_serial_baud_->setValue(9600); spin_serial_baud_->setSingleStep(9600);
    ((QFormLayout*)g_ser->layout())->addRow("端口:", combo_serial_port_);
    ((QFormLayout*)g_ser->layout())->addRow("波特率:", spin_serial_baud_);

    btn_test_serial_ = new QPushButton("测试串口", tab);
    btn_test_serial_->setStyleSheet("QPushButton{background:#1e3d5a;color:#4af;font-weight:bold}");
    connect(btn_test_serial_, &QPushButton::clicked, this, &MainWindow::onTestSerial);
    ((QFormLayout*)g_ser->layout())->addRow("", btn_test_serial_);

    serial_test_result_ = new QLabel("点击按钮测试", tab);
    serial_test_result_->setWordWrap(true);
    serial_test_result_->setStyleSheet("QLabel{background:#0d1620;border:1px solid #2a3a4a;border-radius:3px;padding:6px;color:#889;font:11px Consolas}");
    ((QFormLayout*)g_ser->layout())->addRow("结果:", serial_test_result_);

    help_serial_sim_ = new QLabel(tab);
    help_serial_sim_->setText(
        "无硬件 PLC 时，可用 com0com 虚拟串口对调试：\n"
        "  1. 安装 https://sourceforge.net/projects/com0com/\n"
        "  2. 生成 COM5↔COM6 虚拟串口对\n"
        "  3. config.yaml 中 serial.port 设为 COM6\n"
        "  4. 运行: python scripts/plc_simulator.py COM5 --interval 2.0\n"
        "  完整协议见 docs/SERIAL_PROTOCOL.md");
    help_serial_sim_->setWordWrap(true);
    help_serial_sim_->setStyleSheet("QLabel{background:#0d1620;border:1px solid #3a4a2a;border-radius:3px;padding:8px;color:#7a8;font:11px Consolas;line-height:1.4}");
    ((QFormLayout*)g_ser->layout())->addRow("说明:", help_serial_sim_);

    left->addWidget(g_ser);

    left->addStretch();
    hbox->addLayout(left, 1);

    /* ── 右栏：模拟测试 ── */
    QGroupBox* g_sim = new QGroupBox("模拟测试（模型验证）", tab);
    g_sim->setStyleSheet("QGroupBox{font:bold 14px 'Microsoft YaHei';border:2px solid #3a5a4a;"
                         " border-radius:6px;margin-top:16px;padding-top:12px;color:#4a8;background:#141e2a}");
    QVBoxLayout* sim_l = new QVBoxLayout(g_sim); sim_l->setSpacing(6);

    btn_simulate_ = new QPushButton(" 加载图片 → 推理检测", tab);
    btn_simulate_->setStyleSheet("QPushButton{background:#1e3d5a;color:#4a8;font-weight:bold;"
                                 " border:1px solid #2a5a7a;padding:10px 20px;font-size:13px}");
    connect(btn_simulate_, &QPushButton::clicked, this, &MainWindow::onSimulateImage);
    sim_l->addWidget(btn_simulate_);

    sim_image_view_ = new QLabel(tab);
    sim_image_view_->setMinimumSize(420, 320);
    sim_image_view_->setAlignment(Qt::AlignCenter);
    sim_image_view_->setStyleSheet("QLabel{background:#0a121c;border:1px solid #2a3a4a;border-radius:4px;color:#445}");
    sim_image_view_->setText("← 加载图片显示结果");
    sim_l->addWidget(sim_image_view_, 1);

    sim_result_text_ = new QLabel(tab);
    sim_result_text_->setMinimumHeight(90);
    sim_result_text_->setWordWrap(true);
    sim_result_text_->setStyleSheet("QLabel{background:#0d1620;border:1px solid #2a3a4a;border-radius:4px;padding:8px;color:#889;font:12px Consolas}");
    sim_result_text_->setText("检测结果：\n  (等待测试)");
    sim_l->addWidget(sim_result_text_);

    hbox->addWidget(g_sim, 1);
}

/* ============================================================ */
/*  updateFrame                                                  */
/* ============================================================ */
void MainWindow::updateFrame() {
    if (!running_) return;
    QImage img;
    if (!display_qimage_queue_ || !display_qimage_queue_->pop_latest(img, 1)) return;

    if (!g_first_frame) {
        g_first_frame = true;
        model_status_->setText(model_loaded_?"✓ 已加载":"✗ 未加载");
    }

    auto& sr = SharedResultBox::instance();
    std::vector<BoundingBox> boxes;
    InspectionSummary summary;
    int pcb_cnt = 0; double fps_val = 0.0;
    { std::lock_guard<std::mutex> lock(sr.mutex);
      boxes = sr.boxes; summary = sr.summary;
      pcb_cnt = sr.pcb_count; fps_val = sr.fps;
      // 更新串口状态
      lbl_serial_state_->setText(QString::fromStdString(sr.serial_state_str));
      {
          QString color;
          switch (sr.serial_state) {
              case SerialCommState::Disconnected: color = "#a44"; break;
              case SerialCommState::Idle:         color = "#4a8"; break;
              case SerialCommState::WaitingTrig:  color = "#4af"; break;
              case SerialCommState::Inferring:    color = "#fa0"; break;
              case SerialCommState::Reporting:    color = "#f80"; break;
              case SerialCommState::Timeout:      color = "#f44"; break;
          }
          lbl_serial_state_->setStyleSheet(QString("QLabel{font:12px Consolas;color:%1;padding:4px}").arg(color));
      }
      // 更新模型信息
      if (sr.model_loaded) {
          auto& mi = sr.model_info;
          lbl_model_ver_->setText(QString("版本: v%1").arg(QString::fromStdString(mi.model_version)));
          lbl_model_date_->setText(QString("训练: %1").arg(QString::fromStdString(mi.training_date)));
      }
      lbl_model_thresholds_->setText(
          QString("阈值: conf=%1 NMS=%2")
              .arg(sr.conf_threshold, 0, 'f', 2)
              .arg(sr.nms_threshold, 0, 'f', 2));
    }

    if (!boxes.empty()) draw_boxes(img, boxes);

    camera_view_->setPixmap(QPixmap::fromImage(img).scaled(
        camera_view_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));

    stat_total_->setText(QString::number(summary.total_inspected));
    stat_pass_->setText(QString::number(summary.pass_count));
    stat_fail_->setText(QString::number(summary.fail_count));
    stat_yield_->setText(QString("%1%").arg(summary.yield_rate,0,'f',1));
    stat_pcb_->setText(QString::number(pcb_cnt));
    stat_fps_->setText(QString::number(fps_val,'f',1));
    stat_avg_time_->setText(QString("%1 ms").arg(summary.avg_inference_time_ms,0,'f',1));

    QImage chart(210,150,QImage::Format_ARGB32); chart.fill(Qt::transparent);
    QPainter p(&chart); p.setRenderHint(QPainter::Antialiasing);
    QColor cols[6]={{255,80,80},{80,200,80},{80,80,255},{255,200,80},{200,80,255},{80,200,200}};
    int maxv=1; for(int c:summary.defect_counts) if(c>maxv)maxv=c;
    float sc=maxv>0?110.0f/maxv:1;
    for(int i=0;i<6&&i<(int)summary.defect_counts.size();++i){
        int h=(int)(summary.defect_counts[i]*sc);
        p.fillRect(8+i*32,140-h,26,h,cols[i]);
        p.setPen(cols[i]); p.setFont(QFont("Consolas",8));
        p.drawText(8+i*32,155,26,16,Qt::AlignCenter,QString::number(summary.defect_counts[i]));
    }
    p.end();
    chart_widget_->setPixmap(QPixmap::fromImage(chart));
}

/* ============================================================ */
/*  模型切换                                                     */
/* ============================================================ */
void MainWindow::onBrowseModel() {
    QString p = QFileDialog::getOpenFileName(this,"选择 ONNX 模型","","ONNX (*.onnx);;所有文件 (*)");
    if(!p.isEmpty()){edit_model_path_->setText(p); settings_->model_path=p.toStdString();}
}

void MainWindow::onApplyModel() {
    QString path = model_selector_->currentText();
    if (path.isEmpty()) return;
    if (!inferencer_) { appendLog("推理器不可用"); return; }

    // 锁定模型互斥锁，等待推理线程完成当前推理
    auto& sr = SharedResultBox::instance();
    std::unique_lock<std::mutex> model_lock(sr.model_mutex);

    // 标记模型不可用，清空旧检测框
    model_loaded_ = false;
    {
        std::lock_guard<std::mutex> lk(sr.mutex);
        sr.model_loaded = false;
        sr.boxes.clear();
    }

    // 安全卸载旧模型
    inferencer_->unload_model();

    // 加载新模型
    bool ok = inferencer_->load_model(path.toStdString(), false, 0);

    // 释放模型锁，推理线程可继续
    model_lock.unlock();

    model_loaded_ = ok;
    sr.model_loaded = ok;
    model_status_->setText(ok ? "✓ 已加载" : "✗ 失败");
    model_status_->setStyleSheet(ok ? "font-weight:bold;color:#4a8" : "font-weight:bold;color:#a44");
    appendLog(QString("模型 %1: %2").arg(path).arg(ok ? "加载成功" : "加载失败"));
}

/* ============================================================ */
/*  相机测试                                                     */
/* ============================================================ */
void MainWindow::onTestCamera() {
    btn_test_camera_->setEnabled(false);
    btn_test_camera_->setText("检测中...");
    QApplication::processEvents();

    int dev = settings_->camera_id;

    if (!camera_ || !camera_->is_opened()) {
        camera_test_result_->setText(QString("✗ Camera %1: 相机未打开（无相机模式）").arg(dev));
        camera_test_result_->setStyleSheet("QLabel{background:#0d1620;border:1px solid #a44;border-radius:3px;padding:6px;color:#a44;font:11px Consolas}");
    } else {
        CameraConfig cfg = camera_->config();
        int w = static_cast<int>(camera_->get(cv::CAP_PROP_FRAME_WIDTH));
        int h = static_cast<int>(camera_->get(cv::CAP_PROP_FRAME_HEIGHT));
        // 尝试采集一帧验证相机是否正常工作
        cv::Mat frame;
        bool ok = camera_->capture_frame(frame, 1000);
        if (ok && !frame.empty()) {
            camera_test_result_->setText(QString("✓ Camera %1: 正常工作中\n  配置: %2x%3\n  当前: %4x%5")
                .arg(dev).arg(cfg.width).arg(cfg.height).arg(frame.cols).arg(frame.rows));
            camera_test_result_->setStyleSheet("QLabel{background:#0d1620;border:1px solid #4a8;border-radius:3px;padding:6px;color:#4a8;font:11px Consolas}");
        } else {
            camera_test_result_->setText(QString("⚠ Camera %1: 已打开但无法读取帧").arg(dev));
            camera_test_result_->setStyleSheet("QLabel{background:#0d1620;border:1px solid #fa0;border-radius:3px;padding:6px;color:#fa0;font:11px Consolas}");
        }
    }

    btn_test_camera_->setEnabled(true);
    btn_test_camera_->setText("检测相机");
}

/* ============================================================ */
/*  串口测试                                                     */
/* ============================================================ */
void MainWindow::onTestSerial() {
    btn_test_serial_->setEnabled(false);
    btn_test_serial_->setText("测试中...");
    QApplication::processEvents();

    QString port = combo_serial_port_->currentText();
    int baud = spin_serial_baud_->value();

#ifdef _WIN32
    std::string target = "\\\\.\\" + port.toStdString();
    HANDLE h = CreateFileA(target.c_str(), GENERIC_READ|GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        QString msg;
        if (err==2) msg="端口不存在或已被占用";
        else if (err==5) msg="访问被拒绝（权限不足）";
        else msg=QString("错误码 %1").arg(err);
        serial_test_result_->setText(QString("✗ %1 @ %2: %3").arg(port).arg(baud).arg(msg));
        serial_test_result_->setStyleSheet("QLabel{background:#0d1620;border:1px solid #a44;border-radius:3px;padding:6px;color:#a44;font:11px Consolas}");
    } else {
        DCB dcb={0}; dcb.DCBlength=sizeof(DCB);
        GetCommState(h, &dcb);
        serial_test_result_->setText(QString("✓ %1 @ %2: 打开成功\n  当前配置: %3-%4-%5")
            .arg(port).arg(baud).arg(dcb.ByteSize).arg(dcb.Parity==NOPARITY?'N':dcb.Parity==EVENPARITY?'E':'O')
            .arg(dcb.StopBits==TWOSTOPBITS?2:1));
        serial_test_result_->setStyleSheet("QLabel{background:#0d1620;border:1px solid #4a8;border-radius:3px;padding:6px;color:#4a8;font:11px Consolas}");
        CloseHandle(h);
    }
#else
    serial_test_result_->setText("串口测试仅支持 Windows");
#endif

    btn_test_serial_->setEnabled(true);
    btn_test_serial_->setText("测试串口");
}

/* ============================================================ */
/*  模拟测试                                                     */
/* ============================================================ */
static std::atomic<bool> g_video_playing{false};

/* 停止视频仿真并恢复 UI */
static void stop_video_sim(QTimer* timer, QPushButton* btn, std::atomic<bool>& flag) {
    timer->stop();
    delete (cv::VideoCapture*)timer->property("capPtr").value<uintptr_t>();
    timer->deleteLater();
    flag = false;
    btn->setEnabled(true);
    btn->setText(" 加载图片 → 推理检测");
}

void MainWindow::onSimulateImage() {
    QString path = QFileDialog::getOpenFileName(this,"选择测试图像或视频","",
        "Images (*.jpg *.png *.bmp);;Videos (*.mp4 *.avi *.mkv);;All (*)");
    if (path.isEmpty()) return;

    QString ext = QFileInfo(path).suffix().toLower();
    bool is_video = (ext == "mp4" || ext == "avi" || ext == "mkv");

    if (!is_video) {
        /* ── 单张图片模式 ── */
        cv::Mat img = cv::imread(path.toStdString());
        if (img.empty()) { QMessageBox::warning(this,"错误","无法加载图片: "+path); return; }

        QImage qimg = mat_to_qimage(img);
        sim_image_view_->setPixmap(QPixmap::fromImage(qimg).scaled(
            sim_image_view_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));

        if (!model_loaded_ || !inferencer_) {
            sim_result_text_->setText("检测结果：\n  模型未加载，无法推理。");
            return;
        }

        auto results = inferencer_->infer(img);
        std::vector<BoundingBox> detections;
        if (!results.empty()) detections = results[0].detections;
        detections = postprocessor_->filter(detections);

        QImage res_img = mat_to_qimage(img);
        draw_boxes(res_img, detections);
        sim_image_view_->setPixmap(QPixmap::fromImage(res_img).scaled(
            sim_image_view_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));

        QString txt = "检测结果：\n";
        if (detections.empty()) txt += "  未检测到缺陷\n";
        else {
            txt += QString("  检测到 %1 个缺陷：\n").arg(detections.size());
            int cc[6]={0}; float cm[6]={0};
            for (auto& b:detections) {
                if (b.class_id>=0&&b.class_id<6) { cc[b.class_id]++; if(b.confidence>cm[b.class_id]) cm[b.class_id]=b.confidence; }
            }
            for (int i=0;i<6;++i) if(cc[i]>0)
                txt += QString("    %1: %2 个 (最高 %3%)\n").arg(class_name(i)).arg(cc[i]).arg((int)(cm[i]*100));
        }
        sim_result_text_->setText(txt);
        appendLog(QString("模拟测试: %1 → %2 个缺陷").arg(QFileInfo(path).fileName()).arg(detections.size()));
    } else {
        if (g_video_playing) { QMessageBox::information(this, "提示", "视频正在播放中"); return; }

        cv::VideoCapture probe(path.toStdString());
        if (!probe.isOpened()) { QMessageBox::warning(this, "错误", "无法打开视频"); return; }
        int total = (int)probe.get(cv::CAP_PROP_FRAME_COUNT);

        // 用第一帧的停留阶段（帧60-119）测量 PCB 宽度 iw 和中心偏移 cx
        // 视频结构: 每 210 帧 = 60进入 + 60停留 + 60退出 + 30间隔
        int pcb_iw = 0, pcb_cx = 0;  // 由第一个停留阶段确定
        bool params_ready = false;
        auto measure_pcb = [&](const cv::Mat& frame, int fid) {
            if (params_ready) return;
            int cycle = fid % 210;
            if (cycle < 60 || cycle >= 120) return;  // 非停留阶段
            cv::Mat gray, mask;
            cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
            cv::threshold(gray(cv::Rect(0, 100, gray.cols, gray.rows - 200)),
                          mask, 240, 255, cv::THRESH_BINARY_INV);
            std::vector<std::vector<cv::Point>> contours;
            cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
            if (contours.empty()) return;
            auto it = std::max_element(contours.begin(), contours.end(),
                [](auto& a, auto& b) { return cv::contourArea(a) < cv::contourArea(b); });
            cv::Rect r = cv::boundingRect(*it);
            pcb_iw = r.width;               // PCB 宽度
            pcb_cx = r.x;                   // 停留时 PCB 左边缘 = cx
            params_ready = true;
        };

        // 用公式计算当前帧的 PCB 中心 x
        auto pcb_center_x = [&](int fid) -> int {
            int W = 3256;  // 视频画布宽度
            int cycle = fid % 210;
            if (cycle >= 180) return -1;   // 间隔阶段
            if (!params_ready) return -1;
            float x;  // PCB 左边缘
            if (cycle < 60)        // 进入: 0→59
                x = W + (float)(pcb_cx - W) * cycle / 60.0f;
            else if (cycle < 120)  // 停留: 60→119
                x = (float)pcb_cx;
            else                   // 退出: 120→179
                x = pcb_cx + (float)(-W - pcb_iw - pcb_cx) * (cycle - 120) / 60.0f;
            return (int)(x + pcb_iw / 2.0f);
        };

        struct SharedResult {
            std::vector<BoundingBox> boxes;
            int ref_l = 0, ref_r = 0;  // 推理时 PCB 的可见左右边缘
            std::mutex mtx;
        };
        auto* result = new SharedResult;

        auto display = new QTimer(this);
        display->setProperty("cap", (uintptr_t)new cv::VideoCapture(path.toStdString()));
        display->setProperty("fid", 0);
        display->setProperty("total", total);
        display->setProperty("result", (uintptr_t)result);

        auto find_edge = [](const cv::Mat& frame) -> std::pair<int,int> {
            cv::Mat gray, mask;
            cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
            cv::threshold(gray(cv::Rect(0, 100, gray.cols, gray.rows - 200)),
                          mask, 240, 255, cv::THRESH_BINARY_INV);
            std::vector<std::vector<cv::Point>> contours;
            cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
            if (contours.empty()) return {0, 0};
            auto it = std::max_element(contours.begin(), contours.end(),
                [](auto& a, auto& b) { return cv::contourArea(a) < cv::contourArea(b); });
            cv::Rect r = cv::boundingRect(*it);
            return {r.x, r.x + r.width};
        };

        connect(display, &QTimer::timeout, this, [this, display, total, result, find_edge]() {
            auto* cap = (cv::VideoCapture*)display->property("cap").value<uintptr_t>();
            int fid = display->property("fid").toInt();
            int W = 3256;

            cv::Mat frame;
            if (!cap->read(frame)) {
                cap->set(cv::CAP_PROP_POS_FRAMES, 0);
                if (!cap->read(frame)) { stop_video_sim(display, btn_simulate_, ::g_video_playing); delete result; return; }
            }
            fid++;
            display->setProperty("fid", fid);

            auto [cur_l, cur_r] = find_edge(frame);
            int pcb_id = fid / 210 + 1;
            int phase = fid % 210;

            QImage qimg = mat_to_qimage(frame);

            // 每 ~30 帧异步推理
            static int skip = 0;
            skip++;
            if (skip >= 30 && model_loaded_ && inferencer_) {
                skip = 0;
                cv::Mat f2 = frame.clone();
                int ref_l = cur_l, ref_r = cur_r;
                auto* rp = result;
                std::async(std::launch::async, [this, f2, ref_l, ref_r, rp]() {
                    auto r = inferencer_->infer(f2);
                    std::vector<BoundingBox> bx;
                    if (!r.empty()) bx = r[0].detections;
                    bx = postprocessor_->filter(bx);
                    { std::lock_guard<std::mutex> l(rp->mtx);
                      rp->boxes = std::move(bx);
                      rp->ref_l = ref_l; rp->ref_r = ref_r; }
                });
            }

            // 跟踪：任意一边可见就跟随
            bool pcb_visible = (phase >= 0 && phase < 180);
            if (pcb_visible && cur_r > 0 && cur_l >= 0) {
                std::lock_guard<std::mutex> lk(result->mtx);
                if (!result->boxes.empty()) {
                    int offset_x = 0;
                    // 左侧可见 → 用左侧
                    if (cur_l > 0 && result->ref_l > 0)
                        offset_x = cur_l - result->ref_l;
                    // 左侧不可见（进入/退出到左边界外）→ 用右侧
                    else if (result->ref_r > 0)
                        offset_x = cur_r - result->ref_r;

                    if (offset_x != 0) {
                        std::vector<BoundingBox> moved = result->boxes;
                        for (auto& b : moved) b.x += offset_x;
                        draw_boxes(qimg, moved);
                    }
                }
            }

            sim_image_view_->setPixmap(QPixmap::fromImage(qimg).scaled(
                sim_image_view_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));

            int cycle_fid = fid % 210;
            if (cycle_fid < 60)
                sim_result_text_->setText(QString("PCB #%1 进入中...").arg(pcb_id));
            else if (cycle_fid < 120)
                sim_result_text_->setText(QString("PCB #%1 检测中 | %2 个缺陷")
                    .arg(pcb_id).arg(result->boxes.size()));
            else if (cycle_fid < 180)
                sim_result_text_->setText(QString("PCB #%1 离开中...").arg(pcb_id));
            else
                sim_result_text_->setText("等待下一块 PCB...");

            if (fid >= total) {
                stop_video_sim(display, btn_simulate_, ::g_video_playing);
                delete result;
            }
        });

        sim_result_text_->setText("视频播放中...");
        btn_simulate_->setEnabled(false);
        btn_simulate_->setText("视频播放中...");
        g_video_playing = true;
        display->start(33);
    }
}

/* ============================================================ */
/*  通用槽                                                       */
/* ============================================================ */
void MainWindow::onStartStop() { running_=!running_; btn_start_->setText(running_?"暂停":"继续"); }

void MainWindow::onScreenshot() {
    QPixmap px=camera_view_->pixmap(); if(px.isNull())return;
    QString p=QFileDialog::getSaveFileName(this,"保存截图","screenshot.jpg","Images (*.jpg *.png)");
    if(!p.isEmpty()) px.save(p);
}

/* 保存当前参数到 config.yaml */
void MainWindow::onSaveConfig() {
    if (!settings_ || !settings_->config) {
        QMessageBox::warning(this, "保存失败", "配置对象不可用");
        return;
    }
    auto cfg = settings_->config->app_config();
    cfg.model_path = edit_model_path_->text().toStdString();
    cfg.camera.video_path = edit_video_path_->text().toStdString();
    cfg.camera.loop_video = check_loop_video_->isChecked();
    cfg.serial.port = combo_serial_port_->currentText().toStdString();
    cfg.serial.baudrate = spin_serial_baud_->value();
    cfg.confidence_threshold = slider_conf_->value() / 100.0f;
    cfg.nms_threshold = slider_nms_->value() / 100.0f;
    settings_->config->set_app_config(cfg);
    if (settings_->config->save("config/config.yaml")) {
        QMessageBox::information(this, "保存成功", "参数已保存到 config/config.yaml\n重启程序后生效。");
        Logger::instance().info("配置已保存到 config/config.yaml");
    } else {
        QMessageBox::warning(this, "保存失败", "写入 config/config.yaml 失败");
    }
}

void MainWindow::onConfThreshold(int v){
    float t=v/100.0f; label_conf_->setText(QString("置信度: %1").arg(t,0,'f',2));
    if(postprocessor_) postprocessor_->set_confidence_threshold(t);
    if(settings_) settings_->conf_threshold = t;
}
void MainWindow::onNmsThreshold(int v){
    float t=v/100.0f; label_nms_->setText(QString("NMS: %1").arg(t,0,'f',2));
    if(postprocessor_) postprocessor_->set_nms_threshold(t);
    if(settings_) settings_->nms_threshold = t;
}

/* ============================================================ */
/*  日志                                                         */
/* ============================================================ */
void MainWindow::refreshLog() {
    auto entries = Logger::instance().recent_logs(100);
    int ex = log_list_->count();
    for (int i=ex;i<(int)entries.size();++i) {
        auto& e=entries[i];
        QString t = QString("[%1] [%2] %3").arg(QString::fromStdString(e.timestamp))
            .arg(e.level==LogLevel::Error?"ERR":e.level==LogLevel::Warn?"WARN":e.level==LogLevel::Info?"INFO":"DBG")
            .arg(QString::fromStdString(e.message));
        QColor c = e.level==LogLevel::Error?QColor(255,100,100):e.level==LogLevel::Warn?QColor(255,200,80):QColor(136,136,153);
        log_list_->addItem(t);
        log_list_->item(log_list_->count()-1)->setForeground(c);
    }
    while (log_list_->count()>200) delete log_list_->takeItem(0);
    if (!entries.empty()) log_list_->scrollToBottom();
}

void MainWindow::appendLog(const QString& text) {
    Logger::instance().info(text.toStdString());
}
