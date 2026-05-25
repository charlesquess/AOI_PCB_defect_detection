"""
USB 摄像头测试脚本 — 对标 C++ Camera 模块 API。
用法:
  conda run -n yolov5 python scripts/test_camera.py [设备号]
"""
import sys
import time
from pathlib import Path

import cv2
import numpy as np


# ── 工具函数，对应 C++ Camera 静态方法 ────────────────────────

# OpenCV 后端映射
BACKENDS = {
    "AutoDetect": None,
    "DShow": cv2.CAP_DSHOW,
    "MSMF":  cv2.CAP_MSMF,
    "VFW":   cv2.CAP_VFW,
}


def probe_device(device_id: int, backend: str = "AutoDetect",
                 timeout_ms: int = 2000) -> bool:
    """探测设备是否可访问，对应 C++ Camera::probe_device"""
    cv_backend = BACKENDS.get(backend)
    cap = cv2.VideoCapture(device_id, cv_backend) if cv_backend else \
          cv2.VideoCapture(device_id)
    if not cap.isOpened():
        return False
    deadline = time.time() + timeout_ms / 1000.0
    ok = False
    while time.time() < deadline:
        ret, frame = cap.read()
        if ret and frame is not None and frame.size > 0:
            ok = True
            break
    cap.release()
    return ok


def list_devices(max_devices: int = 10) -> list[int]:
    """枚举可用相机设备，对应 C++ Camera::list_available_devices"""
    devices = []
    for i in range(max_devices):
        cap = cv2.VideoCapture(i, cv2.CAP_DSHOW)
        if cap.isOpened():
            devices.append(i)
            cap.release()
    return devices


def frame_healthy(frame: np.ndarray) -> tuple[bool, str]:
    """帧健康检查：检测空帧、全黑、全白，对应 C++ Camera::frame_healthy"""
    if frame is None or frame.size == 0:
        return False, "空帧"
    h, w = frame.shape[:2]
    if h == 0 or w == 0:
        return False, "尺寸为零"
    mean = cv2.mean(frame)
    if all(v < 1 for v in mean[:3]):
        return False, "全黑帧"
    if all(v > 254 for v in mean[:3]):
        return False, "全白帧"
    return True, "正常"


def measure_fps(cap: cv2.VideoCapture, num_frames: int = 60) -> float:
    """实测帧率，通过连续采集 num_frames 帧计算"""
    start = time.perf_counter()
    count = 0
    for _ in range(num_frames):
        ret, _ = cap.read()
        if ret:
            count += 1
    elapsed = time.perf_counter() - start
    return count / elapsed if elapsed > 0 else 0.0


# ── 主测试流程 ────────────────────────────────────────────────

def main():
    device_id = int(sys.argv[1]) if len(sys.argv) > 1 else 0

    print("=" * 55)
    print(f"  摄像头测试 — device_id = {device_id}")
    print("=" * 55)

    # 1. 枚举所有可用相机
    print("\n[1] 扫描相机...")
    available = list_devices()
    if not available:
        print("  未找到任何相机！")
        return 1
    print(f"  可用设备: {available}")
    if device_id not in available:
        print(f"  设备 {device_id} 不在列表中，仍尝试打开...")

    # 2. 探测指定设备
    print(f"\n[2] 探测设备 {device_id}...")
    ok = probe_device(device_id, "DShow", 3000)
    print(f"  {'成功' if ok else '失败'} (DirectShow)")

    if not ok:
        print("  尝试 MSMF 后端...")
        ok = probe_device(device_id, "MSMF", 3000)
        print(f"  {'成功' if ok else '失败'} (MSMF)")

    if not ok:
        print("  相机不可访问。")
        return 1

    # 3. 打开相机
    print(f"\n[3] 使用 DirectShow 打开相机 {device_id}...")
    cap = cv2.VideoCapture(device_id, cv2.CAP_DSHOW)
    if not cap.isOpened():
        print("  打开失败！")
        return 1

    # 查询实际分辨率
    w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    h = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    fps_target = cap.get(cv2.CAP_PROP_FPS)
    print(f"  分辨率    : {w} x {h}")
    print(f"  目标帧率  : {fps_target:.1f}")

    # 4. 连续采集 10 帧并检查健康状态
    print(f"\n[4] 采集 10 帧...")
    healthy_count = 0
    for i in range(10):
        ret, frame = cap.read()
        if ret and frame is not None:
            healthy, reason = frame_healthy(frame)
            if healthy:
                healthy_count += 1
            print(f"  帧 {i+1:2d}: {frame.shape[1]}x{frame.shape[0]} "
                  f"{'正常' if healthy else '异常: ' + reason}")
        else:
            print(f"  帧 {i+1:2d}: 失败")

    print(f"\n  健康帧: {healthy_count}/10")

    # 5. FPS 基准测试
    print(f"\n[5] FPS 基准测试 (60 帧)...")
    fps = measure_fps(cap, 60)
    print(f"  实测帧率: {fps:.1f}")

    # 6. 保存快照
    print(f"\n[6] 保存快照...")
    ret, frame = cap.read()
    if ret:
        snapshot_dir = Path(__file__).resolve().parent.parent / "data"
        snapshot_dir.mkdir(exist_ok=True)
        path = str(snapshot_dir / "camera_snapshot.jpg")
        cv2.imwrite(path, frame)
        print(f"  已保存到 {path}")
    else:
        print("  保存失败")

    # 7. 交互式预览（需要 OpenCV GUI 支持）
    try:
        probe = np.zeros((10, 10, 3), dtype=np.uint8)
        cv2.imshow("_probe_", probe)
        cv2.waitKey(1)
        cv2.destroyWindow("_probe_")
        has_gui = True
    except cv2.error:
        has_gui = False

    if has_gui:
        print(f"\n[7] 交互式预览 — 按 Q 或 ESC 退出")
        fps_timer = time.time()
        fps_display = 0.0
        fc = 0
        while True:
            ret, frame = cap.read()
            if not ret:
                break
            fc += 1
            now = time.time()
            if now - fps_timer >= 1.0:
                fps_display = fc / (now - fps_timer)
                fc = 0
                fps_timer = now
            info = f"相机 {device_id}  FPS: {fps_display:.1f}  Q/ESC 退出"
            cv2.putText(frame, info, (10, 30),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
            cv2.imshow("摄像头测试", frame)
            key = cv2.waitKey(1) & 0xFF
            if key == ord('q') or key == 27:
                break
        cv2.destroyAllWindows()
    else:
        print("\n[7] GUI 不可用（无头 OpenCV），跳过交互预览")

    cap.release()
    print("\n== 摄像头测试完成 ==")
    return 0


if __name__ == "__main__":
    sys.exit(main())
