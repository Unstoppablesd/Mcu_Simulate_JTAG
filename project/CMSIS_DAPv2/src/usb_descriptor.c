/*****************************************************************************
 * @file usb_descriptor.c
 * @brief USB HID descriptors for CMSIS-DAP v2
 *
 * CMSIS-DAP v2 over HID transport (compatible with openFPGALoader).
 * VID: 0x0D28 (ARM mbed), PID: 0x0204 (CMSIS-DAP v2)
 *
 * HID report: 64-byte generic vendor-defined Input & Output.
 * Command/response format:
 *   Request:  [cmd][data...]
 *   Response: [cmd][status][data...]
 *****************************************************************************/

#include "usbd_core.h"
#include "usbd_hid.h"
#include "usb_hid.h"

/*===========================================================================
 * Constants
 *===========================================================================*/
#define USBD_VID            0x0D28   /* ARM mbed */
#define USBD_PID            0x0204   /* CMSIS-DAP v2 */
#define USBD_MAX_POWER      100      /* 100mA */
#define USBD_LANGID_STRING  0x0409   /* English (US) */

/*!< HID report size (64 bytes = CMSIS-DAP max packet) */
#define HID_REPORT_SIZE     64

/*!< Endpoint addresses */
#define HID_IN_EP           0x81     /* Interrupt IN */
#define HID_OUT_EP          0x02     /* Interrupt OUT */
#define HID_EP_INTERVAL     1        /* Polling interval (1ms) */

/*===========================================================================
 * Total descriptor size calculation:
 *   Config:  9
 *   Interface: 9
 *   HID:     9
 *   EP OUT:  7
 *   EP IN:   7
 *   Total:  41
 *===========================================================================*/
#define USB_CONFIG_SIZE     41

/*===========================================================================
 * HID Report Descriptor (generic 64-byte vendor-defined)
 *
 * This defines a simple HID collection with:
 *   - Input  report: 64 bytes (device -> host, CMSIS-DAP response)
 *   - Output report: 64 bytes (host -> device, CMSIS-DAP command)
 *===========================================================================*/
#define HID_REPORT_DESC_SIZE 34

static const uint8_t hid_report_descriptor[HID_REPORT_DESC_SIZE] = {
    /* Usage Page (Vendor Defined 0xFF00) */
    0x06, 0x00, 0xFF,  /* USAGE_PAGE (Vendor Defined Page 1) */
    /* Usage (Vendor 1) */
    0x09, 0x01,        /* USAGE (Vendor Usage 1) */
    /* Collection (Application) */
    0xA1, 0x01,        /* COLLECTION (Application) */
    /* --- Input Report: CMSIS-DAP response (device -> host) --- */
    0x09, 0x02,        /*   USAGE (Vendor Usage 2) */
    0x15, 0x00,        /*   LOGICAL_MINIMUM (0) */
    0x26, 0xFF, 0x00,  /*   LOGICAL_MAXIMUM (255) */
    0x75, 0x08,        /*   REPORT_SIZE (8) */
    0x95, 0x40,        /*   REPORT_COUNT (64) */
    0x81, 0x02,        /*   INPUT (Data,Var,Abs) */
    /* --- Output Report: CMSIS-DAP command (host -> device) --- */
    0x09, 0x03,        /*   USAGE (Vendor Usage 3) */
    0x15, 0x00,        /*   LOGICAL_MINIMUM (0) */
    0x26, 0xFF, 0x00,  /*   LOGICAL_MAXIMUM (255) */
    0x75, 0x08,        /*   REPORT_SIZE (8) */
    0x95, 0x40,        /*   REPORT_COUNT (64) */
    0x91, 0x02,        /*   OUTPUT (Data,Var,Abs) */
    /* End Collection */
    0xC0,              /* END_COLLECTION */
};

/*===========================================================================
 * USB Device/Config/Interface/HID/Endpoint Descriptors
 *
 * Full descriptor layout:
 *   Device (18) + Config (9) + Interface (9) + HID (9) + EP_OUT (7)
 *   + EP_IN (7) + String descriptors
 *===========================================================================*/

static const uint8_t hid_descriptor[] = {
    /*--- Device Descriptor (18 bytes) ---*/
    USB_DEVICE_DESCRIPTOR_INIT(
        USB_2_0,                    /* bcdUSB */
        0x00,                       /* bDeviceClass (defined at interface) */
        0x00,                       /* bDeviceSubClass */
        0x00,                       /* bDeviceProtocol */
        USBD_VID,                   /* idVendor */
        USBD_PID,                   /* idProduct */
        0x0100,                     /* bcdDevice */
        0x01                        /* bNumConfigurations */
    ),

    /*--- Configuration Descriptor (9 bytes) ---*/
    USB_CONFIG_DESCRIPTOR_INIT(
        USB_CONFIG_SIZE,            /* wTotalLength */
        0x01,                       /* bNumInterfaces */
        0x01,                       /* bConfigurationValue */
        USB_CONFIG_BUS_POWERED,     /* bmAttributes */
        USBD_MAX_POWER              /* bMaxPower (100mA) */
    ),

    /*--- Interface Descriptor (9 bytes) ---*/
    0x09,                          /* bLength */
    USB_DESCRIPTOR_TYPE_INTERFACE, /* bDescriptorType */
    0x00,                          /* bInterfaceNumber */
    0x00,                          /* bAlternateSetting */
    0x02,                          /* bNumEndpoints (IN + OUT) */
    0x03,                          /* bInterfaceClass = HID */
    0x00,                          /* bInterfaceSubClass = None */
    0x00,                          /* bInterfaceProtocol = None */
    0x00,                          /* iInterface */

    /*--- HID Descriptor (9 bytes) ---*/
    0x09,                          /* bLength */
    HID_DESCRIPTOR_TYPE_HID,       /* bDescriptorType = HID */
    0x11, 0x01,                    /* bcdHID = 1.11 */
    0x00,                          /* bCountryCode = None */
    0x01,                          /* bNumDescriptors */
    HID_DESCRIPTOR_TYPE_HID_REPORT,/* bDescriptorType = Report */
    HID_REPORT_DESC_SIZE, 0x00,    /* wItemLength */

    /*--- Endpoint Descriptor: Interrupt OUT (7 bytes) ---*/
    0x07,                          /* bLength */
    USB_DESCRIPTOR_TYPE_ENDPOINT,  /* bDescriptorType */
    HID_OUT_EP,                    /* bEndpointAddress (0x02) */
    0x03,                          /* bmAttributes = Interrupt */
    HID_REPORT_SIZE, 0x00,         /* wMaxPacketSize = 64 */
    HID_EP_INTERVAL,               /* bInterval = 1ms */

    /*--- Endpoint Descriptor: Interrupt IN (7 bytes) ---*/
    0x07,                          /* bLength */
    USB_DESCRIPTOR_TYPE_ENDPOINT,  /* bDescriptorType */
    HID_IN_EP,                     /* bEndpointAddress (0x81) */
    0x03,                          /* bmAttributes = Interrupt */
    HID_REPORT_SIZE, 0x00,         /* wMaxPacketSize = 64 */
    HID_EP_INTERVAL,               /* bInterval = 1ms */

    /*--- String 0: Language ID ---*/
    USB_LANGID_INIT(USBD_LANGID_STRING),

    /*--- String 1: Manufacturer ---*/
    0x12,                       /* bLength = 18 */
    USB_DESCRIPTOR_TYPE_STRING,
    'A', 0x00, 'R', 0x00, 'M', 0x00, ' ', 0x00,
    'm', 0x00, 'b', 0x00, 'e', 0x00, 'd', 0x00,

    /*--- String 2: Product ("CMSIS-DAP v2 (N32G43x)" = 22 chars) ---*/
    0x2E,                       /* bLength = 46 (2 + 22*2) */
    USB_DESCRIPTOR_TYPE_STRING,
    'C', 0x00, 'M', 0x00, 'S', 0x00, 'I', 0x00,
    'S', 0x00, '-', 0x00, 'D', 0x00, 'A', 0x00,
    'P', 0x00, ' ', 0x00, 'v', 0x00, '2', 0x00,
    ' ', 0x00, '(', 0x00, 'N', 0x00, '3', 0x00,
    '2', 0x00, 'G', 0x00, '4', 0x00, '3', 0x00,
    'x', 0x00, ')', 0x00,

    /*--- String 3: Serial Number ---*/
    0x12,                       /* bLength = 18 */
    USB_DESCRIPTOR_TYPE_STRING,
    '0', 0x00, '0', 0x00, '0', 0x00, '0', 0x00,
    '0', 0x00, '0', 0x00, '0', 0x00, '0', 0x00,

    0x00  /* Terminator */
};

/*===========================================================================
 * Public Functions
 *===========================================================================*/

/**
 * @brief Register the CMSIS-DAP USB HID descriptors.
 */
void cmsis_dap_usb_desc_init(void)
{
    usbd_desc_register(hid_descriptor);
}

/**
 * @brief Register the HID report descriptor.
 * Must be called after usbd_hid_add_interface (which sets intf_num).
 */
void cmsis_dap_hid_report_register(uint8_t intf_num)
{
    usbd_hid_report_descriptor_register(intf_num,
        hid_report_descriptor, HID_REPORT_DESC_SIZE);
}
