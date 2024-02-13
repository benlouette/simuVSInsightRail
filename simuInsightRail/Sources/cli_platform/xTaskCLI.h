#ifdef __cplusplus
extern "C" {
#endif

/*
 * xTaskComm.h
 *
 *  Created on: 8 jul. 2014
 *      Author: g100797
 */

#ifndef XTASKCLIHANDLER_H_
#define XTASKCLIHANDLER_H_

/*
 * Includes
 */
#include "configCLI.h"
#include "CLIio.h"
#include <portmacro.h>

/*
 * Macros
 */

// Test framework relies on this exact message
#define SYSTEM_BOOT_MSG		"SYSTEM STARTED\n"


/*
 * Types
 */


/*
 * Data
 */



/*
 * Functions
 */

void xTaskCLI_Init(void);
void xTaskCLI(void *pvParameters);

BaseType_t xTaskCLI_NotifyCliRxData_ISR( uint32_t cliInterface );



extern uint32_t CLI_uart_err_count;
extern uint32_t CLI_rx_fifo_overrun;
#endif /* XTASKCLIHANDLER_H_ */



#ifdef __cplusplus
}
#endif