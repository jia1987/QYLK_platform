#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""RS485 从机模拟器 —— 模拟北京枢点电机驱动板（设计方案 D11）。

用途：无硬件时在虚拟串口对上闭环验证主机软件（帧编解码、总线调度、
状态机、故障处理）。虚拟串口：Windows 用 com0com，WSL/Linux 用 socat。

用法：
  python rs485_sim.py --port COM5                 # 模拟地址 1-6 六个头
  python rs485_sim.py --port /dev/ttyUSB1 --addrs 1,2,3
  python rs485_sim.py --port COM5 --verbose       # 打印总线流量

运行时控制台命令（回车执行）：
  fault <addr> <over|under|temp|hall|stall|short>  注入故障
  clear <addr>          清除故障
  unplug <addr>         模拟拔出治疗头（不应答）
  plug <addr>           模拟插入治疗头
  state                 打印所有头状态
  quit                  退出
"""
from __future__ import annotations  # 兼容 Ubuntu 20.04 的 Python 3.8（bytes | None 语法）

import argparse
import sys
import threading
import time

try:
    import serial
except ImportError:
    sys.exit("需要 pyserial：pip install pyserial")

# ---------------- 协议常量（与 src/core/frame.h 一致） ----------------
FRAME_LEN = 8
CLOSED_LOOP_BIT = 0x1000
MAX_SPEED_VALUE = 3001
MIN_CLOSED_LOOP_RPM = 100
COAST_THRESHOLD = 50

FAULT_BITS = {
    "over": 0x01, "under": 0x02, "temp": 0x04,
    "hall": 0x10, "stall": 0x20, "short": 0x40,
}


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def build_frame(header: int, addr: int, b3: int, b4: int, b5: int) -> bytes:
    body = bytes([header, FRAME_LEN, addr & 0xFF, b3 & 0xFF, b4 & 0xFF, b5 & 0xFF])
    crc = crc16_modbus(body)
    return body + bytes([crc & 0xFF, (crc >> 8) & 0xFF])


def frame_ok(f: bytes) -> bool:
    if len(f) != FRAME_LEN or f[1] != FRAME_LEN:
        return False
    crc = crc16_modbus(f[:6])
    return f[6] == (crc & 0xFF) and f[7] == ((crc >> 8) & 0xFF)


def s24(v: int) -> int:
    """有符号 -> 24 位补码"""
    return v & 0xFFFFFF


class Head:
    """一个治疗头（电机驱动板）的仿真状态"""

    def __init__(self, addr: int):
        self.addr = addr
        self.present = True
        # 0xAA / 0x54 可配置参数（出厂默认，见设计文档 §2 冻结帧表）
        self.closed_loop = True
        self.stall_time = 0x14
        self.pole_pairs = 2
        self.current_limit = 0x4D
        self.soft_start = 0x10
        # 运行状态
        self.target_rpm = 0
        self.rpm = 0.0
        self.direction = 0          # 0=A 1=B
        self.braking = False
        self.fault = 0
        self.temp = 32.0
        self.position = 0           # 24 位有符号计数：6*极对数 counts/圈
        self.last_active = time.monotonic()

    @property
    def accel(self) -> float:
        # 缓启动值越大加速越慢（0x10 -> ≈1176 RPM/s）
        return 20000.0 / (self.soft_start + 1)

    def step(self, dt: float):
        if not self.present:
            return
        if self.fault:
            self.target_rpm = 0
        if self.braking:
            self.rpm = max(0.0, self.rpm - 8000.0 * dt)
            if self.rpm <= 0:
                self.braking = False
        else:
            target = float(self.target_rpm)
            if self.rpm < target:
                self.rpm = min(target, self.rpm + self.accel * dt)
            elif self.rpm > target:
                self.rpm = max(target, self.rpm - self.accel * 2.0 * dt)  # 滑行/减速
        # 位置积分（方向影响符号）
        sign = -1 if self.direction else 1
        self.position += sign * self.rpm / 60.0 * dt * 6 * self.pole_pairs
        self.position = int(self.position)
        if self.position > 0x7FFFFF:
            self.position -= 0x1000000
        elif self.position < -0x800000:
            self.position += 0x1000000
        # 温度：运行微升，静止回落
        target_temp = 32.0 + (25.0 * self.rpm / MAX_SPEED_VALUE if self.rpm > 0 else 0.0)
        self.temp += (target_temp - self.temp) * dt * 0.05

    @property
    def running(self) -> bool:
        return self.rpm > 5.0

    def handle(self, f: bytes) -> bytes | None:
        """处理一条主机帧，返回应答帧或 None（不应答/广播）"""
        if not frame_ok(f):
            return None                     # 防御：CRC/长度不过一律静默（协议规定）
        cmd, addr = f[0], f[2]
        if cmd == 0x60:                     # 广播：无应答（本软件不应使用）
            if addr == 0xEE:
                self._apply_ctrl(f[3] << 8 | f[4], f[5])
            return None
        if addr != self.addr:
            return None                     # 不是我的地址：静默
        self.last_active = time.monotonic()

        if cmd == 0x55:                     # 电机控制
            if self.fault and f[5] not in (0x02, 0x03, 0x04):
                pass                        # 故障中：普通运行指令仍应答成功但不执行
            else:
                self._apply_ctrl(f[3] << 8 | f[4], f[5])
            return build_frame(0x11, addr, 0xAA, 0x00, 0x00)

        if cmd == 0xAA:                     # 出厂初始化（单头！）
            if self.running:
                return build_frame(0x11, addr, 0xEE, 0x00, 0x00)
            self.closed_loop = bool(f[3] & 0x10)
            self.stall_time = f[4]
            self.pole_pairs = f[5]
            return build_frame(0x11, addr, 0xAA, 0x00, 0x00)

        if cmd == 0x54:                     # 参数配置
            self.current_limit, self.soft_start = f[3], f[4]
            return build_frame(0x11, addr, 0xAA, 0x00, 0x00)

        if cmd == 0x5D:                     # 霍尔矫正
            return build_frame(0x11, addr, 0xAA, 0x00, 0x01)  # 霍尔状态0 方向提示1

        if cmd == 0x56:                     # 读转速
            v = int(round(self.rpm))
            return build_frame(0x12, addr, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF)

        if cmd == 0x57:                     # 读位置
            v = s24(self.position)
            return build_frame(0x13, addr, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF)

        if cmd == 0x58:                     # 读故障/温度/状态
            fb = self.fault | (0x80 if self.running else 0x00)
            return build_frame(0x14, addr, fb, int(self.temp) & 0xFF,
                               1 if self.running else 0)
        return None                         # 未知指令：静默（协议规定无应答）

    def _apply_ctrl(self, value: int, status: int):
        if status in (0x02, 0x03):          # 故障复位
            self.fault = 0
            self.braking = False
            self.direction = status & 0x01
        if status == 0x04:                  # 刹车
            self.braking = True
            self.target_rpm = 0
            return
        if status in (0x00, 0x01):
            self.direction = status
        closed = bool(value & CLOSED_LOOP_BIT)
        mag = value & 0x0FFF
        if mag < COAST_THRESHOLD:
            self.target_rpm = 0             # 滑行停止
        elif closed and mag >= MIN_CLOSED_LOOP_RPM:
            self.target_rpm = min(mag, MAX_SPEED_VALUE)
        elif not closed:
            # 开环占空比 -> 近似转速（仿真：占空比比例 × 上限）
            self.target_rpm = int(mag / MAX_SPEED_VALUE * MAX_SPEED_VALUE)
        self.braking = False


class Bus:
    def __init__(self, addrs, verbose=False):
        self.heads = {a: Head(a) for a in addrs}
        self.verbose = verbose
        self.lock = threading.Lock()

    def head_by_addr(self, addr):
        with self.lock:
            return self.heads.get(addr)

    def step(self, dt):
        with self.lock:
            for h in self.heads.values():
                h.step(dt)

    def set_fault(self, addr, name):
        with self.lock:
            h = self.heads.get(addr)
            if h:
                h.fault |= FAULT_BITS[name]
                h.target_rpm = 0

    def clear_fault(self, addr):
        with self.lock:
            h = self.heads.get(addr)
            if h:
                h.fault = 0

    def set_present(self, addr, present):
        with self.lock:
            h = self.heads.get(addr)
            if h:
                h.present = present
                if not present:
                    h.rpm = 0.0
                    h.target_rpm = 0

    def dump(self):
        with self.lock:
            for a, h in sorted(self.heads.items()):
                print(f"  addr={a} present={h.present} target={h.target_rpm} "
                      f"rpm={h.rpm:.0f} dir={'B' if h.direction else 'A'} "
                      f"fault=0x{h.fault:02X} temp={h.temp:.1f}C pos={h.position}")


def main():
    ap = argparse.ArgumentParser(description="北京枢点 RS485 电机驱动板模拟器")
    ap.add_argument("--port", required=True, help="串口设备（com0com/socat 虚拟对的一端）")
    ap.add_argument("--addrs", default="1,2,3,4,5,6", help="模拟的 485 地址列表")
    ap.add_argument("--baud", type=int, default=57600)
    ap.add_argument("--verbose", action="store_true", help="打印总线流量")
    args = ap.parse_args()
    addrs = [int(x, 0) for x in args.addrs.split(",") if x.strip()]
    bus = Bus(addrs, args.verbose)

    ser = serial.Serial(args.port, args.baud, timeout=0.02)
    print(f"[sim] {args.port} @ {args.baud} 8N1, 模拟地址: {addrs}")
    print("[sim] 命令: fault/clear/unplug/plug/state/quit（--help 见文件头）")

    stop = threading.Event()

    def physics():
        while not stop.is_set():
            bus.step(0.05)
            stop.wait(0.05)

    def console():
        for line in sys.stdin:
            parts = line.split()
            if not parts:
                continue
            cmd = parts[0].lower()
            try:
                if cmd == "quit":
                    stop.set()
                    return
                elif cmd == "state":
                    bus.dump()
                elif cmd == "fault" and len(parts) == 3:
                    bus.set_fault(int(parts[1], 0), parts[2].lower())
                elif cmd == "clear" and len(parts) == 2:
                    bus.clear_fault(int(parts[1], 0))
                elif cmd == "unplug" and len(parts) == 2:
                    bus.set_present(int(parts[1], 0), False)
                elif cmd == "plug" and len(parts) == 2:
                    bus.set_present(int(parts[1], 0), True)
                else:
                    print("[sim] 未知命令")
            except (ValueError, KeyError) as e:
                print(f"[sim] 命令错误: {e}")

    threading.Thread(target=physics, daemon=True).start()
    threading.Thread(target=console, daemon=True).start()

    buf = bytearray()
    master_headers = {0xAA, 0x54, 0x5D, 0x55, 0x60, 0x59, 0x5A, 0x56, 0x57, 0x58}
    try:
        while not stop.is_set():
            chunk = ser.read(64)
            if chunk:
                buf += chunk
            # 滑窗找帧：帧头合法 + CRC 通过才处理（噪声直接丢弃，协议规定无应答）
            i = 0
            while i + FRAME_LEN <= len(buf):
                if buf[i] in master_headers and buf[i + 1] == FRAME_LEN:
                    f = bytes(buf[i:i + FRAME_LEN])
                    if frame_ok(f):
                        if args.verbose:
                            print(f"[rx] {f.hex(' ').upper()}")
                        replies = []
                        for h in list(bus.heads.values()):
                            if not h.present:
                                continue
                            r = h.handle(f)
                            if r:
                                replies.append(r)
                        for r in replies:
                            time.sleep(0.002)  # 协议：2ms 换向间隔
                            ser.write(r)
                            if args.verbose:
                                print(f"[tx] {r.hex(' ').upper()}")
                        del buf[i:i + FRAME_LEN]
                        continue
                i += 1
            if len(buf) > 256:
                del buf[:-FRAME_LEN]
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        ser.close()
        print("[sim] 退出")


if __name__ == "__main__":
    main()
