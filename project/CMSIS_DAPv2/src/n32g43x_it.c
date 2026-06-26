/*****************************************************************************
 * @file n32g43x_it.c
 * @brief Interrupt handlers for CMSIS-DAP v2
 *****************************************************************************/

#include "n32g43x_it.h"
#include "n32g43x.h"
#include "main.h"

/******************************************************************************/
/*            Cortex-M4 Processor Exceptions Handlers                         */
/******************************************************************************/

void NMI_Handler(void)
{
}

void MemManage_Handler(void)
{
    while (1)
    {
    }
}

void BusFault_Handler(void)
{
    while (1)
    {
    }
}

void UsageFault_Handler(void)
{
    while (1)
    {
    }
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

/* PendSV_Handler is used by RT-Thread for context switching */
/* SysTick_Handler is defined in board.c */

/******************************************************************************/
/*                 N32G43x Peripherals Interrupt Handlers                     */
/******************************************************************************/

/**
 * @brief USB WakeUp interrupt handler.
 */
void USBWakeUp_IRQHandler(void)
{
    EXTI_ClrITPendBit(EXTI_LINE17);
}
