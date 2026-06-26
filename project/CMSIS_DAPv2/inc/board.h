/*****************************************************************************
 * @file board.h
 * @brief Board definitions for CMSIS-DAP v2
 *****************************************************************************/

#ifndef __BOARD_H__
#define __BOARD_H__

/*===========================================================================
 * Memory configuration for N32G43x (32KB SRAM)
 *===========================================================================*/
#define N32G43X_SRAM_SIZE       32
#define N32G43X_SRAM_START      (0x20000000 + N32G43X_SRAM_SIZE / 2 * 1024)
#define N32G43X_SRAM_END        (0x20000000 + N32G43X_SRAM_SIZE * 1024)

void rt_hw_board_init(void);

#endif /* __BOARD_H__ */
