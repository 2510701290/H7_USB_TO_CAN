#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/logging/log.h>
#include "Basic_USB.h"
#include <zephyr/drivers/can.h>
#include <zephyr/devicetree.h> 
#include <zephyr/usb/usbd_msg.h>
#include <soc.h>

K_MSGQ_DEFINE(CAN_ERR_MSGQ, sizeof(uint8_t), 2, 1);

LOG_MODULE_REGISTER(usb_can, LOG_LEVEL_INF);

/* ==================================================================
 * USB 设备栈对象：整个工程只在这里定义一次
 *   USBD_DEVICE_DEFINE / USBD_DESC_*_DEFINE 展开为 static 变量，
 *   若写在头文件里会在每个 TU 里各生成一份（曾经的 5 份副本问题）。
 * ================================================================== */
USBD_DEVICE_DEFINE(usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), 0x1D50, 0x606F);

USBD_DESC_LANG_DEFINE(lang);
USBD_DESC_MANUFACTURER_DEFINE(mfr, "A_Shui");
USBD_DESC_PRODUCT_DEFINE(product, "product_0");
USBD_DESC_SERIAL_NUMBER_DEFINE(sn);

USBD_DESC_CONFIG_DEFINE(fs_config_desc, "Full-Speed Configuration");
USBD_CONFIGURATION_DEFINE(fs_config, 0, 250, &fs_config_desc);
USBD_DESC_CONFIG_DEFINE(hs_config_desc, "High-Speed Configuration");
USBD_CONFIGURATION_DEFINE(hs_config, 0, 250, &hs_config_desc);

/* BOS 能力：USB2.0 Extension(LPM) */
static const struct usb_bos_capability_lpm bos_cap_lpm = {
    .bLength = sizeof(struct usb_bos_capability_lpm),   //字节数
    .bDescriptorType = USB_DESC_DEVICE_CAPABILITY,      //Device Capability 描述符
    .bDevCapabilityType = USB_BOS_CAPABILITY_EXTENSION, //USB2.0拓展，如果windows免驱可能要更改
    // bit0：必须为0
    // bit1：支持LPM             =0为不支持
    // bit2：BESL有效            =0不提供BESL参数
    // bit3：baseline BESL有效   =0为不支持
    // bits4–7：为baseline BESL值（单位µs）
    // bit8：deep BESL 数值有效
    // bits：Deep BESL value值（单位µs）
    .bmAttributes = 0UL,
};
USBD_DESC_BOS_DEFINE(bos_lpm, sizeof(bos_cap_lpm), &bos_cap_lpm);

struct usbd_context *usb_main_get(void)
{
    return &usbd;
}

static void usbd_msg_cb(struct usbd_context *const ctx, const struct usbd_msg *const msg)
{
    LOG_INF("USBD: %s (type %d)", usbd_msg_type_string(msg->type), msg->type);

    switch (msg->type) {
    case USBD_MSG_RESET:
        LOG_INF("  reset, bus speed = %d", usbd_bus_speed(ctx));
        break;
    case USBD_MSG_CONFIGURATION:
        LOG_INF("  configured = %d, bus speed = %d", msg->status, usbd_bus_speed(ctx));
        break;
    case USBD_MSG_UDC_ERROR:
    case USBD_MSG_STACK_ERROR:
        LOG_ERR("  stack/udc error %d", msg->status);
        break;
    default:
        break;
    }
}

/* ==================================================================
 * STM32H7（LQFP100 等小封装）专属补丁
 *   PC2 / PC3 与 PC2_C / PC3_C 是同一个焊盘，内部经模拟开关分两路。
 *   复位后 SYSCFG_PMCR.PC2SO / PC3SO = 0 → 模拟开关闭合，焊盘接向模拟侧，
 *   数字功能（AF10 = ULPI_DIR / ULPI_NXT）时序严重劣化甚至不可用。
 *   Zephyr 的 pinctrl 只写 GPIOx_MODER/AFR，不会动这一位；CubeMX 则会
 *   生成 HAL_SYSCFG_AnalogSwitchConfig(..., SYSCFG_SWITCH_PCx_OPEN)。
 *   官方 ULPI 参考板（h747i_disco / portenta_h7 / h757i_eval）都改用了
 *   PI11/PH4，而 LQFP100 上只有 PC2/PC3 可选，只能自己打开模拟开关。
 * ================================================================== */
static void usb_ulpi_analog_switch_fixup(void)
{
#if defined(SYSCFG_PMCR_PC2SO) && defined(SYSCFG_PMCR_PC3SO)
    const uint32_t mask = SYSCFG_PMCR_PC2SO | SYSCFG_PMCR_PC3SO;

    if ((SYSCFG->PMCR & mask) != mask) {
        SYSCFG->PMCR |= mask;
        LOG_INF("SYSCFG->PMCR = 0x%08x (PC2/PC3 analog switch opened)", SYSCFG->PMCR);
    }
#endif
}

/* ---------------- 临时诊断：OTG/ULPI 寄存器快照 ----------------
 * 用 printk 而不是 LOG_xxx：printk 是同步输出，即使 log 线程被饿死也能打出来。
 *   GOTGCTL.BSESVLD  → PHY 是否上报 VBUS/session 有效（ULPI 上行通路是否通）
 *   GRSTCTL.AHBIDL   → AHB 空闲，HAL 里多处会等这一位
 *   GUSBCFG.PHYSEL   → H7 上 0 = ULPI，1 = 内置 FS PHY
 * ------------------------------------------------------------ */
static void usb_dump_otg(const char *tag)
{
    USB_OTG_GlobalTypeDef *otg =
        (USB_OTG_GlobalTypeDef *)(uintptr_t)DT_REG_ADDR(DT_NODELABEL(zephyr_udc0));

    printk("[USB] %s: GUSBCFG=0x%08x GCCFG=0x%08x GRSTCTL=0x%08x GOTGCTL=0x%08x GINTSTS=0x%08x\n",
           tag, otg->GUSBCFG, otg->GCCFG, otg->GRSTCTL, otg->GOTGCTL, otg->GINTSTS);
    printk("[USB]     PHYSEL=%u ULPIFSLS=%u | AHBIDL=%u | BSESVLD=%u ASESVLD=%u\n",
           (unsigned)((otg->GUSBCFG >> 6) & 1U),
           (unsigned)((otg->GUSBCFG >> 30) & 1U),
           (unsigned)((otg->GRSTCTL >> 31) & 1U),
           (unsigned)((otg->GOTGCTL >> 19) & 1U),
           (unsigned)((otg->GOTGCTL >> 18) & 1U));
}

int usb_device_setup(void)
{
    int err;

    usb_ulpi_analog_switch_fixup();
    usb_dump_otg("before usbd_init");

    ops.event = gs_usb_event_handler;

    err = can_channels_validate(can_channels);
    if (err != 0) { LOG_ERR("can_channels_validate (err %d)", err); return err; }

    err = gs_usb_register(gs_usb_dev, can_channels,         /* ★ 补（必须在 usbd_init 之前） */
                          ARRAY_SIZE(can_channels), &ops, NULL);
    if (err != 0) { LOG_ERR("gs_usb_register (err %d)", err); return err; }

    if (!device_is_ready(gs_usb_dev)) { LOG_ERR("gs_usb not ready"); return -ENODEV; }

    /* ---- 配置：只有控制器支持 HS 才注册 HS 那一套；FS 那一套始终注册 ----
     * 注意：maximum-speed="full-speed" 时驱动会把 caps.hs 置 0，
     * 此时 usbd_add_configuration(USBD_SPEED_HS) 会返回 -ENOTSUP，必须跳过。 */
    const bool has_hs = (usbd_caps_speed(&usbd) == USBD_SPEED_HS);

    LOG_INF("controller caps: %s", has_hs ? "HS" : "FS only");

    if (has_hs) {
        err = usbd_add_configuration(&usbd, USBD_SPEED_HS, &hs_config);
        if (err != 0) { LOG_ERR("add HS config (err %d)", err); return err; }

        err = usbd_register_class(&usbd, "gs_usb_0", USBD_SPEED_HS, 1);
        if (err != 0) { LOG_ERR("register class HS (err %d)", err); return err; }

        err = usbd_device_set_code_triple(&usbd, USBD_SPEED_HS, 0, 0, 0);
        if (err != 0) { LOG_ERR("code triple HS (err %d)", err); return err; }

        /* bcdUSB = 2.0.1，触发主机读 BOS（WinUSB 免驱需要） */
        err = usbd_device_set_bcd_usb(&usbd, USBD_SPEED_HS, USB_SRN_2_0_1);
        if (err != 0) { LOG_ERR("bcdUSB HS (err %d)", err); return err; }
    }

    err = usbd_add_configuration(&usbd, USBD_SPEED_FS, &fs_config);
    if (err != 0) { LOG_ERR("add FS config (err %d)", err); return err; }

    err = usbd_register_class(&usbd, "gs_usb_0", USBD_SPEED_FS, 1);
    if (err != 0) { LOG_ERR("register class FS (err %d)", err); return err; }

    err = usbd_device_set_code_triple(&usbd, USBD_SPEED_FS, 0, 0, 0);
    if (err != 0) { LOG_ERR("code triple FS (err %d)", err); return err; }

    err = usbd_device_set_bcd_usb(&usbd, USBD_SPEED_FS, USB_SRN_2_0_1);
    if (err != 0) { LOG_ERR("bcdUSB FS (err %d)", err); return err; }

    err = usbd_device_set_bcd_device(&usbd, APP_VERSION_BCD);
    if (err != 0) { LOG_ERR("bcdDevice (err %d)", err); return err; }

    /* ---- 字符串 / BOS 描述符（lang 必须第一个加） ---- */
    err = usbd_add_descriptor(&usbd, &lang);
    if (err != 0) { LOG_ERR("add lang (err %d)", err); return err; }

    err = usbd_add_descriptor(&usbd, &mfr);
    if (err != 0) { LOG_ERR("add mfr (err %d)", err); return err; }

    err = usbd_add_descriptor(&usbd, &product);
    if (err != 0) { LOG_ERR("add product (err %d)", err); return err; }

    err = usbd_add_descriptor(&usbd, &sn);
    if (err != 0) { LOG_ERR("add serial (err %d)", err); return err; }

    err = usbd_add_descriptor(&usbd, &bos_lpm);
    if (err != 0) { LOG_ERR("add BOS (err %d)", err); return err; }

    err = usbd_msg_register_cb(&usbd, usbd_msg_cb);
    if (err != 0) { LOG_ERR("msg cb (err %d)", err); return err; }

    /* ---- 初始化并使能 ---- */
    LOG_INF("caps speed = %d (0=FS 1=HS)", (int)usbd_caps_speed(&usbd));

    printk("[USB] >>> usbd_init\n");
    err = usbd_init(&usbd);
    printk("[USB] <<< usbd_init ret=%d\n", err);
    if (err != 0) { LOG_ERR("usbd_init (err %d)", err); return err; }

    usb_dump_otg("after usbd_init");

    /* usbd_enable() 在“已使能”时返回 -EALREADY，属于已是目标状态，重试几次 */
    printk("[USB] >>> usbd_enable\n");
    for (int i = 0; i < 3; i++) {
        err = usbd_enable(&usbd);
        if (err == 0 || err == -EALREADY) {
            break;
        }
        LOG_WRN("usbd_enable retry %d (err %d)", i, err);
        k_sleep(K_MSEC(100));
    }
    printk("[USB] <<< usbd_enable ret=%d\n", err);
    if (err != 0 && err != -EALREADY) { LOG_ERR("usbd_enable (err %d)", err); return err; }

    usb_dump_otg("after usbd_enable");

    LOG_INF("USB enabled, bus speed = %d (0=FS 1=HS)", (int)usbd_bus_speed(&usbd));

    return 0;
}

/* ==================== ops 事件 ==================== */
atomic_t USB_CHANNEL_STARTED[CONFIG_USBD_GS_USB_MAX_CHANNELS] = {0},
         USB_CHANNEL_ERROR_OFF[CONFIG_USBD_GS_USB_MAX_CHANNELS] = {0},
         USB_CHANNEL_ERROR_ON[CONFIG_USBD_GS_USB_MAX_CHANNELS] = {0},
         USB_CHANNEL_ACTIVITY_RX[CONFIG_USBD_GS_USB_MAX_CHANNELS] = {0},
         USB_CHANNEL_ACTIVITY_TX[CONFIG_USBD_GS_USB_MAX_CHANNELS] = {0};

const struct device *can_channels[CONFIG_USBD_GS_USB_MAX_CHANNELS] =
{
    DEVICE_DT_GET(DT_NODELABEL(fdcan1)),
    DEVICE_DT_GET(DT_NODELABEL(can_loopback0)),
    DEVICE_DT_GET(DT_NODELABEL(fdcan3)),
};

const struct device *gs_usb_dev = DEVICE_DT_GET(DT_NODELABEL(gs_usb0));
struct gs_usb_ops ops;

int can_channels_validate(const struct device **channels)
{
    for (int i = 0; i < CAN_CHANNELS_NUM; i++) {
        if (channels[i] == NULL) 
        {
            return -EINVAL;
        }
    }
    return 0;
}

int gs_usb_event_handler(const struct device *dev, uint16_t ch,
                                enum gs_usb_event event, void *user_data)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(user_data);

    /* 防御：gs_usb 上报的通道号若越界，会踩坏 USB_CHANNEL_*[] */
    if (ch >= CONFIG_USBD_GS_USB_MAX_CHANNELS) {
        return -EINVAL;
    }

    switch (event)
    {
        case GS_USB_EVENT_CHANNEL_STARTED:
            atomic_inc(&USB_CHANNEL_STARTED[ch]);break;
        case GS_USB_EVENT_CHANNEL_ERROR_OFF:
            atomic_inc(&USB_CHANNEL_ERROR_OFF[ch]);break;
        case GS_USB_EVENT_CHANNEL_ERROR_ON:
            atomic_inc(&USB_CHANNEL_ERROR_ON[ch]);break;
        case GS_USB_EVENT_CHANNEL_ACTIVITY_RX:
            atomic_inc(&USB_CHANNEL_ACTIVITY_RX[ch]);break;
        case GS_USB_EVENT_CHANNEL_ACTIVITY_TX:
            atomic_inc(&USB_CHANNEL_ACTIVITY_TX[ch]);break;
        default:
            break;
    }
    // 错误计数>=128
    // if(event == GS_USB_EVENT_CHANNEL_ERROR_ON)
    // {
    //     uint8_t ch_8 = (uint8_t)ch;
    //     k_msgq_put(&CAN_ERR_MSGQ, &ch_8, K_NO_WAIT);
    //     return -1;
    // }
    return 0;
}