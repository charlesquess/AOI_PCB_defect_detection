"""
YOLOv5 模型评估 — 运行 val.py 导出指标到 logs/eval/

用法:
  conda run -n yolov5 python scripts/evaluate.py
  conda run -n yolov5 python scripts/evaluate.py --weights ../yolov5-7.0/runs/train/exp7/weights/best.pt
"""
import argparse
import json
import re
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
YOLOV5_DIR = PROJECT_ROOT.parent / "yolov5-7.0"
EVAL_DIR = PROJECT_ROOT / "logs" / "eval"

# 默认上线门槛
THRESHOLDS = {
    "mAP@0.5": 0.70,
    "missing_hole_recall": 0.80,
    "mouse_bite_recall": 0.75,
    "open_circuit_recall": 0.80,
    "short_recall": 0.75,
    "spur_recall": 0.70,
    "spurious_copper_recall": 0.75,
}


def find_latest_weights():
    candidates = sorted(YOLOV5_DIR.glob("runs/train/*/weights/best.pt"))
    if not candidates:
        print("错误: 未找到训练好的 best.pt。请先运行 train.py")
        sys.exit(1)
    return candidates[-1]


def parse_val_output(text):
    """从 val.py 标准输出中提取指标"""
    metrics = {}
    # mAP@0.5
    m = re.search(r"mAP@\.5\s+([\d.]+)", text)
    if m:
        metrics["mAP@0.5"] = float(m.group(1))
    # mAP@0.5:0.95
    m = re.search(r"mAP@\.5:\.95\s+([\d.]+)", text)
    if m:
        metrics["mAP@0.5:0.95"] = float(m.group(1))
    # Per-class:  Class     Images  Instances          P          R      mAP@.5
    # 搜索表格行
    lines = text.split("\n")
    in_table = False
    for line in lines:
        if "all" in line and "mAP@.5" in line:
            in_table = True
            continue
        if in_table and line.strip() and not line.startswith("Speed"):
            parts = line.split()
            if len(parts) >= 7:
                cls_name = parts[0]
                p = float(parts[3]) if parts[3] != "?" else 0.0
                r = float(parts[4]) if parts[4] != "?" else 0.0
                map50 = float(parts[5]) if parts[5] != "?" else 0.0
                metrics[f"{cls_name}_precision"] = p
                metrics[f"{cls_name}_recall"] = r
                metrics[f"{cls_name}_mAP@0.5"] = map50
    return metrics


def check_thresholds(metrics):
    """检查指标是否通过上线门槛"""
    results = []
    all_pass = True
    for key, threshold in THRESHOLDS.items():
        actual = metrics.get(key)
        if actual is None:
            results.append(f"  ⚠️  {key}: N/A (未找到)")
            continue
        passed = actual >= threshold
        status = "✅" if passed else "❌"
        results.append(f"  {status} {key}: {actual:.3f} (门槛: {threshold})")
        if not passed:
            all_pass = False
    return results, all_pass


def generate_html_report(report_dir, metrics, all_pass):
    """从 val.py 输出生成美观的 HTML 评估报告"""
    # 找图片资源
    images = {}
    for name in ("confusion_matrix", "PR_curve", "F1_curve", "P_curve", "R_curve"):
        for ext in (".png", ".jpg"):
            p = report_dir / f"{name}{ext}"
            if p.exists():
                images[name] = p.name
                break

    val_batches = sorted(report_dir.glob("val_batch*_pred.jpg"))[:6]

    # 构建每类指标表格
    class_rows = ""
    class_names_list = ["missing_hole", "mouse_bite", "open_circuit", "short", "spur", "spurious_copper"]
    for cls in class_names_list:
        p = metrics.get(f"{cls}_precision")
        r = metrics.get(f"{cls}_recall")
        m = metrics.get(f"{cls}_mAP@0.5")
        p_str = f"{p:.3f}" if p is not None else "N/A"
        r_str = f"{r:.3f}" if r is not None else "N/A"
        m_str = f"{m:.3f}" if m is not None else "N/A"
        class_rows += f"""
            <tr>
                <td><span class="color-dot" style="background:hsl({hash(cls) % 360},70%,50%)"></span>{cls}</td>
                <td class="val">{p_str}</td>
                <td class="val">{r_str}</td>
                <td class="val">{m_str}</td>
            </tr>"""

    # 门槛检查行
    threshold_rows = ""
    for key, threshold in THRESHOLDS.items():
        actual = metrics.get(key)
        if actual is None:
            threshold_rows += f"""
            <tr>
                <td>{key}</td>
                <td class="val warn">N/A</td>
                <td class="val">{threshold}</td>
                <td class="warn">⚠️ 未找到</td>
            </tr>"""
        else:
            passed = actual >= threshold
            status = "✅ PASS" if passed else "❌ FAIL"
            cls_name = "pass" if passed else "fail"
            threshold_rows += f"""
            <tr>
                <td>{key}</td>
                <td class="val">{actual:.3f}</td>
                <td class="val">{threshold}</td>
                <td class="{cls_name}">{status}</td>
            </tr>"""

    # val batch 预览
    batch_gallery = ""
    for bp in val_batches:
        batch_gallery += f'            <img src="{bp.name}" class="batch-img" />\n'

    # 性能曲线 gallery
    curve_gallery = ""
    for key in ("confusion_matrix", "PR_curve", "F1_curve", "P_curve", "R_curve"):
        if key in images:
            curve_gallery += f'            <figure><img src="{images[key]}" /><figcaption>{key}</figcaption></figure>\n'

    html = f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>PCB 缺陷检测 — 评估报告</title>
<style>
* {{ box-sizing: border-box; margin: 0; padding: 0; }}
body {{ font-family: -apple-system, 'Segoe UI', sans-serif; max-width: 1100px; margin: 0 auto; padding: 24px; background: #f8f9fa; color: #333; }}
h1 {{ font-size: 1.6em; margin-bottom: 8px; }}
h2 {{ font-size: 1.2em; margin: 24px 0 12px; border-bottom: 2px solid #dee2e6; padding-bottom: 6px; }}
.info {{ background: #e9ecef; border-radius: 8px; padding: 16px; margin: 16px 0; }}
.info td {{ padding: 2px 16px 2px 0; }}
.overall {{ font-size: 1.3em; text-align: center; padding: 16px; border-radius: 8px; margin: 16px 0; }}
.overall.pass {{ background: #d4edda; color: #155724; border: 1px solid #c3e6cb; }}
.overall.fail {{ background: #f8d7da; color: #721c24; border: 1px solid #f5c6cb; }}
table {{ width: 100%; border-collapse: collapse; margin: 8px 0; background: white; border-radius: 6px; overflow: hidden; box-shadow: 0 1px 3px rgba(0,0,0,.08); }}
th, td {{ padding: 8px 12px; text-align: left; border-bottom: 1px solid #eee; }}
th {{ background: #f1f3f5; font-weight: 600; }}
.val {{ font-family: 'Consolas', 'Courier New', monospace; text-align: right; }}
.pass {{ color: #28a745; font-weight: bold; }}
.fail {{ color: #dc3545; font-weight: bold; }}
.warn {{ color: #e67e22; }}
.color-dot {{ display: inline-block; width: 10px; height: 10px; border-radius: 50%; margin-right: 6px; }}
figure {{ display: inline-block; margin: 8px; text-align: center; vertical-align: top; }}
figure img {{ max-width: 480px; border: 1px solid #ddd; border-radius: 4px; }}
figcaption {{ font-size: .85em; color: #666; margin-top: 4px; }}
.batch-img {{ max-width: 240px; margin: 4px; border: 1px solid #ddd; border-radius: 4px; }}
.gallery {{ display: flex; flex-wrap: wrap; gap: 8px; margin: 8px 0; }}
</style>
</head>
<body>

<h1>PCB 缺陷检测 — 评估报告</h1>

<div class="info">
<table>
<tr><td><strong>模型</strong></td><td>{report_dir.parent.parent.name / report_dir.parent.name / report_dir.name}</td></tr>
<tr><td><strong>时间</strong></td><td>{report_dir.name[:4]}-{report_dir.name[4:6]}-{report_dir.name[6:8]} {report_dir.name[9:11]}:{report_dir.name[11:13]}:{report_dir.name[13:15]}</td></tr>
<tr><td><strong>mAP@0.5</strong></td><td class="val">{metrics.get('mAP@0.5', 'N/A')}</td></tr>
<tr><td><strong>mAP@0.5:0.95</strong></td><td class="val">{metrics.get('mAP@0.5:0.95', 'N/A')}</td></tr>
</table>
</div>

<div class="overall {'pass' if all_pass else 'fail'}">
{'✅ 全部指标通过上线门槛' if all_pass else '❌ 部分指标未达门槛，请优化模型'}
</div>

<h2>每类指标</h2>
<table>
<thead><tr><th>类别</th><th>Precision</th><th>Recall</th><th>mAP@0.5</th></tr></thead>
<tbody>{class_rows}</tbody>
</table>

<h2>上线门槛检查</h2>
<table>
<thead><tr><th>指标</th><th>实际值</th><th>门槛</th><th>状态</th></tr></thead>
<tbody>{threshold_rows}</tbody>
</table>

<h2>性能曲线</h2>
<div class="gallery">{curve_gallery}</div>

<h2>Val Batch 预测样例</h2>
<div class="gallery">{batch_gallery}</div>

</body>
</html>"""

    html_path = report_dir / "report.html"
    with open(html_path, "w", encoding="utf-8") as f:
        f.write(html)
    return html_path


def main():
    parser = argparse.ArgumentParser(description="评估 YOLOv5 PCB 缺陷检测模型")
    parser.add_argument("--weights", type=str, default=None, help="模型权重路径")
    parser.add_argument("--data", type=str,
                        default=str(PROJECT_ROOT / "data" / "yolo_dataset" / "dataset.yaml"),
                        help="数据集配置")
    parser.add_argument("--img", type=int, default=640, help="输入尺寸")
    parser.add_argument("--batch", type=int, default=16, help="批次大小")
    parser.add_argument("--conf", type=float, default=0.001, help="置信度阈值")
    parser.add_argument("--iou", type=float, default=0.6, help="NMS IoU 阈值")
    args = parser.parse_args()

    if args.weights:
        weights = Path(args.weights).resolve()
    else:
        weights = find_latest_weights()

    if not weights.exists():
        print(f"错误: 权重文件不存在: {weights}")
        sys.exit(1)

    data_yaml = Path(args.data)
    if not data_yaml.exists():
        print(f"错误: 数据集配置不存在: {data_yaml}")
        sys.exit(1)

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    report_dir = EVAL_DIR / timestamp
    report_dir.mkdir(parents=True, exist_ok=True)

    print("=" * 60)
    print(f"模型:     {weights}")
    print(f"数据集:   {data_yaml}")
    print(f"输出目录: {report_dir}")
    print("=" * 60)

    # 运行 val.py
    cmd = [
        sys.executable, str(YOLOV5_DIR / "val.py"),
        "--weights", str(weights),
        "--data", str(data_yaml),
        "--img", str(args.img),
        "--batch", str(args.batch),
        "--conf-thres", str(args.conf),
        "--iou-thres", str(args.iou),
        "--save-json",
        "--save-hybrid",
    ]
    print(f"运行: {' '.join(cmd)}\n")
    result = subprocess.run(cmd, capture_output=True, text=True, cwd=str(YOLOV5_DIR))

    # 保存原始输出
    with open(report_dir / "val_output.log", "w", encoding="utf-8") as f:
        f.write(result.stdout)
        if result.stderr:
            f.write("\n\n--- STDERR ---\n")
            f.write(result.stderr)

    # 解析指标
    metrics = parse_val_output(result.stdout)

    # 查找 val.py 输出的结果目录
    val_exp_dirs = sorted((YOLOV5_DIR / "runs" / "val").glob("exp*"))
    if val_exp_dirs:
        latest_val = val_exp_dirs[-1]
        for item in latest_val.iterdir():
            dest = report_dir / item.name
            if item.is_dir():
                shutil.copytree(item, dest, dirs_exist_ok=True)
            else:
                shutil.copy2(item, dest)
        print(f"\n验证结果已复制到: {report_dir}")

    # 评估摘要
    print("\n" + "=" * 60)
    print("评估摘要")
    print("=" * 60)

    for key, value in sorted(metrics.items()):
        print(f"  {key}: {value:.4f}")

    # 门槛检查
    print(f"\n上线门槛检查:")
    check_results, all_pass = check_thresholds(metrics)
    for line in check_results:
        print(line)

    summary = {
        "timestamp": timestamp,
        "model": str(weights),
        "dataset": str(data_yaml),
        "metrics": metrics,
        "thresholds": THRESHOLDS,
        "all_pass": all_pass,
    }
    with open(report_dir / "report.json", "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2, ensure_ascii=False)

    print(f"\n报告保存至: {report_dir / 'report.json'}")
    print(f"\n总结: {'[PASS] 全部通过' if all_pass else '[FAIL] 部分指标未达门槛，请优化模型'}")

    # 生成 HTML 报告
    html_path = generate_html_report(report_dir, metrics, all_pass)
    print(f"HTML 报告: {html_path}")

    # 更新 manifest.json 中的 mAP
    manifest_path = PROJECT_ROOT / "models" / "manifest.json"
    if manifest_path.exists() and "mAP@0.5" in metrics:
        try:
            with open(manifest_path, "r", encoding="utf-8") as f:
                manifest = json.load(f)
            manifest["mAP"] = metrics["mAP@0.5"]
            manifest["last_evaluation"] = timestamp
            with open(manifest_path, "w", encoding="utf-8") as f:
                json.dump(manifest, f, indent=2, ensure_ascii=False)
            print(f"manifest.json mAP 已更新: {metrics['mAP@0.5']:.3f}")
        except Exception as e:
            print(f"更新 manifest.json 失败: {e}")


if __name__ == "__main__":
    main()
