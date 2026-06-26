/*****************************************************************************
 * @file main.h
 * @brief CMSIS-DAP v2 main header
 *****************************************************************************/

#ifndef __MAIN_H__
#define __MAIN_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "n32g43x.h"

/*===========================================================================
 * System clock values
 *===========================================================================*/
#define SYSCLK_VALUE_48MHz  ((uint32_t)48000000)
#define SYSCLK_VALUE_72MHz  ((uint32_t)72000000)
#define SYSCLK_VALUE_96MHz  ((uint32_t)96000000)

/*===========================================================================
 * Function declarations
 *===========================================================================*/
void USB_Interrupts_Config(void);
ErrorStatus Set_USBClock(uint32_t sysclk);
ErrorStatus USB_Config(uint32_t sysclk);

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H__ */
