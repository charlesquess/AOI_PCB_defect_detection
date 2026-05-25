"""
PCB 缺陷检测 — 推理性能基准测试 (Benchmark)

模拟 C++ 流水线的完整流程：图片加载 → 滑窗裁剪 → ONNX 推理 → 计时
生成 logs/benchmark_*.txt 用于与 README 性能表对照验收。

用法:
  python scripts/benchmark.py                          # 默认：用 PCB_USED 大图跑全部场景
  python scripts/benchmark.py --scenario single         # 仅小图 (640x480 模拟 USB)
  python scripts/benchmark.py --scenario large          # 仅大图滑窗 (PCB_USED)
  python scripts/benchmark.py --iterations 10           # 每场景跑 10 轮
"""
import argparse
import json
import time
import numpy as np
from datetime import datetime
from pathlib import Path

try:
    import onnxruntime as ort
except ImportError:
    ort = None

try:
    from PIL import Image
except ImportError:
    Image = None

PROJECT_ROOT = Path(__file__).resolve().parent.parent
MODEL_PATH = PROJECT_ROOT / "models" / "model.onnx"
PCB_USED_DIR = PROJECT_ROOT.parent / "PCB_DATASET" / "PCB_USED"
BENCHMARK_DIR = PROJECT_ROOT / "logs"
INPUT_SIZE = 640  # 模型输入尺寸


def load_image(path, target_size=None):
    """加载图片，必要时缩放到 target_size，返回 CHW float32 数组"""
    img = Image.open(path).convert("RGB")
    orig_w, orig_h = img.size
    if target_size:
        # 保持宽高比的 letterbox 填充 (匹配 inferencer.cpp letterbox 逻辑)
        scale = min(target_size[0] / orig_w, target_size[1] / orig_h)
        nw, nh = int(orig_w * scale), int(orig_h * scale)
        img = img.resize((nw, nh), Image.BILINEAR)
        new_img = Image.new("RGB", target_size, (114, 114, 114))  # 灰色填充
        new_img.paste(img, ((target_size[0] - nw) // 2, (target_size[1] - nh) // 2))
        img = new_img
    arr = np.array(img, dtype=np.float32).transpose(2, 0, 1) / 255.0
    return arr[np.newaxis, :, :, :], orig_w, orig_h


def sliding_windows(w, h, win_size=INPUT_SIZE, stride=None):
    """生成滑窗坐标列表，与 C++ inferencer.cpp 逻辑一致"""
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


def crop_pad(image_np, x, y, win_size=INPUT_SIZE):
    """裁剪并填充到 win_size×win_size (BORDER_REFLECT)，匹配 C++ 逻辑"""
    h, w = image_np.shape[:2]
    x1, y1 = x, y
    x2, y2 = min(x + win_size, w), min(y + win_size, h)
    cw, ch = x2 - x1, y2 - y1
    crop = image_np[y1:y2, x1:x2]
    # 白色填充 (C++ 使用 cv::Scalar::all(255))
    padded = np.full((win_size, win_size, 3), 255, dtype=np.uint8)
    padded[:ch, :cw] = crop
    # 转 CHW + 归一化
    blob = padded.transpose(2, 0, 1).astype(np.float32) / 255.0
    return blob[np.newaxis, :, :, :]


class Benchmark:
    def __init__(self, model_path):
        if ort is None:
            raise RuntimeError("onnxruntime 未安装: pip install onnxruntime")
        if Image is None:
            raise RuntimeError("Pillow 未安装: pip install Pillow")
        self.session = ort.InferenceSession(str(model_path))
        self.input_name = self.session.get_inputs()[0].name
        self.output_name = self.session.get_outputs()[0].name
        # 获取模型输入尺寸
        shape = self.session.get_inputs()[0].shape
        self.input_h = shape[2] if len(shape) >= 4 and shape[2] > 0 else INPUT_SIZE
        self.input_w = shape[3] if len(shape) >= 4 and shape[3] > 0 else INPUT_SIZE
        print(f"模型: {model_path.name}")
        print(f"输入: {self.input_name} {shape}")
        print(f"输出: {self.output_name} {self.session.get_outputs()[0].shape}")
        print()

    def infer(self, blob):
        """执行单次推理，返回耗时(ms)"""
        start = time.perf_counter()
        self.session.run([self.output_name], {self.input_name: blob})
        return (time.perf_counter() - start) * 1000

    def run_single_window(self, image_path, iterations=5):
        """测试单窗口推理延迟 (模拟 USB 640x480 场景)"""
        print(f"[单窗口] {image_path.name}")
        blob, _, _ = load_image(image_path, target_size=(self.input_w, self.input_h))
        times = []
        for i in range(iterations):
            ms = self.infer(blob)
            times.append(ms)
            print(f"  run {i+1}: {ms:.1f}ms")
        avg = np.mean(times)
        return {
            "scenario": "single_window",
            "image": str(image_path),
            "iterations": iterations,
            "avg_ms": round(avg, 1),
            "min_ms": round(min(times), 1),
            "max_ms": round(max(times), 1),
            "fps": round(1000 / avg, 1),
        }

    def run_large_image_sliding(self, image_path, iterations=3):
        """测试大图滑窗推理 (模拟 PCB 整板场景)"""
        print(f"[大图滑窗] {image_path.name}")
        img = np.array(Image.open(image_path).convert("RGB"))
        h, w = img.shape[:2]
        wins = sliding_windows(w, h, self.input_w)
        print(f"  图片: {w}x{h}, 滑窗数: {len(wins)}")

        all_total_times = []
        all_win_times = []
        for run in range(iterations):
            total_ms = 0.0
            win_times = []
            for idx, (wx, wy) in enumerate(wins):
                blob = crop_pad(img, wx, wy, self.input_w)
                ms = self.infer(blob)
                win_times.append(ms)
                total_ms += ms
            all_total_times.append(total_ms)
            all_win_times.extend(win_times)
            print(f"   run {run+1}: {total_ms:.0f}ms total, {total_ms/len(wins):.0f}ms/win")

        avg_total = np.mean(all_total_times)
        return {
            "scenario": "large_image_sliding",
            "image": str(image_path),
            "image_size": f"{w}x{h}",
            "windows": len(wins),
            "iterations": iterations,
            "avg_total_ms": round(avg_total, 0),
            "min_total_ms": round(min(all_total_times), 0),
            "max_total_ms": round(max(all_total_times), 0),
            "avg_per_window_ms": round(np.mean(all_win_times), 1),
            "max_per_window_ms": round(max(all_win_times), 1),
            "throughput_fps": round(1000 / (avg_total / len(wins)) * min(8, len(wins)), 1),
        }

    def run_usb_simulation(self, image_path, num_frames=50):
        """模拟 USB 连续采集模式 (连续推理 num_frames 帧，统计 FPS)"""
        print(f"[USB 连续模式] {image_path.name}, {num_frames} 帧")
        blob, _, _ = load_image(image_path, target_size=(self.input_w, self.input_h))
        times = []
        for i in range(num_frames):
            ms = self.infer(blob)
            times.append(ms)
        avg = np.mean(times)
        fps = 1000 / avg if avg > 0 else 0
        print(f"  平均: {avg:.1f}ms, FPS: {fps:.1f}")
        return {
            "scenario": "usb_continuous",
            "image": str(image_path),
            "num_frames": num_frames,
            "avg_ms": round(avg, 1),
            "min_ms": round(min(times), 1),
            "max_ms": round(max(times), 1),
            "fps": round(fps, 1),
        }


def main():
    parser = argparse.ArgumentParser(description="PCB 检测性能基准测试")
    parser.add_argument("--model", type=str, default=str(MODEL_PATH))
    parser.add_argument("--scenario", choices=["single", "large", "usb", "all"], default="all")
    parser.add_argument("--iterations", type=int, default=5, help="单场景重复次数")
    parser.add_argument("--usb-frames", type=int, default=50, help="USB 模式帧数")
    args = parser.parse_args()

    model_path = Path(args.model)
    if not model_path.exists():
        print(f"错误: 模型不存在: {model_path}")
        return 1

    # 找测试图片
    single_images = sorted(Path(PROJECT_ROOT / "data" / "yolo_dataset" / "images" / "val").glob("*.jpg"))
    large_images = sorted(PCB_USED_DIR.glob("*.JPG")) if PCB_USED_DIR.exists() else []

    if not single_images and not large_images:
        # 用训练的图片也行
        single_images = sorted(Path(PROJECT_ROOT / "data" / "yolo_dataset" / "images" / "train").glob("*.jpg"))[:10]

    if not single_images:
        print("错误: 找不到测试图片")
        return 1

    print("=" * 60)
    print(f"PCB 缺陷检测 — 基准测试")
    print(f"时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print(f"Python: ort {ort.__version__ if ort else 'N/A'}")
    print(f"模型: {model_path}")
    print("=" * 60)

    bm = Benchmark(model_path)

    results = []
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

    # ── 场景 1: 单窗口 (640x480 USB 模拟) ──
    if args.scenario in ("single", "all") and single_images:
        print("\n" + "-" * 50)
        print("场景 1: 单窗口推理 (640x480 → 模型输入)")
        print("-" * 50)
        img = single_images[0]
        r = bm.run_single_window(img, args.iterations)
        results.append(r)

    # ── 场景 2: 大图滑窗 ──
    if args.scenario in ("large", "all") and large_images:
        print("\n" + "-" * 50)
        print("场景 2: 大图滑窗推理 (PCB 整板)")
        print("-" * 50)
        for img in large_images[:3]:  # 最多 3 张大图
            r = bm.run_large_image_sliding(img, max(1, args.iterations // 2))
            results.append(r)

    # ── 场景 3: USB 连续模式 ──
    if args.scenario in ("usb", "all"):
        print("\n" + "-" * 50)
        print("场景 3: USB 连续采集模式")
        print("-" * 50)
        img = single_images[0] if single_images else large_images[0]
        r = bm.run_usb_simulation(img, args.usb_frames)
        results.append(r)

    # ── 报告 ──
    print("\n" + "=" * 60)
    print("测试报告")
    print("=" * 60)
    report_lines = []
    report_lines.append(f"# PCB 检测基准测试报告")
    report_lines.append(f"时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    report_lines.append(f"模型: {model_path}")
    report_lines.append(f"ONNX Runtime: {ort.__version__ if ort else 'N/A'}")
    report_lines.append(f"CPU: {ort.get_available_providers() if ort else 'N/A'}")
    report_lines.append("")

    for r in results:
        report_lines.append(f"## {r['scenario']}")
        report_lines.append(f"  图片: {r.get('image', 'N/A')}")
        if r["scenario"] == "single_window":
            report_lines.append(f"  平均延迟: {r['avg_ms']}ms")
            report_lines.append(f"  最小延迟: {r['min_ms']}ms")
            report_lines.append(f"  最大延迟: {r['max_ms']}ms")
            report_lines.append(f"  等效 FPS: {r['fps']}")
        elif r["scenario"] == "large_image_sliding":
            report_lines.append(f"  图片尺寸: {r['image_size']}")
            report_lines.append(f"  滑窗数: {r['windows']}")
            report_lines.append(f"  总延迟(平均): {r['avg_total_ms']}ms")
            report_lines.append(f"  每窗延迟(平均): {r['avg_per_window_ms']}ms")
            report_lines.append(f"  每窗延迟(最大): {r['max_per_window_ms']}ms")
            report_lines.append(f"  等效吞吐: {r['throughput_fps']} img/s")
        elif r["scenario"] == "usb_continuous":
            report_lines.append(f"  帧数: {r['num_frames']}")
            report_lines.append(f"  平均延迟: {r['avg_ms']}ms")
            report_lines.append(f"  最小延迟: {r['min_ms']}ms")
            report_lines.append(f"  最大延迟: {r['max_ms']}ms")
            report_lines.append(f"  FPS: {r['fps']}")
        report_lines.append("")

    report = "\n".join(report_lines)
    print(report)

    benchmark_file = BENCHMARK_DIR / f"benchmark_{timestamp}.txt"
    BENCHMARK_DIR.mkdir(parents=True, exist_ok=True)
    with open(benchmark_file, "w", encoding="utf-8") as f:
        f.write(report)
    print(f"报告已保存: {benchmark_file}")

    # 也保存 JSON 格式
    json_file = BENCHMARK_DIR / f"benchmark_{timestamp}.json"
    with open(json_file, "w", encoding="utf-8") as f:
        json.dump({"timestamp": timestamp, "results": results}, f, indent=2, ensure_ascii=False)
    print(f"JSON 已保存: {json_file}")

    return 0


if __name__ == "__main__":
    exit(main())
