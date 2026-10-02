import sys
import can

IFACE = sys.argv[1] if len(sys.argv) > 1 else "can1"
TRIGGER_ID = 0x205   # 只在收到这帧时回复
REPLY_ID = 0x010     # 回复用的 ID

bus = can.Bus(interface="socketcan", channel=IFACE, receive_own_messages=False)
print(f"[{IFACE}] 等待 0x{TRIGGER_ID:03X}，收到后用 0x{REPLY_ID:03X} 回发同样数据（Ctrl-C 退出）")

try:
    while True:
        msg = bus.recv(timeout=1.0)          # 1s 超时，方便 Ctrl-C
        if msg is None or msg.is_error_frame:
            continue

        if msg.arbitration_id != TRIGGER_ID:
            continue

        print(f"RX {msg.timestamp:.6f} 0x{msg.arbitration_id:03X} "
              f"[{len(msg.data)}] {msg.data.hex(' ')}")

        reply = can.Message(
            arbitration_id=REPLY_ID,
            is_extended_id=False,            # 0x010 是 11 位标准帧（合法，≤0x7FF）
            is_fd=msg.is_fd,                 # 收到 FD 帧就按 FD 回发；经典帧自动 False
            bitrate_switch=msg.bitrate_switch,
            data=msg.data,                   # 同样数据
        )
        bus.send(reply)
        print(f"TX 0x{REPLY_ID:03X} [{len(reply.data)}] {reply.data.hex(' ')}")
except KeyboardInterrupt:
    pass
finally:
    bus.shutdown()