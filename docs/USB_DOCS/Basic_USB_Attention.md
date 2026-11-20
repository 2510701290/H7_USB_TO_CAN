# USB 代码注意事项总集（`src/Basic_USB.cpp`）

> 更新时间：2026-11-20
> 本文件收录 `src/Basic_USB.cpp` **源码里原有的全部注释**，源码已做「零注释」处理。
> 改这部分代码之前先读这里，否则很容易踩回已经踩过的坑。
> 姊妹文档：`HS_USB_ULPI_BRINGUP.md`（技术细节）、`USB_DEBUG_POSTMORTEM.md`（复盘）

---

## 目录

1. [USB 设备栈对象只能定义一次](#1-usb-设备栈对象只能定义一次)
2. [BOS / LPM 描述符](#2-bos--lpm-描述符)
3. [模拟开关 PC2SO/PC3SO 必须为 0](#3-模拟开关-pc2sopc3so-必须为-0)
4. [USB3300 低功耗唤醒（手册依据）](#4-usb3300-低功耗唤醒手册依据)
5. [PD9 长复位脉冲](#5-pd9-长复位脉冲)
6. [STP 引脚操作顺序](#6-stp-引脚操作顺序)
7. [`usb_device_setup()` 的调用顺序](#7-usb_device_setup-的调用顺序)
8. [配置注册与 speed 分支](#8-配置注册与-speed-分支)
9. [描述符与 BOS](#9-描述符与-bos)
10. [usbd_enable 的 -EALREADY](#10-usbd_enable-的--ealready)
11. [gs_usb 事件回调](#11-gs_usb-事件回调)

---

## 1. USB 设备栈对象只能定义一次

`USBD_DEVICE_DEFINE` / `USBD_DESC_*_DEFINE` 会**展开成 `static` 变量**。
所以它们只能写在 `.cpp` 里定义一次。

> ⚠️ **如果写在头文件里，会在每个翻译单元（TU）里各生成一份副本。**
> 本项目曾经出现过 **5 份副本**的问题：`usbd_add_configuration()` 加到 A 副本，
> `usbd_init()` 用的却是 B 副本 → 配置丢失、枚举异常。

当前定义位置（`Basic_USB.cpp` 顶部，全局唯一）：

| 对象 | 内容 |
|---|---|
| `usbd` | VID `0x1D50` / PID `0x606F`（gs_usb 标准 ID） |
| `lang` | 语言 ID 描述符 |
| `mfr` | `"A_Shui"` |
| `product` | `"product_0"` |
| `sn` | 序列号（来自芯片 UID） |
| `fs_config` | Full-Speed 配置，`bMaxPower = 250`（500 mA） |
| `hs_config` | High-Speed 配置，`bMaxPower = 250`（500 mA） |
| `bos_lpm` | BOS 能力描述符 |

---

## 2. BOS / LPM 描述符

`bos_cap_lpm.bmAttributes` 是 **32 位**的位域，含义如下（当前值 `0UL`）：

| 位 | 含义 | 当前 |
|---|---|---|
| bit0 | 保留，必须为 0 | 0 |
| bit1 | 支持 LPM | 0 = 不支持 |
| bit2 | BESL 有效 | 0 = 不提供 BESL |
| bit3 | baseline BESL 有效 | 0 = 不支持 |
| bit4~7 | baseline BESL 值（单位 µs） | 0 |
| bit8 | Deep BESL 有效 | 0 |
| bit9~12 | Deep BESL 值（单位 µs） | 0 |

`bDevCapabilityType = USB_BOS_CAPABILITY_EXTENSION` → USB 2.0 扩展能力。

> ⚠️ **如果 Windows 下免驱失败（设备管理器报黄色感叹号 / 无法自动装 WinUSB），
> 先来改这里。** Linux 下不受影响。

---

## 3. 模拟开关 PC2SO/PC3SO 必须为 0

### 3.1 硬件事实

`PC2 / PC3` 与 `PC2_C / PC3_C` **共用同一个物理焊盘**，内部经一个模拟开关分两路。

`SYSCFG_PMCR.PC2SO`（RM0468）：

| 值 | 效果 |
|---|---|
| **0** | 开关**闭合**，`PC2` 与 `PC2_C` **连通** → 数字功能可用 |
| **1** | 开关**打开**，两焊盘**断开**，`PC2_C` 只剩模拟功能 |

### 3.2 本板的接线（决定必须为 0）

LQFP100 封装上 **只有 `PC2_C` / `PC3_C` 两个焊盘**，原理图对应：

```
ULPI_DIR  → U5-12(USB3300) → U2-17(MCU) → PC2_C
ULPI_NXT  → U5-11(USB3300) → U2-18(MCU) → PC3_C
```

数字功能要送到 `PC2_C/PC3_C`，就**必须**让模拟开关**闭合**。

### 3.3 置 1 的后果链（实测）

```
PC2SO/PC3SO = 1（打开）
  → DIR(PC2)、NXT(PC3) 恒为 0，MCU 读不到 PHY 的握手
  → 核心发出的 ULPI 寄存器写全部无效
  → PHY 的 Function Control 没被改成 HS / TermSelect
  → D+ 上拉不出现（实测 0.031 V）
  → 主机完全看不到设备（dmesg 一片空白）
```

### 3.4 为什么很难发现

`60MHz 时钟`、`FIFO 冲刷`、`usbd_init()` **都只需要 CK 一根线**，
所以即使 DIR/NXT 完全断开，这些步骤也一路"成功"，把所有排查方向都带偏了。

> ⚠️ **判据**：调用前 `SYSCFG->PMCR = 0x0f000000`（bit26/27 为 1）；
> 修复后应为 `0x03000000`（只剩 bit24/25 只读位）。

### 3.5 为什么不能照抄 CubeMX

- Zephyr 的 pinctrl **只写** `GPIOx_MODER/AFR`，**不会动** `PMCR` 这一位；
- CubeMX 反而会生成 `HAL_SYSCFG_AnalogSwitchConfig(..., SYSCFG_SWITCH_PCx_OPEN)` —— **方向是反的，不能照抄。**

---

## 4. USB3300 低功耗唤醒（手册依据）

手册：**DS00001783C**

| 手册位置 | 内容 | 对本代码的意义 |
|---|---|---|
| **表 5-7** | `SuspendM=0`（LPM）时 `VDDA1.8 = 0 V`，而 `VDD1.8` 仍为 1.6~2.0 V | 现场量到 "VDDA 不是 1.8 V" 就是 PHY **正在睡**的确定性证据（是结果，不是原因） |
| **§6.1.9.1** | 清 `SuspendM` 后 5 个 CLKOUT 周期内关掉 60 MHz | 睡下去非常快，来不及反应 |
| **§6.1.9.2** | 退出 LPM 必须由 Link 拉高 STP；`TSTART = 2.25~3.5 ms` 后 CLKOUT 才起来、DIR 才释放。**若 DIR 还没释放就撤销 STP，PHY 会当成 false resume 重新睡回去** | 所以 STP 要拉高够久（本代码取 10 ms） |
| **§6.1.11** | reset 只能在 CLKOUT 有效后才能撤销 | 板载 RC 复位太短，见第 5 节 |
| **§6.1.9.3** | Start-up Protection：**Link 未就绪时必须先断言 STP**，否则 PHY 会把数据总线状态当成 TX CMD，发出杂散数据甚至做出**非法寄存器写**（清掉 `SuspendM` → PHY 立刻睡回去） | **这是必须在 `usbd_init()` 全程按住 STP 的原因** |

### 4.1 为什么唤醒窗口会被抢走

手册说「MCU 热复位时 STP 三态，靠 PHY 内部上把 STP 拉高来唤醒」，
但这个上拉**只在 POR 后有效**；而且固件一旦把 `PC0` 配成 `ULPI_STP` 功能
（OTG 内核空闲时驱动为低），唤醒窗口就被掐断，PHY 会**永久卡在 LPM**，
连 MCU 反复复位都救不回来。

> ⚠️ Zephyr 的 `udc_stm32` 在 **`POST_KERNEL` 阶段**就 `pinctrl_apply_state()`
> 把 PC0 变成 `AF10(ULPI_STP)` 了，所以到 `main()` 时唤醒窗口早就过去了。
> 因此必须显式抢回引脚做一次唤醒。

### 4.2 实测证据

一旦 OTG 的 ULPI 接口被激活，**PA5 上的 60 MHz 会消失**；
把 STP 拉高 3 秒后又恢复 —— 这正是「PHY 被 OTG 的杂散写弄睡」的特征。

---

## 5. PD9 长复位脉冲

### 5.1 板上复位电路

```
U5 pin9 (RESET，高有效) ←── MCU PD9
              │
              ├── C39 = 100 nF → 3V3
              └── R25 = 4.7 kΩ → GND
```

RC 时间常数：τ = 4.7 kΩ × 100 nF = **470 µs**
→ RESET 在 3.3 V 建立后约 **0.2~0.7 ms** 就自动释放了。

### 5.2 问题

USB3300 的 CLKOUT 启动时间 `TSTART = 2.25~3.5 ms`，而 §6.1.11 明确
**reset 必须在 CLKOUT 有效后才能撤销**。

> ⚠️ 板上这根 RC 复位太"快"，PHY 很可能在内部稳压器 / 晶振还没稳时就被放开了。

### 5.3 解决

用 MCU 主动重新打一次足够长的复位脉冲：

| 宏 | 值 | 理由 |
|---|---|---|
| `USB_PHY_RST_PORT` | `GPIOD` | U5 pin9 → PD9 |
| `USB_PHY_RST_PIN` | `9` | 同上 |
| `USB_PHY_RST_ASSERT_MS` | `20` | 远大于 470 µs 的 RC 常数 |
| `USB_PHY_RST_RELEASE_MS` | `10` | > `TSTART`(max) 3.5 ms |

---

## 6. STP 引脚操作顺序

### 6.1 先置 ODR，再切 MODER

> ⚠️ **必须先写 `BSRR` 把输出数据寄存器置高，再把 `MODER` 切成输出模式。**
> 否则切换瞬间 `MODER` 从"模拟/输入"变成"输出"时，`ODR` 还是 0，
> 会把 STP **拉低一下** —— 触发 §6.1.9.2 的 **false resume**，
> PHY 又睡回去。同样的道理也适用于 PD9 复位引脚。

### 6.2 唤醒函数故意不交还 AF10

`usb_ulpi_phy_wakeup()` 结束时**故意不把 PC0 交还给 AF10**，让 STP 一直保持高电平。
调用方必须在 `usbd_init()` **之后**再调用 `usb_ulpi_phy_stp_release()`。

### 6.3 唤醒时长

| 宏 | 值 | 说明 |
|---|---|---|
| `USB_ULPI_WAKEUP_MS` | `10` | 手册 `TSTART` 最大 3.5 ms，取 10 ms 留余量 |

> 💡 **调试技巧**：临时改成 `3000`，配合万用表量三个点，一次定性：
> - `PC0`（MCU 侧 STP）应为 3.3 V → 证明 MCU 真的拉高了
> - `PHY pin13`（STP）应为 3.3 V → 证明走线通
> - `PHY pin29`（VDDA1.8）应从 0 V → 1.8 V → 证明 PHY 真的被叫醒了
>
> 若 pin13 是 3.3 V 而 pin29 仍是 0 V，说明 STP 收到了但稳压器/晶振起不来（硬件问题）。

---

## 7. `usb_device_setup()` 的调用顺序

**顺序不能改**，每一步都有依赖：

```
① usb_ulpi_analog_switch_fixup()        闭合 PC2/PC3 模拟开关，否则后面全白做
② RCC->AHB1ENR 置 USB1OTGHSEN + USB1OTGHSULPIEN   先开时钟门控
③ k_busy_wait(200)
④ usb_phy_hw_reset()                    PD9 打 20 ms 复位脉冲
⑤ usb_ulpi_phy_wakeup()                 PC0 拉高并保持
⑥ can_channels_validate()
⑦ gs_usb_register()                     ★ 必须在 usbd_init() 之前
⑧ usbd_add_configuration / register_class / set_code_triple / set_bcd_* （HS + FS 各一套）
⑨ usbd_add_descriptor（lang → mfr → product → sn → bos）
⑩ usbd_msg_register_cb()
⑪ usbd_init()                           ★ 全程 STP 保持高电平
⑫ usb_ulpi_phy_stp_release()            ★ 必须在 usbd_init() 之后
⑬ usbd_enable()（最多重试 3 次，每次间隔 100 ms）
```

### 7.1 为什么②要先于④⑤

之前是「PHY 先出钟、`udc_stm32_init` 里后开门控」。
如果 ULPI 时钟域**需要门控先就绪才能锁住外部时钟**，那个顺序就永远收不到。
这里只置位、不回卷，等效于提前帮驱动做好这一步。

### 7.2 为什么⑦要早于⑪

`gs_usb_register()` 注册的是 gs_usb 类的设备与 ops；
`usbd_init()` 之后设备栈就冻结了，再注册会失败或不被识别。

---

## 8. 配置注册与 speed 分支

### 8.1 HS / FS 两套配置的处理

```c
const bool has_hs = (usbd_caps_speed(&usbd) == USBD_SPEED_HS);
```

> ⚠️ 当 DTS 里 `maximum-speed = "full-speed"` 时，驱动会把 `caps.hs` 置 0，
> 此时 `usbd_add_configuration(USBD_SPEED_HS)` 会返回 **`-ENOTSUP`**，
> **必须跳过**，否则整个初始化直接返回错误。

- `has_hs == true` → 注册 HS 一套（配置 / class / code triple / bcdUSB）
- **FS 一套始终注册**（不依赖 `has_hs`）

### 8.2 相关宏与常量

| 项 | 值 | 说明 |
|---|---|---|
| `APP_VERSION_BCD` | 见 `Basic_USB.h` | `bcdDevice` |
| `USB_SRN_2_0_1` | — | `bcdUSB = 2.0.1` |
| `CONFIG_USBD_GS_USB_MAX_CHANNELS` | 3 | gs_usb 通道数 |

### 8.3 本板必须用 `high-speed`

ST 的 `stm32h7xx_ll_usb.c` **只清 `GUSBCFG.ULPIFSLS`，从不置位**，
所以 `full-speed` + ULPI PHY 这条组合在 HAL 层面就是不通的。

> ⚠️ **外部 ULPI PHY 只能跑 HS，DTS 必须写 `maximum-speed = "high-speed"`。**

---

## 9. 描述符与 BOS

### 9.1 添加顺序

> ⚠️ **`lang`（语言 ID）描述符必须第一个添加**，否则后续字符串索引会全部错位。

顺序：`lang` → `mfr` → `product` → `sn` → `bos_lpm`

### 9.2 `bcdUSB = 2.0.1` 的作用

设为 `USB_SRN_2_0_1` 是为了**触发主机读取 BOS 描述符** ——
Windows 下 **WinUSB 免驱**需要主机先读 BOS 判定设备能力。

### 9.3 gs_usb 使用的 VID/PID

| 字段 | 值 |
|---|---|
| `idVendor` | `0x1D50` |
| `idProduct` | `0x606F` |

Linux 内核的 `gs_usb` 驱动靠这两个 ID 匹配，**改成别的 ID 就不认了**。

---

## 10. `usbd_enable` 的 `-EALREADY`

`usbd_enable()` 在**设备已处于使能状态**时返回 `-EALREADY`，
这属于「已经是目标状态」，**应视为成功**，不能当错误处理。

当前实现重试 3 次、每次等 100 ms，并对 `-EALREADY` 直接放行。

---

## 11. gs_usb 事件回调

### 11.1 通道号越界防御

> ⚠️ `gs_usb` 上报的通道号 `ch` 若越界，会踩坏 `USB_CHANNEL_*[]` 数组
> （每个数组长度都是 `CONFIG_USBD_GS_USB_MAX_CHANNELS`）。
> 回调入口必须做 `if (ch >= CONFIG_USBD_GS_USB_MAX_CHANNELS) return -EINVAL;`

### 11.2 五个计数器

| 数组 | 对应事件 |
|---|---|
| `USB_CHANNEL_STARTED` | `GS_USB_EVENT_CHANNEL_STARTED` |
| `USB_CHANNEL_ERROR_OFF` | `GS_USB_EVENT_CHANNEL_ERROR_OFF` |
| `USB_CHANNEL_ERROR_ON` | `GS_USB_EVENT_CHANNEL_ERROR_ON` |
| `USB_CHANNEL_ACTIVITY_RX` | `GS_USB_EVENT_CHANNEL_ACTIVITY_RX` |
| `USB_CHANNEL_ACTIVITY_TX` | `GS_USB_EVENT_CHANNEL_ACTIVITY_TX` |

全部是 `atomic_t`，用 `atomic_inc()` 累加。

### 11.3 被注释掉的「错误计数 ≥128 上报」逻辑

源码里曾有一段被注释掉的代码，原意是：

```c
/* 错误计数 >= 128 时，把通道号丢进 CAN_ERR_MSGQ，交给 USB_CAN_ERR_thread 处理 */
if (event == GS_USB_EVENT_CHANNEL_ERROR_ON) {
    uint8_t ch_8 = (uint8_t)ch;
    k_msgq_put(&CAN_ERR_MSGQ, &ch_8, K_NO_WAIT);
    return -1;
}
```

> ⚠️ **这段目前是关闭状态。** `CAN_ERR_MSGQ` 深度只有 **2**，
> 且对应的线程 `USB_CAN_ERR_tid` 在 `main.cpp` 里也是注释掉的。
> 若要启用错误自动恢复，**必须同时**把 `USB_CAN_ERR_tid` 的
> `K_THREAD_DEFINE` 打开，否则队列会满（`k_msgq_put` 用 `K_NO_WAIT` 会静默丢弃）。

---

## 附录：三个通道的设备映射

```c
const struct device *can_channels[CONFIG_USBD_GS_USB_MAX_CHANNELS] = {
    DEVICE_DT_GET(DT_NODELABEL(fdcan1)),        /* can0  真实 FDCAN1 */
    DEVICE_DT_GET(DT_NODELABEL(can_loopback0)), /* can1  虚拟回环   */
    DEVICE_DT_GET(DT_NODELABEL(fdcan3)),        /* can2  真实 FDCAN3 */
};
```

> ⚠️ `fdcan1` 同时被 `main.cpp` 的 `CAN2_thread` 使用。
> `gs_usb` 与 `CAN2_thread` **只能二选一**，由 `APP_STANDALONE_CAN_MODE` 控制。
