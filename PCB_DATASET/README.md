# PCB 缺陷数据集（本地目录）

本目录用于存放 **原始 PCB 缺陷检测数据集**，采用 Pascal VOC 格式（`images/` + `Annotations/`）。

> **为何仓库里没有数据文件？**  
> 完整数据集体积较大（数百张高分辨率 PCB 大图 + XML 标注），不适合放入 Git；且数据著作权归 [Kaggle 数据集提供方](https://www.kaggle.com/datasets/akhatova/pcb-defects) 所有。克隆本仓库后，请按下方步骤**自行下载**并解压到本目录。

本仓库仅跟踪 **`README.md`** 与 **`.gitignore`**，不会包含任何 `.jpg` / `.xml` 等大文件。

---

## 数据来源

| 项 | 说明 |
|----|------|
| **名称** | PCB Defects |
| **平台** | [Kaggle — akhatova/pcb-defects](https://www.kaggle.com/datasets/akhatova/pcb-defects) |
| **格式** | Pascal VOC（按缺陷类别分子目录） |
| **用途** | 训练 YOLOv5、离线评测、`batch_infer` / `benchmark` 等 |

下载入口（需 Kaggle 账号）：

**https://www.kaggle.com/datasets/akhatova/pcb-defects**

---

## 下载与放置

### 方式一：网页下载（推荐首次使用）

1. 打开上述 Kaggle 页面，登录后点击 **Download**。
2. 解压压缩包，得到含 `images/`、`Annotations/` 的目录树。
3. 将 **`images` 与 `Annotations` 两个文件夹** 放到本仓库的 `PCB_DATASET/` 下（与本文同级），最终路径形如：

   ```
   AOI_PCB_defect_detection/PCB_DATASET/images/...
   AOI_PCB_defect_detection/PCB_DATASET/Annotations/...
   ```

### 方式二：Kaggle CLI

```bash
# 安装并配置 API Token：https://www.kaggle.com/docs/api
pip install kaggle

# 在仓库根目录执行（下载到当前目录后需移动到 PCB_DATASET）
kaggle datasets download -d akhatova/pcb-defects
unzip pcb-defects.zip -d PCB_DATASET
```

Windows PowerShell 解压示例：

```powershell
cd C:\Users\29814\AOI_PCB_defect_detection
kaggle datasets download -d akhatova/pcb-defects -p PCB_DATASET
Expand-Archive -Path PCB_DATASET\pcb-defects.zip -DestinationPath PCB_DATASET
# 若 zip 内多一层目录，把 images/、Annotations/ 挪到 PCB_DATASET 根下
```

---

## 目录结构（解压后应满足）

`defect_detection/scripts/preprocess_dataset.py` 会扫描 **`Annotations/**/*.xml`**，并按相同相对路径在 **`images/<类别>/<stem>.jpg`** 查找对应图片：

```
PCB_DATASET/
├── README.md                 # 本说明（已在 Git 中）
├── .gitignore                # 忽略本地数据，防止误提交
├── images/                   # ← 需自行下载
│   ├── Missing_hole/
│   │   └── 01_missing_hole_*.jpg
│   ├── Mouse_bite/
│   ├── Open_circuit/
│   ├── Short/
│   ├── Spur/
│   └── Spurious_copper/
└── Annotations/              # ← 需自行下载
    ├── Missing_hole/
    │   └── 01_missing_hole_*.xml
    ├── Mouse_bite/
    ├── Open_circuit/
    ├── Short/
    ├── Spur/
    └── Spurious_copper/
```

> 子目录名称以 Kaggle 压缩包为准；只要 **`Annotations` 与 `images` 的相对路径一一对应**（同名 `.xml` / `.jpg`），预处理脚本即可运行。

### 可选：`PCB_USED/`

部分脚本（如 `defect_detection/scripts/benchmark.py`）会读取 `PCB_DATASET/PCB_USED/` 下的大图做性能测试。该目录**不是训练必需**，可按本地需要自行整理放入。

---

## 放置完成后自检

在仓库根目录执行：

```powershell
# 两个根目录存在
Test-Path PCB_DATASET\images
Test-Path PCB_DATASET\Annotations

# 应有大量成对样本（数量因 Kaggle 版本略有差异，通常数百对）
(Get-ChildItem PCB_DATASET\Annotations -Recurse -Filter *.xml).Count
(Get-ChildItem PCB_DATASET\images -Recurse -Filter *.jpg).Count
```

若 XML 数量 > 0 且与 JPG 数量接近，即可进行下一步预处理。

---

## 预处理为 YOLO 训练集

原始大图约 **3034×1586**，本项目采用 **640×640 滑窗**（步长 320，50% 重叠），训练集另做 **0°/90°/180°/270°** 正交增强：

```bash
# 建议在 yolov5 conda 环境中运行（需 opencv-python）
conda run -n yolov5 python defect_detection/scripts/preprocess_dataset.py
```

输出目录（已在 Git 忽略或需本地生成）：

```
defect_detection/data/yolo_dataset/
├── images/train|val/
├── labels/train|val/
└── dataset.yaml          # 供 yolov5-7.0/train.py 使用
```

训练命令见仓库根 [README.md](../README.md) 或 [defect_detection/README.md](../defect_detection/README.md)。

---

## 缺陷类别（6 类）

与全项目 **类别 ID** 一致（`preprocess_dataset.py` / `dataset.yaml` / 推理配置）：

| ID | 工程内名称 | 数据集 XML 常见标签 |
|:--:|------------|---------------------|
| 0 | `missing_hole` | Missing Hole |
| 1 | `mouse_bite` | Mouse Bite |
| 2 | `open_circuit` | Open Circuit |
| 3 | `short` | Short |
| 4 | `spur` | Spur |
| 5 | `spurious_copper` | Spurious Copper |

---

## 在本项目中的使用场景

| 场景 | 相关脚本 / 文档 |
|------|-----------------|
| 生成 YOLO 切片 | `defect_detection/scripts/preprocess_dataset.py` |
| 离线大图滑窗推理 | `defect_detection/scripts/batch_infer.py` |
| 性能基准 | `defect_detection/scripts/benchmark.py` |
| 仿真视频素材 | `defect_detection/scripts/make_simulation_video.py` |
| 验证集指标 | `defect_detection/scripts/evaluate.py` |

---

## 许可与引用

- 数据集的使用、再分发与商用须遵守 **Kaggle 及数据集作者** 的条款；本仓库 [defect_detection/LICENSE](../defect_detection/LICENSE) **不**授予数据集本身的权利。
- 在论文、报告或 README 中引用时，请注明数据来源：**Kaggle — [PCB Defects (akhatova)](https://www.kaggle.com/datasets/akhatova/pcb-defects)**。

---

## 常见问题

**Q：克隆后只有本 README，没有图片？**  
A：正常。请按上文从 Kaggle 下载并解压到 `PCB_DATASET/`。

**Q：预处理报「找不到 xml」或图片为 0？**  
A：检查 `images` 与 `Annotations` 是否都在 `PCB_DATASET` 根下，且子目录结构一致；不要只解压到深层嵌套目录而未上移。

**Q：能否把数据集提交到 Git？**  
A：不建议。体积大、易触发平台限制，且可能违反数据许可。请仅在本地或私有存储中保留，通过本 README 说明获取方式即可。
