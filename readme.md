# 安装环境
sudo apt install python3-pip python3-tk
pip install pyusb

# 上位机命令
## 经典虚拟回环CAN
sudo ip link set can0 down; 
sudo ip link set can0 type can bitrate 1000000 sample-point 0.8 loopback on;
sudo ip link set can0 up;

## 经典CAN
sudo ip link set can1 down;
sudo ip link set can1 type can bitrate 1000000 sample-point 0.8;
sudo ip link set can1 up;

## FDCAN初始
sudo ip link set can2 down;
sudo ip link set can2 type can bitrate 1000000 sample-point 0.8 dbitrate 5000000 dsample-point 0.8 fd on;
sudo ip link set can2 up;

## 接收can
<!-- 例： -->
candump -e -ta can0 can1

<!-- -表 -->
-e	     显示错误帧
-ta	     绝对时间戳（-td 是相邻帧间隔，-tz 从 0 开始）
-x	     打印 rx/tx 方向 + BRS/ESI 列
-d	     监控内核丢帧（DROPCOUNT，USB 带宽不够时第一时间看出来）
-L	     log 格式输出
-8	     经典帧显示原始 DLC（{} 里的 9..15）
-a	     附带 ASCII 显示
-n 10    收 10 帧后退出
-T 3000	 3 秒无帧退出

## 经典can发送
ansend can0 123#DEADBEEF

## FDCAN发送
cansend can1 123##1.de.ad.be.ef

## 上位机重新枚举CAN设备
可使用python3 ${workspaceFolder}/USB_RESET.py，详情见USB_RESET.py。

# 可选开启vscode配置：
## 插件拓展推荐
1. Devicetree LSP
2. Zephyr IDE Extension Pack

## 任务快捷键
Ctrl+Shift+P(显示并运行命令)
Preferences: Open Keyboard Shortcuts (JSON)
例：
[
  {
    "key": "ctrl+shift+b",
    "command": "workbench.action.tasks.runTask",
    "args": "west build (H7_Work)"
  },
  {
    "key": "ctrl+shift+f",
    "command": "workbench.action.tasks.runTask",
    "args": "west flash + rtt (pyocd)"
  }
]
