/*****************************************************************************
 * @file jtag_driver.h
 * @brief JTAG GPIO bit-banging driver for CMSIS-DAP
 *
 * GPIO pin assignments for JTAG signals:
 *   TCK  - PB3  (Clock, output, 50MHz capable)
 *   TMS  - PB4  (Mode Select, output)
 *   TDI  - PB5  (Data In from debugger to target, output)
 *   TDO  - PB6  (Data Out from target to debugger, input)
 *   SRST - PB7  (System Reset, output, open-drain, optional)
 *
 * All pins are on GPIOB for efficient port-wide operations.
 *****************************************************************************/

#ifndef __JTAG_DRIVER_H__
#define __JTAG_DRIVER_H__

#include <stdint.h>
#include "n32g43x.h"

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * Pin Definitions
 *===========================================================================*/
#define JTAG_PORT               GPIOB

/* JTAG signal pins on GPIOB */
#define JTAG_PIN_TCK            GPIO_PIN_3
#define JTAG_PIN_TMS            GPIO_PIN_4
#define JTAG_PIN_TDI            GPIO_PIN_5
#define JTAG_PIN_TDO            GPIO_PIN_6
#define JTAG_PIN_SRST           GPIO_PIN_7

/*===========================================================================
 * JTAG Bit Macros (for fast bit-banging)
 *===========================================================================*/

/* Output bit masks */
#define JTAG_TCK_Msk            (1U << 3)
#define JTAG_TMS_Msk            (1U << 4)
#define JTAG_TDI_Msk            (1U << 5)
#define JTAG_SRST_Msk           (1U << 7)

/* Input bit mask */
#define JTAG_TDO_Msk            (1U << 6)

/*===========================================================================
 * JTAG Clock frequency presets (at 96MHz SYSCLK)
 *===========================================================================*/
#define JTAG_CLOCK_1MHz         1
#define JTAG_CLOCK_2MHz         2
#define JTAG_CLOCK_5MHz         5
#define JTAG_CLOCK_10MHz        10

/*===========================================================================
 * API Functions
 *===========================================================================*/

/**
 * @brief Initialize JTAG GPIO pins and reset state.
 */
void jtag_gpio_init(void);

/**
 * @brief De-initialize JTAG GPIO (for reconfiguration).
 */
void jtag_gpio_deinit(void);

/**
 * @brief Set JTAG clock divider.
 * @param clk_khz Desired clock in kHz (approximate, software timed)
 */
void jtag_set_clock(uint32_t clk_khz);

/**
 * @brief Reset the JTAG TAP controller.
 * Sends at least 5 TCK cycles with TMS=1, then returns to Run-Test/Idle.
 */
void jtag_tap_reset(void);

/**
 * @brief Shift to a specific TAP state.
 * @param state Target TAP state (encoded as per JTAG standard)
 */
void jtag_goto_state(uint8_t state);

/**
 * @brief Shift data through JTAG (TDI/TDO).
 * @param tdi_data  Pointer to TDI data buffer
 * @param tdo_data  Pointer to TDO data buffer (may be NULL)
 * @param bit_count Number of bits to shift
 * @param last_tms  1 if TMS should be high on last bit (exit shift state)
 */
void jtag_shift_data(const uint8_t *tdi_data, uint8_t *tdo_data,
                     uint32_t bit_count, uint8_t last_tms);

/**
 * @brief Read JTAG TDO pin directly.
 * @return 1 if TDO is high, 0 if low.
 */
static inline uint8_t jtag_read_tdo(void)
{
    return (JTAG_PORT->PID & JTAG_TDO_Msk) ? 1 : 0;
}

/**
 * @brief Toggle TCK for a given number of cycles.
 * @param cycles Number of TCK cycles
 * @param tms    TMS value to hold during these cycles
 */
void jtag_clock_cycles(uint32_t cycles, uint8_t tms);

/**
 * @brief Assert system reset (drive SRST low).
 */
void jtag_srst_assert(void);

/**
 * @brief De-assert system reset (release SRST).
 */
void jtag_srst_deassert(void);

/**
 * @brief Get current JTAG clock divider value (for direct bit-banging loops).
 */
uint32_t jtag_get_clk_divider(void);

#ifdef __cplusplus
}
#endif

#endif /* __JTAG_DRIVER_H__ */
