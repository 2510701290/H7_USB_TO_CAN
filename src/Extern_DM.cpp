#include "Extern_DM.h"

#include <errno.h>

namespace
{
uint16_t Float_To_UInt(float value, float min_value, float max_value, uint8_t bits)
{
    if (value < min_value)
        value = min_value;
    if (value > max_value)
        value = max_value;

    const float span = max_value - min_value;
    const uint32_t max_uint = (1U << bits) - 1U;
    const float normalized = (value - min_value) / span;
    return (uint16_t)(normalized * (float)max_uint);
}

float UInt_To_Float(uint16_t value, float min_value, float max_value, uint8_t bits)
{
    const uint32_t max_uint = (1U << bits) - 1U;
    return ((float)value / (float)max_uint) *
           (max_value - min_value) + min_value;
}
}

int Send_DM(const struct device *can,
            uint16_t Id,
            float Kp,
            float Kd,
            float Position,
            float Velocity,
            float Torque,
            float P_Max,
            float V_Max,
            float T_Max)
{
    if (can == nullptr || P_Max <= 0.0f || V_Max <= 0.0f || T_Max <= 0.0f)
        return -EINVAL;

    struct can_frame Frame =
    {
        .id = Id,
        .dlc = 8,
    };

    const uint16_t q_uint = Float_To_UInt(Position, -P_Max, P_Max, 16);
    const uint16_t dq_uint = Float_To_UInt(Velocity, -V_Max, V_Max, 12);
    const uint16_t kp_uint = Float_To_UInt(Kp, 0.0f, 500.0f, 12);
    const uint16_t kd_uint = Float_To_UInt(Kd, 0.0f, 5.0f, 12);
    const uint16_t tau_uint = Float_To_UInt(Torque, -T_Max, T_Max, 12);

    /* 达妙 MIT 命令：q(16) + dq(12) + kp(12) + kd(12) + tau(12)。 */
    Frame.data[0] = (uint8_t)(q_uint >> 8);
    Frame.data[1] = (uint8_t)q_uint;
    Frame.data[2] = (uint8_t)(dq_uint >> 4);
    Frame.data[3] = (uint8_t)(((dq_uint & 0x0F) << 4) | (kp_uint >> 8));
    Frame.data[4] = (uint8_t)kp_uint;
    Frame.data[5] = (uint8_t)(kd_uint >> 4);
    Frame.data[6] = (uint8_t)(((kd_uint & 0x0F) << 4) | (tau_uint >> 8));
    Frame.data[7] = (uint8_t)tau_uint;

    return can_send(can, &Frame, K_NO_WAIT, NULL, NULL);
}

int Send_DM_Command(const struct device *can, uint16_t Id, uint8_t Command)
{
    if (can == nullptr)
        return -EINVAL;

    struct can_frame Frame =
    {
        .id = Id,
        .dlc = 8,
    };

    for (uint8_t i = 0; i < 7; i++)
        Frame.data[i] = 0xFF;
    Frame.data[7] = Command;

    return can_send(can, &Frame, K_NO_WAIT, NULL, NULL);
}

int Enable_DM(const struct device *can, uint16_t Id)
{
    return Send_DM_Command(can, Id, 0xFC);
}

int Disable_DM(const struct device *can, uint16_t Id)
{
    return Send_DM_Command(can, Id, 0xFD);
}

int Set_Zero_DM(const struct device *can, uint16_t Id)
{
    return Send_DM_Command(can, Id, 0xFE);
}

bool Decode_DM(const struct can_frame *Frame,
               struct DM_Feedback *Feedback,
               float P_Max,
               float V_Max,
               float T_Max)
{
    if (Frame == nullptr || Feedback == nullptr || Frame->dlc != 8 ||
        P_Max <= 0.0f || V_Max <= 0.0f || T_Max <= 0.0f)
        return false;

    const uint8_t *data = Frame->data;
    const uint16_t q_uint = ((uint16_t)data[1] << 8) | data[2];
    const uint16_t dq_uint = ((uint16_t)data[3] << 4) | (data[4] >> 4);
    const uint16_t tau_uint = ((uint16_t)(data[4] & 0x0F) << 8) | data[5];

    Feedback->can_id = Frame->id;
    Feedback->error = (data[0] >> 4) & 0x0F;
    Feedback->position = UInt_To_Float(q_uint, -P_Max, P_Max, 16);
    Feedback->velocity = UInt_To_Float(dq_uint, -V_Max, V_Max, 12);
    Feedback->torque = UInt_To_Float(tau_uint, -T_Max, T_Max, 12);
    Feedback->mos_temperature = data[6];
    Feedback->rotor_temperature = data[7];
    return true;
}
