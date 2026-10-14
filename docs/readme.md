# 安装环境
```bash
# 终端显示
sudo apt install python3-pip python3-tk
pip install pyusb
```

```bash
# 终端控制
pip install python-can
```

```bash
# 可能需要，按理来说是不需要的
pip install pylink-square
pip install pyocd
```

# 上位机命令
1. 经典虚拟回环CAN
```bash
sudo ip link set can0 down; 
sudo ip link set can0 type can bitrate 1000000 sample-point 0.8 loopback on;
sudo ip link set can0 up;
```

2. 经典CAN
```bash
sudo ip link set can1 down;
sudo ip link set can1 type can bitrate 1000000 sample-point 0.8;
sudo ip link set can1 up;
```

3. FDCAN初始
```bash
sudo ip link set can2 down;
sudo ip link set can2 type can bitrate 1000000 sample-point 0.8 dbitrate 5000000 dsample-point 0.8 fd on;
sudo ip link set can2 up;
```

4. 接收can
```bash
# 参考表：
# -e	     显示错误帧
# -ta	     绝对时间戳（-td 是相邻帧间隔，-tz 从 0 开始）
# -x	     打印 rx/tx 方向 + BRS/ESI 列
# -d	     监控内核丢帧（DROPCOUNT，USB 带宽不够时第一时间看出来）
# -L	     log 格式输出
# -8	     经典帧显示原始 DLC（{} 里的 9..15）
# -a	     附带 ASCII 显示
# -n 10    收 10 帧后退出
# -T 3000	 3 秒无帧退出

# 例：
candump -e -ta can0 can1
```

5. 经典can发送
```bash
cansend can0 123#DEADBEEF
```

6. FDCAN发送
```bash
cansend can1 123##1.de.ad.be.ef
```

7. 查看CAN状态
```bash
ip -d -s link show can0 can1
```

8. 上位机重新枚举CAN设备
```bash
# 详情见USB_RESET.py。
# 正确参考：reset 1d50:606f bus ...
python3 userpy/USB_RESET.py
```

9. 连接RTT（需接上对应烧录器）
```bash
# pyocd（dap）
west rtt --runner pyocd
# SEGGER (Jlink)
west rtt --tool-opt="-jlinkscriptfile /home/hjq/Zephyr_text/H7_Work/rtt_range.JLinkScript"
```

10. 上传git
```bash
git add -A
git commit -m "2026.10.14"
git push
```


# 可选开启vscode配置：
## 插件拓展推荐
1. Devicetree LSP
2. Zephyr IDE Extension Pack
3. 我写的神秘插件,可以将vscode视图下的变量加入到ozone
```md
- Ctrl+Shift+P(显示并运行命令)
- Developer: Install Extension from Location
- 打开./Z_SEGGER/ozone_watch
```

## 任务快捷键
```json
// Ctrl+Shift+P(显示并运行命令)
// Preferences: Open Keyboard Shortcuts (JSON)
// 例：
[
  {
    "key": "ctrl+shift+b",
    "command": "workbench.action.tasks.runTask",
    "args": "west build (H7_Work)"
  },
  {
    "key": "ctrl+shift+s",
    "command": "workbench.action.tasks.runTask",
    "args": "west flash + rtt"
  },
  {
    "key": "ctrl+shift+l",
    "command": "workbench.action.tasks.runTask",
    "args": "open ozone"
  }
]
```

# 纯上位机基础要求(建议)
- flash > 256
- ram > 156