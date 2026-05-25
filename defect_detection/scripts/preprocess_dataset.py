import random
import xml.etree.ElementTree as ET
from pathlib import Path
import cv2
import numpy as np

# ── 配置 ───────────────────────────────────────────────────────
PCB_ROOT = Path(__file__).resolve().parent.parent.parent / "PCB_DATASET"
OUT_ROOT = Path(__file__).resolve().parent.parent / "data" / "yolo_dataset"

CROP_SIZE = 640            
STRIDE = 320               
VAL_SPLIT = 0.15
RANDOM_SEED = 42

ENABLE_ORTHOGONAL_AUG = True 

CLASSES = [
    "missing_hole", "mouse_bite", "open_circuit",
    "short", "spur", "spurious_copper",
]
CLASS_TO_ID = {n: i for i, n in enumerate(CLASSES)}


def parse_voc_xml(xml_path: Path):
    tree = ET.parse(xml_path)
    root = tree.getroot()
    size = root.find("size")
    img_w = int(size.find("width").text)
    img_h = int(size.find("height").text)
    objects = []
    for obj in root.iter("object"):
        name = obj.find("name").text.lower()
        if name not in CLASS_TO_ID:
            continue
        bbox = obj.find("bndbox")
        objects.append({
            "class_id": CLASS_TO_ID[name],
            "xmin": int(float(bbox.find("xmin").text)),
            "ymin": int(float(bbox.find("ymin").text)),
            "xmax": int(float(bbox.find("xmax").text)),
            "ymax": int(float(bbox.find("ymax").text)),
        })
    return img_w, img_h, objects


def sliding_windows_fixed(img_w, img_h):
    windows = []
    y = 0
    while y < img_h:
        x = 0
        while x < img_w:
            windows.append((x, y))
            x += STRIDE
        y += STRIDE
    return windows


def crop_and_pad(img, win_x, win_y):
    h, w = img.shape[:2]
    x1, y1 = win_x, win_y
    x2, y2 = min(win_x + CROP_SIZE, w), min(win_y + CROP_SIZE, h)
    crop = img[y1:y2, x1:x2]
    actual_h, actual_w = crop.shape[:2]
    if actual_h < CROP_SIZE or actual_w < CROP_SIZE:
        pad_b = CROP_SIZE - actual_h
        pad_r = CROP_SIZE - actual_w
        padded = cv2.copyMakeBorder(crop, 0, pad_b, 0, pad_r, cv2.BORDER_REFLECT)
        return padded, actual_h, actual_w
    return crop, actual_h, actual_w


def crop_object_in_window(obj, win_x, win_y, crop_h, crop_w):
    xmin = max(obj["xmin"], win_x)
    ymin = max(obj["ymin"], win_y)
    xmax = min(obj["xmax"], win_x + crop_w)
    ymax = min(obj["ymax"], win_y + crop_h)
    if xmin >= xmax or ymin >= ymax:
        return None
    orig_area = (obj["xmax"] - obj["xmin"]) * (obj["ymax"] - obj["ymin"])
    new_area = (xmax - xmin) * (ymax - ymin)
    if orig_area > 0 and new_area / orig_area < 0.7:
        return None
    return {
        "class_id": obj["class_id"],
        "xmin": xmin - win_x,
        "ymin": ymin - win_y,
        "xmax": xmax - win_x,
        "ymax": ymax - win_y,
    }


def rotate_orthogonal(img, boxes, angle_code):
    if angle_code == 0:
        return img, boxes
    cv2_rot_map = {1: cv2.ROTATE_90_CLOCKWISE, 2: cv2.ROTATE_180, 3: cv2.ROTATE_90_COUNTERCLOCKWISE}
    rot_img = cv2.rotate(img, cv2_rot_map[angle_code])
    h, w = img.shape[:2]
    new_boxes = []
    for b in boxes:
        x1, y1, x2, y2 = b["xmin"], b["ymin"], b["xmax"], b["ymax"]
        if angle_code == 1:
            nx1, ny1, nx2, ny2 = h - y2, x1, h - y1, x2
        elif angle_code == 2:
            nx1, ny1, nx2, ny2 = w - x2, h - y2, w - x1, h - y1
        else:
            nx1, ny1, nx2, ny2 = y1, w - x2, y2, w - x1
        new_boxes.append({
            "class_id": b["class_id"],
            "xmin": nx1, "ymin": ny1, "xmax": nx2, "ymax": ny2
        })
    return rot_img, new_boxes


def to_yolo(rel_obj, img_size):
    cx = (rel_obj["xmin"] + rel_obj["xmax"]) / 2.0 / img_size
    cy = (rel_obj["ymin"] + rel_obj["ymax"]) / 2.0 / img_size
    w_ = (rel_obj["xmax"] - rel_obj["xmin"]) / img_size
    h_ = (rel_obj["ymax"] - rel_obj["ymin"]) / img_size
    return f"{rel_obj['class_id']} {cx:.6f} {cy:.6f} {w_:.6f} {h_:.6f}"


def main():
    random.seed(RANDOM_SEED)
    pairs = []
    for xml_path in sorted((PCB_ROOT / "Annotations").rglob("*.xml")):
        rel = xml_path.relative_to(PCB_ROOT / "Annotations").parts
        img_path = PCB_ROOT / "images" / rel[0] / (xml_path.stem + ".jpg")
        if img_path.exists():
            pairs.append((img_path, xml_path))

    dirs = {}
    for d in ("images", "labels"):
        for s in ("train", "val"):
            p = OUT_ROOT / d / s
            p.mkdir(parents=True, exist_ok=True)
            dirs[f"{d}/{s}"] = p

    total_all = 0
    total_rot = 0

    for img_path, xml_path in pairs:
        img_w, img_h, objects = parse_voc_xml(xml_path)
        windows = sliding_windows_fixed(img_w, img_h)
        img = cv2.imread(str(img_path))

        for (win_x, win_y) in windows:
            crop, ch, cw = crop_and_pad(img, win_x, win_y)
            local_objs = []
            for obj in objects:
                loc = crop_object_in_window(obj, win_x, win_y, ch, cw)
                if loc is not None:
                    local_objs.append(loc)
            if not local_objs:
                continue

            base_stem = f"{img_path.stem}_x{win_x}y{win_y}"
            split = "val" if random.random() < VAL_SPLIT else "train"
            angles = [0, 1, 2, 3] if (ENABLE_ORTHOGONAL_AUG and split == "train") else [0]

            for angle in angles:
                aug_crop, aug_objs = rotate_orthogonal(crop, local_objs, angle)
                stem = f"{base_stem}_rot{angle*90}"
                total_all += 1
                if angle > 0:
                    total_rot += 1

                cv2.imwrite(str(dirs[f"images/{split}"] / f"{stem}.jpg"), aug_crop)
                label_lines = [to_yolo(o, CROP_SIZE) for o in aug_objs]
                (dirs[f"labels/{split}"] / f"{stem}.txt").write_text("\n".join(label_lines))

    nc = len(CLASSES)
    yaml_path = OUT_ROOT / "dataset.yaml"
    yaml_content = (
        f"train: {(OUT_ROOT / 'images' / 'train').as_posix()}\n"
        f"val:   {(OUT_ROOT / 'images' / 'val').as_posix()}\n"
        f"\nnc: {nc}\nnames:\n"
    )
    for i, name in enumerate(CLASSES):
        yaml_content += f"  {i}: {name}\n"
    yaml_path.write_text(yaml_content)

    print(f"\n{'='*55}")
    print(f"  裁剪尺寸     : {CROP_SIZE}x{CROP_SIZE}")
    print(f"  固定步长     : {STRIDE} (50% 重叠)")
    print(f"  边界填充     : BORDER_REFLECT (消除黑边伪影)")
    print(f"  截断过滤     : 保留面积 < 70% 的框丢弃")
    print(f"  正交旋转     : 训练集 4 角度 (0/90/180/270)")
    print(f"  输出总数     : {total_all} (其中旋转: {total_rot})")
    print(f"  输出目录     : {OUT_ROOT}")
    print(f"{'='*55}")


if __name__ == "__main__":
    main()
