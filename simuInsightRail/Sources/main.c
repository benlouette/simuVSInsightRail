#ifdef __cplusplus
extern "C" {
#endif

/* ####################################################################
**     Filename    : main.c
**     Project     : skf_passenger_rail
**     Processor   : MK24FN1M0VDC12
**     Version     : Driver 01.01
**     Compiler    : GNU C Compiler
**     Date/Time   : 2015-11-23, 10:00, # CodeGen: 0
**     Abstract    :
**         Main module.
**         This module contains user's application code.
**     Settings    :
**     Contents    :
**         No public methods
**
** ###################################################################*/
/*!
** @file main.c
** @version 01.01
** @brief
**         Main module.
**         This module contains user's application code.
*/         
/*!
**  @addtogroup main_module main module documentation
**  @{
*/         
/* MODULE main */


/* Including needed modules to compile this module/procedure */
#include "Events.h"
#include "pin_mux.h"
#if CPU_INIT_CONFIG
  #include "Init_Config.h"
#endif
/* User includes (#include below this line is not maintained by Processor Expert) */
#include "fsl_mcg_hal.h"
#include "fsl_os_abstraction.h"
#include "Resources.h"
#include "Device.h"

#define USE_CUNIT
#if defined(USE_CUNIT)
#include <stdio.h>
#include <stdlib.h>

//#define PRINTF_BASED
#if defined(PRINTF_BASED)

#include <stdarg.h>
#include "printgdf.h"
extern void set_print_function(void(*p)(const char*, va_list));

bool has_parameters(const char* str)
{
	int i;
	for (i=0; str[i]; (str[i]=='%' && str[i+1] != '%') ? i++ : *str++);
	return i > 0;
}

#define MAX_UT_CHARS 500
    static char str[MAX_UT_CHARS];

void redirectprintf(const char* fmt, va_list v)
{
	char* pc;

//	if(has_parameters(fmt))
	{
		vsprintf(str, fmt, v);
		pc = str;
	}
#if 0
	else
	{
		pc = (char*)fmt;
	}
#endif

	str[MAX_UT_CHARS-1] = 0;

	int i=0;
	while(*pc++)
	{
		if(++i > MAX_UT_CHARS-1)
			break;

		if (*pc=='\n')
			put_ch('\r'); // convert newlines to carriage return + newline
		put_ch(*pc);
	}
}
#else

#include "CLIio.h"
extern void set_print_character(void(*p)(int));
extern void set_read_character(int(*p)(void));

int getc_replacement(void)
{
	if(CLI_UART_GetCharsInRxBuf() == 0)
		return EOF;

	return get_ch();
}

void putc_replacement(int c)
{
	if (c=='\n')
		put_ch('\r'); // convert newlines to carriage return + newline
	put_ch(c);
}
#endif
#endif

/*lint -save  -e970 Disable MISRA rule (6.3) checking. */
int main(void)
/*lint -restore Enable MISRA rule (6.3) checking. */
{
  /* Write your local variable definition here */

    // the SDK relies on the following globals for various clock calculations (baud rate dividers etc.)
    /* Setup board clock source. */
    g_xtal0ClkFreq = 8192000U;            /* Value of the external crystal or oscillator clock frequency of the system oscillator (OSC) in Hz */
    g_xtalRtcClkFreq = 32768U;            /* Value of the external 32k crystal or oscillator clock frequency of the RTC in Hz */

    /*
     * Need lines below if we are forced to use semihosting..
     */
#if defined(USE_SEMIHOSTING)
    extern void initialise_monitor_handles(void);
    initialise_monitor_handles();
#endif

#if defined(USE_CUNIT)
#if defined(PRINTF_BASED)
    set_print_function(redirectprintf);
#else
    set_print_character(putc_replacement);
    set_read_character(getc_replacement);
#endif
#endif

#if 0
  /*** Processor Expert internal initialization. DON'T REMOVE THIS CODE!!! ***/


	PE_low_level_init();
  /*** End of Processor Expert internal initialization.                    ***/
#else

#endif

#if configUSE_SEGGER_SYSTEM_VIEWER_HOOKS
  SEGGER_SYSVIEW_Conf();
#endif


  /* Platform low-level initialization */
  Resources_InitLowLevel();

  Resources_InitTasks();
  OSA_Start();

  /*** Don't write any code pass this line, or it will be deleted during code generation. ***/
  /*** RTOS startup code. Macro PEX_RTOS_START is defined by the RTOS component. DON'T MODIFY THIS CODE!!! ***/
  #ifdef PEX_RTOS_START
    PEX_RTOS_START();                  /* Startup of the selected RTOS. Macro is defined by the RTOS component. */
  #endif
  /*** End of RTOS startup code.  ***/
  /*** Processor Expert end of main routine. DON'T MODIFY THIS CODE!!! ***/
  for(;;){}
  /*** Processor Expert end of main routine. DON'T WRITE CODE BELOW!!! ***/
} /*** End of main routine. DO NOT MODIFY THIS TEXT!!! ***/

/* END main */
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