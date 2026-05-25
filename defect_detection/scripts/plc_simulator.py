"""
PLC 模拟器 — 通过串口模拟产线 PLC 与 C++ 检测系统通信

用法:
  1. 安装虚拟串口驱动 (首次只需一次):
     下载并安装 com0com: https://sourceforge.net/projects/com0com/
     安装后生成 COM5 <-> COM6 虚拟串口对

  2. 终端1: 运行 C++ 检测系统 (连接 COM6)
     cd defect_detection
     .\build\Release\defect_detection.exe config\config.yaml

  3. 终端2: 运行此 PLC 模拟器 (连接 COM5)
     conda run -n yolov5 python scripts/plc_simulator.py COM5
"""

import sys
import time
import random
import threading
import serial
import serial.tools.list_ports


class PLCSimulator:
    """模拟 PLC，按固定节拍发送触发信号并接收检测结果"""

    def __init__(self, port: str, baudrate: int = 9600,
                 trigger_interval: float = 2.0,
                 auto_trigger: bool = True):
        """
        port:            串口号 (如 COM5)
        baudrate:        波特率
        trigger_interval: 触发间隔（秒），模拟传送带节拍
        auto_trigger:    是否自动发送触发信号
        """
        self.port = port
        self.baudrate = baudrate
        self.trigger_interval = trigger_interval
        self.auto_trigger = auto_trigger
        self.ser = None
        self.running = False

    def open(self):
        """打开串口"""
        try:
            self.ser = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=1.0
            )
            print(f"[PLC] 串口 {self.port} 已打开 @ {self.baudrate} bps")
            return True
        except serial.SerialException as e:
            print(f"[PLC] 打开串口失败: {e}")
            return False

    def close(self):
        if self.ser and self.ser.is_open:
            self.ser.close()
            print("[PLC] 串口已关闭")

    def send_trigger(self):
        """发送触发信号给检测系统"""
        if not self.ser or not self.ser.is_open:
            return
        self.ser.write(b"TRIG\n")
        print(f"[PLC] → 发送 TRIG  (产线节拍 {self.trigger_interval:.1f}s)")

    def listen(self):
        """监听检测系统回传的结果"""
        while self.running and self.ser and self.ser.is_open:
            try:
                line = self.ser.readline().decode('utf-8').strip()
                if line:
                    self._handle_result(line)
            except (serial.SerialException, UnicodeDecodeError) as e:
                print(f"[PLC] 接收错误: {e}")
                break

    def _handle_result(self, result: str):
        """处理检测结果"""
        if result == "PASS":
            print(f"[PLC] ← 良品 OK  [状态机: 空闲]")
        elif result.startswith("FAIL"):
            parts = result.split(":")
            count = parts[1] if len(parts) > 1 else "?"
            print(f"[PLC] ← 缺陷品! 缺陷数={count}  [状态机: 空闲]")
        elif result == "DONE":
            print(f"[PLC] ← 处理完成  [状态机: 空闲 → 等待下个 TRIG]")
        elif result.startswith("STAT"):
            print(f"[PLC] ← 统计: {result}")
        else:
            print(f"[PLC] ← 未知消息: {result}")

    def send_command(self, cmd: str):
        """发送命令"""
        if not self.ser or not self.ser.is_open:
            return
        self.ser.write((cmd + "\n").encode())
        print(f"[PLC] → 发送 {cmd}")

    def interactive_input(self):
        """后台线程：监听键盘输入发送特殊命令"""
        try:
            while self.running:
                line = input().strip().upper()
                if line == "Q" or line == "QUIT":
                    self.running = False
                    break
                elif line == "TRIG":
                    self.send_trigger()
                elif line == "STAT":
                    self.send_command("STAT")
                elif line == "RST":
                    self.send_command("RST")
                    print("[PLC] → 已发送 RST (重置统计)")
                elif line:
                    print(f"[PLC] 未知命令: {line}  可用: TRIG / STAT / RST / Q")
        except (EOFError, KeyboardInterrupt):
            pass

    def run(self):
        """主循环"""
        if not self.open():
            return

        self.running = True

        # 接收线程
        listener = threading.Thread(target=self.listen, daemon=True)
        listener.start()

        # 键盘输入线程
        input_thread = threading.Thread(target=self.interactive_input, daemon=True)
        input_thread.start()

        print(f"[PLC] 模拟器运行中 (按 Ctrl+C 停止)")
        print(f"[PLC] 触发模式: {'自动节拍' if self.auto_trigger else '手动'}")
        print(f"[PLC] 命令: TRIG / STAT / RST / Q")
        print(f"[PLC] {'='*60}")

        try:
            if self.auto_trigger:
                while self.running:
                    self.send_trigger()
                    time.sleep(self.trigger_interval)
            else:
                print("[PLC] 手动模式: 在控制台输入 TRIG 发送触发信号")
                while self.running:
                    time.sleep(0.5)
        except KeyboardInterrupt:
            print("\n[PLC] 用户中断")
        finally:
            self.running = False
            self.close()


def list_ports():
    """列出可用串口"""
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("未检测到串口。请先安装虚拟串口驱动 (com0com)。")
        return []
    print("可用串口:")
    for p in ports:
        print(f"  {p.device}: {p.description}")
    return ports


def main():
    import argparse
    parser = argparse.ArgumentParser(description="PLC 串口模拟器")
    parser.add_argument("port", nargs="?", default="COM5",
                        help="串口号 (默认 COM5)")
    parser.add_argument("--baudrate", type=int, default=9600,
                        help="波特率 (默认 9600)")
    parser.add_argument("--interval", type=float, default=2.0,
                        help="触发间隔秒 (默认 2.0)")
    parser.add_argument("--manual", action="store_true",
                        help="手动触发模式 (按 Enter 发送信号)")
    parser.add_argument("--list", action="store_true",
                        help="列出可用串口")
    args = parser.parse_args()

    if args.list:
        list_ports()
        return

    sim = PLCSimulator(
        port=args.port,
        baudrate=args.baudrate,
        trigger_interval=args.interval,
        auto_trigger=not args.manual,
    )
    sim.run()


if __name__ == "__main__":
    main()
