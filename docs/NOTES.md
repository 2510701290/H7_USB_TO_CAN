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
