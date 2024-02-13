#ifdef __cplusplus
extern "C" {
#endif

/*
 * xTaskWatchdog.c
 *
 *  Created on: 30 nov. 2015
 *      Author: D. van der Velde
 *
 *  Watchdog task
 *  Monitors relevant functional tasks for proper operation by evaluating the action
 *  counters associated with the tasks.
 */

#ifndef SOURCES_DEVICE_XTASKWATCHDOG_C_
#define SOURCES_DEVICE_XTASKWATCHDOG_C_

/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>

#include "xTaskDefs.h"

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

/*
 * xTaskWatchdog_Init
 *
 * @brief Initialise the watchdog task
 */
void xTaskWatchdog_Init( void )
{
    //
}

void xTaskWatchdog( void )
{
    //
}




#endif /* SOURCES_DEVICE_XTASKWATCHDOG_C_ */


#ifdef __cplusplus
}
#endif