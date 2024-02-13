#ifdef __cplusplus
extern "C" {
#endif

/*
 * Timer.c
 *
 *  Created on: 6 July 2016
 *      Author: Bart Willemse
 */

/*
 * Includes
 */
#include "fsl_gpio_driver.h"
#include "Timer.h"
#include "PinDefs.h"

/*
 * Macros
 */

/*
 * Types
 */

/*
 * Data
 */
static volatile uint16_t g_DelayMicrosecsCount = 0;  // volatile so not optimised away

//..............................................................................

/*
 * DelayMicrosecs
 *
 * @desc    Creates approximate microseconds delays (up to 2^16us) using a
 *          simple code loop, FOR 24MHZ PROCESSOR CLOCK SPEED. Only intended
 *          for the following:
 *            - Creating very short local delays (e.g. hardware logic delay
 *              requirements). For longer delays, use the FreeRTOS timer
 *              functionality
 *            - Creating guaranteed minimum delays. Delay might actually be
 *              much longer due to interrupts, FreeRTOS task switching etc
 *
 *          (N.B. The delay periods in this function were implemented by trial
 *          and error using pin toggling)
 *
 * @param
 *
 * @returns -
 *
 * ************************
 * TODO: Generalise this function for different clock speeds etc
 * ************************
 * 
 */

#ifdef _MSC_VER
#pragma optimize("", off)
void DelayMicrosecs(uint16_t Microsecs)
{
    for (volatile int g_DelayMicrosecsCount = 0;
        g_DelayMicrosecsCount < Microsecs;
        g_DelayMicrosecsCount++)
    {

        __nop();
        __nop();
        __nop();
        __nop();
        __nop();
        __nop();
        __nop();
        __nop();
        __nop();
        __nop();
        __nop();
        __nop();
        __nop();
    }
}
#pragma optimize("", on)
#else
void __attribute__((optimize("O0"))) DelayMicrosecs(uint16_t Microsecs)
{
    for (volatile int g_DelayMicrosecsCount = 0;
        g_DelayMicrosecsCount < Microsecs;
        g_DelayMicrosecsCount++)
    {
        // GCC or other compilers
        __asm("nop");
        __asm("nop");
        __asm("nop");
        __asm("nop");
        __asm("nop");
        __asm("nop");
        __asm("nop");
        __asm("nop");
        __asm("nop");
        __asm("nop");
        __asm("nop");
        __asm("nop");
        __asm("nop");
    }
}
#endif


/*
 * DelayMicrosecs_TEST
 *
 * @desc    Tests the DelayMicrosecs() function. Continuously toggles TEST-IO1
 *          with a different delay between each toggle - can then check the
 *          delays on an oscilloscope.
 *          IMPORTANT: Call BEFORE FreeRTOS is started, so that the generated
 *          delays aren't affected by it.
 *
 * @param
 *
 * @returns -
 *
 */
void DelayMicrosecs_TEST(void)
{
    while(1)
    {
        GPIO_DRV_SetPinOutput(TEST_IO2);
        DelayMicrosecs(5);
        GPIO_DRV_ClearPinOutput(TEST_IO2);
        DelayMicrosecs(20);
        GPIO_DRV_SetPinOutput(TEST_IO2);
        DelayMicrosecs(100);
        GPIO_DRV_ClearPinOutput(TEST_IO2);
        DelayMicrosecs(1000);
    }
}




#ifdef __cplusplus
}
#endif