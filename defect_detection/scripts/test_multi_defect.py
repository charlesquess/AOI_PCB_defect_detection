"""
多缺陷合成测试 — 将不同类的缺陷拼到一张 PCB 模板上
用于验证模型能否同时检出多种缺陷

用法:
  python scripts/test_multi_defect.py --weights runs/train/exp/weights/best.pt
"""
import argparse
import random
from pathlib import Path

import cv2
import numpy as np
import torch

PCB_DATASET = Path(__file__).resolve().parent.parent.parent / "PCB_DATASET"
YOLOV5_DIR = Path(__file__).resolve().parent.parent.parent / "yolov5-7.0"

CLASSES = ["missing_hole", "mouse_bite", "open_circuit", "short", "spur", "spurious_copper"]
COLORS = [(255,0,0), (0,255,0), (0,0,255), (255,255,0), (255,0,255), (0,255,255)]


def load_model(weights_path):
    """加载 YOLOv5 模型"""
    import sys
    sys.path.insert(0, str(YOLOV5_DIR))
    from models.common import DetectMultiBackend
    from utils.augmentations import letterbox
    from utils.general import non_max_suppression, scale_boxes

    device = torch.device("cuda:0" if torch.cuda.is_available() else "cpu")
    model = DetectMultiBackend(weights_path, device=device, dnn=False)
    model.eval()
    return model, device


def pick_random_defect():
    """从 PCB_DATASET 随机挑一张带标注的缺陷图"""
    classes_dir = PCB_DATASET / "images"
    annos_dir = PCB_DATASET / "Annotations"

    cls_name = random.choice(CLASSES)
    cls_dir = classes_dir / cls_name
    anno_dir = annos_dir / cls_name

    imgs = list(cls_dir.glob("*.jpg"))
    if not imgs:
        return None, None, None
    img_path = random.choice(imgs)
    anno_path = anno_dir / (img_path.stem + ".xml")
    return img_path, anno_path, cls_name


def extract_defect_patch(img_path, anno_path):
    """从图中裁剪出缺陷区域，返回 (patch, class_name)"""
    import xml.etree.ElementTree as ET
    img = cv2.imread(str(img_path))
    tree = ET.parse(anno_path)
    root = tree.getroot()

    patches = []
    for obj in root.iter("object"):
        name = obj.find("name").text.lower()
        bbox = obj.find("bndbox")
        xmin = int(float(bbox.find("xmin").text))
        ymin = int(float(bbox.find("ymin").text))
        xmax = int(float(bbox.find("xmax").text))
        ymax = int(float(bbox.find("ymax").text))
        patch = img[ymin:ymax, xmin:xmax]
        if patch.size > 0:
            patches.append((patch, name, (xmin, ymin, xmax, ymax)))
    return patches


def compose_pcb_board(defect_patches, board_size=(1586, 3034)):
    """将多个缺陷贴到黑色底板上"""
    board = np.full((board_size[0], board_size[1], 3), 128, dtype=np.uint8)
    placed = []
    for patch, cls_name, _ in defect_patches:
        ph, pw = patch.shape[:2]
        max_x = board_size[1] - pw
        max_y = board_size[0] - ph
        if max_x <= 0 or max_y <= 0:
            continue
        for _ in range(50):
            x = random.randint(0, max_x)
            y = random.randint(0, max_y)
            # 避免重叠
            overlap = False
            for (px, py, pw2, ph2) in placed:
                if (x < px + pw2 and x + pw > px and y < py + ph2 and y + ph > py):
                    overlap = True
                    break
            if not overlap:
                board[y:y+ph, x:x+pw] = patch
                placed.append((x, y, pw, ph, cls_name))
                break
    return board, placed


def detect(model, device, img):
    """对图片做 YOLOv5 推理"""
    import sys
    sys.path.insert(0, str(YOLOV5_DIR))
    from utils.augmentations import letterbox
    from utils.general import non_max_suppression, scale_boxes

    # 预处理
    img_resized = letterbox(img, 640, stride=32, auto=True)[0]
    img_in = img_resized.transpose((2, 0, 1))[::-1]  # BGR→RGB, HWC→CHW
    img_in = np.ascontiguousarray(img_in)
    img_tensor = torch.from_numpy(img_in).to(device).float() / 255.0
    img_tensor = img_tensor.unsqueeze(0)

    # 推理
    pred = model(img_tensor, augment=False, visualize=False)
    pred = non_max_suppression(pred, conf_thres=0.25, iou_thres=0.45, max_det=100)[0]

    # 框坐标映射回原图
    results = []
    if pred is not None and len(pred):
        pred[:, :4] = scale_boxes(img_resized.shape, pred[:, :4], img.shape).round()
        for det in pred:
            x1, y1, x2, y2, conf, cls_id = det.tolist()
            results.append({
                "class": CLASSES[int(cls_id)],
                "conf": conf,
                "bbox": (int(x1), int(y1), int(x2), int(y2)),
            })
    return results


def draw_results(img, placed, detections):
    """绘制真实标注和检测结果"""
    for x, y, pw, ph, cls_name in placed:
        cv2.rectangle(img, (x, y), (x + pw, y + ph), (0, 255, 0), 2)
        label = f"GT:{cls_name}"
        cv2.putText(img, label, (x, y - 5), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 1)

    for det in detections:
        x1, y1, x2, y2 = det["bbox"]
        cls_id = CLASSES.index(det["class"])
        cv2.rectangle(img, (x1, y1), (x2, y2), COLORS[cls_id], 2)
        label = f"{det['class']} {det['conf']:.2f}"
        cv2.putText(img, label, (x1, y2 + 15), cv2.FONT_HERSHEY_SIMPLEX, 0.5, COLORS[cls_id], 1)

    return img


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--weights", type=str, required=True, help="模型权重路径")
    parser.add_argument("--num_defects", type=int, default=6, help="合成缺陷数")
    args = parser.parse_args()

    print("加载模型...")
    model, device = load_model(args.weights)

    print("随机采样缺陷...")
    patches = []
    used_classes = set()
    for _ in range(args.num_defects * 2):
        img_path, anno_path, cls_name = pick_random_defect()
        if img_path is None:
            continue
        found = extract_defect_patch(img_path, anno_path)
        for patch, name, bbox in found:
            if name not in used_classes or len(used_classes) >= 6:
                patches.append((patch, name, bbox))
                used_classes.add(name)
        if len(patches) >= args.num_defects:
            break

    print(f"合成底板 (已选类别: {used_classes})...")
    board, placed = compose_pcb_board(patches)

    print("推理...")
    results = detect(model, device, board)

    # 统计
    gt_classes = set(c for _, _, _, _, c in placed)
    det_classes = set(r["class"] for r in results)
    correct = gt_classes & det_classes
    missed = gt_classes - det_classes

    print(f"\n=== 结果 ===")
    print(f"  真实缺陷类别: {gt_classes}")
    print(f"  检测到类别:   {det_classes}")
    print(f"  正确检出:     {correct}")
    print(f"  漏检:         {missed}")
    print(f"  总检测框数:   {len(results)}")

    # 可视化
    vis = draw_results(board, placed, results)
    cv2.imwrite("multi_defect_test.jpg", vis)
    print(f"\n  结果图已保存: multi_defect_test.jpg")


if __name__ == "__main__":
    main()
