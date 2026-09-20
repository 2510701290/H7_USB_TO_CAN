<!-- 安装 Python 依赖（用于上位机工具） -->
sudo apt install python3-pip python3-tk
pip install pyusb

<!-- CAN初始化 -->
sudo ip link set can0 down; 
sudo ip link set can0 type can bitrate 1000000 loopback on;
sudo ip link set can0 up;

sudo ip link set can1 down;
sudo ip link set can1 type can bitrate 1000000;
sudo ip link set can1 up;

candump -e can0 can1

<!-- 快捷命令：见.tascks.json -->
