/* CS_CAN - CAN 监视 / 查错实现 */
#include "CS_CAN.h"
#include "Basic_USB.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(cs_can, LOG_LEVEL_INF);

/* ============================ 帧打印 ============================ */

static char *cs_can_put_hex_byte(char *p, uint8_t v)
{
    static const char hexdig[] = "0123456789ABCDEF";

    *p++ = hexdig[v >> 4];
    *p++ = hexdig[v & 0x0F];
    *p++ = ' ';

    return p;
}

void CS_CAN_Log_Frame(const char *tag, const struct can_frame *frame)
{
    uint8_t len;
    char hex[CAN_MAX_DLEN * 3 + 1];
    char *p = hex;

    if (frame == NULL) {
        return;
    }

    len = can_dlc_to_bytes(frame->dlc);

    for (uint8_t i = 0; i < len; i++) {
        p = cs_can_put_hex_byte(p, frame->data[i]);
    }
    if (p != hex) {
        p--;    /* 去掉最后一个空格 */
    }
    *p = '\0';

    LOG_INF("%s id=0x%X %s%s%s dlc=%u len=%u [%s]",
            (tag != NULL) ? tag : "CAN",
            frame->id,
            (frame->flags & CAN_FRAME_IDE) ? "EXT" : "STD",
            (frame->flags & CAN_FRAME_FDF) ? " FD" : " CL",
            (frame->flags & CAN_FRAME_BRS) ? " BRS" : "",
            frame->dlc, len, hex);
}

/* ============================ 状态监视 ============================ */

static const char *cs_can_state_str(enum can_state state)
{
    switch (state) {
    case CAN_STATE_ERROR_ACTIVE:  return "ERROR-ACTIVE";
    case CAN_STATE_ERROR_WARNING: return "ERROR-WARNING";
    case CAN_STATE_ERROR_PASSIVE: return "ERROR-PASSIVE";
    case CAN_STATE_BUS_OFF:       return "BUS-OFF";
    case CAN_STATE_STOPPED:       return "STOPPED";
    default:                      return "UNKNOWN";
    }
}

static const char *cs_can_dev_name(const struct device *dev)
{
    if (dev == DEVICE_DT_GET(DT_NODELABEL(fdcan1))) {
        return "fdcan1";
    }
    if (dev == DEVICE_DT_GET(DT_NODELABEL(fdcan3))) {
        return "fdcan3";
    }
    return dev->name;
}

/* 中断上下文调用，保持轻量 */
static void cs_can_state_change_handler(const struct device *dev,
                                        struct can_state_change_callback *callback,
                                        enum can_state state,
                                        struct can_bus_err_cnt err_cnt)
{
    ARG_UNUSED(callback);

    LOG_WRN("%s state=%s tx_err=%u rx_err=%u",
            cs_can_dev_name(dev), cs_can_state_str(state),
            err_cnt.tx_err_cnt, err_cnt.rx_err_cnt);

    /* tx_err 只涨、rx_err 不动 => 全是 ACK 错误：发出去的帧没人应答。
     * 典型原因：总线上只有自己一个节点 / 对端是 listen-only / 接线或终端电阻 /
     * 收发器没工作。再涨到 256 就是 BUS-OFF，此时 can_send 返回
     * -ENETUNREACH（Zephyr SDK 下 = -114，不是 Linux 的 EALREADY）。 */
    if (state == CAN_STATE_ERROR_PASSIVE && err_cnt.rx_err_cnt == 0U) {
        LOG_ERR("%s ERROR-PASSIVE with rx_err=0: ACK errors only, "
                "no other node on bus / wiring / listen-only peer",
                cs_can_dev_name(dev));
    }

    if (state == CAN_STATE_BUS_OFF) {
        LOG_ERR("%s BUS-OFF: check ACK node, wiring/pinmux, bitrate, transceiver",
                cs_can_dev_name(dev));
    }
}

void CS_CAN_Monitor_Init(void)
{
    static const struct device *const devs[] = {
        DEVICE_DT_GET(DT_NODELABEL(fdcan1)),
        DEVICE_DT_GET(DT_NODELABEL(fdcan3)),
    };
    /* 每个设备必须用独立的 callback 结构体（内部带链表节点） */
    static struct can_state_change_callback cbs[ARRAY_SIZE(devs)];
    static bool inited;

    if (inited) {
        return;
    }
    inited = true;

    for (size_t i = 0; i < ARRAY_SIZE(devs); i++) {
        int err;

        can_init_state_change_callback(&cbs[i], cs_can_state_change_handler);
        err = can_add_state_change_callback(devs[i], &cbs[i]);
        if (err != 0) {
            LOG_ERR("add state callback for %s failed (err %d)",
                    cs_can_dev_name(devs[i]), err);
        }
    }
}

/* ============================ 丢包监测 ============================ */

#define CS_CAN_LOSS_PERIOD_MS   1000
#define CS_CAN_LOSS_STACK_SIZE  1536
#define CS_CAN_LOSS_PRIO        K_PRIO_PREEMPT(10)

struct cs_can_pkt_stat {
    uint32_t rx_overrun;    /* 控制器 RX FIFO 溢出：真正的接收丢帧 */
    uint32_t ack_err;
    uint32_t stuff_err;
    uint32_t form_err;
    uint32_t crc_err;
    uint32_t bit0_err;
    uint32_t bit1_err;
    uint32_t usb_rx;        /* gs_usb 每秒真正送到主机的 RX 帧数 */
    uint32_t usb_tx;        /* gs_usb 每秒回给主机的 TX 完成帧数 */
};

static const struct device *const cs_loss_devs[] = {
    DEVICE_DT_GET(DT_NODELABEL(fdcan1)),
    DEVICE_DT_GET(DT_NODELABEL(fdcan3)),
};

static K_THREAD_STACK_DEFINE(cs_loss_stack, CS_CAN_LOSS_STACK_SIZE);
static struct k_thread cs_loss_tid;

static void cs_can_read_counters(const struct device *dev,
                                 struct cs_can_pkt_stat *s)
{
    s->rx_overrun = can_stats_get_rx_overruns(dev);
    s->ack_err    = can_stats_get_ack_errors(dev);
    s->stuff_err  = can_stats_get_stuff_errors(dev);
    s->form_err   = can_stats_get_form_errors(dev);
    s->crc_err    = can_stats_get_crc_errors(dev);
    s->bit0_err   = can_stats_get_bit0_errors(dev);
    s->bit1_err   = can_stats_get_bit1_errors(dev);
    s->usb_rx = 0U;
    s->usb_tx = 0U;

#ifdef CONFIG_USBD_GS_USB_MAX_CHANNELS
    for (size_t ch = 0; ch < ARRAY_SIZE(can_channels); ch++) {
        if (can_channels[ch] == dev) {
            s->usb_rx = (uint32_t)atomic_get(&USB_CHANNEL_ACTIVITY_RX[ch]);
            s->usb_tx = (uint32_t)atomic_get(&USB_CHANNEL_ACTIVITY_TX[ch]);
            break;
        }
    }
#endif
}

static void cs_can_loss_thread(void *p1, void *p2, void *p3)
{
    struct cs_can_pkt_stat prev[ARRAY_SIZE(cs_loss_devs)];
    struct cs_can_pkt_stat cur;

    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    for (size_t i = 0; i < ARRAY_SIZE(cs_loss_devs); i++) {
        cs_can_read_counters(cs_loss_devs[i], &prev[i]);
    }

    while (1) {
        k_sleep(K_MSEC(CS_CAN_LOSS_PERIOD_MS));

        for (size_t i = 0; i < ARRAY_SIZE(cs_loss_devs); i++) {
            const struct device *dev = cs_loss_devs[i];
            struct can_bus_err_cnt err_cnt = { 0, 0 };
            enum can_state state = CAN_STATE_STOPPED;
            uint32_t d_ovr, d_ack, d_stuff, d_form, d_crc, d_bit;
            uint32_t d_usb_rx, d_usb_tx;

            cs_can_read_counters(dev, &cur);
            (void)can_get_state(dev, &state, &err_cnt);

            /* 无符号相减，计数回绕也能算对 */
            d_ovr    = cur.rx_overrun - prev[i].rx_overrun;
            d_ack    = cur.ack_err    - prev[i].ack_err;
            d_stuff  = cur.stuff_err  - prev[i].stuff_err;
            d_form   = cur.form_err   - prev[i].form_err;
            d_crc    = cur.crc_err    - prev[i].crc_err;
            d_bit    = (cur.bit0_err - prev[i].bit0_err) +
                       (cur.bit1_err - prev[i].bit1_err);
            d_usb_rx = cur.usb_rx     - prev[i].usb_rx;
            d_usb_tx = cur.usb_tx     - prev[i].usb_tx;

            LOG_INF("%s %us: rxovr+%u ack+%u crc+%u form+%u stuff+%u bit+%u | "
                    "usb rx+%u tx+%u | %s txerr=%u rxerr=%u",
                    cs_can_dev_name(dev), CS_CAN_LOSS_PERIOD_MS / 1000U,
                    d_ovr, d_ack, d_crc, d_form, d_stuff, d_bit,
                    d_usb_rx, d_usb_tx, cs_can_state_str(state),
                    err_cnt.tx_err_cnt, err_cnt.rx_err_cnt);

            if (d_ovr > 0U) {
                LOG_ERR("%s RX FIFO OVERRUN +%u (total %u): 控制器接收丢帧",
                        cs_can_dev_name(dev), d_ovr, cur.rx_overrun);
            }

            prev[i] = cur;
        }
    }
}

void CS_CAN_Loss_Monitor_Start(void)
{
    static bool started;

    if (started) {
        return;
    }
    started = true;

    k_thread_create(&cs_loss_tid, cs_loss_stack,
                    K_THREAD_STACK_SIZEOF(cs_loss_stack),
                    cs_can_loss_thread, NULL, NULL, NULL,
                    CS_CAN_LOSS_PRIO, 0, K_NO_WAIT);
#ifdef CONFIG_THREAD_NAME
    k_thread_name_set(&cs_loss_tid, "cs_can_loss");
#endif
}
