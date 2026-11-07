#ifndef EXTERN_DM_H_
#define EXTERN_DM_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/drivers/can.h>
#include <zephyr/devicetree.h>

/*
 * 达妙 MIT 模式反馈量程。
 * 位置单位：rad；速度单位：rad/s；力矩单位：N·m。
 * 不同电压/固件版本可能有不同量程，调用者应以电机手册为准。
 */
struct DM_Feedback
{
    uint16_t can_id;
    uint8_t error;
    float position;
    float velocity;
    float torque;
    uint8_t mos_temperature;
    uint8_t rotor_temperature;
};

/*
 * 发送一帧达妙 MIT 控制命令。
 * Id 是电机 CAN 从机 ID；Kp、Kd、Position、Velocity、Torque 使用物理单位。
 * P_Max、V_Max、T_Max 必须匹配具体电机型号。
 */
int Send_DM(const struct device *can,
            uint16_t Id,
            float Kp,
            float Kd,
            float Position,
            float Velocity,
            float Torque,
            float P_Max,
            float V_Max,
            float T_Max);

/* 达妙命令：0xFC 使能，0xFD 停止，0xFE 设置零位。 */
int Send_DM_Command(const struct device *can, uint16_t Id, uint8_t Command);
int Enable_DM(const struct device *can, uint16_t Id);
int Disable_DM(const struct device *can, uint16_t Id);
int Set_Zero_DM(const struct device *can, uint16_t Id);

/* 将一帧 8 字节达妙反馈解码为位置、速度、力矩和温度。 */
bool Decode_DM(const struct can_frame *Frame,
               struct DM_Feedback *Feedback,
               float P_Max,
               float V_Max,
               float T_Max);

#endif
