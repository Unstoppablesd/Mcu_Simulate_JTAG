/*****************************************************************************
 * @file n32g43x_it.h
 * @brief Interrupt handler declarations for CMSIS-DAP v2
 *****************************************************************************/

#ifndef __N32G43X_IT_H__
#define __N32G43X_IT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "n32g43x.h"

void NMI_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void SVC_Handler(void);
void DebugMon_Handler(void);
void PendSV_Handler(void);

#ifdef __cplusplus
}
#endif

#endif /* __N32G43X_IT_H__ */
