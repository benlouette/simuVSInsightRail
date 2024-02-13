#ifdef __cplusplus
extern "C" {
#endif

/*
 * configModem.h
 *
 *  Created on: Nov 24, 2015
 *      Author: D. van der Velde
 */

#ifndef SOURCES_MODEM_CONFIGMODEM_H_
#define SOURCES_MODEM_CONFIGMODEM_H_

#include <stdbool.h>
#include <stdint.h>

// Define here the mapping to the actual used UART port for the modem
#define MODEM_UART_IDX (UART1_IDX)

// unfortunately, also the IRQ vectors must be set
#define MODEM_UART_RX_TX_IRQhandler UART1_RX_TX_IRQHandler
#define MODEM_UART_ERR_IRQhandler UART1_ERR_IRQHandler

// functions to power the modem on and off
bool configModem_PowerOn(void);
bool configModem_PowerOff(void);

bool configModem_PowerCheck(void);

bool configModem_connect(char * addr, uint16_t port);
void configModem_disconnect();
/*
 * NOTE: Interrupt priorities must be set
 */

#endif /* SOURCES_MODEM_CONFIGMODEM_H_ */


#ifdef __cplusplus
}
#endif