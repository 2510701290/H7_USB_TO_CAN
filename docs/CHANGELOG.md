# Changelog

## 2026.9.22
### Added
- 新增了上位机重新枚举设备的功能
- 新增了RTT检查堆栈占用的功能
### Fixed
- 修复了烧录后必须重新上下电才能正常看RTT和连接上下位机的问题
- 扩大了堆栈，修复了USBDRX的栈溢出
### Changed
- 无

## 2026.9.23
### Added
- 新增了上位机重新枚举设备的功能
- 新增了RTT检查堆栈占用的功能
### Fixed
- 无
### Changed
- 更新了READMW.md和NOTES.md，希望对我和后来者的zephyr的开发有所帮助。
- 删除了对上位机windows的支持，转而专注与对linux的适配。

## 2026.9.28
### Added
- 新增了上位机主控和下位机主控电机的模式切换，并留出接口。
- 新增了RTT命令接收，方便快速调试
### Fixed
- 无
### Changed
- 更新了readme和NOTES，增加了CAN的部分学习笔记

## 2026.10.1
### Added
- 测试了上位机接收CAN报文的浮动，上下位机的控制延时，大体可以勉强可以接受，报告在CAN_ANALYSIS1.md，测试数据可以在docs内
- 增加了部分USB的学习笔记
- 增加了RM电机控制封装
- 弱定义了can接收中断
### Fixed
- 发现了K_USB_CAN_ERR_Thread线程的错误，暂时停止
### Changed
- 统一了上下位机切换的入口函数
- 重新分析了一下时钟，拉高CPU主频到540MHz