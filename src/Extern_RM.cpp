#include "Extern_RM.h"

void Send_RM(const struct device *can, uint16_t Id, int16_t M_201, int16_t M_202, int16_t M_203, int16_t M_204)
{
    struct can_frame Frame =
    {
        .id = Id,
        .dlc = 8,
    };
	Frame.data[0] = (uint8_t)(M_201 >> 8);
	Frame.data[1] = (uint8_t)M_201;
	Frame.data[2] = (uint8_t)(M_202 >> 8);
	Frame.data[3] = (uint8_t)M_202;
	Frame.data[4] = (uint8_t)(M_203 >> 8);
	Frame.data[5] = (uint8_t)M_203;
	Frame.data[6] = (uint8_t)(M_204 >> 8);
	Frame.data[7] = (uint8_t)M_204;
	can_send(can, &Frame, K_NO_WAIT, NULL, NULL);
}