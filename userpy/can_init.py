#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
can_init.py —— 上位机初始化（三段，各自可关）

    1. CAN 网卡      ip link set canX type can bitrate ... up
    2. 中断亲和性    把 xHCI 的中断挪到指定 CPU      ← 本项目的关键优化
    3. 进程绑核      把本进程绑到同一颗 CPU

三个文件的分工：

    can_host.py     运行文件：命令行参数、串联、退出时还原系统状态
    can_init.py     ← 本文件：初始化
    can_loop.py     循环：收一帧 → 发一帧（只打印，不统计）

--------------------------------------------------------------------- 怎么用
本文件是纯**定义模块**，不能直接运行，也没有 `main`。只有 can_host.py 是运行文件。

    import can_init
    ok, restore = can_init.init_all("can1", cpu=9, bind=False)
    if not ok:
        return 1
    try:
        ...
    finally:
        can_init.restore_irq(restore)      # 把中断亲和性还回去

只想看当前状态、什么都不改，直接用这几个读函数：

    can_init.link_state("can1")     # → ("UP", "bitrate=1000000 sample-point=0.800")
    can_init.usb_pci_of("can1")     # → "0000:00:14.0"
    can_init.xhci_irq_of("0000:00:14.0")   # → 150

--------------------------------------------------------------------- 权限
三段的权限要求完全不同，别把整件事都当成"必须 sudo"：

    ip link set ...        需要 CAP_NET_ADMIN   → 要 root / sudo
    写 /proc/irq/...       需要 root            → 要 sudo
    开 AF_CAN 收发包        只需要 CAP_NET_RAW   → 普通用户就有（在 can_loop.py）

所以 init_can() 在非 root 时不会去硬改网卡，而是先读状态：网卡**已经 UP**
就放行（收发包本来就不需要 sudo），是 DOWN 才要求 sudo。

--------------------------------------------------------------------- 为什么动中断亲和性
xHCI 控制器（USB 主控）的中断默认全部落在 **CPU 1** 上，而 CPU 1 有 **52% 的时间
睡在 C10**（`intel_idle`，C10 退出延迟 **310 us**）。USB 中断撞上"CPU 1 刚进 C10"
的窗口时，投递会被推迟几百微秒 —— 这就是端到端 >1ms 极端延迟的主因。

把中断挪到一颗**被忙等占满、永远不会进 C-state** 的核上，实测（8 轮 A-B-A-B，
220s 对 220s，4096 组配对零错位）：

    >=1000us :  0.1682/s  →  0      （每 5.9 秒一次 → 0 次，二项检验 p = 7.3e-12）
    >=500us  :  0.9318/s  →  0.0455/s（降 20.5 倍）
    p99.99   :  ~1240 us  →  ~456 us

⚠️ 两个必须知道的边界：
  * 它**只在忙等（can_loop.py 的 --spin）时有效**。用阻塞 recv 的话这颗核会睡进
    C10，优势消失。
  * **反直觉**：单纯关掉 C-state（写 `cpuidle/state*/disable`）**反而更糟** ——
    关键不是"C-state 有多深"，而是"**中断落在一颗会休眠的核上**"。
"""

from __future__ import annotations

import os
import re
import subprocess
import time

PROG = os.path.basename(__file__)

DEFAULT_BITRATE = "1000000"
DEFAULT_SAMPLE_POINT = "0.8"
DEFAULT_IRQ_CPU = 9      # 本机实测有效的核（换机器必须重新查，见 why_cpu_9）


# =============================================================== 小工具

def log(msg: str = "") -> None:
    print(msg, flush=True)


def sh(cmd: list[str]) -> tuple[int, str]:
    """跑一条外部命令，返回 (returncode, 合并后的输出)。不抛异常。"""
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
        return r.returncode, (r.stdout + r.stderr).strip()
    except (OSError, subprocess.SubprocessError) as e:
        return 1, str(e)


def read_text(path: str) -> str | None:
    try:
        with open(path) as f:
            return f.read().strip()
    except OSError:
        return None


def write_text(path: str, val: str) -> bool:
    try:
        with open(path, "w") as f:
            f.write(val)
        return True
    except OSError as e:
        log(f"    ✗ 写 {path} 失败：{e}（需要 root）")
        return False


def link_state(iface: str) -> tuple[str, str]:
    """读一次网卡状态，返回 (state 字样, 位时序摘要)。读不到返回 ("?", "?")。"""
    rc, out = sh(["ip", "-details", "link", "show", iface])
    if rc != 0:
        return "?", "?"
    m = re.search(r"state (\w+)", out)
    state = m.group(1) if m else "?"
    bits = re.search(r"bitrate (\d+) sample-point ([\d.]+)", out)
    detail = f"bitrate={bits.group(1)} sample-point={bits.group(2)}" if bits else "位时序未读出"
    return state, detail


# =============================================================== 一、CAN 网卡

def init_can(iface: str, bitrate: str = DEFAULT_BITRATE,
             sample_point: str = DEFAULT_SAMPLE_POINT) -> bool:
    """把 canX 配好位时序并 up。只动这一张网卡，不碰 USB。

    ⚠️ `ip link set` 需要 **CAP_NET_ADMIN**（= root），而**收发包只需要 CAP_NET_RAW**
       （普通用户默认就有）。所以这两件事的权限要求完全不同，别一看到
       `RTNETLINK answers: Operation not permitted` 就以为整个程序必须 sudo。

       于是这里分两条路：
         root     → 配位时序 + up（正常路径）
         非 root  → 只读一次状态：已经 UP 就放行直接用；是 DOWN 就明确告诉你要 sudo
    """
    log(f"[1/3] CAN 网卡 {iface}")
    if not os.path.exists(f"/sys/class/net/{iface}"):
        log(f"    ✗ {iface} 不存在 —— 设备没枚举上？查 `sudo dmesg | tail` 有没有 "
            f"`gs_usb ... probe ... failed`；恢复配方见 DELAY_ANALYSIS.md §8c.7")
        return False

    state, detail = link_state(iface)
    if os.geteuid() != 0:
        log("    ℹ️  当前不是 root，跳过位时序配置（ip link set 需要 CAP_NET_ADMIN）")
        if state == "UP":
            log(f"    ✅ {iface} 已经是 UP，直接用它   {detail}")
            log("       （收发包只需要 CAP_NET_RAW，普通用户就够了，不用 sudo）")
            return True
        log(f"    ✗ {iface} 当前是 {state}，要拉起它必须加 sudo：")
        log(f"         sudo python3 can_host.py {iface}")
        log("       已经有 UP 的网卡时，可以完全不用 sudo：")
        log(f"         python3 can_host.py can1 --no-can-init --no-irq")
        return False

    # 位时序：失败不致命（网卡可能已经 up 着，改位时序需要先 down）
    rc, out = sh(["ip", "link", "set", iface, "type", "can",
                  "bitrate", bitrate, "sample-point", sample_point])
    if rc != 0:
        log(f"    · 位时序未改（可能已配好）：{out.splitlines()[0] if out else ''}")

    rc, out = sh(["ip", "link", "set", iface, "up"])
    if rc != 0:
        log(f"    ✗ up 失败：{out}")
        if "not permitted" in out.lower():
            log("       → 这是权限问题，但你已经是 root 了，比较少见。检查：")
            log("         ① 是否在容器里（缺 CAP_NET_ADMIN）；② 有没有 AppArmor/SELinux 拦着")
        return False

    # 等内核把 link 置为 RUNNING
    t0 = time.monotonic()
    while time.monotonic() - t0 < 2.0:
        if "UP" in sh(["ip", "-details", "link", "show", iface])[1]:
            break
        time.sleep(0.05)

    operstate = read_text(f"/sys/class/net/{iface}/operstate") or "?"
    _, detail = link_state(iface)
    log(f"    ✅ {iface} up   operstate={operstate}   {detail}")
    return True


# ======================================================= 二、中断亲和性（关键）

def usb_pci_of(iface: str) -> str | None:
    """顺着 /sys/class/net/canX/device 往上走，找到这个 USB 设备挂在哪个 PCI 控制器下。

    例：.../usb3/3-8/3-8.3/3-8.3:1.0  →  往上遇到 usb3（根 hub）→ 它的父目录
        就是 PCI 设备名，如 0000:00:14.0。
    """
    p = os.path.realpath(f"/sys/class/net/{iface}/device")
    while p and p != "/":
        if re.fullmatch(r"usb\d+", os.path.basename(p)):
            return os.path.basename(os.path.realpath(os.path.join(p, "..")))
        p = os.path.dirname(p)
    return None


def xhci_irq_of(pci: str) -> int | None:
    """在 /proc/interrupts 里找属于该 PCI 控制器的 xhci_hcd 中断号。

    行形如：150: 0 21064316 ... IR-PCI-MSI-0000:00:14.0 0-edge xhci_hcd
    """
    try:
        with open("/proc/interrupts") as f:
            for line in f:
                if "xhci_hcd" in line and pci in line:
                    head = line.split(":", 1)[0].strip()
                    if head.isdigit():
                        return int(head)
    except OSError:
        return None
    return None


def init_irq(iface: str, cpu: int = DEFAULT_IRQ_CPU) -> tuple[str, str] | None:
    """把承载 canX 的那个 xHCI 控制器的中断挪到 cpu。

    返回 (affinity_path, 原值) 供退出时还原；没改动（或失败）返回 None。
    """
    log(f"[2/3] 中断亲和性 → CPU{cpu}")
    pci = usb_pci_of(iface)
    if not pci:
        log(f"    · {iface} 不是 USB CAN 设备（vcan?），跳过")
        return None
    irq = xhci_irq_of(pci)
    if irq is None:
        log(f"    ✗ 在 /proc/interrupts 里找不到 {pci} 的 xhci_hcd 行，跳过")
        return None

    path = f"/proc/irq/{irq}/smp_affinity_list"
    old = read_text(path)
    if old is None:
        log(f"    ✗ 读不到 {path}（需要 root）")
        return None
    if old.strip() == str(cpu):
        log(f"    · IRQ {irq}（{pci}）本来就在 CPU{cpu}，无需改动")
        return None
    if not write_text(path, f"{cpu}\n"):
        return None

    now = read_text(path)
    if (now or "").strip() != str(cpu):
        log(f"    ✗ 写入后读回是 {now!r}，没有生效")
        return None
    log(f"    ✅ IRQ {irq}（{pci}）{old} → {now}")
    log(f"       ⚠️ 这条只在忙等（--spin）时有效；阻塞 recv 会让 CPU{cpu} 睡进 C10")
    return path, old


def restore_irq(saved: tuple[str, str] | None) -> None:
    """退出时还原中断亲和性。saved 为 None 时什么都不做。"""
    if not saved:
        return
    path, old = saved
    if write_text(path, old + "\n"):
        log(f"[还原] {path} → {old}")


# =============================================================== 三、进程绑核

def init_bind(cpu: int = DEFAULT_IRQ_CPU) -> bool:
    log(f"[3/3] 进程绑核 → CPU{cpu}")
    try:
        os.sched_setaffinity(0, {cpu})
    except OSError as e:
        log(f"    ✗ 绑核失败：{e}")
        return False
    log(f"    ✅ 当前亲和性 = {sorted(os.sched_getaffinity(0))}")
    return True


# =============================================================== 串起来

def init_all(iface: str,
             cpu: int = DEFAULT_IRQ_CPU,
             bind: bool = False,
             bitrate: str = DEFAULT_BITRATE,
             sample_point: str = DEFAULT_SAMPLE_POINT,
             do_can: bool = True,
             do_irq: bool = True) -> tuple[bool, tuple[str, str] | None]:
    """跑完整三段初始化。

    返回 (是否成功, 中断还原信息)。调用方照这个用：

        ok, restore = can_init.init_all(...)
        if not ok:
            return 1
        try:
            ...
        finally:
            can_init.restore_irq(restore)

    ⚠️ 默认 **不绑核**。实测有效的配置是「忙等 + 中断→CPU9，**进程没显式绑核**」；
       绑核（脚本里叫 irq9p）能把这个"碰巧同核"变成"确定同核"，但**尚未单独复测**。
       如果发现反而变差，去掉 bind 回到实测配置。
    """
    if do_can:
        if not init_can(iface, bitrate, sample_point):
            return False, None
    else:
        log("[1/3] CAN 网卡  跳过（--no-can-init）")

    if not do_irq:
        log("[2/3] 中断亲和性  跳过（--no-irq）")
        log("[3/3] 进程绑核    跳过（--no-irq）")
        return True, None

    why_cpu_9(cpu, bind)
    restore = init_irq(iface, cpu)
    if bind:
        init_bind(cpu)
    else:
        log("[3/3] 进程绑核    · 不绑核（实测配置；要绑加 --bind）")
    return True, restore


def why_cpu_9(cpu: int, bind: bool) -> None:
    """绑核号不能照抄 —— 提醒用户为什么默认是 9，以及换机器要重查什么。"""
    n = os.cpu_count() or 1
    if cpu >= n:
        log(f"⚠️  --irq-cpu {cpu} 超出本机 CPU 数（{n}）")
    if cpu == DEFAULT_IRQ_CPU and not bind:
        log(f"ℹ️  默认中断核 = CPU{DEFAULT_IRQ_CPU}（本机实测值）。换机器请重新查：")
        log("      lscpu -e                            # 看核的物理拓扑")
        log("      cat /proc/interrupts | grep xhci    # 看中断现在落在哪颗核")
        log("      cat /proc/irq/<IRQ>/smp_affinity_list")
