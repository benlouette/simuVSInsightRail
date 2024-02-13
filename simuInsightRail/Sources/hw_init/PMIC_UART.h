#ifdef __cplusplus
extern "C" {
#endif

/*
 * PMIC_UART.h
 *
 *  Created on:
 *      Author:
 */

#ifndef PMICUART_H_
#define PMICUART_H_

#include <stdint.h>
#include <stdbool.h>

QueueHandle_t PMICQueue;

#define MESSAGE_OVERHEAD       (8)
#define MAX_SINGLE_MSG_SIZE    (247)
#define MAX_SINGLE_BUFFER_SIZE (MAX_SINGLE_MSG_SIZE + MESSAGE_OVERHEAD )
#define MAX_NBR_BUFFERS        (8)

typedef struct {
	uint32_t	type;
	uint8_t		size;
	uint8_t 	*buf;
} PMICQueue_t;

void PMIC_UART_Init(void);
const uint32_t PMIC_UART_getUartInstance();
void PMIC_UART_SendBytes(uint8_t *pBytes, uint8_t NumBytes);

#endif // PMICUART_H_




#ifdef __cplusplus
}
#endif