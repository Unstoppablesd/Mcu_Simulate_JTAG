/*****************************************************************************
 * @file main.c
 * @brief CMSIS-DAP v2 main entry
 *
 * Initializes RT-Thread, CherryUSB WinUSB device, and CMSIS-DAP protocol
 * handler. Runs as an RT-Thread application with USB event processing.
 *****************************************************************************/

#include <rtthread.h>
#include "main.h"
#include "usbd_core.h"
#include "usbd_hid.h"
#include "dap_main.h"

/*===========================================================================
 * USB descriptors - declared in usb_descriptor.c
 *===========================================================================*/
extern void cmsis_dap_usb_desc_init(void);
extern void cmsis_dap_hid_report_register(uint8_t intf_num);

/*===========================================================================
 * Endpoint addresses
 *===========================================================================*/
#define HID_IN_EP       0x81
#define HID_OUT_EP      0x02

/*===========================================================================
 * USB Endpoint Callbacks
 *===========================================================================*/

/**
 * @brief Bulk OUT endpoint callback - received data from host.
 *
 * This is the main data entry point. When the host sends a CMSIS-DAP
 * command packet on Bulk OUT, we process it and send the response
 * on Bulk IN.
 */
void usbd_hid_out_handler(uint8_t ep)
{
    uint8_t  req_buffer[64];
    uint32_t req_len = 0;
    uint8_t  resp_buffer[64];
    uint32_t resp_len = 0;

    /* Read incoming HID report from USB (64 bytes) */
    usbd_ep_read(ep, req_buffer, sizeof(req_buffer), &req_len);

    if (req_len > 0) {
        /* Process CMSIS-DAP command */
        memset(resp_buffer, 0, sizeof(resp_buffer));
        dap_process_command(req_buffer, req_len, resp_buffer, &resp_len);

        /* Send response back to host (always 64-byte HID report) */
        usbd_ep_write(HID_IN_EP, resp_buffer, 64, NULL);
    }

    /* Re-arm OUT endpoint for next transfer */
    usbd_ep_read(ep, NULL, 0, NULL);
}

/**
 * @brief Bulk IN endpoint callback - data sent to host.
 */
void usbd_hid_in_handler(uint8_t ep)
{
    /* Data sent successfully, nothing to do */
    (void)ep;
}

/*===========================================================================
 * USB class and interface setup
 *===========================================================================*/

usbd_class_t     cmsis_dap_class;
usbd_interface_t cmsis_dap_intf;

usbd_endpoint_t hid_out_ep = {
    .ep_addr = HID_OUT_EP,
    .ep_cb   = usbd_hid_out_handler,
};

usbd_endpoint_t hid_in_ep = {
    .ep_addr = HID_IN_EP,
    .ep_cb   = usbd_hid_in_handler,
};

/*===========================================================================
 * USB Hardware Initialization
 *===========================================================================*/

/**
 * @brief Pull-up control register manipulation for USB DP line.
 */
#define DP_CTRL         ((__IO unsigned*)(0x40001824))
#define _EnPortPullup() (*DP_CTRL = (*DP_CTRL) | 0x02000000)
#define _DisPortPullup() (*DP_CTRL = (*DP_CTRL) & 0xFDFFFFFF)

/**
 * @brief Configures the USB interrupts.
 */
void USB_Interrupts_Config(void)
{
    NVIC_InitType NVIC_InitStructure;
    EXTI_InitType EXTI_InitStructure;

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    /* Enable the USB LP interrupt */
    NVIC_InitStructure.NVIC_IRQChannel                   = USB_LP_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* Enable the USB Wake-up interrupt */
    NVIC_InitStructure.NVIC_IRQChannel                   = USBWakeUp_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* Configure EXTI line 17 for USB wakeup */
    EXTI_ClrITPendBit(EXTI_LINE17);
    EXTI_InitStruct(&EXTI_InitStructure);
    EXTI_InitStructure.EXTI_Line    = EXTI_LINE17;
    EXTI_InitStructure.EXTI_Mode    = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_InitPeripheral(&EXTI_InitStructure);
}

/**
 * @brief Configures USB Clock input (48MHz).
 */
ErrorStatus Set_USBClock(uint32_t sysclk)
{
    ErrorStatus status = SUCCESS;

    switch (sysclk) {
        case SYSCLK_VALUE_48MHz:
            RCC_ConfigUsbClk(RCC_USBCLK_SRC_PLLCLK_DIV1);
            break;
        case SYSCLK_VALUE_72MHz:
            RCC_ConfigUsbClk(RCC_USBCLK_SRC_PLLCLK_DIV1_5);
            break;
        case SYSCLK_VALUE_96MHz:
            RCC_ConfigUsbClk(RCC_USBCLK_SRC_PLLCLK_DIV2);
            break;
        default:
            status = ERROR;
            break;
    }
    return status;
}

/**
 * @brief Complete USB hardware configuration.
 */
ErrorStatus USB_Config(uint32_t sysclk)
{
    ErrorStatus status = SUCCESS;

    USB_Interrupts_Config();

    if (Set_USBClock(sysclk) == SUCCESS) {
        RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_USB, ENABLE);
        status = SUCCESS;
    } else {
        status = ERROR;
    }

    return status;
}

/**
 * @brief Low-level USB device controller init (called by CherryUSB).
 */
void usb_dc_low_level_init(void)
{
    USB_Interrupts_Config();
    USB_Config(SYSCLK_VALUE_96MHz);
    _EnPortPullup();
}

/**
 * @brief Low-level USB deinit (called by CherryUSB).
 */
void usb_dc_low_level_deinit(void)
{
    _DisPortPullup();
}

/*===========================================================================
 * CMSIS-DAP USB initialization
 *===========================================================================*/

/**
 * @brief Initialize the CMSIS-DAP USB device as HID.
 *
 * Uses CherryUSB HID class driver with:
 *   - Interrupt OUT endpoint (host -> device, CMSIS-DAP commands)
 *   - Interrupt IN endpoint (device -> host, CMSIS-DAP responses)
 *   - Generic 64-byte vendor-defined HID report descriptor
 */
void cmsis_dap_usb_init(void)
{
    /* Register USB descriptors (device, config, interface, HID, endpoints, strings) */
    cmsis_dap_usb_desc_init();

    /* Add a single HID interface */
    usbd_hid_add_interface(&cmsis_dap_class, &cmsis_dap_intf);

    /* Register HID report descriptor (after add_interface sets intf_num) */
    cmsis_dap_hid_report_register(0);

    /* Add Interrupt OUT endpoint */
    usbd_interface_add_endpoint(&cmsis_dap_intf, &hid_out_ep);

    /* Add Interrupt IN endpoint */
    usbd_interface_add_endpoint(&cmsis_dap_intf, &hid_in_ep);

    /* Initialize and start USB device */
    usbd_initialize();
}

/*===========================================================================
 * Logger thread (UART debug output)
 *===========================================================================*/

#define LOG_THREAD_STACK_SIZE   1024
#define LOG_THREAD_PRIORITY     20

static rt_uint8_t log_thread_stack[LOG_THREAD_STACK_SIZE];
static struct rt_thread log_thread;

static void log_thread_entry(void *parameter)
{
    rt_kprintf("=== CMSIS-DAP v2 (N32G43x) Started ===\r\n");
    rt_kprintf("Clock: %d MHz, USB: 48 MHz\r\n", SystemCoreClock / 1000000);
    rt_kprintf("VID: 0x0D28, PID: 0x0204\r\n");
    rt_kprintf("Waiting for USB connection...\r\n");

    while (1) {
        if (usb_device_is_configured()) {
            /* LED could be toggled here to indicate connection */
        }
        rt_thread_delay(500);
    }
}

/*===========================================================================
 * Main Entry
 *===========================================================================*/

/**
 * @brief Main program entry point.
 *
 * RT-Thread will call this after kernel initialization.
 * We set up the CMSIS-DAP USB device and start supporting threads.
 */
int main(void)
{
    /* Initialize CMSIS-DAP protocol handler */
    dap_init();

    /* Initialize USB CMSIS-DAP device */
    cmsis_dap_usb_init();

    /* Start logger thread */
    rt_thread_init(&log_thread,
                   "dap_log",
                   log_thread_entry,
                   RT_NULL,
                   log_thread_stack,
                   LOG_THREAD_STACK_SIZE,
                   LOG_THREAD_PRIORITY,
                   5);
    rt_thread_startup(&log_thread);

    return 0;
}
