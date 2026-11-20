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

USBD_DEVICE_DEFINE(usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), 0x1D50, 0x606F);

USBD_DESC_LANG_DEFINE(lang);
USBD_DESC_MANUFACTURER_DEFINE(mfr, "A_Shui");
USBD_DESC_PRODUCT_DEFINE(product, "product_0");
USBD_DESC_SERIAL_NUMBER_DEFINE(sn);

USBD_DESC_CONFIG_DEFINE(fs_config_desc, "Full-Speed Configuration");
USBD_CONFIGURATION_DEFINE(fs_config, 0, 250, &fs_config_desc);
USBD_DESC_CONFIG_DEFINE(hs_config_desc, "High-Speed Configuration");
USBD_CONFIGURATION_DEFINE(hs_config, 0, 250, &hs_config_desc);

static const struct usb_bos_capability_lpm bos_cap_lpm = {
    .bLength = sizeof(struct usb_bos_capability_lpm),
    .bDescriptorType = USB_DESC_DEVICE_CAPABILITY,
    .bDevCapabilityType = USB_BOS_CAPABILITY_EXTENSION,

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

static void usb_ulpi_analog_switch_fixup(void)
{
#if defined(SYSCFG_PMCR_PC2SO) && defined(SYSCFG_PMCR_PC3SO)
    const uint32_t mask = SYSCFG_PMCR_PC2SO | SYSCFG_PMCR_PC3SO;

    if ((SYSCFG->PMCR & mask) != 0U) {
        SYSCFG->PMCR &= ~mask;
        LOG_INF("SYSCFG->PMCR = 0x%08x (PC2/PC3 analog switch CLOSED)", SYSCFG->PMCR);
    }
#endif
}

#if DT_HAS_COMPAT_STATUS_OKAY(usb_ulpi_phy)
#define USB_ULPI_STP_PORT   GPIOC
#define USB_ULPI_STP_PIN    0U
#define USB_ULPI_STP_AF     10U

#define USB_PHY_RST_PORT    GPIOD
#define USB_PHY_RST_PIN     9U
#define USB_PHY_RST_ASSERT_MS  20
#define USB_PHY_RST_RELEASE_MS 10

#define USB_ULPI_WAKEUP_MS  10

static void usb_phy_hw_reset(void)
{
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIODEN;
    (void)RCC->AHB4ENR;

    USB_PHY_RST_PORT->BSRR = (1U << USB_PHY_RST_PIN);
    USB_PHY_RST_PORT->PUPDR &= ~(3U << (USB_PHY_RST_PIN * 2U));
    USB_PHY_RST_PORT->OTYPER &= ~(1U << USB_PHY_RST_PIN);
    USB_PHY_RST_PORT->OSPEEDR |= (3U << (USB_PHY_RST_PIN * 2U));
    USB_PHY_RST_PORT->MODER =
        (USB_PHY_RST_PORT->MODER & ~(3U << (USB_PHY_RST_PIN * 2U))) |
        (1U << (USB_PHY_RST_PIN * 2U));

    LOG_DBG("PHY RESET(pd9) asserted for %d ms", USB_PHY_RST_ASSERT_MS);
    k_msleep(USB_PHY_RST_ASSERT_MS);

    USB_PHY_RST_PORT->BSRR = (1U << (USB_PHY_RST_PIN + 16U));
    LOG_DBG("PHY RESET(pd9) released, wait %d ms", USB_PHY_RST_RELEASE_MS);
    k_msleep(USB_PHY_RST_RELEASE_MS);
}

static void usb_ulpi_phy_wakeup(void)
{
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOCEN;
    (void)RCC->AHB4ENR;

    USB_ULPI_STP_PORT->BSRR = (1U << USB_ULPI_STP_PIN);
    USB_ULPI_STP_PORT->PUPDR &= ~(3U << (USB_ULPI_STP_PIN * 2U));
    USB_ULPI_STP_PORT->OTYPER &= ~(1U << USB_ULPI_STP_PIN);
    USB_ULPI_STP_PORT->OSPEEDR |= (3U << (USB_ULPI_STP_PIN * 2U));
    USB_ULPI_STP_PORT->MODER =
        (USB_ULPI_STP_PORT->MODER & ~(3U << (USB_ULPI_STP_PIN * 2U))) |
        (1U << (USB_ULPI_STP_PIN * 2U));

    k_msleep(USB_ULPI_WAKEUP_MS);
}

static void usb_ulpi_phy_stp_release(void)
{
    USB_ULPI_STP_PORT->AFR[0] =
        (USB_ULPI_STP_PORT->AFR[0] & ~(0xFU << (USB_ULPI_STP_PIN * 4U))) |
        (USB_ULPI_STP_AF << (USB_ULPI_STP_PIN * 4U));
    USB_ULPI_STP_PORT->MODER =
        (USB_ULPI_STP_PORT->MODER & ~(3U << (USB_ULPI_STP_PIN * 2U))) |
        (2U << (USB_ULPI_STP_PIN * 2U));
    LOG_DBG("STP released to ULPI_STP(AF10)");
}
#else
static void usb_ulpi_phy_wakeup(void) { }
static void usb_ulpi_phy_stp_release(void) { }
static void usb_phy_hw_reset(void) { }
#endif

int usb_device_setup(void)
{
    int err;

    usb_ulpi_analog_switch_fixup();

    RCC->AHB1ENR |= (RCC_AHB1ENR_USB1OTGHSEN | RCC_AHB1ENR_USB1OTGHSULPIEN);
    (void)RCC->AHB1ENR;
    k_busy_wait(200);

    usb_phy_hw_reset();

    usb_ulpi_phy_wakeup();

    ops.event = gs_usb_event_handler;

    err = can_channels_validate(can_channels);
    if (err != 0) { LOG_ERR("can_channels_validate (err %d)", err); return err; }

    err = gs_usb_register(gs_usb_dev, can_channels,
                          ARRAY_SIZE(can_channels), &ops, NULL);
    if (err != 0) { LOG_ERR("gs_usb_register (err %d)", err); return err; }

    if (!device_is_ready(gs_usb_dev)) { LOG_ERR("gs_usb not ready"); return -ENODEV; }

    const bool has_hs = (usbd_caps_speed(&usbd) == USBD_SPEED_HS);

    LOG_INF("controller caps: %s", has_hs ? "HS" : "FS only");

    if (has_hs) {
        err = usbd_add_configuration(&usbd, USBD_SPEED_HS, &hs_config);
        if (err != 0) { LOG_ERR("add HS config (err %d)", err); return err; }

        err = usbd_register_class(&usbd, "gs_usb_0", USBD_SPEED_HS, 1);
        if (err != 0) { LOG_ERR("register class HS (err %d)", err); return err; }

        err = usbd_device_set_code_triple(&usbd, USBD_SPEED_HS, 0, 0, 0);
        if (err != 0) { LOG_ERR("code triple HS (err %d)", err); return err; }

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

    LOG_INF("caps speed = %d (0=FS 1=HS)", (int)usbd_caps_speed(&usbd));

    LOG_DBG("usbd_init (STP held high)");
    err = usbd_init(&usbd);

    usb_ulpi_phy_stp_release();

    if (err != 0) { LOG_ERR("usbd_init (err %d)", err); return err; }

    for (int i = 0; i < 3; i++) {
        err = usbd_enable(&usbd);
        if (err == 0 || err == -EALREADY) {
            break;
        }
        LOG_WRN("usbd_enable retry %d (err %d)", i, err);
        k_sleep(K_MSEC(100));
    }

    if (err != 0 && err != -EALREADY) { LOG_ERR("usbd_enable (err %d)", err); return err; }

    LOG_INF("USB enabled, bus speed = %d (0=FS 1=HS)", (int)usbd_bus_speed(&usbd));

    return 0;
}

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

    return 0;
}
