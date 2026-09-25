#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""跨语言协议一致性测试：C++ 冻结帧表（tests/test_frame_codec.cpp 同源向量）
喂给 Python 从机模拟器（tools/simulator/rs485_sim.py），验证两个实现
对协议的理解一致。任何一方对帧格式/CRC/语义的理解漂移都会在此暴露。

运行：python tests/test_sim_consistency.py   （无需串口，纯逻辑）
"""
from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools", "simulator"))
import rs485_sim as s  # noqa: E402

FAILS = 0


def check(name: str, cond: bool) -> None:
    global FAILS
    print(("PASS " if cond else "FAIL ") + name)
    if not cond:
        FAILS += 1


def master_frame(header, addr, b3, b4, b5):
    body = bytes([header, 8, addr, b3, b4, b5])
    c = s.crc16_modbus(body)
    return body + bytes([c & 0xFF, (c >> 8) & 0xFF])


def reply_val(r: bytes) -> int:
    return (r[3] << 16) | (r[4] << 8) | r[5]


def main() -> int:
    h = s.Head(1)

    # ---- 冻结帧表向量（与 C++ 单测同一来源：设计方案 §2）----
    # AA 08 01 10 14 02 77 28 出厂初始化：闭环+堵转2s+极对数2
    r = h.handle(bytes.fromhex("AA08011014027728"))
    check("AA ack frame", r is not None and s.frame_ok(r) and r[0] == 0x11 and r[3] == 0xAA)
    check("AA params applied", h.closed_loop and h.pole_pairs == 2 and h.stall_time == 0x14)

    # 54 08 01 4D 10 00 70 25 限流0x4D+缓启动0x10+延时0
    r = h.handle(bytes.fromhex("5408014D10007025"))
    check("54 ack", r is not None and s.frame_ok(r) and r[3] == 0xAA)
    check("54 params applied", h.current_limit == 0x4D and h.soft_start == 0x10)

    # 55 08 01 17 08 00 5B E7 闭环 1800RPM(30Hz) A向
    r = h.handle(bytes.fromhex("5508011708005BE7"))
    check("55 run ack", r is not None and s.frame_ok(r) and r[0] == 0x11)
    check("55 target=1800 dirA", h.target_rpm == 1800 and h.direction == 0)

    # 55 08 01 17 08 01 9A 27 同转速 B向
    h.handle(bytes.fromhex("5508011708019A27"))
    check("55 dirB", h.direction == 1)

    # 物理仿真：缓启动 0x10 下 5 秒应爬升到 1800RPM 附近
    for _ in range(100):
        h.step(0.05)
    check("soft-start ramp to 1800", abs(h.rpm - 1800) < 50)

    # 56 08 01 33 33 33 48 FA 读转速 -> 0x12 应答 ≈1800
    r = h.handle(bytes.fromhex("56080133333348FA"))
    check("56 speed reply", s.frame_ok(r) and r[0] == 0x12 and abs(reply_val(r) - 1800) < 50)

    # 58 08 01 33 33 33 49 D4 读状态 -> 运行位 0x80、状态字节 1
    r = h.handle(bytes.fromhex("580801333333 49D4".replace(" ", "")))
    check("58 running flags", s.frame_ok(r) and r[0] == 0x14 and (r[3] & 0x80) and r[5] == 1)

    # 55 08 01 00 00 00 EC 23 滑行停止
    r = h.handle(bytes.fromhex("550801000000EC23"))  # 55 08 01 00 00 00 EC 23
    check("coast stop", s.frame_ok(r) and h.target_rpm == 0)

    # ---- 故障与复位语义（决策 D3）----
    h.fault |= s.FAULT_BITS["stall"]
    h.handle(bytes.fromhex("5508011708005BE7"))
    check("fault blocks run cmd", h.target_rpm == 0)
    r = h.handle(bytes.fromhex("5508010000026DE2"))  # 复位+A向
    check("reset clears fault", h.fault == 0 and s.frame_ok(r))

    # ---- 防御性：坏 CRC / 错误地址 一律静默 ----
    check("bad crc ignored", h.handle(bytes.fromhex("5508011708005BE8")) is None)
    check("other addr ignored", h.handle(master_frame(0x58, 2, 0x33, 0x33, 0x33)) is None)

    # ---- 24 位补码位置（PDF p12 例：0xFFF520 = -2784）----
    h.position = -2784
    r = h.handle(master_frame(0x57, 1, 0x33, 0x33, 0x33))
    raw = reply_val(r)
    pos = raw - 0x1000000 if raw >= 0x800000 else raw
    check("position 24-bit signed", s.frame_ok(r) and r[0] == 0x13 and pos == -2784)

    print("----", "ALL PASS" if FAILS == 0 else f"{FAILS} FAILS")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
