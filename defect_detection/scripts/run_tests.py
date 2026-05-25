"""
PCB 缺陷检测 — 自动化测试

Unit test: NMS, IoU, 坐标变换
Smoke test: ONNX 加载、空图/全黑图推理
Config test: config.yaml 字段完整性

用法:
  python scripts/run_tests.py                         # 全部测试
  python scripts/run_tests.py --unit                   # 仅单元测试
  python scripts/run_tests.py --smoke                  # 仅烟雾测试
  python scripts/run_tests.py --config                 # 仅配置测试
  python scripts/run_tests.py --verbose                # 详细输出
"""
import argparse
import sys
import time
import math
from pathlib import Path
from collections import defaultdict

PROJECT_ROOT = Path(__file__).resolve().parent.parent

PASS = 0
FAIL = 0
SKIP = 0


def assert_eq(a, b, msg=""):
    global PASS, FAIL
    if a == b:
        PASS += 1
        return True
    FAIL += 1
    print(f"  ❌ FAIL: {msg} — expected {b!r}, got {a!r}")
    return False


def assert_almost_eq(a, b, eps=1e-5, msg=""):
    global PASS, FAIL
    if abs(a - b) < eps:
        PASS += 1
        return True
    FAIL += 1
    print(f"  ❌ FAIL: {msg} — expected {b}, got {a}")
    return False


def assert_true(v, msg=""):
    global PASS, FAIL
    if v:
        PASS += 1
        return True
    FAIL += 1
    print(f"  ❌ FAIL: {msg}")
    return False


# ═══════════════════════════════════════════════
#  Unit Tests: IoU
# ═══════════════════════════════════════════════

def iou(a, b):
    """Same logic as inferencer.cpp Inferencer::Impl::iou()"""
    ix = max(a['x'], b['x'])
    iy = max(a['y'], b['y'])
    iw = min(a['x'] + a['w'], b['x'] + b['w']) - ix
    ih = min(a['y'] + a['h'], b['y'] + b['h']) - iy
    if iw <= 0 or ih <= 0:
        return 0.0
    inter = iw * ih
    return inter / (a['w'] * a['h'] + b['w'] * b['h'] - inter)


def nms(boxes, iou_threshold=0.45):
    """Same logic as inferencer.cpp global_nms()"""
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
            if iou(boxes[i], boxes[j]) > iou_threshold:
                suppressed[j] = True
    return result


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


def test_iou():
    print("\n  ── IoU ──")
    # Non-overlapping
    a = {'x': 0, 'y': 0, 'w': 10, 'h': 10}
    b = {'x': 100, 'y': 100, 'w': 10, 'h': 10}
    assert_eq(iou(a, b), 0.0, "non-overlapping")
    # Identical
    assert_eq(iou(a, a), 1.0, "identical")
    # Half overlap
    c = {'x': 5, 'y': 0, 'w': 10, 'h': 10}
    expected = (5 * 10) / (10 * 10 + 10 * 10 - 5 * 10)
    assert_almost_eq(iou(a, c), expected, msg="half overlap")
    # Touching edges
    d = {'x': 10, 'y': 0, 'w': 10, 'h': 10}
    assert_eq(iou(a, d), 0.0, "touching edge")


def test_nms():
    print("\n  ── NMS ──")
    boxes = [
        {'x': 0, 'y': 0, 'w': 10, 'h': 10, 'confidence': 0.9, 'class_id': 0},
        {'x': 1, 'y': 1, 'w': 9, 'h': 9, 'confidence': 0.8, 'class_id': 0},
        {'x': 50, 'y': 50, 'w': 10, 'h': 10, 'confidence': 0.7, 'class_id': 1},
        {'x': 51, 'y': 51, 'w': 9, 'h': 9, 'confidence': 0.6, 'class_id': 1},
    ]
    result = nms(boxes, 0.45)
    assert_eq(len(result), 2, "NMS removes duplicates")
    assert_eq(result[0]['class_id'], 0, "NMS keeps highest confidence of class 0")
    assert_eq(result[1]['class_id'], 1, "NMS keeps highest confidence of class 1")
    # Empty
    assert_eq(len(nms([], 0.45)), 0, "NMS with empty input")
    # Single box
    single = [{'x': 0, 'y': 0, 'w': 10, 'h': 10, 'confidence': 0.9, 'class_id': 0}]
    assert_eq(len(nms(single, 0.45)), 1, "NMS with single box")


def test_sliding_windows():
    print("\n  ── Sliding Windows ──")
    # Small image fits in one window
    assert_eq(sliding_windows(320, 240), [(0, 0)], "small image")
    # Large image generates multiple windows
    wins = sliding_windows(1600, 1200, 640, 320)
    assert_true(len(wins) > 1, "large image generates multiple windows")
    # All coordinates are non-negative
    for x, y in wins:
        assert_true(x >= 0 and y >= 0, f"negative coordinate: ({x}, {y})")
    # First window at origin
    assert_eq(wins[0], (0, 0), "first window at origin")


# ═══════════════════════════════════════════════
#  Smoke Tests
# ═══════════════════════════════════════════════

def test_model_load():
    print("\n  ── ONNX Model Load ──")
    import onnxruntime as ort
    model_path = PROJECT_ROOT / "models" / "model.onnx"
    assert_true(model_path.exists(), f"model file exists: {model_path}")
    try:
        session = ort.InferenceSession(str(model_path))
        shape = session.get_inputs()[0].shape
        assert_eq(len(shape), 4, "model input is 4D")
        assert_true(shape[1] == 3, f"model has 3 input channels, got {shape[1]}")
        num_classes = session.get_outputs()[0].shape[2] - 5
        assert_eq(num_classes, 6, f"model has 6 classes, got {num_classes}")
    except Exception as e:
        assert_true(False, f"model load exception: {e}")


def test_blank_image():
    print("\n  ── Blank Image Inference ──")
    import cv2
    import numpy as np
    import onnxruntime as ort
    model_path = PROJECT_ROOT / "models" / "model.onnx"
    if not model_path.exists():
        return assert_true(False, "model not found")
    try:
        session = ort.InferenceSession(str(model_path))
        # White image (matching crop_pad fill color)
        img = np.full((640, 640, 3), 255, dtype=np.uint8)
        blob = img.transpose(2, 0, 1).astype(np.float32) / 255.0
        blob = blob[np.newaxis, :, :, :]
        outputs = session.run(None, {session.get_inputs()[0].name: blob})
        assert_true(outputs[0].shape[1] > 0, "blank image produces output")
        # Black image
        img_b = np.zeros((640, 640, 3), dtype=np.uint8)
        blob_b = img_b.transpose(2, 0, 1).astype(np.float32) / 255.0
        blob_b = blob_b[np.newaxis, :, :, :]
        outputs_b = session.run(None, {session.get_inputs()[0].name: blob_b})
        assert_true(outputs_b[0].shape[1] > 0, "black image produces output")
    except Exception as e:
        assert_true(False, f"blank/black image inference exception: {e}")


def test_empty_image():
    print("\n  ── Tiny Image (edge case) ──")
    import cv2
    import numpy as np
    import onnxruntime as ort
    model_path = PROJECT_ROOT / "models" / "model.onnx"
    if not model_path.exists():
        return
    try:
        session = ort.InferenceSession(str(model_path))
        # 1x1 image (will be padded to 640x640 in pipeline)
        img = np.full((1, 1, 3), 128, dtype=np.uint8)
        padded = np.full((640, 640, 3), 255, dtype=np.uint8)
        padded[:1, :1] = img
        blob = padded.transpose(2, 0, 1).astype(np.float32) / 255.0
        blob = blob[np.newaxis, :, :, :]
        outputs = session.run(None, {session.get_inputs()[0].name: blob})
        assert_true(outputs[0].shape[1] > 0, "tiny image does not crash")
    except Exception as e:
        assert_true(False, f"tiny image exception: {e}")


# ═══════════════════════════════════════════════
#  Config Tests
# ═══════════════════════════════════════════════

def test_config():
    print("\n  ── Config ──")
    import yaml
    config_path = PROJECT_ROOT / "config" / "config.yaml"
    assert_true(config_path.exists(), f"config exists: {config_path}")
    with open(config_path, "r", encoding="utf-8") as f:
        cfg = yaml.safe_load(f)
    # Required top-level keys
    for key in ("camera", "model", "detection", "serial"):
        assert_true(key in cfg, f"config has '{key}' section")
    # Camera fields
    cam = cfg.get("camera", {})
    for key in ("device_id", "width", "height", "fps"):
        assert_true(key in cam, f"camera has '{key}'")
    # Model fields
    model = cfg.get("model", {})
    for key in ("path", "use_gpu"):
        assert_true(key in model, f"model has '{key}'")
    # Detection fields
    det = cfg.get("detection", {})
    for key in ("confidence_threshold", "nms_threshold", "num_classes"):
        assert_true(key in det, f"detection has '{key}'")
    assert_eq(det.get("num_classes"), 6, "num_classes == 6")
    # Serial fields
    ser = cfg.get("serial", {})
    for key in ("port", "baudrate"):
        assert_true(key in ser, f"serial has '{key}'")


# ═══════════════════════════════════════════════
#  Main
# ═══════════════════════════════════════════════

def main():
    parser = argparse.ArgumentParser(description="PCB 缺陷检测 — 自动化测试")
    parser.add_argument("--unit", action="store_true", help="仅单元测试")
    parser.add_argument("--smoke", action="store_true", help="仅烟雾测试")
    parser.add_argument("--config", action="store_true", help="仅配置测试")
    parser.add_argument("--verbose", action="store_true", help="详细输出")
    args = parser.parse_args()

    run_unit = args.unit or not (args.smoke or args.config)
    run_smoke = args.smoke or not (args.unit or args.config)
    run_config = args.config or not (args.unit or args.smoke)

    global PASS, FAIL, SKIP

    print("=" * 55)
    print("  PCB 缺陷检测 — 自动化测试")
    print(f"  时间: {time.strftime('%Y-%m-%d %H:%M:%S')}")
    print("=" * 55)

    if run_unit:
        print("\n── Unit Tests ──")
        test_iou()
        test_nms()
        test_sliding_windows()

    if run_smoke:
        print("\n── Smoke Tests ──")
        test_model_load()
        test_blank_image()
        test_empty_image()

    if run_config:
        print("\n── Config Tests ──")
        test_config()

    total = PASS + FAIL
    print(f"\n{'=' * 55}")
    print(f"  结果: {PASS} PASS / {FAIL} FAIL / {SKIP} SKIP (共 {total})")
    print(f"{'=' * 55}")

    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
