#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""追溯矩阵生成器（决策 D25：活文档不腐化）。

用法：
  python tools/trm/gen_trm.py            # 生成 docs/06-TRM-追溯矩阵.md
  python tools/trm/gen_trm.py --check    # 只校验零缺口（DoD#4），缺口时退出码 1

数据流：
  docs/02-SRS-软件需求规格.md  表格行（SRS-xxx | 需求 | 来源 | 验证）
  + src/ qml/ CMakeLists 的 `// @srs SRS-xxx` 注解   → 需求↔代码
  + tests/*.cpp 的 `// @srs` 注解                     → 需求↔测试
  + tests/CMakeLists.txt 的 add_test 注册名（含主 SRS ID）→ ctest 可执行载体

零缺口规则：
  1) 每条 SRS 必须有 ≥1 处代码注解；
  2) 验证方式以 T 开头的 SRS 必须有 ≥1 处测试注解；
  3) DEFERRED 显式推迟项豁免缺失部分，但必须挂 T/ISS 编号并单列。
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRS_DOC = ROOT / "docs" / "02-SRS-软件需求规格.md"
TESTS_CMAKE = ROOT / "tests" / "CMakeLists.txt"
OUT_DOC = ROOT / "docs" / "06-TRM-追溯矩阵.md"

# 显式推迟项（必须关联设计方案待办 T-x 或问题日志 ISS-xxx）
DEFERRED = {
    "SRS-005": "T3：Kiosk/systemd 部署工件随 RK 板冻结产出（上电清理部分已由 SRS-018 验证）",
    "SRS-044": "T11/ISS-012：方向持久化 + 绑定流程低速试运行 UI 待补（方向字段与 Idle 限定已实现）",
    "SRS-061": "T11/ISS-012：烧录向导末步「方向试运行确认」随 SRS-044 补齐",
    "SRS-070": "ISS-013：预设修改的 PIN 门禁与 D9 冲突，待用户决策（设置/绑定/导出已门禁）",
}

SRS_RE = re.compile(r"SRS-\d{3}")


def parse_srs_rows() -> list[dict]:
    rows = []
    for line in SRS_DOC.read_text(encoding="utf-8").splitlines():
        if not line.startswith("|"):
            continue
        cells = [c.strip() for c in re.split(r"(?<!\\)\|", line)]
        # cells[0] 为空串（行首 | 之前）
        cells = [c for c in (cells[1:-1] if cells[-1] == "" else cells[1:])]
        if len(cells) < 4 or not re.fullmatch(r"SRS-\d{3}", cells[0]):
            continue
        rows.append({"id": cells[0], "text": cells[1], "src": cells[2], "ver": cells[3]})
    return rows


def scan_annotations() -> tuple[dict, dict]:
    """返回 (code_map, test_map)：srs -> [相对路径…]"""
    code_map: dict[str, list[str]] = {}
    test_map: dict[str, list[str]] = {}
    files = []
    files += (ROOT / "src").rglob("*.h")
    files += (ROOT / "src").rglob("*.cpp")
    files += (ROOT / "qml").glob("*.qml")
    files += [ROOT / "CMakeLists.txt"]
    files += list((ROOT / "src").glob("*/CMakeLists.txt"))
    files += list((ROOT / "tests").glob("*.cpp"))
    for f in files:
        try:
            head = "\n".join(f.read_text(encoding="utf-8").splitlines()[:6])
        except (OSError, UnicodeDecodeError):
            continue
        m = re.search(r"@srs\s+((?:SRS-\d{3}[\s,]*)+)", head)
        if not m:
            continue
        rel = str(f.relative_to(ROOT)).replace("\\", "/")
        target = test_map if rel.startswith("tests/") else code_map
        for srs in SRS_RE.findall(m.group(1)):
            target.setdefault(srs, [])
            if rel not in target[srs]:
                target[srs].append(rel)
    return code_map, test_map


def parse_ctest_registrations() -> dict:
    """add_test(NAME <reg> COMMAND <exe> …) → exe 源文件 -> ctest 注册名"""
    exe_to_reg = {}
    text = TESTS_CMAKE.read_text(encoding="utf-8")
    # 直接 add_test
    for m in re.finditer(r"add_test\(NAME\s+(\S+)\s+COMMAND\s+(\w+)", text):
        exe_to_reg[m.group(2)] = m.group(1)
    # 函数封装：add_core_test/add_audit_test(<exe> <reg>)
    for m in re.finditer(r"add_(?:core|audit)_test\((\w+)\s+(\S+)\)", text):
        exe_to_reg[m.group(1)] = m.group(2)
    return exe_to_reg


def short(p: str) -> str:
    return (p.replace("src/", "").replace("tests/", "").replace("qml/", "qml:")
            .replace("CMakeLists.txt", "CMake"))


def git_rev() -> str:
    try:
        out = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT,
                             capture_output=True, text=True, timeout=10)
        return out.stdout.strip() if out.returncode == 0 else "unknown"
    except Exception:
        return "unknown"


def main() -> int:
    ap = argparse.ArgumentParser(description="生成/校验需求↔代码↔测试追溯矩阵")
    ap.add_argument("--check", action="store_true", help="只校验零缺口，不写文件")
    args = ap.parse_args()

    rows = parse_srs_rows()
    if not rows:
        print("错误：未从 SRS 文档解析到任何需求行", file=sys.stderr)
        return 2
    code_map, test_map = scan_annotations()
    exe_to_reg = parse_ctest_registrations()
    # srs -> ctest 注册名列表
    reg_map: dict[str, list[str]] = {}
    for srs, files in test_map.items():
        for f in files:
            exe = Path(f).stem
            if exe in exe_to_reg:
                reg_map.setdefault(srs, [])
                if exe_to_reg[exe] not in reg_map[srs]:
                    reg_map[srs].append(exe_to_reg[exe])

    gaps, deferred, full = [], [], []
    body = []
    for r in rows:
        sid = r["id"]
        codes = code_map.get(sid, [])
        regs = reg_map.get(sid, [])
        needs_test = r["ver"].startswith("T")
        miss = []
        if not codes:
            miss.append("代码")
        if needs_test and not regs:
            miss.append("测试")
        if miss:
            if sid in DEFERRED:
                deferred.append((sid, miss))
                status = "⏸ 部分推迟"
            else:
                gaps.append((sid, miss))
                status = "❌ 缺口:" + "+".join(miss)
        elif sid in DEFERRED:
            deferred.append((sid, []))
            status = "⏸ 推迟项在案"
        else:
            full.append(sid)
            status = "✅"
        text = r["text"] if len(r["text"]) <= 80 else r["text"][:78] + "…"
        body.append(
            f"| {sid} | {text} | {r['src']} | {r['ver'][:2]} | "
            f"{', '.join(short(c) for c in codes) or '—'} | "
            f"{', '.join(regs) or ('—' if not needs_test else '❌')} | {status} |")

    now = datetime.now(timezone.utc).astimezone().strftime("%Y-%m-%d %H:%M")
    out = [
        "# MDS 需求↔代码↔测试 追溯矩阵（TRM）",
        "",
        "> **本文件由 `tools/trm/gen_trm.py` 自动生成 —— 请勿手改。**",
        f"> 生成时间：{now} · git 基线：{git_rev()} · 校验规则见脚本头注（DoD#4 零缺口）",
        "",
        "| 文档编号 | MDS-TRM-006 |",
        "|---|---|",
        "| 版本 | 自动生成（随 git 基线） |",
        "",
        f"**汇总：SRS 共 {len(rows)} 条 · 完整覆盖 {len(full)} · 推迟项在案 {len(deferred)} · "
        f"缺口 {len(gaps)}**",
        "",
        "| SRS | 需求（截断） | 来源 | 验证 | 代码 | 测试(ctest) | 状态 |",
        "|---|---|---|---|---|---|---|",
    ]
    out += body
    if deferred:
        out += ["", "## 推迟项说明（豁免明细，均挂待办/问题编号）", ""]
        out += [f"- **{sid}**：{DEFERRED[sid]}" + (f"（当前缺：{'、'.join(m)}）" if m else "")
                for sid, m in deferred]
    if gaps:
        out += ["", "## ❌ 缺口（必须清零才算 DoD#4 达成）", ""]
        out += [f"- **{sid}**：缺 {'、'.join(m)}" for sid, m in gaps]
    out.append("")

    if args.check:
        if gaps:
            print(f"TRM 校验失败：{len(gaps)} 条缺口")
            for sid, m in gaps:
                print(f"  {sid}: 缺 {'、'.join(m)}")
            return 1
        print(f"TRM 校验通过：{len(rows)} 条 SRS，完整覆盖 {len(full)}，"
              f"推迟项在案 {len(deferred)}，零缺口")
        return 0

    OUT_DOC.write_text("\n".join(out), encoding="utf-8")
    print(f"已生成 {OUT_DOC.relative_to(ROOT)}：{len(rows)} 条 SRS，"
          f"完整覆盖 {len(full)}，推迟 {len(deferred)}，缺口 {len(gaps)}")
    return 1 if gaps else 0


if __name__ == "__main__":
    sys.exit(main())
