#!/usr/bin/env python3
"""
UWB 시리얼 로그 분석 도구.

ESP32에서 흘러나온 시리얼 로그를 받아서:
  - anchor 별 raw 거리 mean / std / min / max / N
  - 위치(Position) mean / std
  - 알려진 위치(--gt) 입력 시 anchor 별 bias 자동 계산
  - Position 라인의 d1~d4 (보정 후 평균) 통계

사용 예:
  python tools/analyze_log.py log.txt
  python tools/analyze_log.py log.txt --gt 3.37 2.68
  python tools/analyze_log.py log.txt --gt 3.37 2.68 \\
      --anchor-x 0 6.74 0 6.74 --anchor-y 0 0 5.35 0

로그 캡쳐 방법 (Windows):
  pio device monitor > log.txt   # Ctrl+C 로 종료
"""

import argparse
import math
import re
import sys
from collections import defaultdict

# Windows 콘솔 한글 출력 (cp949 → UTF-8)
if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except AttributeError:
        pass  # Python < 3.7


ANCHOR_LINE = re.compile(
    r"Anchor:\s*([0-9a-fA-F]+),\s*Distance:\s*([\d.]+)m"
)
POSITION_LINE = re.compile(
    r"Position:\s*x=([\d.+-]+),\s*y=([\d.+-]+)\s*"
    r"\(d1=([\d.+-]+)\s+d2=([\d.+-]+)\s+d3=([\d.+-]+)\s+d4=([\d.+-]+)\)"
)


def parse_log(path):
    """로그 파일 1회 스캔으로 anchor 거리 / Position / 보정 d 모두 수집."""
    raw = defaultdict(list)         # aid -> [raw distances]
    positions = []                  # [(x, y), ...]
    corrected_d = defaultdict(list) # slot 0~3 -> [d_i in Position lines]

    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = ANCHOR_LINE.search(line)
            if m:
                aid = int(m.group(1), 16)   # 펌웨어가 HEX 로 출력
                d = float(m.group(2))
                raw[aid].append(d)
                continue
            m = POSITION_LINE.search(line)
            if m:
                positions.append((float(m.group(1)), float(m.group(2))))
                for i in range(4):
                    corrected_d[i].append(float(m.group(3 + i)))
    return raw, positions, corrected_d


def stats(values):
    """(mean, std, n) — 표본 1개도 가능."""
    n = len(values)
    if n == 0:
        return (0.0, 0.0, 0)
    mean = sum(values) / n
    var = sum((v - mean) ** 2 for v in values) / n
    return (mean, math.sqrt(var), n)


def fmt_section(title):
    print(f"\n== {title} ==")


def main():
    p = argparse.ArgumentParser(
        description="UWB 시리얼 로그 분석 — anchor 별 통계 + 위치 오차"
    )
    p.add_argument("logfile", help="시리얼 캡쳐 텍스트 파일 경로")
    p.add_argument(
        "--gt",
        type=float,
        nargs=2,
        metavar=("X", "Y"),
        help="태그의 실제 위치 (m). 입력 시 bias 자동 계산.",
    )
    p.add_argument(
        "--anchor-x",
        type=float,
        nargs=4,
        default=[0.00, 6.74, 0.00, 6.74],
        help="Anchor 4개 X 좌표 (기본: main.cpp 현재 값)",
    )
    p.add_argument(
        "--anchor-y",
        type=float,
        nargs=4,
        default=[0.00, 0.00, 5.35, 5.35],
        help="Anchor 4개 Y 좌표 (기본: main.cpp 현재 값)",
    )
    args = p.parse_args()

    raw, positions, corrected_d = parse_log(args.logfile)

    if not raw and not positions:
        print(f"(!) {args.logfile} 에서 Anchor/Position 라인 못 찾음.")
        sys.exit(1)

    fmt_section(f"로그: {args.logfile}")

    # ── Anchor 별 raw 거리 ───────────────────────────────────────
    fmt_section("Anchor 별 raw 거리")
    header = f"{'ID':>4}  {'N':>5}  {'mean(m)':>9}  {'std(cm)':>9}  {'min':>6}  {'max':>6}"
    if args.gt:
        header += f"  {'실제(m)':>9}  {'bias(cm)':>9}"
    print(header)
    print("-" * len(header))
    for aid in sorted(raw.keys()):
        vals = raw[aid]
        mean, std, n = stats(vals)
        line = (
            f"{aid:>4}  {n:>5}  {mean:>9.3f}  "
            f"{std * 100:>9.2f}  {min(vals):>6.3f}  {max(vals):>6.3f}"
        )
        slot = aid - 1   # ANCHOR_ID 1~4 → slot 0~3 (현재 코드 기준)
        if args.gt and 0 <= slot < 4:
            ax = args.anchor_x[slot]
            ay = args.anchor_y[slot]
            true_d = math.hypot(args.gt[0] - ax, args.gt[1] - ay)
            bias_cm = (mean - true_d) * 100
            line += f"  {true_d:>9.3f}  {bias_cm:>+9.2f}"
        print(line)

    # ── 위치(Position) 통계 ──────────────────────────────────────
    fmt_section(f"Position 통계 (N = {len(positions)})")
    if positions:
        xs = [pp[0] for pp in positions]
        ys = [pp[1] for pp in positions]
        mx, sx, _ = stats(xs)
        my, sy, _ = stats(ys)
        print(f"  x: mean = {mx:.3f} m,  std = {sx * 100:.2f} cm")
        print(f"  y: mean = {my:.3f} m,  std = {sy * 100:.2f} cm")
        if args.gt:
            ex_cm = (mx - args.gt[0]) * 100
            ey_cm = (my - args.gt[1]) * 100
            err_cm = math.hypot(ex_cm, ey_cm)
            print(f"  실제 위치:   ({args.gt[0]:.3f}, {args.gt[1]:.3f})")
            print(f"  위치 bias:   ({ex_cm:+.2f}, {ey_cm:+.2f}) cm")
            print(f"  거리 오차:   {err_cm:.2f} cm")
            verdict = "[PASS] 통과" if err_cm <= 15 else "[FAIL] 검증기준(<=15cm) 초과"
            print(f"  {verdict}")
    else:
        print("  Position 라인 없음 — 6 cycle 누적 못 채웠거나 LS 실패.")

    # ── Position 라인의 보정 후 d ────────────────────────────────
    if corrected_d:
        fmt_section("Position 라인 d (mean·std)")
        for i in range(4):
            mean, std, n = stats(corrected_d[i])
            print(f"  d{i + 1}: mean = {mean:.3f} m,  std = {std * 100:.2f} cm  (N = {n})")

    print()


if __name__ == "__main__":
    main()
