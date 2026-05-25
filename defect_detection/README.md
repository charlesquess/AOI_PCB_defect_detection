# PCB Defect Detection System

基于计算机视觉的 PCB 缺陷检测系统。C++ 核心流水线 + YOLOv5 ONNX 推理 + 滑窗预处理。

**v1.0**：仿真环境可交付（USB 相机 / 视频回放 / 离线大图 + 模拟 PLC）。真产线扩展见 [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)。

## 快速开始

```powershell
# 1. 编译
.\build.ps1

# 2. 确认模型
Test-Path .\models\model.onnx

# 3. 演示（推荐 Qt 界面）
.\build\Release\defect_detection_qt.exe config\config.yaml

# 或：无头快速验收
python scripts/run_demo.py
```

完整演示步骤（视频回放、USB、模拟 PLC）见 **[docs/DEMO.md](docs/DEMO.md)**。

## 系统架构

```
                              ┌──────────────┐
                              │  采集线程     │
                              │ Camera 30fps  │
                              └──────┬───────┘
                                     │ raw_queue
                                     ▼
┌─────────────────────────────────────────────────────────────────────┐
│                         推理线程                                     │
│  Preprocess → Inference(ONNX) → Postprocess(NMS) → Tracker → Stat    │
│  串口通信 (PASS/FAIL)                                               │
└─────────────────────────────────────────────────────────────────────┘
                                     │ result_queue
                                     ▼
                              ┌──────────────┐
                              │  UI线程 (主)   │
                              │ 显示 + 键盘    │
                              └──────────────┘
```

三线程通过有界队列解耦，采集满速不停机，推理/UI 互不阻塞。

## 目录结构

```
defect_detection/
├── 3rdparty/             # ONNX Runtime + cuDNN
├── include/ / src/       # C++ 源码
├── config/               # config.yaml
├── models/               # model.onnx + manifest.json
├── logs/                 # 日志、评测、缺陷截图
├── data/                 # 仿真视频、yolo_dataset
├── docs/                 # 项目文档
│   ├── DEMO.md           #   演示指南
│   ├── DEPENDENCIES.md   #   依赖版本
│   ├── SERIAL_PROTOCOL.md#   串口协议
│   ├── THIRD_PARTY_LICENSES.md # 第三方许可（含 YOLOv5 GPL-3.0）
│   └── DEVELOPMENT.md    #   路线图与开发记录
├── scripts/              # 训练、评测、测试、部署脚本
├── build.ps1
├── LICENSE
└── README.md
```

## 模块说明

### Camera — 相机采集

- USB 相机 (OpenCV VideoCapture)，自动探测 DShow / MSMF 后端
- 支持 **视频文件回放**（`camera.video_path` + `loop_video`）
- 曝光/增益/分辨率/FPS、帧超时、自动重连、ROI、快照

### Preprocess — 图像预处理

- 缩放、归一化、直方图均衡、去噪、仿射变换、颜色空间转换

### Inference — ONNX 推理引擎

- **ONNX Runtime 1.18+ (CUDA 12)** — 模型加载 → blob → session.Run → YOLOv5 解析
- 大图 **滑窗** 640×640、跨窗 NMS；支持 CPU / GPU
- 阈值与 manifest 版本校验（见 `main.cpp` / `main_qt.cpp`）

### Postprocess — 后处理

- 置信度过滤、NMS、检测框绘制（解析主要在 `Inferencer` 内完成）

### Tracker — 追踪去重

- **IOU 追踪**（>0.3 视为同一 PCB），连续模式去重；触发模式一板一帧

### Communication — 串口通信

- RS232/485：`TRIG` → 采集 → 推理 → `PASS` / `FAIL:n` / `DONE`
- 调试：`scripts/plc_simulator.py` + com0com 虚拟串口（协议见 `docs/SERIAL_PROTOCOL.md`）

### Statistics — 数据统计

- 良率、缺陷分布、推理耗时；CSV/JSON；FAIL 截图归档至 `logs/defects/`

### UI — 人机界面

- **推荐**：`defect_detection_qt.exe`（Qt 6.6）— 实时画面、统计、调试页（阈值/模型/串口/视频，可保存到 yaml）
- **备选**：`defect_detection.exe`（OpenCV + GDI 中文，轻量调试）

## 数据集预处理

原始 PCB 大图 (3034×1586) → 滑窗 640×640（步长 320，50% 重叠），训练集正交旋转 0/90/180/270°：

```bash
conda run -n yolov5 python scripts/preprocess_dataset.py
```

## 训练与导出

YOLOv5s + P2 微小目标头（4 尺度），超参见 `yolov5-7.0/data/hyps/hyp.pcb.yaml`。

```bash
cd yolov5-7.0
python train.py --img 640 --batch 16 --epochs 100 --patience 15 \
  --data ../defect_detection/data/yolo_dataset/dataset.yaml \
  --hyp data/hyps/hyp.pcb.yaml --weights yolov5s.pt

python export.py --weights runs/train/exp/weights/best.pt --include onnx --img 640
python ../defect_detection/scripts/export_onnx.py
```

`export_onnx.py` 会复制 ONNX 到 `models/model.onnx` 并生成 `models/manifest.json`。

## 依赖项

| 依赖 | 版本 | 用途 |
|------|------|------|
| CUDA Toolkit | 12.6 | GPU |
| cuDNN | 8.9.7 (CUDA 12) | 算子库 |
| ONNX Runtime | 1.18.1 gpu-cuda12 | 推理 |
| OpenCV | 4.13 (vc16) | 图像/相机 |
| Qt | 6.6.2 msvc2019_64 | UI（可选） |
| yaml-cpp | ≥ 0.7 | 配置（必选） |

详细版本与绿色包说明见 **[docs/DEPENDENCIES.md](docs/DEPENDENCIES.md)**。

### 推理性能（本机参考）

| 场景 | GPU | CPU |
|:----|:---:|:---:|
| 单窗口 640×480 | ~7ms | ~170ms |
| 大图 ~50 窗 | ~350ms | ~3.4s |
| USB 连续模式 | ~140 FPS | ~6 FPS |

> 7ms 为单窗 GPU 值；**整板延迟**见 `scripts/benchmark.py`（滑窗数量 × 单窗耗时，不等于单窗值）。

## 构建与运行

### 一键编译

```powershell
.\build.ps1
```

### Qt 版（推荐演示）

```powershell
.\build\Release\defect_detection_qt.exe config\config.yaml
```

### OpenCV 版

```powershell
.\build\Release\defect_detection.exe config\config.yaml
```

### 3rdparty

将 ONNX Runtime GPU (CUDA 12) 与 cuDNN 8.9.7 解压合并到 `3rdparty/onnxruntime/`。下载链接见 [docs/DEPENDENCIES.md](docs/DEPENDENCIES.md)。

### 运行模式

| 模式 | 条件 |
|------|------|
| GPU 检测 | `model.onnx` 存在且 `use_gpu: true` |
| CPU 检测 | 模型存在且 `use_gpu: false` |
| 预览 | 无模型文件，仅显示画面 |

## 配置

主要项见 `config/config.yaml`：

| 配置 | 说明 | 默认 |
|------|------|------|
| `camera.video_path` | 视频回放路径（空=USB） | `""` |
| `model.path` | ONNX 路径 | `models/model.onnx` |
| `model.use_gpu` | GPU 推理 | `true` |
| `detection.confidence_threshold` | 置信度阈值 | `0.6` |
| `serial.port` | 串口（如 COM6） | `COM6` |

Qt 调试页可修改参数并 **保存到 config.yaml**（重启后生效）。

## 测试与验收

```powershell
# 自动化测试
python scripts/run_tests.py

# 本地 CI（编译 + 测试）
scripts\run_ci.bat

# 阶段验收
powershell -ExecutionPolicy Bypass -File scripts\verify_p0.ps1
powershell -ExecutionPolicy Bypass -File scripts\verify_p1.ps1
```

```bash
# 摄像头
python scripts/test_camera.py

# PLC 模拟（COM5，程序连 COM6）
python scripts/plc_simulator.py COM5 --interval 2.0

# 离线批测 / 评估
python scripts/batch_infer.py
python scripts/evaluate.py
```

## 检测类别（6 类）

> ID 顺序：`0=missing_hole` · `1=mouse_bite` · `2=open_circuit` · `3=short` · `4=spur` · `5=spurious_copper`

| ID | 名称 | 数据集 XML |
|:--:|------|------------|
| 0 | missing_hole | Missing Hole |
| 1 | mouse_bite | Mouse Bite |
| 2 | open_circuit | Open Circuit |
| 3 | short | Short |
| 4 | spur | Spur |
| 5 | spurious_copper | Spurious Copper |

## 文档索引

| 文档 | 内容 |
|------|------|
| [docs/DEMO.md](docs/DEMO.md) | 演示场景、验收清单、故障排查 |
| [docs/DEPENDENCIES.md](docs/DEPENDENCIES.md) | 依赖版本、绿色部署包 |
| [docs/SERIAL_PROTOCOL.md](docs/SERIAL_PROTOCOL.md) | PLC 串口协议与状态机 |
| [docs/THIRD_PARTY_LICENSES.md](docs/THIRD_PARTY_LICENSES.md) | 第三方许可（YOLOv5、ORT、OpenCV、Qt 等） |
| [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) | 路线图、排期、P0/P1/P2 任务与开发记录 |

## 项目范围（v1.0 摘要）

| 项 | 选择 |
|----|------|
| 检测 | 6 类全检，任一类检出即 FAIL |
| 采集 | USB 或 `data/pipeline_simulation.mp4` |
| PLC | 虚拟串口模拟，无真机 |
| 状态 | P0、P1 已结项；P2 真产线待扩展 |

交付边界、排期与任务 checkbox 详见 **[docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)**。

## 许可

- **本仓库自有代码与文档**：[LICENSE](LICENSE) — **仅限非商业使用**（学习、科研、毕设等）；商业用途须另行取得书面授权。
- **YOLOv5（训练 / 导出）**：须遵守 **GPL-3.0**（见 `yolov5-7.0/LICENSE`）；商业使用另见 [Ultralytics 许可说明](https://ultralytics.com/license)。
- **其他依赖**：见 [docs/THIRD_PARTY_LICENSES.md](docs/THIRD_PARTY_LICENSES.md)。
