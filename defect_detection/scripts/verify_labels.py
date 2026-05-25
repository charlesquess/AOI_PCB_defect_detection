"""
标签检查 — 从验证集随机抽 10 张图，叠加标注框并保存到 logs/label_check/

用法:
  python scripts/verify_labels.py
  python scripts/verify_labels.py --num 20
"""
import argparse
import random
from pathlib import Path

try:
    from PIL import Image, ImageDraw
except ImportError:
    Image = None

PROJECT_ROOT = Path(__file__).resolve().parent.parent
VAL_IMG_DIR = PROJECT_ROOT / "data" / "yolo_dataset" / "images" / "val"
VAL_LBL_DIR = PROJECT_ROOT / "data" / "yolo_dataset" / "labels" / "val"
OUTPUT_DIR = PROJECT_ROOT / "logs" / "label_check"

CLASSES = ["missing_hole", "mouse_bite", "open_circuit", "short", "spur", "spurious_copper"]
COLORS = [(255, 80, 80), (80, 200, 80), (80, 80, 255), (255, 200, 80), (200, 80, 255), (80, 200, 200)]


def draw_yolo_label(img: Image.Image, label_path: Path, class_id: int, cx, cy, w, h):
    """在 PIL Image 上绘制 YOLO 格式标注框"""
    draw = ImageDraw.Draw(img)
    iw, ih = img.size
    x1 = (cx - w / 2) * iw
    y1 = (cy - h / 2) * ih
    x2 = (cx + w / 2) * iw
    y2 = (cy + h / 2) * ih
    color = COLORS[class_id % len(COLORS)]
    draw.rectangle([x1, y1, x2, y2], outline=color, width=2)
    label = f"{CLASSES[class_id]}"
    # 文字背景
    bbox = draw.textbbox((x1, y1 - 14), label)
    draw.rectangle(bbox, fill=color)
    draw.text((x1, y1 - 14), label, fill="white")


def main():
    parser = argparse.ArgumentParser(description="验证集标签对齐检查")
    parser.add_argument("--num", type=int, default=10, help="抽取图片数")
    args = parser.parse_args()

    if Image is None:
        print("错误: 请安装 Pillow: pip install Pillow")
        return 1

    images = sorted(VAL_IMG_DIR.glob("*.jpg"))
    if not images:
        print(f"错误: 验证集图片不存在: {VAL_IMG_DIR}")
        return 1

    random.seed(42)
    samples = random.sample(images, min(args.num, len(images)))
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    print(f"抽取 {len(samples)} 张验证集图片进行标签检查...")
    errors = 0
    for img_path in samples:
        label_path = VAL_LBL_DIR / (img_path.stem + ".txt")
        if not label_path.exists():
            print(f"  [WARN] 无标签: {img_path.name}")
            errors += 1
            continue

        img = Image.open(img_path).convert("RGB")
        with open(label_path) as f:
            for line in f:
                parts = line.strip().split()
                if len(parts) != 5:
                    continue
                cls, cx, cy, w, h = int(parts[0]), float(parts[1]), float(parts[2]), float(parts[3]), float(parts[4])
                if cls < 0 or cls >= len(CLASSES):
                    print(f"  [WARN] 越界 class_id={cls}: {img_path.name}")
                    errors += 1
                    continue
                if w <= 0 or h <= 0 or w > 1 or h > 1:
                    print(f"  [WARN] 异常坐标 w={w:.3f} h={h:.3f}: {img_path.name}")
                    errors += 1
                    continue
                draw_yolo_label(img, label_path, cls, cx, cy, w, h)

        out_path = OUTPUT_DIR / img_path.name
        img.save(out_path)
        print(f"  [OK] {img_path.name} -> {out_path}")

    print(f"\n完成! {len(samples)} 张已保存到 {OUTPUT_DIR}")
    if errors:
        print(f"发现 {errors} 个问题，请检查上述 [WARN] 项")
    else:
        print("全部标签正常 [PASS]")
    return 0 if errors == 0 else 1


if __name__ == "__main__":
    exit(main())
