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
west rtt --tool-opt="-jlinkscriptfile ${workspaceFolder}/Z_SEGGER/rtt_range.JLinkScript"
```

10. git
```bash
# 上传
git add -A
git commit -m "2026.11.20"
git push
```

```bash
# 分支克隆
git clone -b git分支 --single-branch git网址 保存的文件夹
# 举例
git clone -b 10-Engineer-gimbal/2026.10.14 --single-branch \
  https://github.com/2510701290/H7_USB_TO_CAN.git H7_gimbal
```

```bash
# 本地 + 远程分支
git branch -a
# 切换到已有分支
git switch dev
# 创建并切换到新分支
git switch -c feature
```

11. west.yml的使用
```bash
# 初始化workspace
west init -m west.yml
# 拉取/更新依赖
west update
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

---

# 文档索引

## `docs/`
| 文件 | 内容 |
| --- | --- |
| `readme.md` | 本文：环境安装、上位机命令、git 用法、vscode 配置速查 |
| `NOTES.md` | 杂项笔记 |
| `CHANGELOG.md` | 变更记录 |

## `docs/USB_DOCS/`
| 文件 | 内容 |
| --- | --- |
| `USB_DEBUG_POSTMORTEM.md` | **USB 枚举失败复盘 + 排查手册 + 协作约定**（要先看） |
| `Basic_USB_Attention.md` | **`src/Basic_USB.cpp` 全部注意事项**（源码已零注释，改代码前必读） |
| `HS_USB_ULPI_BRINGUP.md` | USB3300 ULPI 技术细节：三个根因、寄存器判据、引脚核对 |
| `USB_NOTES.md` | USB 相关零散笔记 |

## `docs/CAN_ANALYSIS/`
| 文件 | 内容 |
| --- | --- |
| `CAN_ANALYSIS1/2/3.md` | CAN 通信抓包分析 |
| `CS_*.txt` | 原始抓包日志（体积大，已 gitignore） |

## `docs/原理图/`（已 gitignore）
| 路径 | 内容 |
| --- | --- |
| `USB2.0高速测试板/` | Altium 工程（`.PrjPcb` / `.SchDoc` / `.PcbDoc`） |
| `netlist3.py` | SchDoc 网表提取脚本 |

```bash
# 用法：从原理图里查某个网络接到哪些引脚
python3 docs/原理图/netlist3.py "docs/原理图/USB2.0高速测试板/USB2.0高速测试板原理图.SchDoc" USB_DIR
```

> 网络名是 `USB_` 前缀：`USB_DIR` / `USB_NXT` / `USB_STP` / `USB_CLKOUT` / `USB_D0~D7` /
> `USB_DP` / `USB_DN` / `USB_RST`。搜 `ULPI` 是查不到的。

**典型输出：**

```
[USB_DIR]
    U2-17(PC2_C)
    U5-12(DIR)
```

## `docs/芯片文档/`（已 gitignore）
| 文件 | 内容 |
| --- | --- |
| `C108383_..._USB3300-EZK-TR_规格书_*.PDF` | USB3300 数据手册（DS00001783C） |