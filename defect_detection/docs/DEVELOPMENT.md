# 开发与路线图

本文档记录项目定位、交付边界、阶段排期、P0/P1/P2 任务清单、已知差距与结项记录。  
面向维护者与答辩/评审使用；日常使用与编译运行请参阅 [README.md](../README.md)。

---

## 项目定位（已确认交付模式）

本项目目标是做成 **完整、可演示、工程规范齐全** 的 PCB 缺陷检测系统，而不是绑定真实产线硬件。  
在**无工业相机、无真实 PLC** 的前提下，通过 **USB 摄像头 / 视频文件 / 数据集图片 + 虚拟串口** 完整跑通「采集 → 推理 → 显示 → 统计 → 模拟联机」全链路。

| 维度 | 当前状态 | 说明 |
|------|----------|------|
| **定位** | 仿真优先的完整工程项目 | 架构按产线设计，硬件用模拟件替代，便于答辩/作品集/后续升级 |
| **检测** | 6 类全检 | 见下方「已锁定交付边界」 |
| **成像** | 整图 / 缩略图 + 滑窗 | 训练侧：PCB 大图滑窗 640；演示侧：USB 或视频可低于训练分辨率 |
| **采集** | USB 摄像头（OpenCV） | 无工业相机；可用 `camera.video_path` 播片代替 |
| **联机** | ✅ P0-7 模拟 PLC | 触发模式 (TRIG/PASS/FAIL/DONE)；协议文档化于 `SERIAL_PROTOCOL.md` |
| **配置** | ✅ P0-2 | yaml-cpp 必选，`config.cpp` 完整解析 config.yaml |
| **评估** | ✅ P0-4 | `scripts/evaluate.py` 运行 val.py 导出指标 + 门槛检查 |
| **数据** | ✅ P0-5 | 唯一入口 `preprocess_dataset.py`；`rotate.py` 已归档 |
| **推理链路** | ✅ P0-6 | 去重复 NMS；阈值统一来自配置；`Inferencer` 完成解析+NMS |
| **性能基准** | ✅ P0-8 | `scripts/benchmark.py` 实测三种输入源；整板/单窗延迟明确区分 |
| **模型交付** | ✅ P0-3 | `model.onnx` + `manifest.json`；训练/下载流程文档化 |

**「完美项目」在本仓库的含义**

- 代码与文档一致，配置可改、类别不错位、模型可训练可部署  
- 无硬件也能 **一键演示**（视频/图片/USB + 模拟串口）  
- README / 协议 / 评测报告齐全，像真实产品一样可维护  
- 真产线能力（工业相机、MES）放在 **P2 扩展**，不假装已实现  

---

## 已锁定交付边界（v1.0）

> 以下为当前版本的**正式范围**；真机升级属于 P2，不阻塞 v1.0 交付。

| 项 | 本项目选择 | 说明 |
|----|------------|------|
| **检测类别** | **6 类全检** | `missing_hole`, `mouse_bite`, `open_circuit`, `short`, `spur`, `spurious_copper` |
| **成像范围** | **整图 / 缩略图** | 推理：大图滑窗 640×640；USB 演示可为 640×480 全画面 |
| **节拍（演示）** | 连续模式流畅即可 | 整板 GPU 滑窗见 README 性能表；USB 模式以实测 FPS 为准 |
| **采图** | **USB 摄像头** | 可选 `camera.video_path` 循环播放仿真视频 |
| **PLC** | **模拟** | `plc_simulator.py` + 虚拟 COM 对 |
| **判废** | 任一类检出 → `FAIL` | 串口 `PASS` / `FAIL:<n>` / `DONE` |
| **误检门槛** | 验证集 mAP + 典型样例 | `PCB_DATASET` 划分 val |

> **类别 ID 顺序（全项目唯一标准）**  
> `0=missing_hole` · `1=mouse_bite` · `2=open_circuit` · `3=short` · `4=spur` · `5=spurious_copper`  
> 与 `scripts/preprocess_dataset.py`、`data/yolo_dataset/dataset.yaml` 保持一致。

### 仿真运行方式（无真机时的标准演示）

| 场景 | 配置 / 工具 | 用途 |
|------|-------------|------|
| USB live | `camera.device_id: 0`，`video_path: ""` | 对着屏幕/样品拍 |
| 视频回放 | `camera.video_path: data/xxx.mp4`，`loop_video: true` | 稳定可重复演示 |
| 离线大图 | 对 `PCB_DATASET` 图片推理 | 算法效果、benchmark |
| 模拟 PLC | com0com `COM5↔COM6` + `plc_simulator.py COM5` | 程序连 `COM6` |

```yaml
camera:
  device_id: 0
  video_path: "data/pipeline_simulation.mp4"
  loop_video: true
serial:
  port: "COM6"
```

---

## 路线图总览

```
已锁定交付边界 (v1.0 仿真优先)
        │
        ▼
┌───────────────────────────────────────────────────────────┐
│  P0 核心闭环 — 无真机也要「端到端可演示」                    │
│  配置 · 6类统一 · 训练部署 · 评测 · 数据一致 · 模拟串口      │
└───────────────────────────────────────────────────────────┘
        │
        ▼
┌───────────────────────────────────────────────────────────┐
│  P1 项目做「完美」— 体验、测试、文档、Qt 默认                 │
│  Qt UI · 缺陷归档 · CI · 部署包 · 演示脚本                   │
└───────────────────────────────────────────────────────────┘
        │
        ▼
┌───────────────────────────────────────────────────────────┐
│  P2 真产线扩展（可选）— 有预算/硬件再做                      │
│  工业相机 · 硬触发 · TensorRT · MES · 权限审计               │
└───────────────────────────────────────────────────────────┘
```

### 排期

| 周次 | 任务 |
|------|------|
| 第 1 周 | P0-1 → P0-2 → P0-5 |
| 第 2 周 | P0-3 → P0-4 → P0-6 |
| 第 3 周 | P0-7 → P0-8 |
| 第 4～6 周 | P1：Qt、测试、演示包、文档 |
| 产线落地 | P2 按需选取，不阻塞 v1.0 |

---

## 结项记录

| 阶段 | 日期 | 验收 |
|------|------|------|
| **P0** | 2026-05-20 | `scripts/verify_p0.ps1` → 43 PASS / 0 FAIL |
| **P1** | 2026-05-22 | `scripts/verify_p1.ps1` → 全 PASS |
| **P2** | — | 待硬件后按需推进 |

```powershell
powershell -ExecutionPolicy Bypass -File scripts\verify_p0.ps1
powershell -ExecutionPolicy Bypass -File scripts\verify_p1.ps1
```

---

## P0 — 核心闭环（v1.0 必做）

> **原则**：P0 完成 = clone 后能完成 **训练 → 放模型 → USB/视频演示 → 模拟 PLC 联调**，且 6 类标签不错。

### P0-0 交付边界文档化

- [x] 交付边界已锁定（见上文）

**验收**：文档明确「仿真优先、无工业相机/无真 PLC」。

---

### P0-1 统一类别命名（全仓库一处真相）

**问题**：`types.hpp` 和 `config.yaml` 曾用旧类名，与训练脚本不兼容。

**当前状态**：✅ **已完成**

**任务**

- [x] 以 `preprocess_dataset.py` 的 `CLASSES` 为唯一标准
- [x] `types.hpp`、`config.yaml`、`statistician.cpp`、`qt_main_window.cpp`、`dataset.yaml` 等全部对齐

**验收**：训练 `names`、C++ 枚举、UI 图例、ONNX `nc=6` 一致。

---

### P0-2 实现真正的 YAML 配置加载

**当前状态**：✅ **已完成** — yaml-cpp 必选；`load()` / `save()` / `dump()` 完整实现。

**任务**

- [x] `CMakeLists.txt` — yaml-cpp REQUIRED
- [x] `config.cpp` — 解析各段并序列化回写
- [x] `main.cpp` / `main_qt.cpp` — 从配置读取参数
- [x] Qt 设置页保存阈值、串口、模型路径回写 yaml（P1-5）

**验收**：改 `confidence_threshold` 或 `serial.port` 后行为可观测。

---

### P0-3 冻结「训练 → ONNX → 部署」一条链

**当前状态**：✅ **已完成**

**任务**

- [x] `train.py` → `export.py` → `export_onnx.py` → `models/model.onnx`
- [x] `manifest.json` 自动生成与启动读取
- [x] `download_model.ps1`

---

### P0-4 验证集指标与上线门槛

**当前状态**：✅ **已完成**

**任务**

- [x] `evaluate.py` — val.py + 门槛检查 + manifest mAP 回写
- [x] `logs/eval/` 留档

---

### P0-5 数据增强与标注严格一致

**当前状态**：✅ **已完成（方案 A）**

**任务**

- [x] `rotate.py` 归档为 `rotate.py.legacy`
- [x] 唯一入口 `preprocess_dataset.py`
- [x] `verify_labels.py` 抽检

---

### P0-6 推理链路职责清晰（去重复 NMS）

**当前状态**：✅ **已完成**

**任务**

- [x] NMS 只在 `Inferencer` 内；阈值来自配置
- [x] 移除 `main.cpp` / `main_qt.cpp` 重复 NMS

---

### P0-7 模拟联机闭环

**当前状态**：✅ **已完成**

**任务**

- [x] `SERIAL_PROTOCOL.md`
- [x] 触发模式 TRIG/PASS/FAIL/DONE
- [x] FAIL 截图 + JSON；增强 CSV

---

### P0-8 演示与性能基准

**当前状态**：✅ **已完成**

**任务**

- [x] `benchmark.py` — 数据集大图 / USB / 视频三种场景
- [x] `pipeline_simulation.mp4`

---

### P0-9 总验收脚本

| 维度 | 说明 |
|------|------|
| **脚本** | `scripts/verify_p0.ps1` |
| **检查项** | 43 项 |
| **验收** | 43 PASS / 0 FAIL |

---

## P1 — 仿真环境可交付

> **目标**：零真机也能 **5 分钟跑通演示**。

### ✅ P1 已结项（2026-05-22）

P1-1～P1-8 全部完成，`verify_p1.ps1` 全 PASS。演示步骤见 [DEMO.md](DEMO.md)。

### P1-1 一键演示包

- [x] `run_demo.py`、`make_simulation_video.py`、`pipeline_simulation.mp4`
- [x] `docs/DEMO.md`

### P1-2 离线批测与可视化报告

- [x] `batch_infer.py`
- [x] `evaluate.py` → `report.html`

### P1-3 模型版本与程序绑定

- [x] `MIN_VERSION` 校验、UI 模型信息、ONNX 尺寸/类别一致性检查

### P1-4 自动化测试与 CI

- [x] `run_tests.py`（56 项）
- [x] `run_ci.bat`

### P1-5 Qt UI 为默认演示界面

- [x] `defect_detection_qt.exe` 为推荐入口
- [x] 调试页：视频回放、串口说明、保存到 `config.yaml`

### P1-6 缺陷追溯与报表

- [x] FAIL 截图叠加框 + `datetime` / `pcb_count`
- [x] `export_daily_csv()` → `logs/stats/daily_report.csv`

### P1-7 模拟 PLC 协议强化

- [x] `SerialCommState` 状态机 + Qt 通信面板
- [x] `plc_simulator.py` STAT/RST

### P1-8 部署包与依赖治理

- [x] `package_deploy.bat`、`DEPENDENCIES.md`

---

## P2 — 真产线扩展（待办）

> **不属于 v1.0 范围**；架构已预留相机抽象与串口协议。

### P2-0 工业相机与硬触发

- [ ] 海康 / Basler SDK
- [ ] 硬触发
- [ ] 光源与曝光标定文档

### P2-1 推理性能

- [ ] TensorRT / ORT TensorRT EP
- [ ] 大图滑窗动态批处理

### P2-2 工厂信息系统

- [ ] MES / 数据库
- [ ] OPC UA / Modbus TCP
- [ ] 分拣机构联动

### P2-3 运维与质量

- [ ] 看门狗、远程日志
- [ ] 权限分级、参数审计
- [ ] 漏检样本入待标注池

### P2-4 模型生命周期

- [ ] 模型 OTA / 回滚
- [ ] A/B 对比报告
- [ ] 定期重训 SOP

### P2-5 合规与知识产权

- [ ] 数据集许可说明
- [x] 第三方许可证归档 — `docs/THIRD_PARTY_LICENSES.md`；`LICENSE`（非商业 + YOLOv5 GPL-3.0）
- [ ] 出厂检验报告模板

---

## 已知差距（跟踪项）

| 项 | 现状 | 计划阶段 |
|----|------|----------|
| ~~config.yaml 未加载~~ | ✅ P0-2 | — |
| ~~类别名不统一~~ | ✅ P0-1 | — |
| ~~训练部署链~~ | ✅ P0-3 | — |
| ~~评估门槛~~ | ✅ P0-4 | — |
| ~~数据增强不同步~~ | ✅ P0-5 | — |
| ~~重复 NMS~~ | ✅ P0-6 | — |
| ~~串口触发模式~~ | ✅ P0-7 | — |
| ~~性能基准~~ | ✅ P0-8 | — |
| 工业相机 | USB OpenCV | P2-0 |
| TensorRT | 未实现 | P2-1 |

---

## 开发计划（历史进度）

### ✅ 已完成（基础能力）

- [x] 项目骨架与模块划分
- [x] 相机采集 (USB, 超时/重连/ROI/快照)
- [x] ONNX Runtime 推理（滑窗、跨窗 NMS、GPU）
- [x] 数据集预处理 (滑窗 + 正交旋转)
- [x] YOLOv5 P2 四尺度训练方案
- [x] IOU 追踪 — 连续 / 触发模式
- [x] 串口通信 (RS232/485)
- [x] 三线程流水线
- [x] Qt UI（`defect_detection_qt.exe`）

### 里程碑

- **P0 已结项**（2026-05-20）— `verify_p0.ps1` 43 PASS
- **P1 已结项**（2026-05-22）— `verify_p1.ps1` 全 PASS
- **P2** — 有硬件后按需选取
