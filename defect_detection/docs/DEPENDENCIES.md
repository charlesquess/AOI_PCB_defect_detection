# 依赖版本锁定文档

> **许可说明**：本仓库自有代码为**非商业使用**（见 [LICENSE](../LICENSE)）。YOLOv5 为 **GPL-3.0**；其余组件见 [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)。

## 已验证环境（本机通过测试）

| 依赖 | 版本 | 用途 | 安装源 |
|------|------|------|--------|
| **GPU** | NVIDIA Tesla V100 (CC 7.0) | 推理加速 | 硬件 |
| CUDA Toolkit | **12.6** | GPU 运行时 | [developer.nvidia.com](https://developer.nvidia.com/cuda-12-6-0-download-archive) |
| cuDNN | **8.9.7.29 (for CUDA 12)** | 深度学习算子库 | [developer.nvidia.com](https://developer.download.nvidia.com/compute/cudnn/redist/cudnn/windows-x86_64/cudnn-windows-x86_64-8.9.7.29_cuda12-archive.zip) |
| ONNX Runtime | **1.18.1 (gpu-cuda12)** | 模型推理引擎 | [github.com](https://github.com/microsoft/onnxruntime/releases/tag/v1.18.1) |
| OpenCV | **4.13.0 (vc16)** | 图像处理、相机采集、UI | [opencv.org](https://opencv.org/releases/) |
| Qt | **6.6.2 (msvc2019_64)** | 跨平台桌面 UI | [setup_qt.ps1](../setup_qt.ps1) 或 aqtinstall |
| CMake | **3.20+ (VS 2019 内置)** | 构建系统 | Visual Studio 2019 BuildTools |
| C++ Compiler | **MSVC 14.29 (VS 2019)** | C++17 编译 | [Visual Studio 2019 BuildTools](https://visualstudio.microsoft.com/vs/older-downloads/) |
| yaml-cpp | **0.7+** (conda) | YAML 配置解析 | `conda install yaml-cpp -c conda-forge` |
| Python | **3.10+** (yolov5 conda env) | 辅助脚本、模型评估 | Anaconda |

## 路径约定

| 组件 | 预期位置 |
|------|----------|
| OpenCV | `C:\Opencv2\opencv\build` |
| Qt 6.6.2 | `C:\Qt\6.6.2\msvc2019_64` |
| ONNX Runtime | `defect_detection\3rdparty\onnxruntime` |
| yaml-cpp (conda) | `C:\ProgramData\anaconda3\Library` |

## 兼容性矩阵

| ORT 版本 | CUDA 版本 | cuDNN 版本 | 本机已验证 |
|----------|-----------|-----------|:---------:|
| 1.18.1 | 12.6 | 8.9.7.29 | ✅ |
| 1.23.x | 12.x | 9.x | ⚠️ 兼容 |

> 升级 ONNX Runtime 时需同步更新 `3rdparty/onnxruntime/` 目录下的 DLL 和头文件，并确保 CUDA/cuDNN 版本匹配 ORT 的编译要求。

## 绿色包依赖

运行 `scripts\package_deploy.bat` 后，`deploy\` 目录包含运行所需全部文件：

```
deploy/
├── start.bat                  # 启动入口
├── README.md
├── bin/
│   ├── defect_detection.exe   # OpenCV 版
│   ├── defect_detection_qt.exe (可选，需要 Qt DLL)
│   ├── onnxruntime.dll
│   ├── onnxruntime_providers_shared.dll
│   ├── opencv_world4130.dll
│   ├── yaml-cpp*.dll
│   ├── Qt6*.dll               (可选)
│   └── platforms/qwindows.dll (可选)
├── config/
│   └── config.yaml
├── models/
│   ├── model.onnx
│   └── manifest.json
└── docs/
    ├── SERIAL_PROTOCOL.md
    └── DEPENDENCIES.md
```

## 版本检查命令

```powershell
# CUDA 版本
nvidia-smi

# cuDNN 版本
Get-Item "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.6\include\cudnn_version.h" | Select-String "CUDNN_MAJOR|CUDNN_MINOR"

# ONNX Runtime 版本
python -c "import onnxruntime; print(onnxruntime.__version__)"

# OpenCV 版本
python -c "import cv2; print(cv2.__version__)"

# Qt 版本
Get-Item "C:\Qt\6.6.2\msvc2019_64\bin\Qt6Core.dll" | Select-Object VersionInfo

# CMake 版本
cmake --version

# MSVC 版本
cl.exe 2>&1 | Select-String "Version"
```
