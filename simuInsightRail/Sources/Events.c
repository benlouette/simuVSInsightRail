/* ###################################################################
**     Filename    : Events.c
**     Project     : skf_passenger_rail
**     Processor   : MK24FN1M0VDC12
**     Component   : Events
**     Version     : Driver 01.00
**     Compiler    : GNU C Compiler
**     Date/Time   : 2015-11-23, 10:00, # CodeGen: 0
**     Abstract    :
**         This is user's event module.
**         Put your event handler code here.
**     Settings    :
**     Contents    :
**         No public methods
**
** ###################################################################*/
/*!
** @file Events.c
** @version 01.00
** @brief
**         This is user's event module.
**         Put your event handler code here.
*/         
/*!
**  @addtogroup Events_module Events module documentation
**  @{
*/         
/* MODULE Events */

#include "Events.h"
#include "PinDefs.h"
#include "AD7766_Intrpt.h"

#ifdef __cplusplus
extern "C" {
#endif 
#ifndef _MSC_VER
/* User includes (#include below this line is not maintained by Processor Expert) */

/*
** ===================================================================
**     Event       :  RTOS_vApplicationStackOverflowHook (module Events)
**
**     Component   :  RTOS [FreeRTOS]
**     Description :
**         if enabled, this hook will be called in case of a stack
**         overflow.
**     Parameters  :
**         NAME            - DESCRIPTION
**         pxTask          - Task handle
**       * pcTaskName      - Pointer to task name
**     Returns     : Nothing
** ===================================================================
*/
void RTOS_vApplicationStackOverflowHook(xTaskHandle pxTask, char *pcTaskName)
{
  /* This will get called if a stack overflow is detected during the context
     switch.  Set configCHECK_FOR_STACK_OVERFLOWS to 2 to also check for stack
     problems within nested interrupts, but only do this for debug purposes as
     it will increase the context switch time. */
  (void)pxTask;
  (void)pcTaskName;
  taskDISABLE_INTERRUPTS();
  /* Write your code here ... */
  for(;;) {}
}

/*
** ===================================================================
**     Event       :  RTOS_vApplicationMallocFailedHook (module Events)
**
**     Component   :  RTOS [FreeRTOS]
**     Description :
**         If enabled, the RTOS will call this hook in case memory
**         allocation failed.
**     Parameters  : None
**     Returns     : Nothing
** ===================================================================
*/

void RTOS_vApplicationMallocFailedHook(void)
{
  /* Called if a call to pvPortMalloc() fails because there is insufficient
     free memory available in the FreeRTOS heap.  pvPortMalloc() is called
     internally by FreeRTOS API functions that create tasks, queues, software
     timers, and semaphores.  The size of the FreeRTOS heap is set by the
     configTOTAL_HEAP_SIZE configuration constant in FreeRTOSConfig.h. */
  taskDISABLE_INTERRUPTS();
  /* Write your code here ... */
  for(;;) {}
}





/*
** ===================================================================
**     Interrupt handler : PORTC_IRQHandler
**
**     Description :
**         User interrupt service routine. 
**     Parameters  : None
**     Returns     : Nothing
** ===================================================================
*/
void PORTC_IRQHandler(void)
{
  /* Clear interrupt flag.*/
  // bad idea from processor expert:  PORT_HAL_ClearPortIntFlag(PORTC_BASE_PTR);
  /* Write your code here ... */

  // VERY IMPORTANT: Check that Processor Expert never auto-generates
  // a call to PORT_HAL_ClearPortIntFlag() in this function! 

  if ( GPIO_DRV_IsPinIntPending( EHS5E_DCD ) ) {
 	    void Modem_DCD_ISR(uint32_t modemInterface);// just here, to avoid including too much with the headerfile
 	    GPIO_DRV_ClearPinIntFlag( EHS5E_DCD );
 	    Modem_DCD_ISR(0);// kick of irq handling routine
    }
}
/*
** ===================================================================
**     Interrupt handler : PORTD_IRQHandler
**
**     Description :
**         User interrupt service routine.
**     Parameters  : None
**     Returns     : Nothing
** ===================================================================
*/
void PORTD_IRQHandler(void)
{
  /* Clear interrupt flag.*/
  // bad idea from processor expert:  PORT_HAL_ClearPortIntFlag(PORTC_BASE_PTR);
  /* Write your code here ... */

  // VERY IMPORTANT: Check that Processor Expert never auto-generates
  // a call to PORT_HAL_ClearPortIntFlag() in this function!

  if ( GPIO_DRV_IsPinIntPending( ACCEL_INT1n ) ) {
        void LIS3DH_ISR( void );// just here, to avoid including too much with the headerfile
        GPIO_DRV_ClearPinIntFlag( ACCEL_INT1n );
        LIS3DH_ISR();// kick of irq handling routine
    }
}

/*
** ===================================================================
**     Interrupt handler : PORTE_IRQHandler
**
**     Description :
**         User interrupt service routine.
**     Parameters  : None
**     Returns     : Nothing
** ===================================================================
*/
void PORTE_IRQHandler(void)
{
    // VERY IMPORTANT: Check that Processor Expert never auto-generates
    // a call to PORT_HAL_ClearPortIntFlag() in this function! 

    // ADC_IRQn on PTE5
    if (GPIO_DRV_IsPinIntPending(ADC_IRQn))
    {
        GPIO_DRV_ClearPinIntFlag(ADC_IRQn);
        AD7766Intrpt_ADCIRQn_ISR();
    }
}

/* END Events */
#endif
#ifdef __cplusplus
}  /* extern "C" */
#endif 

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
