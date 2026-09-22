#ifdef CONFIG_USB_DEVICE_STACK_NEXT
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/msos_desc.h>
#include <zephyr/sys/byteorder.h>
#include <cannectivity/usb/class/gs_usb.h>

/* ============ 1. 设备上下文 + 标准字符串/配置描述符 ============ */
USBD_DEVICE_DEFINE(usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), 0x1D50, 0x606F);

USBD_DESC_LANG_DEFINE(lang);
USBD_DESC_MANUFACTURER_DEFINE(mfr, "A_Shui");
USBD_DESC_PRODUCT_DEFINE(product, "product_0");
USBD_DESC_SERIAL_NUMBER_DEFINE(sn);
USBD_DESC_CONFIG_DEFINE(fs_config_desc, "Full-Speed Configuration");
USBD_CONFIGURATION_DEFINE(fs_config, 0, 250, &fs_config_desc);

/* ============ 2. MS OS 2.0 描述符集（WinUSB 免驱）============ */
#define COMPATIBLE_ID_WINUSB 'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00

/* candleLight 用的 DeviceInterfaceGUID（UTF-16LE，末尾含 NUL）*/
#define GS_USB_DEVICE_INTERFACE_GUID                                                 \
    '{', 0x00, 'c', 0x00, '6', 0x00, 'e', 0x00, '5', 0x00, '1', 0x00, '5', 0x00, \
        'a', 0x00, '2', 0x00, '-', 0x00, '8', 0x00, 'd', 0x00, 'c', 0x00,   \
        '6', 0x00, '-', 0x00, '4', 0x00, 'f', 0x00, 'c', 0x00, '4', 0x00,   \
        '-', 0x00, 'a', 0x00, '0', 0x00, '3', 0x00, 'c', 0x00, '-', 0x00,   \
        '9', 0x00, '3', 0x00, '2', 0x00, '5', 0x00, '5', 0x00, '5', 0x00,   \
        'd', 0x00, '6', 0x00, '8', 0x00, 'e', 0x00, '6', 0x00, '}', 0x00,   \
        0x00, 0x00

#define APP_VERSION_BCD 0x0100

struct msos2_descriptor {
    struct msosv2_descriptor_set_header header;
    struct msosv2_compatible_id compatible_id;
    struct msosv2_guids_property guids_property;
} __packed;

static const struct msos2_descriptor msos2_desc = {
    .header = {
        .wLength = sizeof(struct msosv2_descriptor_set_header),
        .wDescriptorType = MS_OS_20_SET_HEADER_DESCRIPTOR,
        .dwWindowsVersion = 0x06030000,          /* Windows 8.1+ */
        .wTotalLength = sizeof(struct msos2_descriptor),
    },
    .compatible_id = {
        .wLength = sizeof(struct msosv2_compatible_id),
        .wDescriptorType = MS_OS_20_FEATURE_COMPATIBLE_ID,
        .CompatibleID = {COMPATIBLE_ID_WINUSB},
    },
    .guids_property = {
        .wLength = sizeof(struct msosv2_guids_property),
        .wDescriptorType = MS_OS_20_FEATURE_REG_PROPERTY,
        .wPropertyDataType = MS_OS_20_PROPERTY_DATA_REG_MULTI_SZ,
        .wPropertyNameLength = 42,                /* sizeof PropertyName[] */
        .PropertyName = {DEVICE_INTERFACE_GUIDS_PROPERTY_NAME},
        .wPropertyDataLength = 80,                /* sizeof bPropertyData[] */
        .bPropertyData = {GS_USB_DEVICE_INTERFACE_GUID},
    },
};

/* ============ 3. BOS 能力：USB2.0 Extension(LPM) + MS OS 2.0 Platform ============ */
static const struct usb_bos_capability_lpm bos_cap_lpm = {
    .bLength = sizeof(struct usb_bos_capability_lpm),
    .bDescriptorType = USB_DESC_DEVICE_CAPABILITY,
    .bDevCapabilityType = USB_BOS_CAPABILITY_EXTENSION,
    .bmAttributes = 0UL,
};

struct usb_bos_msosv2 {
    struct usb_bos_platform_descriptor platform;
    struct usb_bos_capability_msos cap;
} __packed;

static const struct usb_bos_msosv2 bos_cap_msosv2 = {
    .platform = {
        .bLength = sizeof(struct usb_bos_msosv2),
        .bDescriptorType = USB_DESC_DEVICE_CAPABILITY,
        .bDevCapabilityType = USB_BOS_CAPABILITY_PLATFORM,
        .bReserved = 0,
        .PlatformCapabilityUUID = {
            /* MS OS 2.0 Platform Capability ID */
            0xDF, 0x60, 0xDD, 0xD8, 0x89, 0x45, 0xC7, 0x4C,
            0x9C, 0xD2, 0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F,
        },
    },
    .cap = {
        .dwWindowsVersion = sys_cpu_to_le32(0x06030000),
        .wMSOSDescriptorSetTotalLength = sys_cpu_to_le16(sizeof(msos2_desc)),
        .bMS_VendorCode = 0x01,   /* 必须和下面 handler 里的 bRequest 一致 */
        .bAltEnumCode = 0x00,
    },
};

/* ============ 4. 厂商请求 handler：把 MSOS2 描述符集发给主机 ============ */
static struct net_buf *msos_vendor_handler(const struct usbd_context *const ctx,
                                           const struct usb_setup_packet *const setup)
{
    if (setup->bRequest == 0x01 && setup->wIndex == MS_OS_20_DESCRIPTOR_INDEX) {
        size_t len = MIN(sizeof(msos2_desc), setup->wLength);
        struct net_buf *buf = usbd_ep_ctrl_data_in_alloc(ctx, len);

        if (buf == NULL) {
            return NULL;
        }

        net_buf_add_mem(buf, &msos2_desc, len);
        return buf;
    }

    return NULL;
}

/* ============ 5. BOS 描述符节点（必须在上面这些对象/handler 之后）============ */
USBD_DESC_BOS_DEFINE(bos_lpm, sizeof(bos_cap_lpm), &bos_cap_lpm);
USBD_DESC_BOS_VREQ_DEFINE(bos_msosv2, sizeof(bos_cap_msosv2), &bos_cap_msosv2, 0x01,
                          msos_vendor_handler, NULL);

#endif