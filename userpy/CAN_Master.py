# SCHED_FIFO + 绑核
# sudo chrt -f 50 taskset -c 3 python3 CAN_Master.py can1

import gc, sys, can

IFACE = sys.argv[1] if len(sys.argv) > 1 else "can1"
TRIGGER_ID, REPLY_ID = 0x205, 0x010
bus = can.Bus(interface="socketcan", channel=IFACE, receive_own_messages=False)
bus.set_filters([{"can_id": TRIGGER_ID, "can_mask": 0x7FF}])  # 内核层过滤，非目标帧不唤醒 Python

gc.disable()                                   # 关 GC 暂停
reply = can.Message(arbitration_id=REPLY_ID, is_extended_id=False,
                    data=b"\0"*8, check=False) # 复用对象、跳过校验

n = 0
while True:
    msg = bus.recv(1.0)
    if msg is None:
        continue
    reply.is_fd = msg.is_fd
    reply.bitrate_switch = msg.bitrate_switch
    reply.data = msg.data
    bus.send(reply)                            # 先回发！
    n += 1
    if n % 1000 == 0:                          # 打印挪出热路径/降频
        print(n)