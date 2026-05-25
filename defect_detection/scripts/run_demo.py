"""
启动 C++ 检测程序并采集输出（用于无头环境调试）
"""
import subprocess
import time

import os
import sys

build_dir = os.path.join(os.path.dirname(__file__), "..", "build", "Release")
proj_dir = os.path.join(os.path.dirname(__file__), "..")

exe = os.path.join(build_dir, "defect_detection.exe")
cfg = os.path.join(proj_dir, "config", "config.yaml")

print(f"[启动] {exe}")
p = subprocess.Popen([exe, cfg], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                     cwd=proj_dir, creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)

try:
    outs, _ = p.communicate(timeout=15)
    print(outs.decode("utf-8", errors="replace"))
except subprocess.TimeoutExpired:
    p.kill()
    outs, _ = p.communicate()
    print(outs.decode("utf-8", errors="replace"))
