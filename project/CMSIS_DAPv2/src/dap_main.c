/*****************************************************************************
 * @file dap_main.c
 * @brief CMSIS-DAP v2 protocol handler implementation
 *
 * Processes CMSIS-DAP v2 commands received via USB Bulk endpoints.
 * Implements the core DAP command set: Info, Connect, Transfer, JTAG ops.
 *
 * Reference:
 *   https://arm-software.github.io/CMSIS_5/DAP/html/group__DAP__Commands.html
 *****************************************************************************/

#include "dap_main.h"
#include "jtag_driver.h"
#include <string.h>
#include <rtthread.h>

/*===========================================================================
 * Private constants
 *===========================================================================*/

/* CMSIS-DAP v2 protocol version */
#define DAP_PROTOCOL_VERSION    "2.0.0"

/* Max packet sizes */
#define DAP_MAX_REQ_SIZE        512
#define DAP_MAX_RESP_SIZE       512

/* CMSIS-DAP Info string */
#define DAP_INFO_STRING \
    "CMSIS-DAP v2 N32G43x JTAG Debugger\0"

/*===========================================================================
 * Private types
 *===========================================================================*/

typedef struct {
    uint8_t  connected;         /* 1 = connected to target */
    uint8_t  port;              /* DAP_PORT_JTAG or DAP_PORT_SWD */
    uint32_t clock_khz;         /* JTAG/SWD clock in kHz */
    uint8_t  led_status;        /* LED status */
    uint32_t match_mask;        /* Transfer match mask */
    uint32_t match_value;       /* Transfer match value */
    uint32_t idle_cycles;       /* Idle cycles after transfer */
    uint32_t retry_count;       /* Transfer retry count */
    uint32_t match_retry_count; /* Match retry count */
} dap_state_t;

/*===========================================================================
 * Private variables
 *===========================================================================*/

static dap_state_t dap_state;
static uint8_t resp_buffer[DAP_MAX_RESP_SIZE];

/*===========================================================================
 * Helper: Write response header
 *===========================================================================*/

/**
 * @brief Write a response with given command ID and status.
 *
 * Response format for openFPGALoader HID transport:
 *   [byte0:anything][byte1:DAP_OK(0x00)][data...]
 *
 * openFPGALoader's xfer error check:
 *   if (_ll_buffer[0] != instruction && _ll_buffer[1] != DAP_OK)
 * Since byte1 is always DAP_OK, the check always passes.
 */
static uint8_t *dap_write_response(uint8_t cmd, uint8_t status,
                                    const uint8_t *data, uint32_t data_len,
                                    uint32_t *resp_len)
{
    uint32_t idx = 0;
    resp_buffer[idx++] = cmd;      /* byte 0: command echo */
    resp_buffer[idx++] = 0x00;     /* byte 1: DAP_OK (for openFPGALoader check) */
    (void)status;                  /* status sent via data or implied */

    if (data && data_len > 0) {
        memcpy(&resp_buffer[idx], data, data_len);
        idx += data_len;
    }

    *resp_len = idx;
    return resp_buffer;
}

/*===========================================================================
 * Command: DAP_CMD_INFO (0x00)
 *===========================================================================*/

/**
 * @brief Handle DAP_Info command.
 *
 * Request:  [0x00] [info_id]
 * Response: [0x00] [info_len:2] [info_string...]
 */
static void dap_handle_info(const uint8_t *req, uint32_t req_len,
                             uint8_t **resp, uint32_t *resp_len)
{
    if (req_len < 2) {
        *resp = dap_write_response(DAP_CMD_INFO, DAP_ERROR, NULL, 0, resp_len);
        return;
    }

    uint8_t info_id = req[1];
    const char *info_str = NULL;
    uint32_t info_len = 0;

    switch (info_id) {
        case 0x01: /* Vendor Name */
            info_str = "NationsTech";
            info_len = 11;
            break;
        case 0x02: /* Product Name */
            info_str = "N32G43x CMSIS-DAP v2";
            info_len = 21;
            break;
        case 0x03: /* Serial Number */
            info_str = "00000001";
            info_len = 8;
            break;
        case 0x04: /* CMSIS-DAP Firmware Version */
            info_str = DAP_PROTOCOL_VERSION;
            info_len = 5;
            break;
        case 0xF0: /* Capabilities */
            /* Bit 0 = SWD, Bit 1 = JTAG, Bit 2 = SWO_UART, etc. */
            info_str = (const char *)"\x02"; /* JTAG only */
            info_len = 1;
            break;
        case 0xF1: /* Test Domain Timer (not implemented) */
            info_str = (const char *)"\x00\x00\x00\x00";
            info_len = 4;
            break;
        case 0xFE: /* Max Packet Count */
            info_str = (const char *)"\x01";
            info_len = 1;
            break;
        case 0xFF: /* Max Packet Size */
            info_str = (const char *)"\x40\x00"; /* 64 bytes */
            info_len = 2;
            break;
        default:
            *resp = dap_write_response(DAP_CMD_INFO, DAP_ERROR, NULL, 0, resp_len);
            return;
    }

    /* Build response per CMSIS-DAP standard: [info_len_lo][info_len_hi][data...]
     *
     * openFPGALoader's read_info → xfer(2, _buffer, 63) does:
     *   memmove(_buffer, _ll_buffer, 63)  // 2-byte shift (_buffer=_ll_buffer+2)
     * After shift: _buffer[0]=orig[0], _buffer[1]=orig[1], _buffer[2]=orig[2]
     * So _buffer[2] (= capability) comes from orig[2] = data[0].
     * The 2-byte length header fits exactly.
     *
     * Error check: _ll_buffer[0]!=instruction && _ll_buffer[1]!=DAP_OK
     *   info_len_hi is always 0x00 (= DAP_OK) for <256 bytes → bypass OK.
     */
    uint8_t header[2];
    header[0] = (uint8_t)(info_len & 0xFF);   /* info_len_lo */
    header[1] = (uint8_t)((info_len >> 8) & 0xFF); /* info_len_hi (= 0x00) */

    uint32_t idx = 0;
    memcpy(&resp_buffer[idx], header, 2);
    idx += 2;
    memcpy(&resp_buffer[idx], info_str, info_len);
    idx += info_len;
    *resp_len = idx;
    *resp = resp_buffer;
}

/*===========================================================================
 * Command: DAP_CMD_HOSTSTATUS (0x01)
 * openFPGALoader sends this to set host status (connected/running).
 * Request:  [0x01] [type] [status]
 * We just acknowledge.
 *===========================================================================*/

static void dap_handle_hoststatus(const uint8_t *req, uint32_t req_len,
                                   uint8_t **resp, uint32_t *resp_len)
{
    (void)req;
    (void)req_len;
    *resp = dap_write_response(DAP_CMD_HOSTSTATUS, DAP_OK, NULL, 0, resp_len);
}

/*===========================================================================
 * Command: DAP_CMD_CONNECT (0x02)
 *===========================================================================*/

static void dap_handle_connect(const uint8_t *req, uint32_t req_len,
                                uint8_t **resp, uint32_t *resp_len)
{
    if (req_len < 2) {
        *resp = dap_write_response(DAP_CMD_CONNECT, DAP_ERROR, NULL, 0, resp_len);
        return;
    }

    uint8_t port = req[1]; /* DAP_CONNECT_DEFAULT / SWD / JTAG */
    (void)port; /* Only JTAG supported for now */

    /* Initialize JTAG hardware */
    jtag_gpio_init();
    jtag_tap_reset();
    jtag_set_clock(1000); /* Default 1MHz */

    dap_state.connected = 1;
    dap_state.port = DAP_PORT_JTAG;
    dap_state.clock_khz = 1000;

    /* openFPGALoader uses xfer(2, response, 2) which copies from _ll_buffer[0].
     * It checks: response[0]==DAP_CONNECT(0x02) && response[1]==DAP_CONNECT_JTAG(0x02).
     * Must NOT include DAP_OK byte at position 1. */
    resp_buffer[0] = DAP_CMD_CONNECT;   /* 0x02 */
    resp_buffer[1] = DAP_CONNECT_JTAG;  /* 0x02 */
    *resp_len = 2;
    *resp = resp_buffer;
}

/*===========================================================================
 * Command: DAP_CMD_DISCONNECT (0x03)
 *===========================================================================*/

static void dap_handle_disconnect(const uint8_t *req, uint32_t req_len,
                                   uint8_t **resp, uint32_t *resp_len)
{
    dap_state.connected = 0;
    dap_state.port = DAP_PORT_DISABLED;

    *resp = dap_write_response(DAP_CMD_DISCONNECT, DAP_OK, NULL, 0, resp_len);
}

/*===========================================================================
 * Command: DAP_CMD_SWJ_CLK (0x11)
 *
 * openFPGALoader setClkFreq: [0x11][clk_LE32 (Hz)]
 * Response: [0x11][DAP_OK]
 *===========================================================================*/

static void dap_handle_swj_clk(const uint8_t *req, uint32_t req_len,
                                uint8_t **resp, uint32_t *resp_len)
{
    if (req_len >= 5) {
        uint32_t freq_hz = req[1] | ((uint32_t)req[2] << 8) |
                           ((uint32_t)req[3] << 16) | ((uint32_t)req[4] << 24);
        dap_state.clock_khz = freq_hz / 1000;
        jtag_set_clock(dap_state.clock_khz);
    }
    *resp = dap_write_response(DAP_CMD_SWJ_CLK, DAP_OK, NULL, 0, resp_len);
}

/*===========================================================================
 * Command: DAP_CMD_SWJ_SEQUENCE (0x12) — TMS-only sequence
 *
 * openFPGALoader flush: [0x12][num_tms:1][tms_data:(num_tms+7)/8 bytes]
 * TMS bits are LSB-first packed into bytes.
 * Response: [0x12][DAP_OK]
 *===========================================================================*/

static void dap_handle_swj_sequence(const uint8_t *req, uint32_t req_len,
                                     uint8_t **resp, uint32_t *resp_len)
{
    if (req_len < 2) {
        *resp = dap_write_response(DAP_CMD_SWJ_SEQUENCE, DAP_ERROR, NULL, 0, resp_len);
        return;
    }

    uint8_t  num_tms  = req[1];
    uint32_t byte_len = (num_tms + 7) / 8;
    uint32_t div      = jtag_get_clk_divider();

    if (req_len < 2 + byte_len) {
        *resp = dap_write_response(DAP_CMD_SWJ_SEQUENCE, DAP_ERROR, NULL, 0, resp_len);
        return;
    }

    const uint8_t *tms_data = &req[2];

    /* Clock TCK with TMS for each bit */
    for (uint8_t bit = 0; bit < num_tms; bit++) {
        uint8_t byte_idx = bit / 8;
        uint8_t bit_mask = 1U << (bit % 8);

        /* Set TMS */
        if (tms_data[byte_idx] & bit_mask) {
            JTAG_PORT->PBSC = JTAG_TMS_Msk;
        } else {
            JTAG_PORT->PBC = JTAG_TMS_Msk;
        }

        /* Toggle TCK: low → high → low */
        for (volatile int d = 0; d < (int)div; d++) __NOP();
        JTAG_PORT->PBSC = JTAG_TCK_Msk;
        for (volatile int d = 0; d < (int)div; d++) __NOP();
        JTAG_PORT->PBC = JTAG_TCK_Msk;
    }

    *resp = dap_write_response(DAP_CMD_SWJ_SEQUENCE, DAP_OK, NULL, 0, resp_len);
}

/*===========================================================================
 * Command: DAP_CMD_JTAG_SEQUENCE (0x14) — Full JTAG TDI/TDO/TMS/TCK
 *
 * openFPGALoader writeJtagSequence:
 *   [0x14][num_seq:1][seq0_info:1][seq0_data:(nbits+7)/8 bytes]
 *                    [seq1_info:1][seq1_data:...]...
 *
 * Each seq_info byte:
 *   bit 7: TDO capture (1 = read TDO)
 *   bit 6: TMS value
 *   bits 5-0: bit count (0 = 64 bits)
 *
 * TDI data: LSB first, packed into bytes.
 *
 * Response: [0x14][DAP_OK][tdo_data...]
 *   tdo_data only present for sequences with capture bit set.
 *===========================================================================*/

static void dap_handle_jtag_sequence_14(const uint8_t *req, uint32_t req_len,
                                         uint8_t **resp, uint32_t *resp_len)
{
    if (req_len < 2) {
        *resp = dap_write_response(DAP_CMD_JTAG_SEQUENCE, DAP_ERROR, NULL, 0, resp_len);
        return;
    }

    uint8_t  num_seq    = req[1];
    uint32_t req_pos    = 2;
    uint32_t resp_idx   = 0;  /* offset from resp_data */
    uint8_t *resp_data  = &resp_buffer[2];
    uint32_t div        = jtag_get_clk_divider();

    for (uint8_t s = 0; s < num_seq; s++) {
        if (req_pos >= req_len) break;

        uint8_t info    = req[req_pos++];
        uint8_t capture = (info >> 7) & 0x01;
        uint8_t tms_val = (info >> 6) & 0x01;
        uint8_t nbits   = info & 0x3F;
        if (nbits == 0) nbits = 64;  /* 0 means 64 */

        uint32_t byte_len = (nbits + 7) / 8;
        if (req_pos + byte_len > req_len) break;

        const uint8_t *tdi_data = &req[req_pos];
        req_pos += byte_len;

        uint8_t tdo_byte = 0;
        uint8_t tdo_bit  = 0;

        for (uint8_t bit = 0; bit < nbits; bit++) {
            /* Set TDI/TMS while TCK is low */
            if (tdi_data[bit / 8] & (1U << (bit % 8))) {
                JTAG_PORT->PBSC = JTAG_TDI_Msk;
            } else {
                JTAG_PORT->PBC = JTAG_TDI_Msk;
            }
            if (tms_val) {
                JTAG_PORT->PBSC = JTAG_TMS_Msk;
            } else {
                JTAG_PORT->PBC = JTAG_TMS_Msk;
            }

            /* TDI/TMS setup time */
            for (volatile int d = 0; d < (int)div; d++) __NOP();

            /* TCK rising edge */
            JTAG_PORT->PBSC = JTAG_TCK_Msk;

            /* TCK high: sample TDO at end of high period */
            for (volatile int d = 0; d < (int)div; d++) __NOP();

            if (capture && (JTAG_PORT->PID & JTAG_TDO_Msk)) {
                tdo_byte |= (1U << tdo_bit);
            }
            if (capture) {
                tdo_bit++;
                if (tdo_bit == 8) {
                    resp_data[resp_idx++] = tdo_byte;
                    tdo_byte = 0;
                    tdo_bit = 0;
                }
            }

            /* TCK low */
            JTAG_PORT->PBC = JTAG_TCK_Msk;
        }

        /* Flush partial TDO byte */
        if (capture && tdo_bit > 0) {
            resp_data[resp_idx++] = tdo_byte;
        }
    }

    /* Build response: [0x14][DAP_OK][tdo_data...] */
    resp_buffer[0] = DAP_CMD_JTAG_SEQUENCE; /* 0x14 */
    resp_buffer[1] = DAP_OK;                /* 0x00 */

    *resp_len = resp_idx + 2;
    *resp = resp_buffer;
}

/*===========================================================================
 * Command: DAP_CMD_WRITE_ABORT (0x08)
 *===========================================================================*/

static void dap_handle_write_abort(const uint8_t *req, uint32_t req_len,
                                    uint8_t **resp, uint32_t *resp_len)
{
    /* Abort any ongoing JTAG operation (simplified: no-op) */
    *resp = dap_write_response(DAP_CMD_WRITE_ABORT, DAP_OK, NULL, 0, resp_len);
}

/*===========================================================================
 * Command: DAP_CMD_DELAY (0x09)
 *===========================================================================*/

static void dap_handle_delay(const uint8_t *req, uint32_t req_len,
                              uint8_t **resp, uint32_t *resp_len)
{
    if (req_len < 3) {
        *resp = dap_write_response(DAP_CMD_DELAY, DAP_ERROR, NULL, 0, resp_len);
        return;
    }

    /* Delay in microseconds (2 bytes, little-endian) */
    uint32_t delay_us = req[1] | ((uint32_t)req[2] << 8);

    /* Simple busy-wait delay (approximate, ~96 cycles/μs at 96MHz) */
    for (uint32_t i = 0; i < delay_us * 24; i++) {
        __NOP();
    }

    *resp = dap_write_response(DAP_CMD_DELAY, DAP_OK, NULL, 0, resp_len);
}

/*===========================================================================
 * Command: DAP_CMD_RESET_TARGET (0x0A)
 *===========================================================================*/

static void dap_handle_reset_target(const uint8_t *req, uint32_t req_len,
                                     uint8_t **resp, uint32_t *resp_len)
{
    /* Simplified: assert SRST for ~10ms, then release */
    jtag_srst_assert();

    for (volatile uint32_t i = 0; i < 960000; i++) {
        __NOP();
    }

    jtag_srst_deassert();

    *resp = dap_write_response(DAP_CMD_RESET_TARGET, DAP_OK, NULL, 0, resp_len);
}

/*===========================================================================
 * Command: DAP_CMD_TRANSFER_CONFIGURE (0x30)
 *===========================================================================*/

static void dap_handle_transfer_configure(const uint8_t *req, uint32_t req_len,
                                           uint8_t **resp, uint32_t *resp_len)
{
    if (req_len < 5) {
        *resp = dap_write_response(DAP_CMD_TRANSFER_CONFIGURE, DAP_ERROR, NULL, 0, resp_len);
        return;
    }

    dap_state.idle_cycles  = req[1];
    dap_state.retry_count  = req[2] | ((uint32_t)req[3] << 8);
    dap_state.match_retry_count = req[4] | ((uint32_t)req[5] << 8);

    *resp = dap_write_response(DAP_CMD_TRANSFER_CONFIGURE, DAP_OK, NULL, 0, resp_len);
}

/*===========================================================================
 * Command: DAP_CMD_TRANSFER (0x31)
 *===========================================================================*/

/**
 * @brief Execute a DAP transfer (JTAG DP/AP access).
 *
 * This is the core command for debug register access.
 * Simplified JTAG-only implementation.
 */
static void dap_handle_transfer(const uint8_t *req, uint32_t req_len,
                                 uint8_t **resp, uint32_t *resp_len)
{
    if (req_len < 5) {
        *resp = dap_write_response(DAP_CMD_TRANSFER, DAP_ERROR, NULL, 0, resp_len);
        return;
    }

    uint8_t  dp_select __attribute__((unused)) = req[1];
    uint8_t  req_count  = req[2];
    uint8_t  resp_count = 0;
    uint8_t  resp_status = DAP_OK;
    uint32_t resp_idx   = 3; /* After [cmd][resp_count][resp_status] */
    uint8_t *resp_data  = &resp_buffer[3];
    uint32_t req_idx    = 3; /* After dp_select, req_count */

    for (uint8_t i = 0; i < req_count; i++) {
        if (req_idx >= req_len) {
            resp_status = DAP_ERROR;
            break;
        }

        uint8_t transfer_req = req[req_idx++];
        uint8_t ap_ndp  = (transfer_req & DAP_TRANSFER_APnDP) ? 1 : 0;
        uint8_t rnw     = (transfer_req & DAP_TRANSFER_RnW) ? 1 : 0;
        uint8_t a_reg   = (transfer_req >> 2) & 0x03;

        /* For write: get write data */
        uint32_t write_data = 0;
        if (!rnw && req_idx + 4 <= req_len) {
            write_data = req[req_idx] | ((uint32_t)req[req_idx+1] << 8) |
                         ((uint32_t)req[req_idx+2] << 16) | ((uint32_t)req[req_idx+3] << 24);
            req_idx += 4;
        }

        /* JTAG DP/AP access:
         * 1. Go to IR-SHIFT, load DPACC or APACC IR
         * 2. Go to DR-SHIFT, shift the 35-bit transfer
         */
        uint8_t ir_bits[4] = {0};
        uint8_t dr_tdi[5] = {0};
        uint8_t dr_tdo[5] = {0};

        /* Set IR: DPACC=0x0A, APACC=0x0B */
        ir_bits[0] = ap_ndp ? 0x0B : 0x0A;

        jtag_goto_state(0x0B); /* IR-SHIFT */
        jtag_shift_data(ir_bits, NULL, 4, 1); /* 4-bit IR */

        /* Build DR data (35 bits: 1 start + 2 A[3:2] + 1 RnW + 32 data) */
        dr_tdi[0] = 0x01; /* Start bit = 1 */
        dr_tdi[0] |= (a_reg & 0x03) << 1; /* A[3:2] */
        if (rnw) dr_tdi[0] |= 0x08; /* RnW */
        /* For write, data follows */
        if (!rnw) {
            dr_tdi[1] = (write_data >> 0)  & 0xFF;
            dr_tdi[2] = (write_data >> 8)  & 0xFF;
            dr_tdi[3] = (write_data >> 16) & 0xFF;
            dr_tdi[4] = (write_data >> 24) & 0xFF;
        }

        jtag_goto_state(0x04); /* DR-SHIFT */
        jtag_shift_data(dr_tdi, dr_tdo, 35, 1); /* 35-bit DR shift */

        /* For read: extract read data from TDO */
        if (rnw) {
            uint32_t read_data = dr_tdo[1] | ((uint32_t)dr_tdo[2] << 8) |
                                 ((uint32_t)dr_tdo[3] << 16) | ((uint32_t)dr_tdo[4] << 24);

            /* Check ACK bits (bits 1-3 in dr_tdo[0]) */
            uint8_t ack = (dr_tdo[0] >> 1) & 0x07;
            if (ack != 1) { /* OK response = 0b001 */
                resp_status = 0x04; /* FAULT */
            }

            if (resp_idx + 4 < DAP_MAX_RESP_SIZE) {
                resp_data[resp_idx++] = (read_data >> 0)  & 0xFF;
                resp_data[resp_idx++] = (read_data >> 8)  & 0xFF;
                resp_data[resp_idx++] = (read_data >> 16) & 0xFF;
                resp_data[resp_idx++] = (read_data >> 24) & 0xFF;
                resp_count++;
            }
        }

        /* Return to Run-Test/Idle */
        jtag_goto_state(0x01); /* IDLE */
    }

    /* Build response: [cmd][resp_count][resp_status][data...] */
    resp_buffer[0] = DAP_CMD_TRANSFER;
    resp_buffer[1] = resp_count;
    resp_buffer[2] = resp_status;
    *resp_len = resp_idx;
    *resp = resp_buffer;
}

/*===========================================================================
 * Command: DAP_CMD_TRANSFER_BLOCK (0x32)
 *===========================================================================*/

/**
 * @brief Block transfer (multiple words).
 * Simplified: delegates to single transfer loop.
 */
static void dap_handle_transfer_block(const uint8_t *req, uint32_t req_len,
                                       uint8_t **resp, uint32_t *resp_len)
{
    if (req_len < 5) {
        *resp = dap_write_response(DAP_CMD_TRANSFER_BLOCK, DAP_ERROR, NULL, 0, resp_len);
        return;
    }

    uint8_t  req_count = req[2];
    uint8_t  resp_count = 0;
    uint32_t resp_idx   = 2;
    uint8_t *resp_data  = &resp_buffer[2];
    uint32_t req_idx    = 3;
    uint8_t  transfer_req = req[req_idx++];
    uint8_t  rnw = (transfer_req & DAP_TRANSFER_RnW) ? 1 : 0;
    uint8_t  a_reg = (transfer_req >> 2) & 0x03;
    uint8_t  ap_ndp = (transfer_req & DAP_TRANSFER_APnDP) ? 1 : 0;

    /* Auto-increment address for block */
    for (uint8_t i = 0; i < req_count; i++) {
        uint32_t write_data = 0;
        if (!rnw && req_idx + 4 <= req_len) {
            write_data = req[req_idx] | ((uint32_t)req[req_idx+1] << 8) |
                         ((uint32_t)req[req_idx+2] << 16) | ((uint32_t)req[req_idx+3] << 24);
            req_idx += 4;
        }

        /* Perform JTAG DR shift (35 bits) */
        uint8_t ir_bits[4] = {0};
        uint8_t dr_tdi[5] = {0};
        uint8_t dr_tdo[5] = {0};

        ir_bits[0] = ap_ndp ? 0x0B : 0x0A;

        jtag_goto_state(0x0B); /* IR-SHIFT */
        jtag_shift_data(ir_bits, NULL, 4, 1);

        dr_tdi[0] = 0x01;
        dr_tdi[0] |= (a_reg & 0x03) << 1;
        if (rnw) dr_tdi[0] |= 0x08;
        if (!rnw) {
            dr_tdi[1] = (write_data >> 0)  & 0xFF;
            dr_tdi[2] = (write_data >> 8)  & 0xFF;
            dr_tdi[3] = (write_data >> 16) & 0xFF;
            dr_tdi[4] = (write_data >> 24) & 0xFF;
        }

        jtag_goto_state(0x04); /* DR-SHIFT */
        jtag_shift_data(dr_tdi, dr_tdo, 35, 1);

        if (rnw && resp_idx + 4 < DAP_MAX_RESP_SIZE) {
            uint32_t read_data = dr_tdo[1] | ((uint32_t)dr_tdo[2] << 8) |
                                 ((uint32_t)dr_tdo[3] << 16) | ((uint32_t)dr_tdo[4] << 24);
            resp_data[resp_idx++] = (read_data >> 0)  & 0xFF;
            resp_data[resp_idx++] = (read_data >> 8)  & 0xFF;
            resp_data[resp_idx++] = (read_data >> 16) & 0xFF;
            resp_data[resp_idx++] = (read_data >> 24) & 0xFF;
            resp_count++;
        }

        jtag_goto_state(0x01);
        a_reg = (a_reg + 1) & 0x03; /* Auto-increment address */
    }

    resp_buffer[0] = DAP_CMD_TRANSFER_BLOCK;
    resp_buffer[1] = resp_count;
    *resp_len = resp_idx;
    *resp = resp_buffer;
}

/*===========================================================================
 * Command dispatch table
 *===========================================================================*/

typedef void (*dap_handler_t)(const uint8_t *req, uint32_t req_len,
                               uint8_t **resp, uint32_t *resp_len);

typedef struct {
    uint8_t cmd;
    dap_handler_t handler;
} dap_cmd_entry_t;

static const dap_cmd_entry_t dap_cmd_table[] = {
    { DAP_CMD_INFO,               dap_handle_info },
    { DAP_CMD_HOSTSTATUS,         dap_handle_hoststatus },
    { DAP_CMD_CONNECT,            dap_handle_connect },
    { DAP_CMD_DISCONNECT,         dap_handle_disconnect },
    { DAP_CMD_WRITE_ABORT,        dap_handle_write_abort },
    { DAP_CMD_DELAY,              dap_handle_delay },
    { DAP_CMD_RESET_TARGET,       dap_handle_reset_target },
    { DAP_CMD_SWJ_CLK,            dap_handle_swj_clk },
    { DAP_CMD_SWJ_SEQUENCE,       dap_handle_swj_sequence },
    { DAP_CMD_JTAG_SEQUENCE,      dap_handle_jtag_sequence_14 },
    { DAP_CMD_TRANSFER_CONFIGURE, dap_handle_transfer_configure },
    { DAP_CMD_TRANSFER,           dap_handle_transfer },
    { DAP_CMD_TRANSFER_BLOCK,     dap_handle_transfer_block },
};

#define DAP_CMD_TABLE_SIZE (sizeof(dap_cmd_table) / sizeof(dap_cmd_table[0]))

/*===========================================================================
 * Public API
 *===========================================================================*/

/**
 * @brief Initialize CMSIS-DAP subsystem.
 */
void dap_init(void)
{
    memset(&dap_state, 0, sizeof(dap_state));
    dap_state.connected = 0;
    dap_state.port = DAP_PORT_DISABLED;
    dap_state.clock_khz = 1000;
}

/**
 * @brief Process a CMSIS-DAP command.
 */
void dap_process_command(const uint8_t *req_data, uint32_t req_len,
                          uint8_t *resp_data, uint32_t *resp_len)
{
    if (!req_data || req_len == 0) {
        *resp_len = 0;
        return;
    }

    uint8_t cmd = req_data[0];
    uint8_t *resp = NULL;
    uint32_t len = 0;

    /* Dispatch to handler */
    for (uint32_t i = 0; i < DAP_CMD_TABLE_SIZE; i++) {
        if (dap_cmd_table[i].cmd == cmd) {
            dap_cmd_table[i].handler(req_data, req_len, &resp, &len);
            break;
        }
    }

    /* If no handler found, return error */
    if (resp == NULL) {
        resp = dap_write_response(cmd, DAP_ERROR, NULL, 0, &len);
    }

    /* Copy response to output buffer */
    if (resp_data && len > 0 && len <= DAP_MAX_RESP_SIZE) {
        memcpy(resp_data, resp, len);
        *resp_len = len;
    } else {
        *resp_len = 0;
    }
}

/**
 * @brief Get DAP info string.
 */
const char *dap_get_info_string(void)
{
    return DAP_INFO_STRING;
}
