# PCB 缺陷检测系统 — 演示指南

## 前置准备

### 1. 检查可执行文件

```powershell
# 确认 exe 存在
Test-Path .\build\Release\defect_detection.exe
```

如不存在，先编译：

```powershell
.\build.ps1
```

### 2. 检查模型文件

```powershell
Test-Path .\models\model.onnx
```

若无模型，参见 `README.md` 训练章节或运行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\download_model.ps1
```

### 3. 安装 com0com（模拟串口用）

> 仅场景三（触发模式 + PLC 模拟）需要；场景一/二无需串口。

下载安装 https://sourceforge.net/projects/com0com/ ，安装后生成虚拟串口对 `COM5 ↔ COM6`。

> 如 COM5/COM6 被占用，可在 com0com 控制面板中改为其他未用端口号，同步修改 `config/config.yaml` 的 `serial.port`。

---

## 场景一：视频回放演示（推荐，最稳定）

无需相机、无需串口，开箱即跑。

### 步骤

```powershell
# 1. 确保 config.yaml 中 camera.video_path 指向仿真视频
#    编辑 config/config.yaml 设置：
#      camera:
#        device_id: 0
#        video_path: "data/pipeline_simulation.mp4"
#        loop_video: true
#
#    或直接使用 run_demo.py（自动读取默认配置）:

# 2. 启动演示（无头模式 — 仅日志输出，适合快速验证）
python scripts/run_demo.py

# 3. 启动演示（带 UI 窗口）
.\build\Release\defect_detection.exe config\config.yaml
```

### 预期效果

- 窗口显示 PCB 流水线动画（PCB 从右向左移动）
- 每块 PCB 居中时自动检测，叠加检测框和类别标签
- 控制台日志输出检测结果（帧率、检出缺陷数）
- 无模型时自动进入预览模式（仅显示画面，不推理）

---

## 场景二：USB 摄像头演示

需 USB 相机（或笔记本自带摄像头）。

### 步骤

```powershell
# 1. 修改 config.yaml
#    camera:
#      device_id: 0           # 相机设备号（可尝试 0/1/2）
#      video_path: ""          # 留空使用 USB
#      width: 640
#      height: 480

# 2. 启动
.\build\Release\defect_detection.exe config\config.yaml
```

### 预期效果

- 实时显示摄像头画面
- 每帧自动推理，检测框叠加
- 右上角显示实时 FPS

---

## 场景三：触发模式 + PLC 模拟（完整联机闭环）

模拟真实产线：PLC 发送 TRIG → 检测系统推理 → 回传 PASS/FAIL。

### 步骤

```powershell
# 终端 1 — 启动检测系统（接 COM6）
# 确保 config.yaml 中 serial.port: "COM6"
.\build\Release\defect_detection.exe config\config.yaml

# 终端 2 — 启动 PLC 模拟器（接 COM5）
# conda 环境需包含 pyserial
conda run -n yolov5 python scripts/plc_simulator.py COM5 --interval 2.0
```

### 预期效果

- PLC 模拟器每 2 秒发送 TRIG 信号
- 检测系统收到 TRIG 后采集一帧 → 推理 → 回传结果
- 模拟器日志依次显示 `→ TRIG`、`← PASS/FAIL:n`、`← DONE`
- 检出缺陷时，截图自动保存至 `logs/defects/`（含叠加框的 JPG + JSON 元信息）
- CSV 报表自动导出至 `logs/stats/`
- 发送 `RST` 可重置统计计数

### 串口协议速查

| 报文 | 方向 | 含义 |
|------|------|------|
| `TRIG` | PLC → 系统 | 触发采集一帧 |
| `PASS` | 系统 → PLC | 良品 |
| `FAIL:n` | 系统 → PLC | 缺陷品，n = 缺陷数 |
| `DONE` | 系统 → PLC | 本次处理完成 |
| `RST` | PLC → 系统 | 重置统计 |

完整协议见 `docs/SERIAL_PROTOCOL.md`。

---

## 场景四：离线批测（数据集评估）

```powershell
# 对 PCB_DATASET 图片批量推理并生成指标报告
python scripts/evaluate.py
```

输出：`logs/eval/YYYYMMDD/report.json`（含每类 P/R/mAP、混淆矩阵、PR 曲线）。

---

## 故障排查

| 问题 | 原因 | 解决 |
|------|------|------|
| `缺少 onnxruntime.dll` | DLL 未复制到 exe 目录 | 重新运行 `build.ps1` |
| `模型文件不存在` | 未放置 ONNX 模型 | 运行 `scripts/download_model.ps1` 或训练后导出 |
| `串口打开失败` | com0com 未安装或端口号不匹配 | 检查 `config.yaml` 中 `serial.port`，确认 com0com 已安装 |
| `相机打开失败` | 设备号错误或摄像头被占用 | 修改 `device_id`，或改用视频模式（场景一） |
| `中文显示乱码` | OpenCV highgui 不支持中文 | 使用 Qt 版或切换系统区域为中文简体 |

---

## 验收清单

按顺序逐项完成即视为演示通过：

- [ ] `build/Release/defect_detection.exe` 存在
- [ ] `models/model.onnx` 存在
- [ ] `data/pipeline_simulation.mp4` 存在
- [ ] 场景一：视频回放正常显示，检测框正确叠加，日志无报错
- [ ] 场景三（可选）：com0com 虚拟串口对已安装，PLC 模拟器与检测系统握手成功
- [ ] 场景三：FAIL 时截图保存至 `logs/defects/`，CSV 报表正确更新
- [ ] `config/config.yaml` 中 `class_names` 与训练集 6 类一致
- [ ] 串口未连接时程序不崩溃，日志明确提示「串口未连接」
