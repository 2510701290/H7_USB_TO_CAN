from __future__ import annotations

import argparse
import math
import re
import sys
from collections import Counter
from datetime import date
from pathlib import Path
from statistics import mean, median, pstdev, stdev

# --------------------------------------------------------------------- 默认值

DEFAULT_PATH = Path("/home/hjq/Zephyr_text/H7_Work/docs/CS_M_2026_10_6.txt")
DEFAULT_RECV_ID = 0x205          # H7 发出的帧
DEFAULT_SEND_ID = 0x010          # 上位机回的帧
DROP_OVER_US = 100_000           # 超过这个间隔就不算「一次正常应答」（原版 100 ms）
OVER_LIMIT_US = 1_000            # 硬指标阈值：>1 ms 就是超标

_DATE_RE = re.compile(r"\A\d{4}-\d{2}-\d{2}\Z")
_TIME_RE = re.compile(r"\A(\d{2}):(\d{2}):(\d{2})\.(\d{1,9})\Z")
_INT_RE = re.compile(r"\A\d+\Z")

# --------------------------------------------------------------------- 时间解析


def _date_ordinal(d: str) -> int:
    """'2026-10-06' → 儒略日序号。只用于跨零点时保持时间轴单调，单位是天。"""
    return date(int(d[0:4]), int(d[5:7]), int(d[8:10])).toordinal()


def _hms_to_us(t: str) -> int | None:
    """'17:15:11.674287' → 当天内的微秒数。手写切片比 strptime 快一个数量级。"""
    m = _TIME_RE.match(t)
    if m is None:
        return None
    hh, mm, ss, frac = m.groups()
    return ((int(hh) * 60 + int(mm)) * 60 + int(ss)) * 1_000_000 + int(frac.ljust(6, "0")[:6])


def _parse_time_column(t: str) -> int | None:
    """
    老格式的 time 列：'秒.毫秒.微秒' → 微秒增量。
    例：'000.000.251' → 251；'66182.814.175' → 66182814175。
    """
    parts = t.split(".")
    if len(parts) != 3 or not all(_INT_RE.match(p) for p in parts):
        return None
    s, ms, us = (int(p) for p in parts)
    return (s * 1000 + ms) * 1000 + us


def _parse_id(s: str) -> int | None:
    """
    CAN ID 一律按**十六进制**理解，这也是 CAN 工具圈的惯例（candump/cansend 同理）。

        '0x205' → 517      新格式，带前缀
        '205'   → 517      老格式，不带前缀但同样是十六进制

    注意：裸数字如果按十进制读，老格式的 '205' 会变成 0xCD，一帧都匹配不上。
    """
    s = s.strip()
    try:
        return int(s[2:], 16) if s[:2].lower() == "0x" else int(s, 16)
    except ValueError:
        return None


# --------------------------------------------------------------------- 读文件


def _open_auto(path: Path):
    """
    先按 UTF-8 读；解不开再退回 gb18030。
    老日志的中文列名（接收/发送）是 GBK，新日志是纯 ASCII。

    注意：必须先嗅探一小段再开文本流——`path.open()` 本身不会解码，
    编码错误要等到真正迭代时才抛出来，那时已经晚了。
    """
    with path.open("rb") as f:
        head = f.read(1 << 16)
    for enc in ("utf-8", "gb18030"):
        try:
            head.decode(enc)
        except UnicodeDecodeError:
            continue
        return path.open("r", encoding=enc, errors="replace"), enc
    return path.open("r", encoding="latin-1"), "latin-1"


def read_frames(path: Path, recv_id: int, send_id: int):
    """
    读整个日志 → (frames, info)

    frames: [(t_us, can_id, payload_bytes), ...]，t_us 单调递增
    info:   统计信息字典（格式、编码、注释行数、坏行数、跳过行数）
    """
    wanted = {recv_id, send_id}
    frames: list[tuple[int, int, bytes]] = []

    day_cache: dict[str, int] = {}
    fmt = "?"
    n_comment = n_header = n_bad = n_other = 0
    sample_line = ""       # 第一条看不懂的行，用于报错时回显
    prev_us = 0          # 老格式：增量累加用
    base_us = 0          # 新格式：日期换算成的大基数

    fh, encoding = _open_auto(path)
    with fh:
        for raw in fh:
            line = raw.strip()
            if not line:
                continue
            if line[0] == "#":                      # 新格式的表头注释
                n_comment += 1
                continue

            p = line.split()
            if len(p) < 6:
                n_header += 1
                continue

            if _DATE_RE.match(p[0]):                # ---------- 新格式
                if fmt == "?":
                    fmt = "gcan"
                tod = _hms_to_us(p[1])
                cid = _parse_id(p[3]) if len(p) > 3 else None
                if tod is None or cid is None:
                    n_bad += 1
                    continue
                if cid not in wanted:
                    n_other += 1
                    continue
                day = day_cache.get(p[0])
                if day is None:                     # 一天只换算一次
                    day = day_cache[p[0]] = _date_ordinal(p[0]) * 86_400 * 1_000_000
                t_us = day + tod
                body = p[6:]

            elif _INT_RE.match(p[0]):               # ---------- 老格式
                if fmt == "?":
                    fmt = "legacy"
                delta = _parse_time_column(p[1])
                cid = _parse_id(p[3]) if len(p) > 3 else None
                if delta is None or cid is None:
                    n_bad += 1
                    continue
                if cid not in wanted:               # 仍要把增量累加进时间轴
                    n_other += 1
                    prev_us += delta
                    continue
                if prev_us == 0:                    # 第一行：它本身是个绝对时间戳
                    base_us = delta
                    prev_us = delta
                    t_us = delta
                else:
                    prev_us += delta
                    t_us = prev_us
                body = p[7:]

            else:                                   # 列名行之类，不是数据
                n_header += 1
                if not sample_line:
                    sample_line = line
                continue

            try:
                payload = bytes(int(b, 16) for b in body)
            except ValueError:
                payload = b""

            frames.append((t_us, cid, payload))

    info = {
        "format": {"gcan": "GCAN 工具（绝对时间戳）",
                   "legacy": "老格式（逐行增量时间）",
                   "?": "未知"}[fmt],
        "encoding": encoding,
        "comments": n_comment,
        "headers": n_header,
        "bad_lines": n_bad,
        "other_ids": n_other,
        "base_us": base_us,
        "sample_line": sample_line,
    }
    return frames, info


# --------------------------------------------------------------------- 统计小工具


def percentile(sorted_xs: list[int], q: float) -> int:
    """最近秩法分位数。q=0.999 → 第 99.9 百分位。"""
    if not sorted_xs:
        return 0
    i = math.ceil(q * len(sorted_xs)) - 1
    return sorted_xs[min(max(i, 0), len(sorted_xs) - 1)]


def fmt_us(x: float) -> str:
    return f"{x:,.3f}"


# --------------------------------------------------------------------- 主流程


def find_stalls(frames) -> tuple[list[tuple[int, int, int, int]], set[int]]:
    """
    找「时间戳停滞」段：GCAN 工具偶尔会连续若干帧共用同一个时间戳。

    返回 (段列表, 被污染的帧索引集合)，段为 (起, 止, 时间戳, 帧数)。

    这不是总线上的真实现象——之前做延迟归因时踩过这个坑：130 s 的记录里出现
    两段各 20 帧取同一时间戳，直接把某两个 10 s 窗口的均值拉偏了 2 us。
    受影响的那几对 205→010 会算出 0 us 的假延迟，统计时必须知道它们的存在。
    """
    runs: list[tuple[int, int, int, int]] = []
    dirty: set[int] = set()
    n = len(frames)
    i = 0
    while i < n:
        j = i
        while j + 1 < n and frames[j + 1][0] == frames[i][0]:
            j += 1
        if j > i:
            runs.append((i, j, frames[i][0], j - i + 1))
            dirty.update(range(i, j + 1))
        i = j + 1
    return runs, dirty


def analyse(frames, recv_id: int, send_id: int, drop_over: int) -> dict:
    """配对 205 → 010，算出延迟列表。返回结果字典。"""
    # 帧数统计
    counts = Counter(f[1] for f in frames)

    stalls, dirty = find_stalls(frames)

    # 配对：只认日志里**紧挨着**的 recv_id → send_id
    intervals: list[int] = []
    stalled: list[int] = []          # 被时间戳停滞污染的那部分，单独留一份
    skipped = 0
    unpaired = 0
    payload_match = payload_total = 0

    for i in range(len(frames) - 1):
        t0, id0, data0 = frames[i]
        if id0 != recv_id:
            continue
        if frames[i + 1][1] != send_id:
            unpaired += 1            # 源帧后面跟的不是回帧（可能丢帧/插了别的）
            continue
        t1, _, data1 = frames[i + 1]
        dt = t1 - t0
        if dt > drop_over:           # 中间断了一大截，不是一次正常应答
            skipped += 1
            continue
        if i in dirty or (i + 1) in dirty:
            stalled.append(dt)
            continue
        intervals.append(dt)
        payload_total += 1
        if data0 == data1:
            payload_match += 1

    return {
        "counts": counts,
        "total_frames": len(frames),
        "intervals": intervals,
        "stalled": stalled,
        "stalls": stalls,
        "dirty_frames": len(dirty),
        "skipped": skipped,
        "unpaired": unpaired,
        "payload_match": payload_match,
        "payload_total": payload_total,
        "span_us": (frames[-1][0] - frames[0][0]) if len(frames) > 1 else 0,
    }


def report(res: dict, info: dict, recv_id: int, send_id: int,
           drop_over: int, over_limit: int) -> None:
    intervals: list[int] = res["intervals"]
    N = len(intervals)

    tag = lambda i: f"0x{i:03X}"

    # ---------- 解析信息 ----------
    print("===== 日志 =====")
    print(f"- 格式        ：{info['format']}")
    print(f"- 编码        ：{info['encoding']}")
    print(f"- 数据行      ：{res['total_frames']:,}")
    if info["comments"]:
        print(f"- 注释行      ：{info['comments']}")
    if info.get("headers"):
        print(f"- 表头/跳过行 ：{info['headers']}")
    if info["bad_lines"]:
        print(f"- 解析失败行  ：{info['bad_lines']}")
    if info["other_ids"]:
        print(f"- 其它 ID 行  ：{info['other_ids']}")
    span = res["span_us"] / 1_000_000
    print(f"- 时间跨度    ：{span:.6f} s")
    if span > 0:
        print(f"- 平均帧率    ：{res['total_frames'] / span:,.1f} 帧/s")

    # ---------- 帧数统计 ----------
    print("\n===== 帧数统计 =====")
    for cid in sorted(res["counts"], reverse=True):
        print(f"- {tag(cid)} ：{res['counts'][cid]:,}")
    print(f"- 合计     ：{res['total_frames']:,}")

    # ---------- 配对 ----------
    print(f"\n===== 配对 =====")
    print(f"- {tag(recv_id)} -> {tag(send_id)} 有效样本数：{N:,}")
    if drop_over == float("inf"):
        print(f"- 被剔除的间隔（未设上限）：{res['skipped']:,}")
    else:
        print(f"- 被剔除的间隔（>{drop_over / 1000:.1f} ms）：{res['skipped']:,}")
    if res["unpaired"]:
        print(f"- 源帧后面不是回帧的：{res['unpaired']:,}")
    if res["stalled"]:
        print(f"- 因时间戳停滞而剔除：{len(res['stalled']):,}  "
              f"（其中 0 us 的假样本 {sum(1 for x in res['stalled'] if x == 0):,} 个）")

    if res["stalls"]:
        print(f"\n- ⚠️  检测到 GCAN 工具「时间戳停滞」{len(res['stalls'])} 段，"
              f"共 {res['dirty_frames']:,} 帧共用时间戳（工具伪影，不是总线现象）：")
        for lo, hi, ts, cnt in res["stalls"][:5]:
            print(f"      idx {lo}..{hi}   {cnt} 帧同取 {ts} us")
        if len(res["stalls"]) > 5:
            print(f"      …… 另有 {len(res['stalls']) - 5} 段")

    if res["payload_total"]:
        pm = res["payload_match"] / res["payload_total"] * 100
        print(f"\n- payload 与源帧一致的占比：{pm:.4f}%  "
              f"（{res['payload_match']:,}/{res['payload_total']:,}）")

    if N == 0:
        print("\n- 没有有效样本。")
        return

    # ---------- 整体统计（单位：微秒）----------
    mu = mean(intervals)
    print("\n===== 整体统计（单位：微秒）=====")
    print(f"- 平均间隔：{fmt_us(mu)} us")
    print(f"- 中位数  ：{fmt_us(median(intervals))} us")
    print(f"- 最小间隔：{min(intervals):,} us")
    print(f"- 最大间隔：{max(intervals):,} us")
    print(f"- 峰峰值  ：{max(intervals) - min(intervals):,} us")

    # ---------- 标准差 ----------
    sigma_p = pstdev(intervals)
    sigma_s = stdev(intervals)
    cv = sigma_p / mu if mu else 0
    print("\n===== 标准差 / 波动 =====")
    print(f"- 总体标准差 σp     ：{fmt_us(sigma_p)} us")
    print(f"- 样本标准差 σs     ：{fmt_us(sigma_s)} us")
    print(f"- 变异系数 CV=σp/μ  ：{cv:.4f}  ({cv * 100:.2f}%)")

    # ---------- 尾部（本项目最关心的一段）----------
    xs = sorted(intervals)
    print("\n===== 尾部分位数 =====")
    for q, name in ((0.5, "p50"), (0.9, "p90"), (0.99, "p99"),
                    (0.999, "p99.9"), (0.9999, "p99.99")):
        print(f"- {name:<7}：{percentile(xs, q):,} us")
    print(f"- {'max':<7}：{xs[-1]:,} us")
    print(f"- {'σs':<7}：{fmt_us(sigma_s)} us")

    print(f"\n===== 分档计数（速率 = 次数 / 时间跨度）=====")
    for lim in sorted({300, 500, 600, over_limit}):
        n = sum(1 for x in intervals if x >= lim)
        rate = n / span if span > 0 else 0
        print(f"- ≥{lim:>5} us ：{n:>6,} 次   {n / N * 100:8.4f}%   {rate:8.4f} 次/s")

    # ---------- 超过阈值 ----------
    over = [x for x in intervals if x > over_limit]
    n_over = len(over)
    print(f"\n===== 超过 {over_limit} us 的统计 =====")
    print(f"- 超过 {over_limit}us 的次数：{n_over:,}")
    print(f"- 超过 {over_limit}us 的占比：{n_over / N * 100:.4f}%")
    if n_over:
        print(f"- 超过 {over_limit}us 的平均：{fmt_us(mean(over))} us")
        print(f"- 超过 {over_limit}us 的最小：{min(over):,} us")
        print(f"- 超过 {over_limit}us 的最大：{max(over):,} us")
    else:
        print(f"- 没有超过 {over_limit}us 的样本。")

    # ---------- 最小 10% / 最大 10% ----------
    k = max(1, int(N * 0.1))
    print("\n===== 最小 10% =====")
    print(f"- 样本数：{k:,}")
    print(f"- 最小 10% 平均间隔：{fmt_us(mean(xs[:k]))} us")
    print("\n===== 最大 10% =====")
    print(f"- 样本数：{k:,}")
    print(f"- 最大 10% 平均间隔：{fmt_us(mean(xs[-k:]))} us")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description="解析上位机 CAN 日志，统计 0x205 → 0x010 的应答延迟",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""例子:
  python3 parse_can_log.py
  python3 parse_can_log.py ../docs/CS_M_2026_10_5.txt     # 老格式也能读
  python3 parse_can_log.py --drop-over 0                  # 不剔除任何间隔
""")
    ap.add_argument("path", nargs="?", default=str(DEFAULT_PATH), help="日志文件")
    ap.add_argument("--recv-id", default=hex(DEFAULT_RECV_ID), help="源帧 ID（默认 0x205）")
    ap.add_argument("--send-id", default=hex(DEFAULT_SEND_ID), help="回帧 ID（默认 0x010）")
    ap.add_argument("--drop-over", type=int, default=DROP_OVER_US,
                    help=f"间隔超过这个微秒数就当断链剔除（默认 {DROP_OVER_US}，0=不剔除）")
    ap.add_argument("--over-limit", type=int, default=OVER_LIMIT_US,
                    help=f"超标阈值 us（默认 {OVER_LIMIT_US}）")
    a = ap.parse_args(argv)

    path = Path(a.path).expanduser()
    if not path.exists():
        print(f"文件不存在: {path}", file=sys.stderr)
        return 1

    recv_id, send_id = _parse_id(str(a.recv_id)), _parse_id(str(a.send_id))
    if recv_id is None or send_id is None:
        print(f"ID 解析失败: {a.recv_id} / {a.send_id}", file=sys.stderr)
        return 1

    drop_over = a.drop_over if a.drop_over > 0 else float("inf")

    # 诊断信息一律走 stderr：stdout 只留报告正文，这样 `> 报告.md` 出来就是干净的文档
    print(f"读取 {path} ...", file=sys.stderr)
    try:
        frames, info = read_frames(path, recv_id, send_id)
    except OSError as e:
        print(f"读文件失败: {e}", file=sys.stderr)
        return 1
    if not frames:
        e = sys.stderr
        print("没读到任何目标 ID 的帧。", file=e)
        if info.get("sample_line"):
            print(f"\n第一条没认出来的行长这样：\n    {info['sample_line'][:120]}", file=e)
        print("\n本脚本认这两种格式：", file=e)
        print("  1) GCAN 工具导出： 2026-10-06 17:15:11.674287  CAN2  0x205  STD  8  08 A3 ...", file=e)
        print("  2) 老上位机记录  ：  00000001  000.000.251  接收  205  DATA  STANDARD  8  03 A8 ...", file=e)
        print("如果是 candump 那种 '(1790761301.922480) can1 205 [8] ...'，本脚本暂不支持。", file=e)
        return 1

    res = analyse(frames, recv_id, send_id, drop_over)
    report(res, info, recv_id, send_id, drop_over, a.over_limit)
    return 0


if __name__ == "__main__":
    sys.exit(main())
