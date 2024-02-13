#ifdef __cplusplus
extern "C" {
#endif

/*
 * PMIC_UART.c
 *
 *  Created on:
 *      Author:
 * Description:
 *
 */

#include <stdbool.h>
#include <string.h>

#include "fsl_uart_driver.h"

#include "printgdf.h"
#include "DrvUart.h"
#include "PMIC_UART.h"
#include "queue.h"

#define SOH	1
#define STX	2
#define ETX	3
#define EOT	4

typedef enum {
	SEARCH_SOH,
	SEARCH_SIZE,
	SEARCH_STX,
	SEARCH_MSG,
	SEARCH_EOT
} eProtocolStates;

// TODO: Initial quick implementation only for ADC output streaming use - need
// to make more efficient eventually

//..............................................................................

uart_state_t PMIC_UARTState;     // Used by SDK UART driver internals
const uint32_t PMIC_UARTInstance = UART5_IDX;

//..............................................................................

// Prototype declared here because SDK doesn't seem to declare it anywhere(?)
void UART_DRV_IRQHandler(uint32_t instance);
void PMIC_UART_Rx(uint32_t instance, void *p);
int rxbufState = 0;
int rxbufInUse = 0;
uint8_t rxbuf[MAX_NBR_BUFFERS][MAX_SINGLE_BUFFER_SIZE];

/*
 * PMIC_UART_Init
 *
 * @desc	initialise the UART used for PMIC COMMS
 *
 * @param
 *
 * @returns -
 */
void PMIC_UART_Init(void)
{
    uart_user_config_t UARTUserConfig =
    {
        .baudRate = 115200,
        .bitCountPerChar = kUart8BitsPerChar,
        .parityMode = kUartParityDisabled,
        .stopBitCount = kUartOneStopBit
    };
    uart_status_t UARTStatus;

    UARTStatus = UART_DRV_Init(PMIC_UARTInstance, &PMIC_UARTState, &UARTUserConfig);
    (void)UARTStatus;
    UART_DRV_InstallRxCallback(PMIC_UARTInstance, PMIC_UART_Rx, rxbuf[0], NULL, true);
    rxbufState = SEARCH_SOH;
    rxbufInUse = 0;
}

/*
 * PMIC_UART_Rx
 *
 * @desc	Calback function to process the received characters
 *
 * @param	instance the UART to process
 *
 * @param	ps pointer to uart_state_t for the UART
 *
 * @returns - nowt
 */
void PMIC_UART_Rx(uint32_t instance, void *ps)
{
	uint8_t crc, size;
	uint8_t *p;
	uart_state_t *state = (uart_state_t *)ps;
	switch(rxbufState)
	{
	case SEARCH_SOH:
		// search for SOH
		if(SOH != *state->rxBuff)
		{
			return;
		}
		state->rxCallbackParam = state->rxBuff;
		state->rxBuff++;
		rxbufState = SEARCH_SIZE;
		break;

	case SEARCH_SIZE:
		// get the msg size
		if((*state->rxBuff < MESSAGE_OVERHEAD) || (*state->rxBuff > MAX_SINGLE_BUFFER_SIZE))
		{
			rxbufState = SEARCH_SOH;
			return;
		}
		state->rxSize = *state->rxBuff - 4;	// all but SOH, size, STX & EOT
		state->rxBuff++;
		rxbufState = SEARCH_STX;
		break;

	case SEARCH_STX:
		// search for SOH
		if(STX != *state->rxBuff)
		{
			rxbufState = SEARCH_SOH;
			return;
		}
		state->rxBuff++;
		rxbufState = SEARCH_MSG;
		break;

	case SEARCH_MSG:
		// fetch msg, ETX & CRC
		state->rxBuff++;
		state->rxSize--;
		if(0 == state->rxSize)
		{
			rxbufState = SEARCH_EOT;
		}
		break;

	case SEARCH_EOT:
		rxbufState = SEARCH_SOH;	// no matter what !
		if(EOT != *state->rxBuff)
		{
			return;					// EOT not found so exit
		}
		// now check the CRC
		p = state->rxCallbackParam;
		crc = 0;
		size = p[1] - 2;
		for(int i = 0; i < size; i++)
		{
			  crc ^= p[i];
		}
		if(crc != p[size])
		{
			return;
		}
		rxbufInUse = (rxbufInUse + 1) % MAX_NBR_BUFFERS;
		state->rxBuff = rxbuf[rxbufInUse];

		// send a message to the PMIC queue
		PMICQueue_t queue;
		BaseType_t xHigherPriorityTaskWoken;
		queue.type = p[4];
		queue.size = p[1] - MESSAGE_OVERHEAD;
		queue.buf = &p[5];
		xQueueSendFromISR(PMICQueue, &queue, &xHigherPriorityTaskWoken);
		break;

	default:
		state->rxBuff = rxbuf[rxbufInUse];
		rxbufState = SEARCH_SOH;
		break;
	}
}

/*
 * Kept for link compatibility, but we don't want anything
 * sent to PMIC
 */
void PMIC_UART_SendBytes(uint8_t *pBytes, uint8_t NumBytes)
{

}

const uint32_t PMIC_UART_getUartInstance()
{
	return PMIC_UARTInstance;
}


/*
 * UART5_RX_TX_IRQHandler
 *
 * @desc
 *
 * @param
 *
 * @returns -
 */
void /*__attribute__((interrupt))*/UART5_RX_TX_IRQHandler(void)
{
    UART_DRV_IRQHandler(PMIC_UARTInstance);
}

void /*__attribute__((interrupt))*/ UART5_ERR_IRQHandler(void)
{
	DrvUart_ErrorIrqhandling(UART5);
}






#ifdef __cplusplus
}
#endif