#ifdef __cplusplus
extern "C" {
#endif

/*
 * xTaskWatchdog.h
 *
 *  Created on: 30 nov. 2015
 *      Author: D. van der Velde
 */

#ifndef SOURCES_DEVICE_XTASKWATCHDOG_H_
#define SOURCES_DEVICE_XTASKWATCHDOG_H_

/*
 * Includes
 */

/*
 * Macros
 */

/*
 * Types
 */

/*
 * Data
 */

/*
 * Functions
 */

void xTaskWatchdog_Init( void );
void xTaskWatchdog( void *pvParameters );

#endif /* SOURCES_DEVICE_XTASKWATCHDOG_H_ */


#ifdef __cplusplus
}
#endif