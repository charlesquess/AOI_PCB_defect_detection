# AOI PCB 缺陷检测

基于计算机视觉的 **PCB 自动光学检测（AOI）** 工程：从公开数据集、YOLOv5 训练与 ONNX 导出，到 C++ 实时检测流水线（相机/视频采集、滑窗推理、PLC 模拟联机、Qt 界面）。

**v1.0** 以仿真环境可交付为目标（USB 相机 / 视频回放 / 离线大图 + 模拟 PLC）；真产线扩展见 [defect_detection/docs/DEVELOPMENT.md](defect_detection/docs/DEVELOPMENT.md)。

---

## 仓库结构

```
AOI_PCB_defect_detection/
├── defect_detection/     # ★ 检测系统主工程（C++ + ONNX + Qt）
├── yolov5-7.0/           # YOLOv5 训练与导出（含 PCB 超参 hyp.pcb.yaml）
├── PCB_DATASET/          # 原始 PCB 缺陷数据集（Kaggle）
└── README.md             # 本文件（项目总览）
```

| 目录 | 说明 |
|------|------|
| **[defect_detection/](defect_detection/)** | 可运行产品：三线程流水线、配置、模型、脚本、文档。**详细说明见 [defect_detection/README.md](defect_detection/README.md)** |
| **[yolov5-7.0/](yolov5-7.0/)** | Ultralytics YOLOv5 v7.0，用于训练与 `export.py` 导出 ONNX |
| **[PCB_DATASET/](PCB_DATASET/)** | 原始标注数据；来源见 [PCB_DATASET/README.md](PCB_DATASET/README.md) |

---

## 端到端流程

```
PCB_DATASET (XML 标注)
        │
        ▼  preprocess_dataset.py
defect_detection/data/yolo_dataset
        │
        ▼  yolov5-7.0/train.py + export.py + export_onnx.py
defect_detection/models/model.onnx
        │
        ▼  build.ps1 → defect_detection_qt.exe
采集 → 预处理 → ONNX 推理 → 后处理/追踪 → 统计 / 串口 / UI
```

1. **数据**：将 Kaggle 数据集放入 `PCB_DATASET/`，运行 `defect_detection/scripts/preprocess_dataset.py` 生成 YOLO 格式滑窗切片。
2. **训练**：在 `yolov5-7.0/` 下训练并导出 ONNX，经 `defect_detection/scripts/export_onnx.py` 写入 `models/`。
3. **运行**：在 `defect_detection/` 编译并启动 Qt 或无头演示（见下方快速开始）。

---

## 快速开始

检测程序在 **`defect_detection/`** 目录下构建与运行：

```powershell
cd defect_detection

# 1. 编译
.\build.ps1

# 2. 确认模型
Test-Path .\models\model.onnx

# 3. 启动（推荐 Qt 界面）
.\build\Release\defect_detection_qt.exe config\config.yaml
```

无界面快速验收：

```powershell
cd defect_detection
python scripts/run_demo.py
```

完整演示（视频回放、USB、模拟 PLC）、依赖安装与故障排查见 **[defect_detection/docs/DEMO.md](defect_detection/docs/DEMO.md)**。

---

## 系统概览

三线程架构（采集 / 推理 / UI）通过有界队列解耦，支持连续检测与 PLC 触发模式：

```
                    ┌──────────────┐
                    │  采集线程     │
                    │ Camera 30fps  │
                    └──────┬───────┘
                           │ raw_queue
                           ▼
┌─────────────────────────────────────────────────────────────┐
│  推理线程：Preprocess → ONNX → NMS → Tracker → Statistics    │
│  串口：TRIG → PASS / FAIL:n / DONE                           │
└─────────────────────────────────────────────────────────────┘
                           │ result_queue
                           ▼
                    ┌──────────────┐
                    │  UI（Qt 推荐） │
                    └──────────────┘
```

**检测类别（6 类）**：`missing_hole` · `mouse_bite` · `open_circuit` · `short` · `spur` · `spurious_copper`（ID 0–5，全项目统一）。

---

## 训练与导出（摘要）

在已生成 `defect_detection/data/yolo_dataset` 的前提下：

```bash
cd yolov5-7.0
python train.py --img 640 --batch 16 --epochs 100 --patience 15 \
  --data ../defect_detection/data/yolo_dataset/dataset.yaml \
  --hyp data/hyps/hyp.pcb.yaml --weights yolov5s.pt

python export.py --weights runs/train/exp/weights/best.pt --include onnx --img 640
python ../defect_detection/scripts/export_onnx.py
```

数据集预处理、超参说明、性能基准与配置项见 **[defect_detection/README.md](defect_detection/README.md)**。

---

## 文档索引

| 文档 | 内容 |
|------|------|
| [defect_detection/README.md](defect_detection/README.md) | 模块说明、构建、配置、测试、性能表 |
| [defect_detection/docs/DEMO.md](defect_detection/docs/DEMO.md) | 演示场景与验收清单 |
| [defect_detection/docs/DEPENDENCIES.md](defect_detection/docs/DEPENDENCIES.md) | CUDA / ONNX Runtime / OpenCV / Qt 等版本 |
| [defect_detection/docs/SERIAL_PROTOCOL.md](defect_detection/docs/SERIAL_PROTOCOL.md) | PLC 串口协议 |
| [defect_detection/docs/DEVELOPMENT.md](defect_detection/docs/DEVELOPMENT.md) | 路线图、P0/P1/P2、交付边界 |
| [defect_detection/docs/THIRD_PARTY_LICENSES.md](defect_detection/docs/THIRD_PARTY_LICENSES.md) | 第三方许可汇总 |
| [PCB_DATASET/README.md](PCB_DATASET/README.md) | 数据集来源（Kaggle） |

---

## 项目范围（v1.0）

| 项 | 选择 |
|----|------|
| 检测 | 6 类全检，任一类检出即 FAIL |
| 采集 | USB 相机或 `defect_detection/data/` 内仿真视频 |
| PLC | 虚拟串口模拟（`plc_simulator.py`） |
| 状态 | P0、P1 已结项；真产线能力为 P2 扩展 |

---

## 许可

- **本仓库自有代码与文档**：[defect_detection/LICENSE](defect_detection/LICENSE) — **仅限非商业使用**（学习、科研、毕设等）；商业用途须另行取得书面授权。
- **YOLOv5**：须遵守 **GPL-3.0**（见 `yolov5-7.0/LICENSE`）；商业使用另见 [Ultralytics 许可说明](https://ultralytics.com/license)。
- **其他依赖**：见 [defect_detection/docs/THIRD_PARTY_LICENSES.md](defect_detection/docs/THIRD_PARTY_LICENSES.md)。
