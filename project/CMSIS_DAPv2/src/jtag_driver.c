/*****************************************************************************
 * @file jtag_driver.c
 * @brief JTAG GPIO bit-banging driver implementation
 *
 * Uses direct GPIO register access for maximum speed.
 * This implementation targets JTAG-only (not SWD).
 *****************************************************************************/

#include "jtag_driver.h"

/*===========================================================================
 * Private variables
 *===========================================================================*/
static uint32_t jtag_clk_div = 5; /* Default ~5MHz */

uint32_t jtag_get_clk_divider(void)
{
    return jtag_clk_div;
}

/* JTAG TAP state encoding */
typedef enum {
    TAP_RESET         = 0x00,
    TAP_IDLE          = 0x01,
    TAP_DR_SELECT     = 0x02,
    TAP_DR_CAPTURE    = 0x03,
    TAP_DR_SHIFT      = 0x04,
    TAP_DR_EXIT1      = 0x05,
    TAP_DR_PAUSE      = 0x06,
    TAP_DR_EXIT2      = 0x07,
    TAP_DR_UPDATE     = 0x08,
    TAP_IR_SELECT     = 0x09,
    TAP_IR_CAPTURE    = 0x0A,
    TAP_IR_SHIFT      = 0x0B,
    TAP_IR_EXIT1      = 0x0C,
    TAP_IR_PAUSE      = 0x0D,
    TAP_IR_EXIT2      = 0x0E,
    TAP_IR_UPDATE     = 0x0F,
} jtag_tap_state_t;

/* Current TAP state (reserved for optimized state transitions) */
static jtag_tap_state_t current_tap_state __attribute__((unused)) = TAP_RESET;

/*===========================================================================
 * Low-level GPIO helpers (inline for speed)
 *===========================================================================*/

/**
 * @brief Clock delay (approximately)
 */
static inline void jtag_delay(void)
{
    uint32_t i;
    for (i = 0; i < jtag_clk_div; i++) {
        __NOP();
    }
}

/*===========================================================================
 * Public API
 *===========================================================================*/

/**
 * @brief Initialize JTAG GPIO pins.
 */
void jtag_gpio_init(void)
{
    GPIO_InitType gpio_init;

    /* Enable GPIOB clock */
    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOB, ENABLE);

    /* Configure TCK, TMS, TDI, SRST as push-pull outputs */
    GPIO_InitStruct(&gpio_init);
    gpio_init.Pin            = JTAG_PIN_TCK | JTAG_PIN_TMS | JTAG_PIN_TDI | JTAG_PIN_SRST;
    gpio_init.GPIO_Mode      = GPIO_Mode_Out_PP;
    gpio_init.GPIO_Slew_Rate = GPIO_Slew_Rate_High;
    gpio_init.GPIO_Current   = GPIO_DC_8mA;
    gpio_init.GPIO_Pull      = GPIO_No_Pull;
    GPIO_InitPeripheral(JTAG_PORT, &gpio_init);

    /* Configure TDO as input */
    gpio_init.Pin            = JTAG_PIN_TDO;
    gpio_init.GPIO_Mode      = GPIO_Mode_Input;
    gpio_init.GPIO_Pull      = GPIO_Pull_Up;
    GPIO_InitPeripheral(JTAG_PORT, &gpio_init);

    /* Initial state: TCK=0, TMS=1, TDI=1, SRST=1 (inactive) */
    JTAG_PORT->PBC  = JTAG_TCK_Msk;                          /* TCK low */
    JTAG_PORT->PBSC = JTAG_TMS_Msk | JTAG_TDI_Msk | JTAG_SRST_Msk; /* others high */

    current_tap_state = TAP_RESET;
}

/**
 * @brief De-initialize JTAG GPIO.
 */
void jtag_gpio_deinit(void)
{
    /* Set all JTAG pins to input (safe state) */
    GPIO_InitType gpio_init;
    GPIO_InitStruct(&gpio_init);
    gpio_init.Pin            = JTAG_PIN_TCK | JTAG_PIN_TMS | JTAG_PIN_TDI |
                                JTAG_PIN_TDO | JTAG_PIN_SRST;
    gpio_init.GPIO_Mode      = GPIO_Mode_Input;
    gpio_init.GPIO_Pull      = GPIO_No_Pull;
    GPIO_InitPeripheral(JTAG_PORT, &gpio_init);
}

/**
 * @brief Set JTAG clock divider.
 */
void jtag_set_clock(uint32_t clk_khz)
{
    /* Convert kHz to NOP loop count per half-cycle.
     * Sysclk = 96MHz. Each NOP loop iteration ≈ 5 cycles ≈ 52ns.
     * Target: JTAG half-period = 500000 / clk_khz (in ns).
     * NOPs ≈ (500000 / clk_khz) / 52 ≈ 9600 / clk_khz.
     */
    if (clk_khz == 0) clk_khz = 1;
    if (clk_khz > 10000) clk_khz = 10000;

    jtag_clk_div = 10000 / clk_khz;
    if (jtag_clk_div < 2) jtag_clk_div = 2;
}

/**
 * @brief Toggle TCK for a given number of cycles.
 */
void jtag_clock_cycles(uint32_t cycles, uint8_t tms)
{
    uint32_t tms_val = tms ? JTAG_TMS_Msk : 0;
    uint32_t i;

    for (i = 0; i < cycles; i++) {
        /* Set TMS, keep TCK low */
        if (tms_val) {
            JTAG_PORT->PBSC = tms_val;
        } else {
            JTAG_PORT->PBC = JTAG_TMS_Msk;
        }
        jtag_delay();

        /* TCK high */
        JTAG_PORT->PBSC = JTAG_TCK_Msk;
        jtag_delay();

        /* TCK low */
        JTAG_PORT->PBC = JTAG_TCK_Msk;
        jtag_delay();
    }
}

/**
 * @brief Reset JTAG TAP: at least 5 TCK with TMS=1.
 */
void jtag_tap_reset(void)
{
    jtag_clock_cycles(6, 1);
    /* Now in Test-Logic-Reset, move to Run-Test/Idle */
    jtag_clock_cycles(1, 0);
    current_tap_state = TAP_IDLE;
}

/**
 * @brief Shift to specific TAP state (simplified: uses full reset + path).
 */
void jtag_goto_state(uint8_t state)
{
    /* Simplified: always reset and walk to target state.
     * A production implementation would use optimal paths. */
    jtag_tap_reset();

    /* Walk through TAP state machine to target.
     * For simplicity, we handle common cases here. */
    if (state == TAP_IDLE) {
        /* Already at IDLE after reset */
        return;
    }

    /* Go to DR-SELECT (TMS=1) */
    jtag_clock_cycles(1, 1);
    current_tap_state = TAP_DR_SELECT;

    if (state == TAP_DR_SELECT) return;

    /* Go to IR-SELECT (TMS=1) */
    jtag_clock_cycles(1, 1);
    current_tap_state = TAP_IR_SELECT;

    if (state == TAP_IR_SELECT) return;

    /* Go to IR-CAPTURE (TMS=0) */
    jtag_clock_cycles(1, 0);
    current_tap_state = TAP_IR_CAPTURE;

    if (state == TAP_IR_CAPTURE) return;

    /* Go to IR-SHIFT (TMS=0) */
    jtag_clock_cycles(1, 0);
    current_tap_state = TAP_IR_SHIFT;

    /* From SHIFT we can go to EXIT1 (TMS=1) or stay */
    if (state == TAP_IR_SHIFT) {
        current_tap_state = TAP_IR_SHIFT;
        return;
    }

    /* Default: return to Run-Test/Idle via EXIT1→UPDATE→IDLE */
    jtag_clock_cycles(1, 1); /* EXIT1 */
    jtag_clock_cycles(1, 1); /* UPDATE */
    jtag_clock_cycles(1, 0); /* IDLE */
    current_tap_state = TAP_IDLE;
}

/**
 * @brief Shift data through JTAG.
 *
 * @param tdi_data  TDI input data (LSB first per JTAG standard)
 * @param tdo_data  TDO output buffer (may be NULL)
 * @param bit_count Number of bits
 * @param last_tms  1 to exit shift with TMS high on last bit
 */
void jtag_shift_data(const uint8_t *tdi_data, uint8_t *tdo_data,
                     uint32_t bit_count, uint8_t last_tms)
{
    uint32_t i;
    uint8_t tdo_byte = 0;
    uint32_t bit_index = 0;

    for (i = 0; i < bit_count; i++) {
        /* Set TDI (and TMS if it's the last bit) */
        uint32_t tms_flag = (last_tms && (i == bit_count - 1)) ? JTAG_TMS_Msk : 0;

        /* Set TDI bit */
        if (tdi_data && (tdi_data[i / 8] & (1U << (i % 8)))) {
            JTAG_PORT->PBSC = JTAG_TDI_Msk | tms_flag;
        } else {
            if (tms_flag) {
                JTAG_PORT->PBSC = tms_flag;
            }
            JTAG_PORT->PBC = JTAG_TDI_Msk;
        }

        /* Ensure TMS is set correctly for non-last bits */
        if (!last_tms || i != bit_count - 1) {
            JTAG_PORT->PBC = JTAG_TMS_Msk;
        }

        jtag_delay();

        /* TCK high - sample TDO */
        JTAG_PORT->PBSC = JTAG_TCK_Msk;
        jtag_delay();

        /* Read TDO */
        if (tdo_data) {
            if (JTAG_PORT->PID & JTAG_TDO_Msk) {
                tdo_byte |= (1U << bit_index);
            }
            bit_index++;
            if (bit_index == 8) {
                *tdo_data++ = tdo_byte;
                tdo_byte = 0;
                bit_index = 0;
            }
        }

        /* TCK low */
        JTAG_PORT->PBC = JTAG_TCK_Msk;
        jtag_delay();
    }

    /* Flush remaining TDO bits */
    if (tdo_data && bit_index > 0) {
        *tdo_data = tdo_byte;
    }
}

/**
 * @brief Assert SRST (drive low).
 */
void jtag_srst_assert(void)
{
    JTAG_PORT->PBC = JTAG_SRST_Msk;
}

/**
 * @brief De-assert SRST (release high).
 */
void jtag_srst_deassert(void)
{
    JTAG_PORT->PBSC = JTAG_SRST_Msk;
}
