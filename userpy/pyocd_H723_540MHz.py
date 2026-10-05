# 注意，烧录540MHz主频的STM32H723前，需要使用此py启用CPU_FREQ_BOOST，只需启用一次，之后断电重上电即可生效
# 没有试过运行这个py，发现有问题记得提issue，如确定无问题可以删掉此行
from pyocd.core.helpers import ConnectHelper

FLASH = 0x52002000
OPTKEYR, OPTCR, OPTSR_CUR = FLASH + 0x08, FLASH + 0x18, FLASH + 0x1C
OPTCCR, OPTSR2_CUR, OPTSR2_PRG = FLASH + 0x24, FLASH + 0x70, FLASH + 0x74
KEY1, KEY2   = 0x08192A3B, 0x4C5D6E7F
BOOST_BIT    = 1 << 2
OPTSTART_BIT = 1 << 1

with ConnectHelper.session_with_chosen_probe(
        target_override="stm32h723xx", connect_mode="halt") as s:
    t = s.target

    if t.read32(OPTSR2_CUR) & BOOST_BIT:
        print("CPU_FREQ_BOOST 已启用，跳过")
    else:
        t.write32(OPTKEYR, KEY1); t.write32(OPTKEYR, KEY2)
        t.write32(OPTCCR, 0x40000000)
        v = t.read32(OPTSR2_PRG)
        t.write32(OPTSR2_PRG, v | BOOST_BIT)
        t.write32(OPTCR, t.read32(OPTCR) | OPTSTART_BIT)
        for _ in range(2000):                      # 最多等 ~10 s
            if (t.read32(OPTSR_CUR) & 1) == 0:
                break
        sr = t.read32(OPTSR_CUR)
        if (sr >> 30) & 1:
            print("OPTCHANGEERR，编程失败，请重试"); raise SystemExit(1)
        print("CPU_FREQ_BOOST 已写入，请断电重上电后生效")
        t.reset()