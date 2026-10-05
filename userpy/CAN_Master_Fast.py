# 上位机即收即发 —— 低延迟版：绕开 python-can，直接用 AF_CAN 裸套接字
#
# 用法：
#   python3 userpy/CAN_Master_Fast.py can1                       # 与原版 CAN_Master.py 等价
#   python3 userpy/CAN_Master_Fast.py can1 --cpu 3 --rt 50       # 脚本内绑核 + SCHED_FIFO(免 chrt/taskset)
#   python3 userpy/CAN_Master_Fast.py can1 --spin                # 忙等收包(要求独占一个核，见下方说明)
#   python3 userpy/CAN_Master_Fast.py can1 --stats               # 顺带量出"纯软件侧"耗时
#
# 原版 CAN_Master.py 每个来回要做的事（python-can 4.6.1 socketcan 实现）：
#   select() -> recvmsg() -> dissect_can_frame() -> Message() -> 3 次属性赋值
#            -> select() -> build_can_frame() -> send()
# 本版每个来回：
#   recv_into() -> pack_into(新 CAN ID) -> send()        2 次系统调用、0 次对象构造
# 回复帧直接复用收到的原始帧（只改 CAN ID），所以 len / is_fd / BRS / data 与原版语义完全一致。
#
# --spin 说明：非阻塞轮询能把该核钉在 C0，避免 C6(140us)/C10(310us) 唤醒延迟；
#   代价是吃满一个核，且 SCHED_FIFO 下必须把 RT 带宽限制打开，否则内核每 1s 会
#   节流 50ms（见 userpy/host_tune.sh 的 --rt-unlimited）。

import argparse
import os
import socket
import struct
import sys
import time

TRIGGER_ID = 0x205
REPLY_ID = 0x010
CAN_MASK = 0x7FF

CANFD_MTU = 72                      # struct canfd_frame
CAN_MTU = 16                        # struct can_frame
ID = struct.Struct("=I")
TIMESPEC = struct.Struct("@ll")


def open_socket(iface, fd, timestamps):
    s = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    s.bind((iface,))
    # 内核层过滤：非目标帧不唤醒 Python（等价于原版 bus.set_filters）
    s.setsockopt(socket.SOL_CAN_RAW, socket.CAN_RAW_FILTER,
                 struct.pack("=II", TRIGGER_ID, CAN_MASK))
    if fd:
        s.setsockopt(socket.SOL_CAN_RAW, socket.CAN_RAW_FD_FRAMES, 1)
    if timestamps:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_TIMESTAMPNS, 1)
    return s


def apply_sched(args):
    if args.cpu is not None:
        os.sched_setaffinity(0, {args.cpu})
    if args.rt:
        os.sched_setscheduler(0, os.SCHED_FIFO, os.sched_param(args.rt))


def stats_report(n, sw_sum, sw_max):
    if n == 0:
        return
    print(f"[stats] 回包 {n}  软件侧(内核收包时间戳 -> send 返回) "
          f"平均 {sw_sum / n:.1f}us  最大 {sw_max}us", flush=True)


def loop_fast(s, spin, heartbeat):
    """热路径：阻塞 recv_into（1 次系统调用）或忙等。"""
    buf = bytearray(CANFD_MTU)
    mv = memoryview(buf)
    recv_into = s.recv_into
    send = s.send
    pack_id = ID.pack_into

    view = mv
    last_n = -1
    n = 0

    if spin:
        s.setblocking(False)
        while True:
            try:
                got = recv_into(buf)
            except BlockingIOError:
                continue
            if got < CAN_MTU:
                continue
            if got != last_n:
                view, last_n = mv[:got], got
            pack_id(buf, 0, REPLY_ID)
            send(view)
            n += 1
            if n % heartbeat == 0:
                print(n, flush=True)
    else:
        while True:
            got = recv_into(buf)
            if got < CAN_MTU:
                continue
            if got != last_n:
                view, last_n = mv[:got], got
            pack_id(buf, 0, REPLY_ID)
            send(view)
            n += 1
            if n % heartbeat == 0:
                print(n, flush=True)


def loop_stats(s, heartbeat):
    """带时间戳的慢路径：额外拆出"软件侧"耗时，用于判断瓶颈在不在 Python。"""
    buf = bytearray(CANFD_MTU)
    mv = memoryview(buf)
    recvmsg_into = s.recvmsg_into
    send = s.send
    pack_id = ID.pack_into
    cmsg_size = socket.CMSG_SPACE(TIMESPEC.size)

    view = mv
    last_n = -1
    n = 0
    sw_sum = 0
    sw_max = 0

    while True:
        got, anc, _flags, _addr = recvmsg_into([buf], cmsg_size)
        if got < CAN_MTU or not anc:
            continue
        sec, nsec = TIMESPEC.unpack_from(anc[0][2])
        t_rx = sec * 1_000_000_000 + nsec
        if got != last_n:
            view, last_n = mv[:got], got
        pack_id(buf, 0, REPLY_ID)
        send(view)
        t_done = time.time_ns()
        sw = (t_done - t_rx) // 1000
        if sw < 0:                      # 时间戳与墙钟取值点不一致时丢弃
            sw = 0
        sw_sum += sw
        if sw > sw_max:
            sw_max = sw
        n += 1
        if n % heartbeat == 0:
            stats_report(n, sw_sum, sw_max)


def main():
    ap = argparse.ArgumentParser(description="CAN 即收即发（裸套接字低延迟版）")
    ap.add_argument("iface", nargs="?", default="can1")
    ap.add_argument("--fd", action="store_true", help="打开 CAN_RAW_FD_FRAMES（收发 FD 帧）")
    ap.add_argument("--spin", action="store_true", help="忙等收包（吃满一个核，换最低唤醒延迟）")
    ap.add_argument("--cpu", type=int, default=None, help="把本进程绑到指定核")
    ap.add_argument("--rt", type=int, default=None, help="SCHED_FIFO 优先级（需 root/CAP_SYS_NICE）")
    ap.add_argument("--stats", action="store_true", help="量出软件侧耗时（会稍慢一点）")
    ap.add_argument("--heartbeat", type=int, default=1000)
    args = ap.parse_args()

    apply_sched(args)
    s = open_socket(args.iface, args.fd, args.stats)
    print(f"{args.iface}: 监听 {TRIGGER_ID:03x} -> 回发 {REPLY_ID:03x}"
          f"{' [spin]' if args.spin else ''}{' [stats]' if args.stats else ''}", flush=True)

    try:
        if args.stats:
            loop_stats(s, args.heartbeat)
        else:
            loop_fast(s, args.spin, args.heartbeat)
    except KeyboardInterrupt:
        pass
    finally:
        s.close()


if __name__ == "__main__":
    sys.exit(main())
