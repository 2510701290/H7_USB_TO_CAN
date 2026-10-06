#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
can_loop.py —— 循环：收一帧 → 发一帧（只打印，不统计）

    while True:
        收一帧 ── 是 recv_id 吗？ ── 是 → 原样改 ID 成 send_id 发出去

整个策略只有 on_frame() 一个函数，要改行为只改那一处。

三个文件的分工：

    can_host.py     运行文件：命令行参数、串联、退出时还原系统状态
    can_init.py     初始化：CAN 网卡 / 中断亲和性 / 进程绑核
    can_loop.py     ← 本文件：收一发一循环

--------------------------------------------------------------------- 怎么用
本文件是纯**定义模块**，不能直接运行，也没有 `main`。只有 can_host.py 是运行文件。

    import can_loop
    can_loop.run("can1", recv_id=0x205, send_id=0x010)

单独用它做点什么（比如先自己把初始化做完再进循环）：

    import can_loop
    can_loop.run("can1", spin=True, max_frames=100, quiet=False)

命令行参数（`--max-frames` / `--quiet` 之类）定义在 can_host.py 的 build_parser() 里。

--------------------------------------------------------------------- 默认行为
收到 recv_id 的帧后，**payload 原样回发**，只把 CAN ID 换成 send_id。
（和现场测试用的 can_master.py 默认行为一致：收 0x205、回 0x010。）

⚠️ 本文件**故意不做统计** —— 不打帧率、不打耗时、不打周期汇报，收到就打印、
   发完就打印。长时间压测请用 `can_host.py --quiet`，或把输出重定向到文件：
   源是 1 kHz 时这里有 2000 行/秒，终端本身会成为瓶颈，把收发节奏拖慢。
"""

from __future__ import annotations

import errno
import signal
import socket
import struct
import time

# =============================================================== 配置

CAN_MTU = 16             # struct can_frame：id4 + dlc1 + pad3 + data8
CAN_DATA_OFF = 8         # 两种帧布局的数据区偏移都是 8
CANFD_MTU = 72           # struct canfd_frame（这里只当接收缓冲区用，不启用 FD）

CAN_EFF_FLAG = 0x80000000
CAN_RTR_FLAG = 0x40000000
CAN_ERR_FLAG = 0x20000000
CAN_SFF_MASK = 0x000007FF

# CAN ID 是主机字节序 u32 放在偏移 0，所以改 ID 可以直接 pack_into(0)
_ID = struct.Struct("=I")
# ENOBUFS 是**整数 105**，不是异常类 —— 直接写进 except 元组会 TypeError（踩过）
ENOBUFS = getattr(errno, "ENOBUFS", 105)

DEFAULT_IFACE = "can1"
DEFAULT_RECV_ID = 0x205   # 总线上发过来的（现场：下位机 1 kHz 触发帧）
DEFAULT_SEND_ID = 0x010   # 我们回过去的

_running = True


def log(msg: str = "") -> None:
    print(msg, flush=True)


# =============================================================== 套接字

def open_socket(iface: str, recv_id: int) -> socket.socket:
    """打开 AF_CAN 裸套接字，并在内核层过滤，只让 recv_id 上来。

    热路径因此只有 recv_into() → pack_into(改 ID) → send()：2 次系统调用、0 次对象构造。
    """
    s = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    try:
        s.bind((iface,))
        # CAN_RAW_FILTER：内核层过滤，不关心的帧根本不进用户态
        s.setsockopt(socket.SOL_CAN_RAW, socket.CAN_RAW_FILTER,
                     struct.pack("=II", recv_id & CAN_SFF_MASK, CAN_SFF_MASK))
        # CAN_RAW_LOOPBACK 必须保持开启（内核默认也是 1）：
        # 在 vcan / `ip link set canX ... loopback on` 的通道上，关掉它帧压根不会被投递，
        # 连别的 socket 都收不到 —— 本项目 can0 正是这么配的。
        try:
            s.setsockopt(socket.SOL_CAN_RAW, socket.CAN_RAW_LOOPBACK, 1)
        except OSError:
            pass
        s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)   # 抗突发，少丢帧
        return s
    except BaseException:
        s.close()
        raise


def recv_one(s: socket.socket, buf: bytearray):
    """收一帧。返回 (can_id, data)；没收到（忙等空转）返回 None。

    ⚠️ 瞬时错误必须吞掉，不能 raise：
       * BlockingIOError  —— 忙等模式下没帧可收，这是正常的
       * ENOBUFS(105)     —— 忙等高速回环把自己的 socket 缓冲顶满，丢这一帧就行。
                             早先让它 raise 会让整个程序带着 Errno 105 直接退出（真机踩过）
    """
    try:
        got = s.recv_into(buf)
    except (BlockingIOError, InterruptedError, socket.timeout):
        return None
    except OSError as e:
        if e.errno == ENOBUFS:
            return None
        raise

    if got < CAN_MTU:                       # 短帧（错误帧等），跳过
        return None
    can_id = _ID.unpack_from(buf, 0)[0]
    if can_id & (CAN_EFF_FLAG | CAN_ERR_FLAG | CAN_RTR_FLAG):
        return None                         # 默认只处理标准数据帧
    dlc = buf[4]
    return can_id, bytes(buf[CAN_DATA_OFF:CAN_DATA_OFF + dlc])


def send_one(s: socket.socket, send_id: int, data: bytes) -> bool:
    """发一帧：把 data 用 send_id 发出去。返回 False 表示这一帧被内核丢了。

    ⚠️ `send` 也会返回 **ENOBUFS(105)** —— 不是只有 `recv` 会。
       实测：回环乒乓把 TX 队列压满时，Python 直接抛
       `OSError: [Errno 105] No buffer space available` 退出整个程序。
       （C 版会遇到同样的情况，只是大家习惯不看 `send()` 的返回值。）
       这里选择**丢掉这一帧继续跑**：对"收到即回"的语义来说，迟到的回复没有意义，
       而崩掉整个程序是不可接受的。
    """
    frame = bytearray(CAN_MTU)
    _ID.pack_into(frame, 0, send_id & CAN_SFF_MASK)
    frame[4] = len(data)                    # dlc
    frame[CAN_DATA_OFF:CAN_DATA_OFF + len(data)] = data
    try:
        s.send(frame)
        return True
    except (BlockingIOError, InterruptedError):
        return False
    except OSError as e:
        if e.errno == ENOBUFS:
            return False
        raise


# =============================================================== 策略（唯一要改的地方）

def on_frame(s: socket.socket, can_id: int, data: bytes, send_id: int) -> bool:
    """★ 收一发一的策略 —— 整个循环里需要改的就是这一个函数。

    收到一帧后决定怎么回。返回 True 表示已经回了一帧。
    默认：不改内容，只把 ID 换成 send_id 原样回发（payload 镜像）。

    想改成别的内容，就改这里，例如：

        return send_one(s, send_id, bytes([0x01, 0x02]))     # 固定 payload
        return send_one(s, send_id, data[:4])                # 只回前 4 字节
        return False                                         # 什么都不回
    """
    return send_one(s, send_id, data)


# =============================================================== 主循环

def _install_signals() -> None:
    """Ctrl-C / SIGTERM 优雅退出。

    ⚠️ 必须 siginterrupt(True) 关掉 SA_RESTART，否则阻塞模式下空闲时 Ctrl-C 完全
       没反应，会一直挂住（踩过）。
    """
    def on_signal(_signum, _frame):
        global _running
        _running = False

    signal.signal(signal.SIGINT, on_signal)
    signal.signal(signal.SIGTERM, on_signal)
    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            signal.siginterrupt(sig, True)
        except (OSError, ValueError):
            pass


def run(iface: str = DEFAULT_IFACE,
        recv_id: int = DEFAULT_RECV_ID,
        send_id: int = DEFAULT_SEND_ID,
        spin: bool = True,
        max_frames: int = 0,
        max_seconds: float = 0.0,
        quiet: bool = False) -> int:
    """循环：收一帧 → 发一帧，直到 Ctrl-C / 收满 / 到点。

    spin=True   非阻塞忙等（默认）—— 中断亲和性优化依赖它：这颗核被顶住不进 C-state，
                中断才能立刻投递。代价是 100% 占一颗核。
    spin=False  阻塞 recv（0.5s 超时）—— 不吃 CPU，但那颗核会睡进 C10，延迟变差。
    quiet=True  不逐帧打印（源 1 kHz 时那是 2000 行/秒，终端会成为瓶颈）。
    """
    global _running
    _running = True
    _install_signals()

    s = open_socket(iface, recv_id)
    if spin:
        s.setblocking(False)
    else:
        s.settimeout(0.5)

    buf = bytearray(CANFD_MTU)              # 提前建好，热路径里不再分配
    t0 = time.monotonic()
    rx = 0

    log()
    log(f"[运行] {iface}  收 0x{recv_id:03X} → 回 0x{send_id:03X}"
        f"   {'忙等 --spin' if spin else '阻塞 recv'}")
    log("       Ctrl-C 退出")
    log()

    def why_stop(now: float) -> str | None:
        if not _running:
            return "收到 Ctrl-C / SIGTERM"
        if max_frames and rx >= max_frames:
            return f"已收满 {max_frames} 帧"
        if max_seconds and now - t0 >= max_seconds:
            return f"已到 {max_seconds:.0f}s"
        return None

    # 忙等时 4096 次空转才看一次表（对热路径可忽略）；阻塞时每次空返回已经等了
    # 0.5s，必须每次都看，否则 4096 × 0.5s = 2048s 才检查一次退出条件。
    idle_tick = 4096 if spin else 1
    idle = 0

    while _running:
        got = recv_one(s, buf)
        if got is None:
            idle += 1
            if idle >= idle_tick:
                idle = 0
                why = why_stop(time.monotonic())
                if why:
                    log(f"\n[退出] {why}")
                    break
            continue
        idle = 0

        can_id, data = got
        if can_id != recv_id:
            continue
        rx += 1

        ok = on_frame(s, can_id, data, send_id)
        if not quiet:
            log(f"  ← 0x{can_id:03X}  [{len(data):2d}]  {data.hex(' ')}")
            if ok:
                log(f"  → 0x{send_id:03X}  [{len(data):2d}]  {data.hex(' ')}")
            else:
                log(f"  ✗ 0x{send_id:03X} 发送被内核丢弃（ENOBUFS，TX 队列满）")

        why = why_stop(time.monotonic())
        if why:
            log(f"\n[退出] {why}")
            break

    log()
    log(f"[结束] 共收 {rx} 帧")
    s.close()
    return 0
