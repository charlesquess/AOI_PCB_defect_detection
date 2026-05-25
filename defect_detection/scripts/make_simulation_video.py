"""
PCB 流水线仿真视频 v2 — 消除闪烁
PCBs 从右侧缓缓进入 → 停顿 → 从左侧移出。每块板子居中帧记一次检测。
"""
import random
import xml.etree.ElementTree as ET
from pathlib import Path

import cv2
import numpy as np
from tqdm import tqdm

PCB_ROOT = Path(__file__).resolve().parent.parent.parent / "PCB_DATASET"
OUTPUT = Path(__file__).resolve().parent.parent / "data" / "pipeline_simulation.mp4"

FPS = 30
PADDING = 200
STAY_FRAMES = 60          # 2 秒居中
ENTER_FRAMES = 60         # 2 秒进入
EXIT_FRAMES = 60          # 2 秒退出
GAP_FRAMES = 30           # 1 秒过渡

# 收集图片
paths = sorted((PCB_ROOT / "images").rglob("*.jpg"))
random.seed(42)
random.shuffle(paths)

# 取前 20 张
paths = paths[:20]

# 最大尺寸
max_w, max_h = 0, 0
for p in paths:
    h, w = cv2.imread(str(p)).shape[:2]
    max_w, max_h = max(max_w, w), max(max_h, h)

W = max_w + PADDING
H = max_h + PADDING

fourcc = cv2.VideoWriter_fourcc(*"mp4v")
writer = cv2.VideoWriter(str(OUTPUT), fourcc, FPS, (W, H))

# 预置文字位置（避免每帧重新计算）
label_x, label_y = 30, 50
font = cv2.FONT_HERSHEY_SIMPLEX

# 生成帧
total = 0
for idx, img_path in enumerate(tqdm(paths, desc="生成视频")):
    img = cv2.imread(str(img_path), cv2.IMREAD_COLOR)
    ih, iw = img.shape[:2]
    cx = (W - iw) // 2
    cy = (H - ih) // 2

    # ── a) 进入 ──
    for i in range(ENTER_FRAMES):
        canvas = np.full((H, W, 3), 255, dtype=np.uint8)
        t = i / ENTER_FRAMES
        x = int(W + (cx - W) * t)
        x1, y1 = max(x, 0), max(cy, 0)
        x2, y2 = min(x + iw, W), min(cy + ih, H)
        sx1 = max(-x, 0)
        sy1 = max(-cy, 0)
        if x2 > x1 and y2 > y1:
            canvas[y1:y2, x1:x2] = img[sy1:sy1+y2-y1, sx1:sx1+x2-x1]
        cv2.putText(canvas, f"PCB #{idx+1}", (label_x, label_y),
                    font, 1.0, (80, 80, 80), 2)
        cv2.putText(canvas, ">>> conveyer >>>", (W - 280, label_y),
                    font, 0.7, (160, 160, 160), 1)
        writer.write(canvas)
        total += 1

    # ── b) 居中 ──
    for i in range(STAY_FRAMES):
        canvas = np.full((H, W, 3), 255, dtype=np.uint8)
        canvas[cy:cy+ih, cx:cx+iw] = img
        cv2.putText(canvas, f"PCB #{idx+1}  [检测中]", (label_x, label_y),
                    font, 1.0, (0, 0, 0), 2)
        cv2.putText(canvas, ">>> conveyer >>>", (W - 280, label_y),
                    font, 0.7, (160, 160, 160), 1)

        writer.write(canvas)
        total += 1

    # ── c) 退出 ──
    for i in range(EXIT_FRAMES):
        canvas = np.full((H, W, 3), 255, dtype=np.uint8)
        t = i / EXIT_FRAMES
        x = int(cx + (-W - iw - cx) * t)
        # 安全放置：处理部分不可见
        x1, y1 = max(x, 0), max(cy, 0)
        x2, y2 = min(x + iw, W), min(cy + ih, H)
        sx1 = max(-x, 0)
        sy1 = max(-cy, 0)
        if x2 > x1 and y2 > y1:
            canvas[y1:y2, x1:x2] = img[sy1:sy1+y2-y1, sx1:sx1+x2-x1]
        cv2.putText(canvas, f"PCB #{idx+1}", (label_x, label_y),
                    font, 1.0, (80, 80, 80), 2)
        cv2.putText(canvas, ">>> conveyer >>>", (W - 280, label_y),
                    font, 0.7, (160, 160, 160), 1)
        writer.write(canvas)
        total += 1

    # ── d) 过渡 ──
    for i in range(GAP_FRAMES):
        canvas = np.full((H, W, 3), 255, dtype=np.uint8)
        cv2.putText(canvas, "等待下一块 PCB...", (label_x, label_y),
                    font, 0.8, (180, 180, 180), 1)
        writer.write(canvas)
        total += 1

writer.release()
print(f"视频已生成: {OUTPUT}")
print(f"  尺寸: {W}x{H}, 帧数: {total}, 时长: {total/FPS:.1f}s")
