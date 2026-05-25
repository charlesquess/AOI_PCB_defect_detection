# 第三方组件许可证

本文件说明 `defect_detection` 项目所依赖的主要第三方软件及其许可类型。  
**本仓库自有代码的许可见根目录 [LICENSE](../LICENSE)（非商业使用）。**

> 下列信息供合规参考，不构成法律意见。正式商用前请咨询法务。

---

## 与检测链路强相关

| 组件 | 版本（参考） | 许可 | 说明 |
|------|-------------|------|------|
| **YOLOv5** | 7.0（`yolov5-7.0/`） | **GPL-3.0** | 用于 `train.py`、`val.py`、`export.py` 及权重训练；须遵守 GPL-3.0。商业使用见 [Ultralytics Licensing](https://ultralytics.com/license)。许可证全文：`yolov5-7.0/LICENSE` |
| **ONNX Runtime** | 1.18.1 | MIT | C++ 推理运行时，见 `3rdparty/onnxruntime/` |
| **OpenCV** | 4.13 | Apache 2.0 | 图像处理、相机、部分 UI |
| **Qt** | 6.6.2 | LGPL v3（动态链接） | `defect_detection_qt.exe`；若静态链接或修改 Qt 须满足 LGPL 义务 |
| **yaml-cpp** | 0.7+ | MIT | 配置文件解析 |
| **cuDNN / CUDA** | 8.9.7 / 12.6 | NVIDIA 软件许可 | 仅随 NVIDIA 驱动/SDK 使用，见 NVIDIA EULA |

---

## Python 脚本环境（训练 / 评测 / 工具）

| 组件 | 典型用途 | 许可（常见） |
|------|----------|-------------|
| PyTorch | YOLOv5 训练后端 | BSD-style（见 PyTorch 发行版） |
| NumPy、Pillow 等 | 数据处理 | 各自开源许可 |
| conda / pip 其他依赖 | `requirements`、环境 | 以各包 PyPI 元数据为准 |

训练与评估请在 `yolov5` conda 环境中进行，并同时遵守 **YOLOv5 GPL-3.0**。

---

## 合规要点（摘要）

1. **非商业**：本仓库 `LICENSE` 禁止将自有代码用于商业用途（除非另行授权）。
2. **YOLOv5**：训练、导出、分发含 YOLOv5 代码或受其约束的制品时，须遵守 **GPL-3.0**；商业场景须核查 Ultralytics 商业许可。
3. **运行时**：部署 `defect_detection.exe` / `defect_detection_qt.exe` 时，须在分发包中保留各依赖库的许可声明（绿色包已复制 `LICENSE` 与本文档时一并提供）。
4. **ONNX 模型**：由 YOLOv5 训练并导出的 `model.onnx` 与训练数据、使用场景相关；再分发或商用前请结合 YOLOv5 许可与数据集许可一并评估。

---

## 许可证全文获取

| 组件 | 路径 / 链接 |
|------|-------------|
| YOLOv5 GPL-3.0 | `../yolov5-7.0/LICENSE` 或 https://www.gnu.org/licenses/gpl-3.0.html |
| 本项目 | `../LICENSE` |
| ONNX Runtime | https://github.com/microsoft/onnxruntime/blob/main/LICENSE |
| OpenCV | https://opencv.org/license/ |
| Qt | https://www.qt.io/licensing/ |
