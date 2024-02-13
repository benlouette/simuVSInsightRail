#ifdef __cplusplus
extern "C" {
#endif

/*!
** @file HF.c
** @version 01.00
** @brief
**          Component to simplify hard faults for ARM/Kinetis.
*/         

/*
 * Includes
 */
#include "HF.h"
#include "stdbool.h"
#include "MK24F12.h"
#include "Vbat.h"

extern uint32_t __app_origin[];
extern void ConfigureRTC();
extern void ConfigureRtcWakeupNS(uint32_t);

/*
 * Functions
 */

/*
** ===================================================================
**     Method      :  HF_HandlerC (component HardFault)
**
**     Description :
**         Additional handler which decodes the processor status
**         This method is internal.
**         Used for debugging only.
**
**         NOTE: Make sure the write buffer is disabled in order to
**         pin-point the exact instruction that caused the HF interrupt.
**
** ===================================================================
*/
/**
 * This is called from the HardFaultHandler with a pointer the Fault stack
 * as the parameter. We can then read the values from the stack and place them
 * into local variables for ease of reading.
 * We then read the various Fault Status and Address Registers to help decode
 * cause of the fault.
 * The function ends with a BKPT instruction to force control back into the debugger
 */
#ifdef _MSC_VER // Check if using Microsoft Visual Studio compiler

 // Disable warning about unused-but-set-variable
#pragma warning(disable: 4100)

#endif

void HF_HandlerC(uint32_t* hardfault_args)
{
    // Declare and initialize your variables
    volatile unsigned long stacked_r0 = (unsigned long)hardfault_args[0];
    volatile unsigned long stacked_r1 = (unsigned long)hardfault_args[1];
    volatile unsigned long stacked_r2 = (unsigned long)hardfault_args[2];
    volatile unsigned long stacked_r3 = (unsigned long)hardfault_args[3];
    volatile unsigned long stacked_r12 = (unsigned long)hardfault_args[4];
    volatile unsigned long stacked_lr = (unsigned long)hardfault_args[5];
    volatile unsigned long stacked_pc = (unsigned long)hardfault_args[6];
    volatile unsigned long stacked_psr = (unsigned long)hardfault_args[7];
    volatile unsigned long _CFSR;
    volatile unsigned long _HFSR;
    volatile unsigned long _DFSR;
    volatile unsigned long _AFSR;
    volatile unsigned long _BFAR;
    volatile unsigned long _MMAR;

    // Ignore warning about unused-but-set-variable for GCC-based compilers
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif

    // Configurable Fault Status Register
    _CFSR = (*((volatile unsigned long*)(0xE000ED28)));

    // Hard Fault Status Register
    _HFSR = (*((volatile unsigned long*)(0xE000ED2C)));

    // Debug Fault Status Register
    _DFSR = (*((volatile unsigned long*)(0xE000ED30)));

    // Auxiliary Fault Status Register
    _AFSR = (*((volatile unsigned long*)(0xE000ED3C)));

    // Read the Fault Address Registers
    _MMAR = (*((volatile unsigned long*)(0xE000ED34)));
    _BFAR = (*((volatile unsigned long*)(0xE000ED38)));

#ifdef __GNUC__
#pragma GCC diagnostic pop
    // Check if debug mode is enabled
    if ((*(uint32_t*)CoreDebug_BASE) & CoreDebug_DHCSR_C_DEBUGEN_Msk)
    {
        __asm("BKPT #0\n"); // Cause the debugger to stop
    }
    else
    {
        // Additional handling code for hard fault scenario
    }
#endif


}


/*
** ===================================================================
**     Method      :  HF_HardFaultHandler (component HardFault)
**     Description :
**         Hard Fault Handler
**     Parameters  : None
**     Returns     : Nothing
** ===================================================================
*/
#ifdef _MSC_VER // Check if using Microsoft Visual Studio compiler

// Disable warning about unused-but-set-variable
#pragma warning(disable: 4100)

// Define the function without the naked attribute
void HardFault_Handler(void)
{
	printf("NMI_Handler\n");
}

#else // For other compilers

// Ignore warning about unused-but-set-variable
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"

// Define the function with the naked attribute
__attribute__((naked)) void HardFault_Handler(void)
{
	__asm volatile (
	" movs r0,#4      \n"  /* load bit mask into R0 */
		" mov r1, lr      \n"  /* load link register into R1 */
		" tst r0, r1      \n"  /* compare with bitmask */
		" beq _MSP        \n"  /* if bitmask is set: stack pointer is in PSP. Otherwise in MSP */
		" mrs r0, psp     \n"  /* otherwise: stack pointer is in PSP */
		" b _GetPC        \n"  /* go to part which loads the PC */
		"_MSP:              \n"  /* stack pointer is in MSP register */
		" mrs r0, msp     \n"  /* load stack pointer into R0 */
		"_GetPC:            \n"  /* find out where the hard fault happened */
		" ldr r1,[r0,#20] \n"  /* load program counter into R1. R1 contains address of the next instruction where the hard fault happened */
		" b HF_HandlerC   \n"  /* decode more information. R0 contains pointer to stack frame */
		);
}

#endif


/* END HF. */

/*!
** @}
*/
/*
** ###################################################################
**
**     This file was created by Processor Expert 10.5 [05.21]
**     for the Freescale Kinetis series of microcontrollers.
**
** ###################################################################
*/


#ifdef __cplusplus
}
#endif