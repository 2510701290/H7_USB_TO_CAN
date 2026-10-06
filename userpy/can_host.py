#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
can_host.py —— 运行文件：解析参数 → 初始化 → 循环收一发一 → 退出还原

三个文件的分工：

    can_host.py     ← 本文件。命令行参数、把下面两个串起来、退出时还原系统状态
    can_init.py        初始化：CAN 网卡 / 中断亲和性 / 进程绑核
    can_loop.py        循环：收一帧 → 发一帧（只打印，不统计）

要改「收到帧之后做什么」   → 改 can_loop.py 里的 on_frame()，就那一个函数
要改「初始化都干什么」      → 改 can_init.py
要改「有哪些命令行参数」    → 改本文件的 build_parser()

--------------------------------------------------------------------- 常用
    python3 can_host.py can1 --no-can-init --no-irq    # 网卡已 UP：完全不用 sudo
    sudo python3 can_host.py can1                      # 完整初始化
    sudo python3 can_host.py can1 --bind               # 顺便把进程绑到中断核
    python3 can_host.py can1 --max-frames 20           # 收 20 帧看看长什么样
    python3 can_host.py can1 --quiet --max-seconds 60  # 不逐帧打印，跑一分钟

⚠️ 第一次跑最常踩的坑：`RTNETLINK answers: Operation not permitted`
   这不是卡住，是**权限**，而且只跟初始化那三步有关（详见 can_init.py）：

       ip link set ...        需要 CAP_NET_ADMIN   → 要 sudo
       写 /proc/irq/...       需要 root            → 要 sudo
       开 AF_CAN 收发包        只需要 CAP_NET_RAW   → 普通用户就有，不用 sudo

   所以网卡**本来就已经 UP** 时，加 `--no-can-init --no-irq` 就一行 sudo 都不需要。
"""

from __future__ import annotations

import argparse
import os
import socket
import sys

# 三个文件放哪儿都能互相 import（不依赖当前工作目录）
HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import can_init                                            # noqa: E402
import can_loop                                            # noqa: E402


def parse_id(s: str) -> int:
    """接受 `0x205` 或 `517` 两种写法。"""
    return int(s, 16) if s.lower().startswith("0x") else int(s, 10)


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(
        prog="can_host.py",
        description="上位机：初始化（CAN + 中断亲和性 + 绑核）+ 收一发一循环",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""例子:
  python3 can_host.py can1 --no-can-init --no-irq   网卡已 UP，免 sudo
  sudo python3 can_host.py can1                     完整初始化
  sudo python3 can_host.py can1 --bind              顺便让进程和中断同核
  python3 can_host.py can1 --max-frames 20          收 20 帧就退出
""")
    ap.add_argument("iface", nargs="?", default=can_loop.DEFAULT_IFACE,
                    help=f"CAN 网卡名（默认 {can_loop.DEFAULT_IFACE}）")
    ap.add_argument("--recv-id", default=hex(can_loop.DEFAULT_RECV_ID),
                    help=f"收哪个 CAN ID，0x 前缀或十进制"
                         f"（默认 0x{can_loop.DEFAULT_RECV_ID:03X}）")
    ap.add_argument("--send-id", default=hex(can_loop.DEFAULT_SEND_ID),
                    help=f"回哪个 CAN ID（默认 0x{can_loop.DEFAULT_SEND_ID:03X}）")

    g = ap.add_argument_group("初始化（can_init.py）")
    g.add_argument("--bitrate", default=can_init.DEFAULT_BITRATE,
                   help=f"默认 {can_init.DEFAULT_BITRATE}")
    g.add_argument("--sample-point", default=can_init.DEFAULT_SAMPLE_POINT,
                   help=f"默认 {can_init.DEFAULT_SAMPLE_POINT}")
    g.add_argument("--no-can-init", action="store_true", help="跳过 CAN 网卡初始化")
    g.add_argument("--irq-cpu", type=int, default=can_init.DEFAULT_IRQ_CPU, metavar="N",
                   help=f"把 xHCI 中断挪到 CPU N（默认 {can_init.DEFAULT_IRQ_CPU}，"
                        f"退出时自动还原）")
    g.add_argument("--no-irq", action="store_true", help="不动中断亲和性（同时不绑核）")
    g.add_argument("--bind", action="store_true",
                   help="把本进程绑到 --irq-cpu。⚠️ 这个组合尚未单独复测；"
                        "实测有效的版本是【不绑核】，发现变差就去掉这个开关")

    g = ap.add_argument_group("循环（can_loop.py）")
    g.add_argument("--spin", action="store_true", default=True,
                   help="非阻塞忙等收包（默认开启，irq 优化依赖它）")
    g.add_argument("--blocking", dest="spin", action="store_false",
                   help="改用阻塞 recv（会让 --irq-cpu 的优化失效）")
    g.add_argument("--max-frames", type=int, default=0, help="收满 N 帧后退出（0=不限）")
    g.add_argument("--max-seconds", type=float, default=0,
                   help="跑满 N 秒后退出（0=不限）。源没开时靠它兜底，别用 timeout sudo")
    g.add_argument("--quiet", action="store_true",
                   help="不逐帧打印（源 1 kHz 时打印是 2000 行/秒，会拖慢收发节奏）")
    return ap


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    recv_id = parse_id(str(args.recv_id))
    send_id = parse_id(str(args.send_id))

    if not hasattr(socket, "AF_CAN"):
        can_init.log("[err] 本机 Python 没有 AF_CAN（只有 Linux 支持）")
        return 2

    can_init.log("=" * 74)
    can_init.log("can_host.py —— 上位机初始化 + 收一发一")
    can_init.log("=" * 74)

    # ---------------- 初始化 ----------------
    ok, restore = can_init.init_all(
        args.iface,
        cpu=args.irq_cpu,
        bind=args.bind,
        bitrate=args.bitrate,
        sample_point=args.sample_point,
        do_can=not args.no_can_init,
        do_irq=not args.no_irq,
    )
    if not ok:
        return 1

    # ---------------- 循环 ----------------
    try:
        return can_loop.run(
            args.iface, recv_id, send_id,
            spin=args.spin,
            max_frames=args.max_frames,
            max_seconds=args.max_seconds,
            quiet=args.quiet,
        )
    except OSError as e:
        can_init.log(f"\n[err] {e}")
        return 1
    finally:
        # 不管怎么退出（正常结束 / Ctrl-C / 抛异常）都要把中断亲和性还原
        can_init.restore_irq(restore)


if __name__ == "__main__":
    sys.exit(main())
