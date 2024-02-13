#ifdef __cplusplus
extern "C" {
#endif

/*
 * DrvUart.h
 *
 *  Created on: 1 okt. 2014
 *      Author: g100797
 */

#ifndef DRVUART_H_
#define DRVUART_H_

/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>

#include "fsl_uart_hal.h"
/*
 * Macros
 */

/*
 * Types
 */

/*! @brief tUart driver instance */
typedef struct sDrvUart tUart;

/*
 * Data
 */

/*
 * Functions
 */

tUart *DrvUart_Open( uint32_t uartId );
//bool DrvUart_Disable( tUart *pUart );
//bool DrvUart_Configure( tUart *pUart, tDrvUartConfig *pConfig );
bool DrvUart_SetClockConfiguration( tUart *pUart );

void DrvUart_Init(uint32_t instance, uint32_t baudRate, uart_parity_mode_t parity, bool hwFlowControl );
void DrvUart_ErrorIrqhandling(UART_Type * uartBase );

#endif /* DRVUART_H_ */


#ifdef __cplusplus
}
#endif