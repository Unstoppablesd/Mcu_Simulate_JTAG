/*****************************************************************************
 * @file dap_main.h
 * @brief CMSIS-DAP v2 protocol handler - header
 *
 * Implements CMSIS-DAP v2 command processing over USB Bulk endpoints.
 * Reference: https://arm-software.github.io/CMSIS_5/DAP/html/index.html
 *****************************************************************************/

#ifndef __DAP_MAIN_H__
#define __DAP_MAIN_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * CMSIS-DAP v2 Command IDs
 *===========================================================================*/

/* General Commands (0x00-0x0F) */
#define DAP_CMD_INFO                0x00
#define DAP_CMD_HOSTSTATUS          0x01
#define DAP_CMD_CONNECT             0x02
#define DAP_CMD_DISCONNECT          0x03
#define DAP_CMD_WRITE_ABORT         0x08
#define DAP_CMD_DELAY               0x09
#define DAP_CMD_RESET_TARGET        0x0A

/* SWJ/JTAG Commands (0x10-0x1F) — matches openFPGALoader */
#define DAP_CMD_SWJ_CLK             0x11  /* set clock frequency */
#define DAP_CMD_SWJ_SEQUENCE        0x12  /* TMS-only sequence */
#define DAP_CMD_JTAG_SEQUENCE       0x14  /* JTAG TDI/TDO/TMS sequence */

/* Transfer Commands (0x30-0x3F) */
#define DAP_CMD_TRANSFER_CONFIGURE  0x30
#define DAP_CMD_TRANSFER            0x31
#define DAP_CMD_TRANSFER_BLOCK      0x32

/*===========================================================================
 * CMSIS-DAP Response Status
 *===========================================================================*/
#define DAP_OK                      0x00
#define DAP_ERROR                   0xFF

/*===========================================================================
 * Port definitions
 *===========================================================================*/
#define DAP_PORT_DISABLED           0x00
#define DAP_PORT_JTAG               0x01
#define DAP_PORT_SWD                0x02

/*===========================================================================
 * Connect / Disconnect
 *===========================================================================*/
#define DAP_CONNECT_DEFAULT         0x00
#define DAP_CONNECT_SWD             0x01
#define DAP_CONNECT_JTAG            0x02

/*===========================================================================
 * Transfer command flags (used by DAP_CMD_TRANSFER for pyOCD/OpenOCD)
 *===========================================================================*/
#define DAP_TRANSFER_APnDP          0x01
#define DAP_TRANSFER_RnW            0x02

/*===========================================================================
 * API Functions
 *===========================================================================*/

/**
 * @brief Initialize CMSIS-DAP subsystem.
 */
void dap_init(void);

/**
 * @brief Process a CMSIS-DAP command received on USB Bulk OUT.
 * @param req_data  Pointer to received command data
 * @param req_len   Length of received command
 * @param resp_data Pointer to response buffer (filled by this function)
 * @param resp_len  Output: length of response data
 */
void dap_process_command(const uint8_t *req_data, uint32_t req_len,
                          uint8_t *resp_data, uint32_t *resp_len);

/**
 * @brief Get the DAP info string.
 */
const char *dap_get_info_string(void);

#ifdef __cplusplus
}
#endif

#endif /* __DAP_MAIN_H__ */
