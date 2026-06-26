/*****************************************************************************
 * @file board.c
 * @brief Board initialization for CMSIS-DAP v2
 *****************************************************************************/

#include <rthw.h>
#include <rtthread.h>
#include "n32g43x.h"
#include "board.h"

/**
 * @brief Configures Vector Table base location.
 */
void NVIC_Configuration(void)
{
#ifdef  VECT_TAB_RAM
    NVIC_SetVectorTable(NVIC_VectTab_RAM, 0x0);
#else
    NVIC_SetVectorTable(NVIC_VectTab_FLASH, 0x0);
#endif
}

/**
 * @brief SysTick interrupt handler for RT-Thread tick.
 */
void SysTick_Handler(void)
{
    rt_interrupt_enter();
    rt_tick_increase();
    rt_interrupt_leave();
}

/**
 * @brief Initialize the N32G43x board hardware.
 */
void rt_hw_board_init(void)
{
    /* NVIC Configuration */
    NVIC_Configuration();

    /* Configure SysTick (10ms tick = 100Hz) */
    SysTick_Config(SystemCoreClock / RT_TICK_PER_SECOND);

#ifdef RT_USING_HEAP
    /* Initialize dynamic memory heap */
    rt_system_heap_init((void *)N32G43X_SRAM_START, (void *)N32G43X_SRAM_END);
#endif

#ifdef RT_USING_COMPONENTS_INIT
    rt_components_board_init();
#endif

#ifdef RT_USING_CONSOLE
    rt_console_set_device(RT_CONSOLE_DEVICE_NAME);
#endif
}
