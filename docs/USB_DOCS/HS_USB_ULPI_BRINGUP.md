# STM32H723 + ULPI(USB3300) 高速 USB 调试记录

> 更新时间：2026-11-20
> 工程：`Z_Gimbal`（board `H7_Work`，SoC `STM32H723VG` LQFP100）
> 状态：**已枚举成功，跑在 HS (480 Mbps)**
> 姊妹文档：`docs/USB_DOCS/USB_DEBUG_POSTMORTEM.md`（复盘与排查手册）、`docs/USB_DOCS/USB_NOTES.md`

---

## 0. 一句话结论

**根因有三个，全部已修复：**

| # | 根因 | 修复 |
|---|---|---|
| 1 | **`SYSCFG->PMCR.PC2SO/PC3SO` 被置 1（打开模拟开关）**，而 LQFP100 上 `DIR/NXT` 只焊在 `PC2_C/PC3_C`，数字功能必须让开关**闭合** | 改成 `&= ~mask`（置 **0**） |
| 2 | **OTG 初始化时把 PHY 写睡**（手册 §6.1.9.3 的"非法寄存器写"），FIFO 冲刷永远超时 | `usbd_init()` **全程把 STP(PC0) 按住高电平**，初始化完再交还 AF10 |
| 3 | **ST HAL 里 `GUSBCFG.ULPIFSLS` 只清不置**，`maximum-speed="full-speed"` 与 ULPI PHY 不兼容 | DTS 改 **`maximum-speed = "high-speed"`** |

> 曾经把问题归因于"RBIAS / VDD1.8 / PHY 不出 60MHz"，**那是误判**。
> 后来用 TIM2 硬件计数实测 PA5 = **60.005 MHz**，PHY 一直是好的。

---

## 1. 关键证据链

```
PA5 freq before wake:        600056 edges/10ms -> 60.005 MHz   ← 时钟一直都有
PA5 freq after init+release: 600059 edges/10ms -> 60.005 MHz
PA5 freq after enable:       600066 edges/10ms -> 60.006 MHz
```

| 寄存器 | 值 | 解读 |
|---|---|---|
| `GRSTCTL` | `0x80000030` | `AHBIDL=1` + `TXFFLSH/RXFFLSH=1` 永不清 → **拿不到 PHY 时钟**（修复前） |
| `GRSTCTL` | bit5/bit4 归 0 | 冲刷完成（修复后） |
| `GUSBCFG` | `0x40001410` | `FDMOD(bit30)=1` 设备模式；`PHYSEL(bit6)=0` ULPI 模式 |
| `GOTGCTL` bit6/bit7 | `BVALOEN/BVALOVAL=1` | **驱动强制覆盖 VBUS 有效**，所以 `BSESVLD=1` 不能作为 ULPI 通信正常的证据 |
| `GOTGCTL` bit18/19 | `ASVLD/BSESVLD` | 修复后**由 PHY 真实上报**（同时置位） |
| `SYSCFG->PMCR` | `0x0f000000` → `0x03000000` | 修复前 bit26/27 被置 1（开关打开）→ 修复后只剩 bit24/25 只读位 |
| `GPIOC IDR` | `0x00000000` | 修复前 **DIR(PC2)/NXT(PC3) 恒 0 → 模拟开关把数字侧切断了** |

**修复前后主机侧对比：**

```
修复前：dmesg 一片空白；R15(D+) 对 GND = 0.031 V（没有上拉）
修复后：USBD: Bus reset → Actual device speed 2 (HS) → Get Descriptor → 读 BOS
        R15(D+) 对 GND = 0.16 V（枚举过程中，总线在活动）
```

最终 `dmesg`：

```
usb 3-7: new high-speed USB device number 18 using xhci_hcd
usb 3-7: New USB device found, idVendor=1d50, idProduct=606f
usb 3-7: Product: product_0   Manufacturer: A_Shui
gs_usb 3-7:1.0: Configuring for 3 interfaces
```

---

## 2. 引脚映射（已用 PCB 网表逐根核对，与 DTS 完全一致）

| 信号 | PHY 脚 | MCU 脚 | MCU 端口 | DTS |
|---|---|---|---|---|
| DATA0 | U5-24 | U2-25 | PA3 | ✅ |
| DATA1 | U5-23 | U2-34 | PB0 | ✅ |
| DATA2 | U5-22 | U2-35 | PB1 | ✅ |
| DATA3 | U5-21 | U2-46 | PB10 | ✅ |
| DATA4 | U5-20 | U2-47 | PB11 | ✅ |
| DATA5 | U5-19 | U2-51 | PB12 | ✅ |
| DATA6 | U5-18 | U2-52 | PB13 | ✅ |
| DATA7 | U5-17 | U2-91 | PB5 | ✅ |
| CLKOUT | U5-14 | U2-29（经 R1） | PA5 | ✅ |
| STP | U5-13 | U2-15 | PC0 | ✅ |
| DIR | U5-12 | U2-17 | **PC2_C** | ✅（需模拟开关闭合） |
| NXT | U5-11 | U2-18 | **PC3_C** | ✅（需模拟开关闭合） |
| RESET | U5-9 | U2-56 | PD9 | 由固件打 20ms 复位脉冲 |

> USB3300 **没有** REFSEL / CSB / VBUS 引脚（那是 ULPI 收发器之外的器件才有的），
> 不要照着别的 PHY 的原理图去对。

**板级元件（均已核对，合格）：**

| 元件 | 值 | 作用 |
|---|---|---|
| `R23` | 12 K | RBIAS → GND |
| `X2` | 24 MHz | 晶振 |
| `C34` / `C35` | 20 pF | 晶振负载 |
| `R22` | 1 MΩ | 晶振并联偏置 |
| `R25` + `C39` | 4.7 K↓ + 100 nF→3V3 | POR，τ = 470 µs（**偏短**，见 §3.2） |
| `C33` | 4.7 µF | VDDA1.8 |
| `C36`/`C37`/`C38` | 4.7 µF | VDD1.8 |
| `R15` / `R17` | 22 Ω | D+ / D- 串阻 |
| `R14` / `R19` | 5.1 K | CC1 / CC2 |
| `REG_EN` | 直连 VDD3.3 | 内部稳压器常使能 |

USB 座是 **16-pin Type-C**，`DP1+DP2`、`DN1+DN2` 已并联，正反插都可以。

---

## 3. 代码改动（`src/Basic_USB.cpp`）

### 3.1 模拟开关必须闭合

```c
SYSCFG->PMCR &= ~(SYSCFG_PMCR_PC2SO | SYSCFG_PMCR_PC3SO);   /* 0 = 闭合 */
```

- `0` = 闭合，`PC2` 与 `PC2_C` 连通（数字信号可达）
- `1` = 打开，两焊盘断开，`PC2_C` 只剩模拟功能
- 修复前日志 `PMCR = 0x0f000000`（bit26/27 被置 1）→ DIR/NXT 悬空
- 修复后日志 `PMCR = 0x03000000`（只剩 bit24/25 只读位）

Zephyr 的 pinctrl 只写 `MODER/AFR`，**不会动这一位**；CubeMX 反而会生成
`HAL_SYSCFG_AnalogSwitchConfig(..., SYSCFG_SWITCH_PCx_OPEN)`，不能照抄。

### 3.2 PD9 给 PHY 打一次长复位

板上 `C39/R25` 的 RC 只给 ~0.2~0.7 ms 复位宽度，而 USB3300 的 `TSTART = 2.25~3.5 ms`
（手册 §6.1.11 要求 reset 只能在 CLKOUT 有效后撤销）。固件用 PD9 补一个 **20 ms**
的干净脉冲，释放后再等 10 ms。

```c
#define USB_PHY_RST_PORT        GPIOD
#define USB_PHY_RST_PIN         9U
#define USB_PHY_RST_ASSERT_MS   20   /* 远大于 470us 的 RC 时间常数 */
#define USB_PHY_RST_RELEASE_MS  10   /* > TSTART(max) 3.5ms */
```

### 3.3 `usbd_init()` 期间按住 STP

```c
usb_ulpi_phy_wakeup();       /* PC0 = GPIO 输出高，且故意不释放 */
...
usbd_init(&usbd);            /* 全程 STP 保持高电平 */
usb_ulpi_phy_stp_release();  /* 初始化完才交还 AF10 */
```

依据手册 §6.1.9.3 Start-up Protection：Link 未就绪时必须先断言 STP，
否则 PHY 会把数据总线状态当成 TX CMD，**做出非法寄存器写**
（清掉 `SuspendM` → PHY 立刻睡回去）。

实测特征：一旦 OTG 的 ULPI 接口被激活，PA5 上的 60 MHz 就消失；
按住 STP 后 60 MHz 又回来 —— 这就是"PHY 被 OTG 的杂散写弄睡"的证据。

> ⚠️ 切 GPIO 输出时**必须先置 `BSRR` 再改 `MODER`**，否则切换瞬间会把 STP 拉低一下，
> 触发 §6.1.9.2 的 false resume。

### 3.4 DTS

```dts
maximum-speed = "high-speed";   /* 外部 ULPI PHY 只能跑 HS */
```

原因：ST 的 `stm32h7xx_ll_usb.c` 只 **清** `GUSBCFG.ULPIFSLS`，从不置位，
所以 `full-speed` + ULPI PHY 这条组合在 HAL 层面就是不通的。

---

## 4. 排查轨迹（精简版，供下次参考）

| # | 现象 | 结论 |
|---|---|---|
| 1 | `PCD_Init failed, 1` → `usbd_init ret=-5` | `HAL_PCD_Init` 只回 `HAL_ERROR`，必须靠寄存器定位 |
| 2 | `GOTGCTL` 的 `BVALOEN/BVALOVAL=1` | 证明 `USB_DevInit` 已执行 → 失败点在 `USB_FlushTxFifo` |
| 3 | 把 STP 按住高电平 → `usbd_init ret=0` | **根因 2** 找到 |
| 4 | 但 `lsusb` 仍无设备 | 转查物理链路 |
| 5 | 同线连手机能枚举 | 线 / 主机口无罪 |
| 6 | `R15`(D+) = 0.031 V | **D+ 上拉没使能** → ULPI 寄存器写失效 |
| 7 | `GPIOC IDR`: `PC2(DIR)=0, PC3(NXT)=0` | **DIR/NXT 没接到 MCU** |
| 8 | `PC2SO/PC3SO` 置 0（闭合） | **根因 1** 找到 → 枚举成功 |
| 9 | 枚举成功但速度不对 | **根因 3**（`maximum-speed`）|

**踩过的坑（工具层面）：**

- **软件采样法判断 PA5 有没有时钟会假阳性**：60 MHz 下 1 pF 耦合阻抗(2.6 kΩ) 小于内部上拉(40 kΩ)，
  浮空引脚也能读出 ~50% 翻转。**要用 TIM2 外部时钟模式数边沿**才能量准。
- **不要禁用再重开 `RCC->AHB1ENR` 的 USB 门控去"探针"**：实测这样会把 PHY 弄睡，
  探针结果不可信，还会污染 OTG 状态。
- `BSESVLD=1` 在 `BVALOEN/BVALOVAL` 被置位时是**软件强制**的，不能用作 ULPI 通信正常的证据。
- **60 MHz 存在 ≠ ULPI 通路正常**：`USB_FlushTxFifo()` 只需要 CK 一根线。

---

## 5. 命令速查

```bash
# 构建 / 烧录 / 日志
west build -p always -b H7_Work -s /home/rm/Desktop/Z_Gimbal -d /home/rm/Desktop/Z_Gimbal/build
west flash --runner pyocd
# 烧录前必须先释放调试器，否则报 "Unable to claim interface for probe"
pkill -f "pyocd rtt"
west rtt --runner pyocd
```

关键寄存器（`pyocd cmd` 免改代码直读）：

```bash
pyocd cmd -t stm32h723xx -c "halt" \
  -c "read32 0x40040000"   -c "read32 0x40040010" \
  -c "read32 0x40040014"   -c "read32 0x40040804" \
  -c "read32 0x40040808"   -c "read32 0x58000408" \
  -c "read32 0x58020810"   -c "go"
```

| 地址 | 寄存器 | 关注位 |
|---|---|---|
| `0x40040000` | `GOTGCTL` | bit6/7 `BVALOEN/BVALOVAL`（驱动强制）；bit18/19 `ASVLD/BSESVLD`（PHY 真实上报） |
| `0x40040010` | `GRSTCTL` | bit31 `AHBIDL`；bit5 `TXFFLSH`；bit4 `RXFFLSH` |
| `0x40040014` | `GINTSTS` | bit3 `SOF`；bit12 `USBRST`；bit13 `ENUMDNE` |
| `0x40040804` | `DCTL` | bit1 `SDIS`（0 = 已连接） |
| `0x40040808` | `DSTS` | bit0 `SUSPSTS`；bit2:1 `ENUMSPD`（0 = HS） |
| `0x58000408` | `SYSCFG->PMCR` | bit26 `PC2SO`；bit27 `PC3SO`（**必须为 0**） |
| `0x58020810` | `GPIOC IDR` | bit0 `STP`；bit2 `DIR`；bit3 `NXT` |

主机侧：

```bash
lsusb | grep -i 1d50
sudo dmesg | tail -30
```

PCB 网表核对（Altium `.SchDoc`，同名网络 + 几何连通）：

```bash
python3 docs/原理图/netlist3.py "docs/原理图/USB2.0高速测试板/USB2.0高速测试板原理图.SchDoc" <关键字>
```

---

## 6. gs_usb 三通道 CAN-FD 验证

```bash
# can0 = fdcan1(真实)   can1 = can_loopback0(虚拟回环)   can2 = fdcan3(真实)
sudo ip link set can0 type can bitrate 1000000 sample-point 0.8 \
                          dbitrate 5000000 dsample-point 0.8 fd on
sudo ip link set can0 up
candump -e -ta -x -d can0
```

实测（can1 内部回环，1 M / 5 M）：

```
RX: 20 bytes 4 packets 0 errors
TX: 20 bytes 4 packets 0 errors
state ERROR-ACTIVE (berr-counter tx 0 rx 0)，bus-off / arbit-lost 全 0
```

经典帧 `123#DEADBEEF` ×3 与 FD+BRS 帧 `456##1AABBCCDDEEFF0011` 均正常。

> 注意：`main.cpp` 的 `APP_STANDALONE_CAN_MODE` 默认是 `0`（USB 模式）。
> `gs_usb` 和 `CAN2_thread` 都独占 `fdcan1`，**只能二选一**；
> 要脱机跑云台就把它改成 `1`。

---

## 7. 待办

- [ ] 三通道 CAN-FD 满载压测；若丢帧把 `CONFIG_USBD_GS_USB_POOL_SIZE` 192 → 256
- [ ] `CAN_Clear_All_Filters()` 用 `0..n-1` 当 filter id，mcan 下有越界噪声，应自己保存返回的 id
- [ ] `M_S_Control::C_S_TO_M()` 里重复调用 `gs_usb_register()` 会重复注册 CAN filter，需加保护
- [ ] `boot_cnt`（`__noinit`）实际不累加，建议改写到 `RTC->BKPxR`
- [ ] 可选：`CONFIG_RESET_ON_FATAL_ERROR=n`，避免 fatal error 自动复位丢失现场

---

## 8. 资源占用

```
FLASH:       146424 B / 1 MB     13.96%
RAM:         148104 B / 312 KB   46.36%
RTT_NOCACHE:   8184 B / 8 KB     99.90%   ← 已接近上限
```
