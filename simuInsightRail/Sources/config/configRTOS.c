#ifdef __cplusplus
extern "C" {
#endif

/*
 * configRTOS.c
 *
 *  Created on: 10 dec. 2015
 *      Author: Daniel van der Velde
 *
 * This file contains the FreeRTOS hook functions that need to be forwarded to the
 * device specific application hooks.
 *
 */

/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>

#include <FreeRTOS.h>
#include "Vbat.h"
#include "Resources.h"

/*
 * Functions
 */

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
void RTOS_vApplicationStackOverflowHook(TaskHandle_t pxTask, char *pcTaskName)
{
	/* This will get called if a stack overflow is detected during the context
     switch.  Set configCHECK_FOR_STACK_OVERFLOWS to 2 to also check for stack
     problems within nested interrupts, but only do this for debug purposes as
     it will increase the context switch time. */

	Vbat_FlagStackOverflow(Resources_GetTaskIndex(pxTask));

	(void)pxTask;
	(void)pcTaskName;

	taskDISABLE_INTERRUPTS();
	/* Write your code here ... */
	for(;;) {}
}

/*
** ===================================================================
**     Event       :  RTOS_vApplicationTickHook (module Events)
**
**     Component   :  RTOS [FreeRTOS]
**     Description :
**         If enabled, this hook will be called by the RTOS for every
**         tick increment.
**     Parameters  : None
**     Returns     : Nothing
** ===================================================================
*/
void RTOS_vApplicationTickHook(void)
{
  /* Called for every RTOS tick. */
  /* Write your code here ... */
}

/*
** ===================================================================
**     Event       :  RTOS_vApplicationIdleHook (module Events)
**
**     Component   :  RTOS [FreeRTOS]
**     Description :
**         If enabled, this hook will be called when the RTOS is idle.
**         This might be a good place to go into low power mode.
**     Parameters  : None
**     Returns     : Nothing
** ===================================================================
*/
void RTOS_vApplicationIdleHook(void)
{
  /* Called whenever the RTOS is idle (from the IDLE task).
     Here would be a good place to put the CPU into low power mode. */
  /* Write your code here ... */
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
	Vbat_FlagHeapOverflow();
	taskDISABLE_INTERRUPTS();
	/* Write your code here ... */
	for(;;) {}
}



#ifdef __cplusplus
}
#endif