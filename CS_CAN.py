from collections import Counter
from statistics import mean, median, pstdev, stdev
from pathlib import Path

path = Path("/home/hjq/Zephyr_text/H7_Work/docs/CS_M_2026_10_2.txt")

if not path.exists():
    print(f"文件不存在: {path}")
    raise SystemExit(1)

rows = []

# 用 gb18030 打开，避免中文“接收”解码失败导致列错位
with path.open("r", encoding="gb18030", errors="ignore") as f:
    next(f, None)  # 跳过表头
    for line in f:
        parts = line.split()
        if len(parts) < 6:
            continue

        idx = int(parts[0])
        t = parts[1]          # 形如 000.000.285
        can_id = parts[3]     # 205 或 010

        # 时间格式：秒.毫秒.微秒
        s, ms, us = t.split(".")
        dt_us = int(s) * 1_000_000 + int(ms) * 1000 + int(us)  # 微秒

        rows.append((idx, can_id, dt_us))

# ---------- 帧数统计 ----------
cnt = Counter(r[1] for r in rows)
print("帧数统计：", cnt)
print("总行数：", len(rows))

# ---------- 收集 205 -> 010 的有效间隔，剔除 > 100ms (100000 us) ----------
intervals = []
skipped = 0

for i in range(len(rows) - 1):
    if rows[i][1] == "205" and rows[i + 1][1] == "010":
        dt_us = rows[i + 1][2]
        if dt_us > 100_000:      # 大于 100ms = 100000us 剔除
            skipped += 1
            continue
        intervals.append(dt_us)

N = len(intervals)
print(f"\n205 -> 010 有效样本数：{N}")
print(f"被剔除的大于 100ms 的样本数：{skipped}")

if N == 0:
    print("没有有效样本。")
    raise SystemExit(0)

# ---------- 整体统计（单位：微秒）----------
print("\n===== 整体统计（单位：微秒）=====")
print(f"平均间隔：{mean(intervals):.3f} us")
print(f"中位数  ：{median(intervals):.3f} us")
print(f"最小间隔：{min(intervals)} us")
print(f"最大间隔：{max(intervals)} us")
print(f"峰峰值  ：{max(intervals) - min(intervals)} us")

# ---------- 标准差 ----------
sigma_p = pstdev(intervals)   # 总体标准差
sigma_s = stdev(intervals)    # 样本标准差
mu = mean(intervals)
cv = sigma_p / mu if mu else 0

print("\n===== 标准差 / 波动 =====")
print(f"总体标准差 σp     ：{sigma_p:.3f} us")
print(f"样本标准差 σs     ：{sigma_s:.3f} us")
print(f"变异系数 CV=σp/μ  ：{cv:.4f}  ({cv*100:.2f}%)")

# ---------- 超过 1000us 的统计 ----------
over_1000 = [x for x in intervals if x > 1000]
n_over = len(over_1000)

print("\n===== 超过 1000 us 的统计 =====")
print(f"超过 1000us 的次数：{n_over}")
print(f"超过 1000us 的占比：{n_over / N * 100:.4f}%")

if n_over > 0:
    print(f"超过 1000us 的平均：{mean(over_1000):.3f} us")
    print(f"超过 1000us 的最小：{min(over_1000)} us")
    print(f"超过 1000us 的最大：{max(over_1000)} us")
else:
    print("没有超过 1000us 的样本。")

# ---------- 最小 10% 和最大 10% 的平均值（单位：微秒）----------
intervals_sorted = sorted(intervals)
k = int(N * 0.1)
if k == 0:
    k = 1

min_10 = intervals_sorted[:k]       # 最小的 10%
max_10 = intervals_sorted[-k:]      # 最大的 10%

print("\n===== 最小 10% =====")
print(f"样本数：{len(min_10)}")
print(f"最小 10% 平均间隔：{mean(min_10):.3f} us")

print("\n===== 最大 10% =====")
print(f"样本数：{len(max_10)}")
print(f"最大 10% 平均间隔：{mean(max_10):.3f} us")