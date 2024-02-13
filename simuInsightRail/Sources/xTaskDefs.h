#ifdef __cplusplus
extern "C" {
#endif

/*
 * xTaskDefs.h
 *
 *  Created on: 23-Nov-2015
 *      Author: D. van der Velde
 *
 * xTaskDefs.h for Passenger Rail Cellular Node
 *
 *
 *
 */

#ifndef XTASKDEFS_H_
#define XTASKDEFS_H_

/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"


/*----------------------------------------------------------------------------*
 |                                    THREAD INIT PARAMETERS                  |
 *----------------------------------------------------------------------------*/

typedef struct {
    uint32_t    startCondition;
    // Ref to data store
    // Ref to power module
} tPvParams;


#define FREERTOS_THREAD_TASK_OVERHEAD  0 // RAM space that FreeRTOS uses per thread (overhead). Should be added to allocated stack per thread

/*----------------------------------------------------------------------------*
 |                                    THREAD PRIORITIES                       |
 |                      (higher value is higher prio with freeRTOS)           |
 *----------------------------------------------------------------------------*/
// TODO Finalize task design

// in the freertos config file (FreeRTOConfig.h) the freertos timertask is now set at tskIDLE_PRIORITY + 7

#define PRIORITY_XTASK_DEVICE           ( tskIDLE_PRIORITY + 6 )
#define PRIORITY_XTASK_MEASURE          ( tskIDLE_PRIORITY + 5 )

#define PRIORITY_XTASK_MODEM            ( tskIDLE_PRIORITY + 4 )
#define PRIORITY_XTASK_GNSS             ( tskIDLE_PRIORITY + 3 )

#define PRIORITY_XTASK_PMIC             ( tskIDLE_PRIORITY + 3 )
#define PRIORITY_XTASK_APP              ( tskIDLE_PRIORITY + 3 )
#define PRIORITY_XTASK_OTA              ( tskIDLE_PRIORITY + 3 )
#define PRIORITY_TASK_COMM              ( tskIDLE_PRIORITY + 3 )
#define PRIORITY_XTASK_CLI              ( tskIDLE_PRIORITY + 3 )
#define PRIORITY_XTASK_BINCLI           ( tskIDLE_PRIORITY + 3 )

#define PRIORITY_TASK_MQTT              ( tskIDLE_PRIORITY + 2 )

//#define PRIORITY_XTASK_MEASURE_POSTPR   ( tskIDLE_PRIORITY + 1 )

#define PRIORITY_XTASK_POWER            ( tskIDLE_PRIORITY + 1 )
#define PRIORITY_XTASK_EXT_FLASH        ( tskIDLE_PRIORITY + 1 )






/*----------------------------------------------------------------------------*
 |                                    THREAD STACK SIZES                      |
 *----------------------------------------------------------------------------*/
// note, the allocation is in words, not bytes, a word on ARM is 4 bytes
#define STACKSIZE_XTASK_MEASURE           ( ( unsigned portSHORT)(   256 + FREERTOS_THREAD_TASK_OVERHEAD))
#define STACKSIZE_XTASK_DEVICE            ( ( unsigned portSHORT)(   256 + FREERTOS_THREAD_TASK_OVERHEAD))
#define STACKSIZE_XTASK_APP               ( ( unsigned portSHORT)(   4*256 + FREERTOS_THREAD_TASK_OVERHEAD))
#define STACKSIZE_XTASK_OTA				  ( ( unsigned portSHORT)(   4*256 + FREERTOS_THREAD_TASK_OVERHEAD))
#define STACKSIZE_TASK_COMM               ( ( unsigned portSHORT)(   512 + FREERTOS_THREAD_TASK_OVERHEAD))
#define STACKSIZE_XTASK_MQTT              ( ( unsigned portSHORT)(   256 + FREERTOS_THREAD_TASK_OVERHEAD))
#define STACKSIZE_XTASK_MODEM             ( ( unsigned portSHORT)(   256 + FREERTOS_THREAD_TASK_OVERHEAD))
#define STACKSIZE_XTASK_CLI               ( ( unsigned portSHORT)(   4*256 + FREERTOS_THREAD_TASK_OVERHEAD))
#define STACKSIZE_XTASK_POWER             ( ( unsigned portSHORT)(   256 + FREERTOS_THREAD_TASK_OVERHEAD))
#define STACKSIZE_XTASK_EXT_FLASH         ( ( unsigned portSHORT)(   256 + FREERTOS_THREAD_TASK_OVERHEAD))
#define STACKSIZE_XTASK_PMIC              ( ( unsigned portSHORT)(   2*256 + FREERTOS_THREAD_TASK_OVERHEAD))
#define STACKSIZE_XTASK_GNSS              ( ( unsigned portSHORT)(   2*256 + FREERTOS_THREAD_TASK_OVERHEAD))
#define STACKSIZE_XTASK_BINCLI            ( ( unsigned portSHORT)(   2*256 + FREERTOS_THREAD_TASK_OVERHEAD))




/*----------------------------------------------------------------------------*
 |                                    THREAD PROTOTYPES                       |
 *----------------------------------------------------------------------------*/
portTASK_FUNCTION_PROTO( xTaskMeasure,         pvParameters);
portTASK_FUNCTION_PROTO( xTaskDevice,          pvParameters);
#if 0
portTASK_FUNCTION_PROTO( xTaskDevice_Timer,    pvParameters);
#endif
portTASK_FUNCTION_PROTO( xTaskApp_xTaskApp,    pvParameters);
portTASK_FUNCTION_PROTO( xTaskMQTT,            pvParameters);
portTASK_FUNCTION_PROTO( xTaskComm,            pvParameters);
portTASK_FUNCTION_PROTO( xTaskModem,           pvParameters);
portTASK_FUNCTION_PROTO( xTaskCLI,             pvParameters);
portTASK_FUNCTION_PROTO( xTaskPOWER,           pvParameters);
portTASK_FUNCTION_PROTO( xTaskExtFlash,        pvParameters);

//portTASK_FUNCTION_PROTO( xTaskGnss,            pvParameters);
portTASK_FUNCTION_PROTO( taskGnss,            pvParameters);


#endif /* XTASKDEFS_H_ */


#ifdef __cplusplus
}
#endif