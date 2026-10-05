import sys
import usb.core

VID, PID = 0x1D50, 0x606F

def reset(dev, why=""):
    try:
        dev.reset()
        print(f"reset {dev.idVendor:04x}:{dev.idProduct:04x} "
              f"bus {dev.bus} addr {dev.address} {why}")
        return 1
    except Exception as e:
        print(f"skip  {dev.idVendor:04x}:{dev.idProduct:04x}: {e}")
        return 0

if "--all" in sys.argv:
    n = 0
    for d in usb.core.find(find_all=True):
        if d.bDeviceClass == 9:          # 跳过 hub / root hub
            continue
        n += reset(d)
    print(f"done, {n} device(s)")
else:
    dev = usb.core.find(idVendor=VID, idProduct=PID)
    if dev is None:
        sys.exit(f"device {VID:04x}:{PID:04x} not found")
    reset(dev)