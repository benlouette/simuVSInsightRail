#ifdef __cplusplus
extern "C" {
#endif

/*
 * xTaskDevice.h
 *
 *  Created on: 30 jun. 2014
 *      Author: g100797
 */

#ifndef XTASKDEVICE_H_
#define XTASKDEVICE_H_

/*
 * Includes
 */
#include <stdint.h>
#include "Time.h"

#include "xTaskDefs.h"

#include "Device.h"

/*
 * Macros
 */

/*
 * Types
 */
#define POFS_MIN	15

/*
 * Data
 */

/*
 * Functions
 */

/*
 * Device task init and task functions
 */
void xTaskDevice_Init( void );
void xTaskDevice( void *pvParameters );

/* Event functions */

/* Synchronous functions */
bool xTaskDeviceShutdown(bool bCommsCycle);
bool xTaskDeviceSetWakeUptime(uint32_t rtcSleepTime, uint32_t ds137nSleepTime);

TickType_t xTaskDevice_TicksRemaining();

#endif /* XTASKDEVICE_H_ */


#ifdef __cplusplus
}
#endif