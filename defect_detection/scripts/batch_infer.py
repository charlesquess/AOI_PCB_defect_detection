"""
PCB 缺陷检测 — 离线批量推理

对 PCB_DATASET 原始大图执行滑窗推理（模拟 C++ 流水线），
输出检测可视化、失败样例及推理报告。

用法:
  python scripts/batch_infer.py                                               # PCB_DATASET/images
  python scripts/batch_infer.py --model models/model.onnx --conf 0.5
  python scripts/batch_infer.py --val                                         # yolo_dataset/val
  python scripts/batch_infer.py --data data/yolo_dataset/images/val
  python scripts/batch_infer.py --data PCB_DATASET/images/Short --output logs/batch_infer/short_test
"""
import argparse
import json
import time
import numpy as np
from datetime import datetime
from pathlib import Path
from collections import defaultdict

try:
    import cv2
except ImportError:
    cv2 = None

try:
    import onnxruntime as ort
except ImportError:
    ort = None

CLASSES = ['missing_hole', 'mouse_bite', 'open_circuit', 'short', 'spur', 'spurious_copper']
COLORS = [
    (0, 0, 255),      # missing_hole - red
    (0, 255, 0),      # mouse_bite - green
    (255, 0, 0),      # open_circuit - blue
    (0, 255, 255),    # short - yellow
    (255, 0, 255),    # spur - magenta
    (255, 255, 0),    # spurious_copper - cyan
]

PROJECT_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_MODEL = PROJECT_ROOT / "models" / "model.onnx"
OUTPUT_DIR = PROJECT_ROOT / "logs" / "batch_infer"


def sliding_windows(w, h, win_size=640, stride=None):
    if stride is None:
        stride = win_size // 2
    if w <= win_size and h <= win_size:
        return [(0, 0)]
    wins = []
    for y in range(0, h, stride):
        for x in range(0, w, stride):
            wins.append((x, y))
    if not wins:
        wins.append((0, 0))
    return wins


def crop_pad(image_np, x, y, win_size=640):
    h, w = image_np.shape[:2]
    x1, y1 = x, y
    x2, y2 = min(x + win_size, w), min(y + win_size, h)
    cw, ch = x2 - x1, y2 - y1
    if cw <= 0 or ch <= 0:
        return None
    crop = image_np[y1:y2, x1:x2].copy()
    padded = np.full((win_size, win_size, 3), 255, dtype=np.uint8)
    padded[:ch, :cw] = crop
    return padded


def nms(boxes, iou_threshold=0.45):
    if not boxes:
        return []
    boxes = sorted(boxes, key=lambda b: b['confidence'], reverse=True)
    result = []
    suppressed = [False] * len(boxes)
    for i in range(len(boxes)):
        if suppressed[i]:
            continue
        result.append(boxes[i])
        for j in range(i + 1, len(boxes)):
            if suppressed[j] or boxes[i]['class_id'] != boxes[j]['class_id']:
                continue
            ix = max(boxes[i]['x'], boxes[j]['x'])
            iy = max(boxes[i]['y'], boxes[j]['y'])
            iw = min(boxes[i]['x'] + boxes[i]['w'], boxes[j]['x'] + boxes[j]['w']) - ix
            ih = min(boxes[i]['y'] + boxes[i]['h'], boxes[j]['y'] + boxes[j]['h']) - iy
            if iw > 0 and ih > 0:
                inter = iw * ih
                union = boxes[i]['w'] * boxes[i]['h'] + boxes[j]['w'] * boxes[j]['h'] - inter
                if union > 0 and inter / union > iou_threshold:
                    suppressed[j] = True
    return result


class BatchInfer:
    def __init__(self, model_path, conf_threshold=0.6, nms_threshold=0.45, win_size=640):
        if ort is None:
            raise RuntimeError("请安装 onnxruntime: pip install onnxruntime")
        if cv2 is None:
            raise RuntimeError("请安装 opencv-python: pip install opencv-python")

        self.session = ort.InferenceSession(str(model_path))
        shape = self.session.get_inputs()[0].shape
        self.input_w = shape[3] if len(shape) >= 4 and shape[3] > 0 else win_size
        self.input_h = shape[2] if len(shape) >= 4 and shape[2] > 0 else win_size
        self.input_name = self.session.get_inputs()[0].name
        self.output_name = self.session.get_outputs()[0].name
        self.conf_threshold = conf_threshold
        self.nms_threshold = nms_threshold
        self.input_size = win_size

        output_shape = self.session.get_outputs()[0].shape
        self.num_classes = output_shape[2] - 5 if len(output_shape) >= 3 else 6
        print(f"模型: {model_path.name}")
        print(f"  输入: {self.input_name} {shape}")
        print(f"  输出: {self.output_name} {output_shape}")
        print(f"  类别数: {self.num_classes}")
        print(f"  置信度阈值: {conf_threshold}")
        print(f"  NMS 阈值: {nms_threshold}")

    def infer_crop(self, crop_np, offset_x, offset_y):
        h, w = crop_np.shape[:2]
        if h != self.input_h or w != self.input_w:
            padded = np.full((self.input_h, self.input_w, 3), 255, dtype=np.uint8)
            padded[:min(h, self.input_h), :min(w, self.input_w)] = crop_np[:min(h, self.input_h), :min(w, self.input_w)]
            crop_np = padded

        blob = crop_np.transpose(2, 0, 1).astype(np.float32) / 255.0
        blob = blob[np.newaxis, :, :, :]

        outputs = self.session.run([self.output_name], {self.input_name: blob})
        raw = outputs[0]

        _, num, attr = raw.shape
        nc = attr - 5
        boxes = []
        for i in range(num):
            obj = float(raw[0, i, 4])
            if obj < 0.001:
                continue
            max_c = 0.0
            cid = 0
            for c in range(nc):
                s = float(raw[0, i, 5 + c])
                if s > max_c:
                    max_c = s
                    cid = c
            conf = obj * max_c
            if conf < 0.001:
                continue
            cx, cy, bw, bh = float(raw[0, i, 0]), float(raw[0, i, 1]), float(raw[0, i, 2]), float(raw[0, i, 3])
            boxes.append({
                'x': cx - bw / 2 + offset_x,
                'y': cy - bh / 2 + offset_y,
                'w': bw,
                'h': bh,
                'confidence': conf,
                'class_id': cid,
                'label': CLASSES[cid] if cid < len(CLASSES) else f"class_{cid}",
            })
        return boxes

    def infer_image(self, image):
        h, w = image.shape[:2]
        wins = sliding_windows(w, h, self.input_size)
        all_boxes = []
        for wx, wy in wins:
            crop = crop_pad(image, wx, wy, self.input_size)
            if crop is None:
                continue
            all_boxes.extend(self.infer_crop(crop, wx, wy))
        all_boxes = nms(all_boxes, self.nms_threshold)
        return [b for b in all_boxes if b['confidence'] >= self.conf_threshold]


def collect_images(data_path):
    data_path = Path(data_path)
    if not data_path.exists():
        return []
    extensions = {'.jpg', '.jpeg', '.png', '.bmp', '.tif', '.tiff'}
    images = []
    if data_path.is_dir():
        subdirs = [d for d in data_path.iterdir() if d.is_dir()]
        if subdirs:
            for subdir in subdirs:
                for f in sorted(subdir.iterdir()):
                    if f.suffix.lower() in extensions:
                        images.append(f)
        else:
            for f in sorted(data_path.iterdir()):
                if f.suffix.lower() in extensions:
                    images.append(f)
    elif data_path.is_file() and data_path.suffix.lower() in extensions:
        images = [data_path]
    return images


def draw_boxes(image, boxes):
    result = image.copy()
    for b in boxes:
        cid = b['class_id']
        color = COLORS[cid % len(COLORS)]
        x1, y1 = int(b['x']), int(b['y'])
        x2, y2 = int(b['x'] + b['w']), int(b['y'] + b['h'])
        cv2.rectangle(result, (x1, y1), (x2, y2), color, 2)
        label = f"{b['label']} {b['confidence']:.2f}"
        (tw, th), _ = cv2.getTextSize(label, cv2.FONT_HERSHEY_SIMPLEX, 0.5, 1)
        cv2.rectangle(result, (x1, y1 - th - 4), (x1 + tw + 4, y1), color, -1)
        cv2.putText(result, label, (x1 + 2, y1 - 2), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1)
    return result


def generate_report(results, output_dir, model_path, conf_threshold, nms_threshold, elapsed):
    total_images = len(results)
    total_detections = sum(r['num_detections'] for r in results)
    images_with_defects = sum(1 for r in results if r['num_detections'] > 0)
    class_counts = defaultdict(int)
    for r in results:
        for b in r['boxes']:
            class_counts[b['label']] += 1

    report_path = output_dir / "report.md"
    lines = [
        "# PCB 离线批测报告",
        "",
        f"**模型**: {model_path}",
        f"**置信度阈值**: {conf_threshold} | **NMS 阈值**: {nms_threshold}",
        f"**推理耗时**: {elapsed:.1f}s",
        f"**批测时间**: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}",
        "",
        "## 汇总",
        "",
        "| 指标 | 值 |",
        "|------|-----|",
        f"| 图片总数 | {total_images} |",
        f"| 检出缺陷图片 | {images_with_defects} |",
        f"| 总缺陷数 | {total_detections} |",
    ]
    for cls_name in CLASSES:
        lines.append(f"| {cls_name} | {class_counts.get(cls_name, 0)} |")
    lines.append("")
    lines.append("## 逐图结果")
    lines.append("")

    for r in results:
        lines.append(f"### {r['name']}")
        lines.append("")
        if 'annotated_path' in r:
            rel_path = r['annotated_path'].name
            lines.append(f"![]({rel_path})")
            lines.append("")
        lines.append(f"| 属性 | 值 |")
        lines.append(f"|------|-----|")
        lines.append(f"| 原图尺寸 | {r['width']}×{r['height']} |")
        lines.append(f"| 检出数 | {r['num_detections']} |")
        if r['num_detections'] > 0:
            for b in r['boxes']:
                lines.append(f"| {b['label']}({b['class_id']}) | x={b['x']:.0f} y={b['y']:.0f} w={b['w']:.0f} h={b['h']:.0f} conf={b['confidence']:.2f} |")
        lines.append("")

    with open(report_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    return report_path


def main():
    parser = argparse.ArgumentParser(description="PCB 缺陷检测 — 离线批量推理")
    parser.add_argument("--model", type=str, default=str(DEFAULT_MODEL), help="ONNX 模型路径")
    parser.add_argument("--data", type=str, default="", help="图片目录 (默认 PCB_DATASET/images)")
    parser.add_argument("--conf", type=float, default=0.6, help="置信度阈值 (默认 0.6)")
    parser.add_argument("--nms", type=float, default=0.45, help="NMS IoU 阈值 (默认 0.45)")
    parser.add_argument("--val", action="store_true", help="使用 yolo_dataset/val 集替代 PCB_DATASET")
    parser.add_argument("--output", type=str, default="", help="输出目录 (默认 logs/batch_infer/{timestamp})")
    parser.add_argument("--no-draw", action="store_false", dest="draw", help="不保存可视化图片")
    args = parser.parse_args()

    model_path = Path(args.model)
    if not model_path.exists():
        print(f"错误: 模型不存在: {model_path}")
        return 1

    if args.val:
        data_path = PROJECT_ROOT / "data" / "yolo_dataset" / "images" / "val"
    elif args.data:
        data_path = Path(args.data)
        if not data_path.is_absolute():
            data_path = PROJECT_ROOT / args.data
    else:
        data_path = PROJECT_ROOT.parent / "PCB_DATASET" / "images"

    if not data_path.exists():
        print(f"错误: 数据目录不存在: {data_path}")
        return 1

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    output_dir = Path(args.output) if args.output else OUTPUT_DIR / timestamp
    output_dir.mkdir(parents=True, exist_ok=True)

    images = collect_images(data_path)
    if not images:
        print(f"错误: 未找到图片: {data_path}")
        return 1
    print(f"找到 {len(images)} 张图片")
    print(f"输出目录: {output_dir}")

    infer = BatchInfer(model_path, args.conf, args.nms)

    results = []
    start = time.time()
    for idx, img_path in enumerate(images):
        rel = img_path.relative_to(data_path.parent if data_path.name.lower() in ('images', 'val', 'train') else data_path)
        print(f"[{idx+1}/{len(images)}] {rel}...", end=" ", flush=True)

        img = cv2.imread(str(img_path))
        if img is None:
            print("跳过(无法读取)")
            continue

        h, w = img.shape[:2]
        boxes = infer.infer_image(img)

        entry = {
            'name': str(rel.as_posix()),
            'path': str(img_path),
            'width': w,
            'height': h,
            'num_detections': len(boxes),
            'boxes': boxes,
        }

        if args.draw:
            annotated = draw_boxes(img, boxes)
            annotated_name = f"{idx+1:04d}_{img_path.stem}_annotated.jpg"
            annotated_path = output_dir / annotated_name
            cv2.imwrite(str(annotated_path), annotated, [cv2.IMWRITE_JPEG_QUALITY, 90])
            entry['annotated_path'] = annotated_path

        results.append(entry)
        print(f"{len(boxes)} 个检出")

    elapsed = time.time() - start
    print(f"\n推理完成: {len(results)} 张, 耗时 {elapsed:.1f}s")

    report_path = generate_report(results, output_dir, model_path.name, args.conf, args.nms, elapsed)
    print(f"报告: {report_path}")

    json_results = []
    for r in results:
        jr = {k: v for k, v in r.items() if k not in ('boxes', 'annotated_path')}
        jr['annotated'] = str(r.get('annotated_path', ''))
        jr['boxes'] = [
            {'x': round(b['x'], 1), 'y': round(b['y'], 1),
             'w': round(b['w'], 1), 'h': round(b['h'], 1),
             'confidence': round(b['confidence'], 3),
             'class_id': b['class_id'], 'label': b['label']}
            for b in r['boxes']
        ]
        json_results.append(jr)

    json_path = output_dir / "results.json"
    with open(json_path, "w", encoding="utf-8") as f:
        json.dump({
            'timestamp': timestamp,
            'model': str(model_path),
            'conf_threshold': args.conf,
            'nms_threshold': args.nms,
            'total_images': len(results),
            'total_defects': sum(r['num_detections'] for r in results),
            'results': json_results,
        }, f, indent=2, ensure_ascii=False)
    print(f"JSON: {json_path}")

    # Print summary
    defects = sum(r['num_detections'] for r in results)
    print(f"\n汇总: {len(results)} 张图片, 检出 {defects} 个缺陷")
    return 0


if __name__ == "__main__":
    exit(main())
