# 内存
- FLASH - 存放固件代码、中断向量表、只读数据（const 变量）等。掉电后数据不丢失。

- RAM   - 存放全局变量、静态变量、堆（Heap）和栈（Stack）。
        - 注：在 STM32H7 中，这通常指的是 AXI SRAM (D1域)。

- ITCM  - 这是一种零等待状态的高速内存，直接挂在 CPU 内核上。
        - 注：通常用于存放对时间极其敏感的中断服务程序 (ISR) 或核心算法代码。

- DTCM  - 零等待状态的高速内存，专用于数据。
        - 通常用于存放栈 (Stack)、关键全局变量或高频访问的数据。

- EXT MEM - 这是通过外部总线（如 FMC/Quad-SPI）连接的外部 SDRAM 或 NOR Flash。
          - 通常用于存放大图片、音频文件、GUI 资源或巨大的数组。

- SRAM  - STM32H7 系列将内部 RAM 分成了多个不同的域（Domain）和块（Block）。
        - SRAM0/1/2通常属于D2域，常用于 DMA 传输或通用数据缓冲。
        - SRAM4: 属于D3域（低功耗域），系统低功耗模式下能保持数据。

# CAN详解
## 1. dts
```dts
// @interrupts local：can_mcan.c
// @int0   can_mcan_line_0_isr();
// @int1   can_mcan_line_1_isr();
// @calib  stm32h7_mcan_irq_config_##n() 现在无启用

// @bosch,mram-cfg
// std 过滤器 28×4  = 112
// ext 过滤器  8×8  =  64
// RX FIFO0   6×72 = 432
// TX Event   3×8  =  24
// TX buffer  3×72 = 216
// 合计             = 848 = 0x350(FDCAN2 的 offset)
&fdcan2 {
	pinctrl-0 = <&fdcan2_rx_pb12 &fdcan2_tx_pb13>;
	pinctrl-names = "default";
	clocks = <&rcc STM32_CLOCK(APB1_2, 8)>,
		 <&rcc STM32_SRC_PLL2_Q FDCAN_SEL(2)>;
	/*            "int0"  "int1"   "calib" */
	interrupts = <20 0>, <22 0>, <63 0>;
	bosch,mram-cfg = <0x350 28 8 6 0 0 3 3>;
	status = "okay";
};
```

## 2. can_mcan_line_0_isr()详解
- 标志位：BO/EP/EW
- 函数：can_mcan_state_change_handler()
```md
介绍：处理CAN控制器状态变化的回调函数
1) 读取当前 CAN 状态和错误计数器
2) 通知用户注册的状态变化回调
3) 如果进入Bus Off状态，取消所有待发送的报文，通知发送回调失败，并按照配置决定自动恢复
```

- 标志位：TEFN
- 函数：can_mcan_tx_event_handler()
```md
介绍：处理TX事件FIFO的中断处理函数。当CAN控制器成功发送一帧报文或发送被取消后，触发中断。
```

- 标志位：TEFL
- 触发：LOG_ERR("TX FIFO element lost") + k_sem_give(tx_sem)
```md
介绍：TX Event FIFO 满导致事件丢失（有帧发完了但回调没跑到）；只补一个信号量，至少不会卡死发送
```

- 标志位：ARA
- 触发：LOG_ERR("Access to reserved address")
```md
介绍：IP访问了保留地址（驱动/IP 配置异常，正常不该出现）
```

- 标志位：MRAF
- 触发：LOG_ERR("Message RAM access failure")	
```md
介绍：报文RAM访问失败（时钟/使能类问题）
```

- 标志位：PEA/PED	
- 触发：无
```md
介绍：仅 CONFIG_CAN_STATS=y 时：can_mcan_read_psr()把 PSR 的 LEC/DLEC 解码成统计：can_stats_get_stuff/form/ack/bit0/bit1/crc_errors() 唯一的数据来源。源码注释说明：这两个中断"否则会非常频繁"，所以默认不开，当前工程也没有开。
```

## 3. can_mcan_line_1_isr()
- 寄存器：RF0N
- 触发：can_mcan_get_message(mram_offsets[RX_FIFO0], RXF0S, RXF0A)
```md
介绍：从RX FIFO中读取接收到的CAN帧，并分发到对应过滤器回调的中断处理函数，即如标准帧const struct device *dev; dev->config->callbacks->std[filt_idx].function
```

- RF1N	
- can_mcan_get_message(mram_offsets[RX_FIFO1], RXF0S, RXF0A)
```md
介绍：有类似RF0N的触发函数，但rx-fifo1-elements = 0，所以永远不会触发。
```

- RF0L/RF1L
- LOG_ERR("Message lost on FIFO0/1") + CAN_STATS_RX_OVERRUN_INC()
```md
介绍：记录CAN设备发生接收溢出的次数
```

## 4. CAN过滤器和中断回调
```c
/* 
        @num       @1
        @location: can_mcan.c
        @use:      分配一个软件标准过滤器槽
        @load:     1.使用互斥锁 2.强制使用FIFO0 3.过滤器元素写入M_CAN硬件MRAM
*/
can_mcan_add_rx_filter_std();/can_mcan_add_rx_filter_ext();

/* 
        @num       @2
        @location: can_mcan.c
        @use:      @1 的上层调用，配置过滤器和中断回调
        @load:     1.filter 2.can_rx_callback_t中断回调 3.将设备标志busy
*/
can_mcan_add_rx_filter();

/* 
        @num       @3
        @location: gs_usb.c
        @use:      @2 的上层调用，把一个CAN设备注册为gs_usb驱动中的一个CAN通道，并为这个CAN设备注册filter
        @load:     1.fliter 2.callback 3.设置状态变化回调函数，将gs_usb_can_rx_callback设置在dev->config->callbacks->std[filt_idx].function，优先级继承can_mcan_line_1_isr()
*/
gs_usb_register_channel();

/* 
        @num       @4
        @location: gs_usb.c
        @use:      @3 的上层调用，初始化一个gs_usb设备，并将多个CAN设备注册为gs_usb的CAN通道
        @load:     1.初始化操作回调struct gs_usb_data *data = dev->data; data->ops就是操作回调 2.初始化多个通道
*/
gs_usb_register();

/* 
        @num       @5
        @location: gs_usb.c
        @use:      gs_usb_data data中的ops的上层调用
        @load:     1.主机的通道模式请求的核心函数，完成后触发ops。
                   2.主机的通道识别请求(CONFIG_USBD_GS_USB_IDENTIFICATION=N不触发ops)。
                   3.Zephyr CAN控制器的状态变化回调，完成后触发ops。
                   4.gs_usb驱动中的接收上传线程。它从接收FIFO中取出待发送给USB主机的帧，通过USB批量输入端点上传给主机，并在传输完成后触发ops。
*/
gs_usb_request_mode();
gs_usb_request_identify();
gs_usb_can_state_change_callback();
gs_usb_rx_thread();
```

# USB详解
## 1. dts
```dts
// @soc     : zephyr/dts/arm/st/h7/stm32h723.dtsi (usbotg_hs 原始节点)
// @base    : 0x40040000              OTG_HS，AHB1 时钟位 25
// @irqs    : <77 "otghs"> <74 "ep1_out"> <75 "ep1_in">，驱动只注册 "otghs"(IRQ 77)
// @phy     : H7最高开Full_Speed
// @eps     : num-bidir-endpoints = <9>  支持 9 个双向端点
//            ram-size = <DT_SIZE_K(4)>  ram的大小
// @clk     : 时钟源，只能配置48MHz
zephyr_udc0: &usbotg_hs {
	pinctrl-0 = <&usb_otg_hs_dm_pa11 &usb_otg_hs_dp_pa12>;
	pinctrl-names = "default";
	interrupts = <77 0>, <74 0>, <75 0>;
	num-bidir-endpoints = <9>;
	phys = <&otghs_fs_phy>;
	maximum-speed = "full-speed";
	ram-size = <DT_SIZE_K(4)>;
	clocks = <&rcc STM32_CLOCK(AHB1, 25)>,
		 <&rcc STM32_SRC_PLL1_Q USB_SEL(1)>;
	status = "okay";
};
```

## 2. 描述符与端点
```c
// @location: inc/USB.h
// VID:PID = 1d50:606f(candleLight)
USBD_DEVICE_DEFINE(usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), 0x1D50, 0x606F);
USBD_DESC_LANG_DEFINE(lang);                      // LANGID
USBD_DESC_MANUFACTURER_DEFINE(mfr, "A_Shui");     // iManufacturer
USBD_DESC_PRODUCT_DEFINE(product, "product_0");   // iProduct
USBD_DESC_SERIAL_NUMBER_DEFINE(sn);               // iSerialNumber(HWINFO 生成)
USBD_DESC_CONFIG_DEFINE(fs_config_desc, "Full-Speed Configuration");
// 属性0、最大电流 250×2mA = 500 mA
USBD_CONFIGURATION_DEFINE(fs_config, 0, 250, &fs_config_desc);
// BOS + LPM 能力节点
USBD_DESC_BOS_DEFINE(bos_lpm, sizeof(bos_cap_lpm), &bos_cap_lpm);
```

```md
介绍：为什么要有 0x01 这个"多余"的 OUT 端点
Linux 内核 gs_usb 驱动在 v6.12.5 之前把端点地址**硬编码**成 0x81(IN)/0x02(OUT)；
而 Zephyr 的 USB 设备栈可能按 UDC 能力**重写**端点地址，于是再加一个 0x01 OUT 兜底。
python-can 等驱动也有同样假设。代价只是多一点 RAM/Flash，开着最安全。
```
- `usbd_device_set_bcd_usb(..., USB_SRN_2_0_1)` 把bcdUSB设为 **2.0.1**：只有 >2.0.0 主机才会读BOS（WinUSB 需要）。
- `usbd_device_set_code_triple(FS, 0, 0, 0)`：设备类/子类/协议全 0，接口类 = `USB_BCC_VENDOR`
  → 没有系统内置类驱动抢绑，交给主机侧 WinUSB / gs_usb。

## 3. 初始化顺序 (main.cpp)
```md
1) ops.event = gs_usb_event_handler            挂事件回调（必须在 register 之前）
2) can_channels_validate(can_channels)         逐项判 NULL
3) gs_usb_register(gs_usb_dev, can_channels, ARRAY_SIZE(can_channels), &ops, NULL)
      校验 nchannels ∈ [1, ARRAY_SIZE(data->channels)]（后者 = CONFIG_USBD_GS_USB_MAX_CHANNELS）
      逐通道 gs_usb_register_channel():
        can_get_capabilities() → features
        can_add_rx_filter(gs_usb_can_rx_callback) ×2（标准/扩展，mask=0 全通）
        can_set_state_change_callback(gs_usb_can_state_change_callback)
4) usbd_add_descriptor(): lang → mfr → product → sn
5) usbd_add_configuration(&usbd, USBD_SPEED_FS, &fs_config)
6) usbd_register_class(&usbd, "gs_usb_0", USBD_SPEED_FS, 1)
7) usbd_device_set_code_triple(FS,0,0,0)
   usbd_device_set_bcd_usb(FS, 2.0.1)
   usbd_device_set_bcd_device(APP_VERSION_BCD = 0x0100)
8) usbd_add_descriptor(&usbd, &bos_lpm)
9) usbd_init(&usbd)  →  usbd_enable(&usbd)   ← 这一步 udc_enable() 才拉 D+，主机开始枚举
```
- **任何一步 `main_err != 0` 都直接 `return`** → USB 根本不使能；而板上 D+ 外部上拉常闭，
  主机在 MCU 复位时**不会**重新枚举 → 看起来"设备还在"，其实已经是旧 netdev。
- ⚠️ 第 3 步失败会让 `data->nchannels` 保持 **0**，而 `gs_usb_request_device_config()` 里是
  `dc.nchannels = data->nchannels - 1U`（u8）→ 下溢 255 → 主机 `probe error -22`
  （`Driver cannot handle more that 255 CAN interfaces`）。所以 register 必须在 enable 之前成功。

## 4. 控制传输 / bRequest 表
标准请求（GET_DESCRIPTOR / SET_ADDRESS / SET_CONFIGURATION …）由 usbd 核心处理；
厂商请求先由 `USBD_VENDOR_REQ(...)`（`gs_usb_vendor_requests`）白名单登记，再分发到：

```c
// @location: gs_usb.c
gs_usb_control_to_host();   // IN 方向：setup → 造 net_buf 回主机
gs_usb_control_to_dev();    // OUT 方向：setup + net_buf → 改设备状态
```
| bRequest | 方向 | 类处理函数 | 作用 |
|---|---|---|---|
| 0x00 HOST_FORMAT | OUT | `gs_usb_request_host_format` | 校验主机字节序 == 0xBEEF |
| 0x01 BITTIMING | OUT | `gs_usb_request_bittiming` | `can_set_timing()` 经典段位时序 |
| 0x02 MODE | OUT | `gs_usb_request_mode` | RESET→`can_stop()`；START→`can_set_mode()`+`can_start()` |
| 0x03 BERR | — | — | 不支持 |
| 0x04 BT_CONST | IN | `gs_usb_request_bt_const` | 返回 fclk_can / brp / tseg / sjw 上下限（内核据此算位时序） |
| 0x05 DEVICE_CONFIG | IN | `gs_usb_request_device_config` | **nchannels-1**、sw/hw version |
| 0x06 TIMESTAMP | IN | `gs_usb_request_timestamp` | 需 `CONFIG_USBD_GS_USB_TIMESTAMP`（未开） |
| 0x07 IDENTIFY | OUT | `gs_usb_request_identify` | 需 `CONFIG_USBD_GS_USB_IDENTIFICATION`（未开→-ENOTSUP） |
| 0x08/0x09 GET/SET_USER_ID | — | — | 不支持 |
| 0x0A DATA_BITTIMING | OUT | `gs_usb_request_data_bittiming` | FD 数据段位时序 |
| 0x0B BT_CONST_EXT | IN | `gs_usb_request_bt_const_ext` | FD 数据段上下限（需 `CONFIG_CAN_FD_MODE`） |
| 0x0C/0x0D SET/GET_TERMINATION | OUT/IN | — | 需 `CONFIG_USBD_GS_USB_TERMINATION`（未开） |
| 0x0E GET_STATE | IN | `gs_usb_request_get_state` | 返回 state + rxerr/txerr（features 里恒有 GET_STATE） |

```md
主机侧典型枚举顺序：
HOST_FORMAT → DEVICE_CONFIG → BT_CONST → (BT_CONST_EXT) → BITTIMING → (DATA_BITTIMING) → MODE(START)
MODE(START) 的 flags 必须 ⊆ channel->features（= CAN 驱动 caps 映射 + ops 提供的特性），
多一位就 -ENOTSUP。features 映射见 gs_usb_features_from_capabilities()：
LOOPBACK→BIT1、LISTENONLY→BIT0、FD→BIT8|BIT10、ONE_SHOT→BIT3、3_SAMPLES→BIT2。
```

## 5. 数据通路
两条方向都经过 `rx_fifo`，TX 完成后的"回声帧"也从 **IN** 端点回主机。

**OUT（主机 → CAN 总线）**
```md
主机 bulk OUT (0x01 / 0x02)
 → UDC 传完 → usbd 线程调用类回调 .request() = gs_usb_request()
 → k_fifo_put(&data->tx_fifo)  +  gs_usb_out_start()（立刻再挂一个 OUT 传输，双缓冲流水）
 → gs_usb_tx_thread：剥 12B 头 → 校验 channel < nchannels 且 channel->started
 → can_send(channel->dev, &frame, K_FOREVER, gs_usb_can_tx_callback, buf)
 → TX 完成回调 gs_usb_can_tx_callback()：把这块 buf 改写成回声帧
   （echo_id = 主机给的值，can_id/can_dlc 清零）→ k_fifo_put(&data->rx_fifo) → IN 端点
```
**IN（CAN 总线 → 主机）**
```md
CAN 控制器收到帧 → 过滤器回调 gs_usb_can_rx_callback()
 → gs_usb_buf_alloc(gs_usb_pool, K_NO_WAIT)（分配到就 0，分配不到就丢帧）
 → 填 12B 头（echo_id = 0xFFFFFFFF 表示这是 RX 帧）+ 8B / 64B 数据
 → k_fifo_put(&data->rx_fifo)
 → gs_usb_rx_thread：usbd_ep_enqueue(IN) → k_sem_take(&data->in_sem, K_FOREVER)
 → IN 传完 → .request() 里 net_buf_unref() + k_sem_give(&in_sem) → 才处理下一帧
```
```md
介绍：几个关键约束
1) IN 方向**严格串行**：in_sem 初值 0/上限 1，同时只有 1 个 IN 传输在途
   → 帧率上限 ≈ 1/(IN 传输往返)，这也是"USB 速率才是瓶颈"的原因之一。
2) 池耗尽丢帧：gs_usb_buf_alloc 失败 → LOG_ERR + k_sem_give(&channel->rx_overflows)，丢这一帧；
   gs_usb_rx_thread 在 enqueue 之前 k_sem_take(rx_overflows, K_NO_WAIT) 成功，
   就把 GS_USB_CAN_FLAG_OVERFLOW(BIT0) 打在**下一帧**上 → 没有独立丢帧计数。
3) 错误帧（BO/EP/EW）由 gs_usb_can_state_change_callback() 生成，走同一条 rx_fifo/IN 路径，
   can_id 带 GS_USB_CAN_ID_FLAG_ERR | ERR_CNTL/BUSOFF/RESTARTED/CNT，data[6]/[7] = TX/RX 错误计数。
   CAN_STATE_STOPPED 不报。
4) 主机发出的帧只有在"通道已 start 且回环打开"时才回得来（回环语义见 CAN 笔记）。
```

## 6. 线程 / 中断 / 缓冲池
| 名称 | 创建处 | 优先级 | 栈 | 职责 |
|---|---|---|---|---|
| OTG_HS ISR (IRQ 77) | `udc_stm32.c` `IRQ_CONNECT(..., HAL_PCD_IRQHandler, ...)` | 中断 | 共享 ISR 栈 | HAL_PCD 中断，往 `k_msgq` 塞 `struct udc_stm32_msg` |
| `usb@40040000` | `udc_stm32.c` `k_thread_create(...)` | `K_PRIO_COOP(8)` | 4096 | 取 msgq → HAL/`udc_submit_event()` → `usbd_event_carrier()` → `k_msgq_put(usbd_msgq)` |
| `usbd` | `usbd_core.c` `usbd_pre_init()`（SYS_INIT POST_KERNEL, prio 90） | `K_PRIO_COOP(8)` | 8192 | 消费 `usbd_msgq` → `usbd_event_handler()` → 第9章的标准请求 + 类回调 `.control_to_*`/`.request`/`.enable`/`.disable` |
| `gs_usb_tx` | `gs_usb_preinit()`（POST_KERNEL 设备初始化） | 1 | 8192 | tx_fifo → `can_send()` |
| `gs_usb_rx` | 同上 | 2 | 8192 | rx_fifo → IN 端点 enqueue |
| `USB_CAN_Err` | `main.cpp` `K_THREAD_DEFINE(...,2,0,0)` | 2 | 1024 | 每 5 s 读并清零 ops 计数 |
| `sysworkq` | 内核 | — | 4096 | usbd 的**事件通知**（`CONFIG_USBD_MSG_DEFERRED_MODE=y` → `usbd_msg.c` 的 `k_work`） |
| `logging` | 内核 | — | 2048 | RTT 输出（deferred） |

```md
注意：udc_stm32 线程与 usbd 线程同为协作式 8 级，协作式不会互相抢占，只在阻塞点切换，
所以类回调（gs_usb_request 等）跑在 usbd 线程里，而 gs_usb_tx/rx 是可抢占的 1/2 级。
```
三个互不相干的缓冲池（别混为一谈）：
```md
1) gs_usb host frame 池：CONFIG_USBD_GS_USB_POOL_SIZE=192 个元素，
   每块 GS_USB_HOST_FRAME_MAX_SIZE = 12B 头 + 64B 数据(FD 模式) = 76B
   （UDC_BUF_POOL_DEFINE 建立，额外带 sizeof(struct udc_buf_info)）。
   RX/TX 共用，所有通道共用 → 池耗尽就是丢帧/OVERFLOW 标志。
2) UDC 层**共用**请求池：CONFIG_UDC_BUF_COUNT=64(描述符数) + CONFIG_UDC_BUF_POOL_SIZE=8192(数据字节)，
   定义在 udc_common.c 的 udc_ep_pool；对齐/粒度按 CONFIG_DCACHE_LINE_SIZE，一个 FS 满包(64B)
   请求实际吃 ~96~128B → 两个参数要配对加大。
3) 系统堆 CONFIG_HEAP_MEM_POOL_SIZE=8192（k_malloc）。
另外 OTG 内部 FIFO 只有 ram-size=4K，CONFIG_UDC_STM32_OTG_RXFIFO_BASELINE_SIZE=600。
```

## 7. ops 事件回调链
```c
/* 
        @num       @1
        @location: gs_usb.c
        @use:      注册设备类：保存 ops/nchannels，逐通道调用 @2
        @load:     1.nchannels 非法或通道未 ready 就提前 return（nchannels 保持 0！）
*/
gs_usb_register();

/* 
        @num       @2
        @location: gs_usb.c
        @use:      把一个 CAN 设备变成 gs_usb 的一个通道
        @load:     1.can_get_capabilities → features 2.can_add_rx_filter(gs_usb_can_rx_callback)×2
                   3.can_set_state_change_callback(gs_usb_can_state_change_callback)
*/
gs_usb_register_channel();

/* 
        @num       @3
        @location: gs_usb.c
        @use:      产生事件的四个源头（都通过 data->ops.event → inc/USB.cpp）
        @load:     1.gs_usb_request_mode()  START/RESET 成功 → CHANNEL_STARTED / CHANNEL_STOPPED
                   2.gs_usb_request_identify() 未开 CONFIG → -ENOTSUP（不触发）
                   3.gs_usb_can_state_change_callback() EP/BO → CHANNEL_ERROR_ON，回升 → CHANNEL_ERROR_OFF
                   4.gs_usb_rx_thread() 每帧 IN 传输完成后 → ACTIVITY_RX / ACTIVITY_TX
*/
gs_usb_request_mode();
gs_usb_request_identify();
gs_usb_can_state_change_callback();
gs_usb_rx_thread();

/* 
        @num       @4
        @location: src/USB.cpp
        @use:      把 @3 的事件累加到原子计数（供应用观察）
        @load:     1.atomic_inc(&USB_CHANNEL_STARTED[ch]) 等五个数组
*/
gs_usb_event_handler();

/* 
        @num       @5
        @location: src/K_USB_CAN_ERR_Thread.cpp
        @use:      每 5 s 打印一次并清零计数
        @load:     1.atomic_clear() 读并清零 2.比较前后值判断"通道是否在工作"
*/
USB_CAN_ERR_thread();
```

