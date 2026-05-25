"""
YOLOv5 训练完成后导出 ONNX + 复制到 C++ 项目 + 生成 manifest.json

流程:
  cd yolov5-7.0
  python train.py --img 640 --batch 16 --epochs 100 ...
  python export.py --weights runs/train/exp/weights/best.pt --include onnx --img 640
  python ../defect_detection/scripts/export_onnx.py
"""
import json
import shutil
from datetime import date
from pathlib import Path

# 默认路径
YOLOV5_DIR = Path(__file__).resolve().parent.parent.parent / "yolov5-7.0"
MODEL_DIR  = Path(__file__).resolve().parent.parent / "models"
MODEL_DST  = MODEL_DIR / "model.onnx"
MANIFEST   = MODEL_DIR / "manifest.json"

CLASSES = [
    "missing_hole", "mouse_bite", "open_circuit",
    "short", "spur", "spurious_copper",
]

# 搜索最佳 ONNX 模型
onnx_candidates = sorted(YOLOV5_DIR.glob("runs/train/*/weights/best.onnx"))

if not onnx_candidates:
    print("未找到导出的 ONNX 模型。请先运行:")
    print(f"  cd {YOLOV5_DIR}")
    print("  python export.py --weights runs/train/exp/weights/best.pt --include onnx --img 640")
    exit(1)

src = onnx_candidates[-1]  # 最新的

# 从训练目录名提取日期（runs/train/exp/ 或 runs/train/exp17/）
train_dir = src.parent.parent
train_date = date.fromtimestamp(train_dir.stat().st_mtime).isoformat()

print(f"源文件: {src}  ({src.stat().st_size / 1024:.0f} KB)")
print(f"目标:   {MODEL_DST}")

MODEL_DIR.mkdir(parents=True, exist_ok=True)
shutil.copy2(src, MODEL_DST)
print(f"已复制到: {MODEL_DST}")

# 生成 manifest.json
manifest = {
    "model_version": "1.0.0",
    "training_date": train_date,
    "description": "YOLOv5s P2 PCB defect detection",
    "input_width": 640,
    "input_height": 640,
    "num_classes": len(CLASSES),
    "class_names": CLASSES,
    "mAP": None,
    "framework": "YOLOv5",
    "runtime": "ONNX Runtime",
}
with open(MANIFEST, "w", encoding="utf-8") as f:
    json.dump(manifest, f, indent=2, ensure_ascii=False)
print(f"已生成:  {MANIFEST}")
print("\nC++ 程序将自动加载 models/model.onnx")
print("提示: 运行 val.py 后可将 mAP 填入 manifest.json")
