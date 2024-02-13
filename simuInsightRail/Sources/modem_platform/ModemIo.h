#ifdef __cplusplus
extern "C" {
#endif

/*
 * ModemIo.h
 *
 *  Copied from CLIio.h
 *      Author: George de Fockert
 */

#ifndef SOURCES_MODEM_MODEMIO_H_
#define SOURCES_MODEM_MODEMIO_H_

/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>

#include "configModem.h"

/*
 * Macros
 */
#if 0
// moved to the configModem.h

// Define here the mapping to the actual used UART port
#define MODEM_UART_IDX (UART0_IDX)

// unfortunately, also the IRQ vectors must be set
#define MODEM_UART_RX_TX_IRQhandler UART0_RX_TX_IRQHandler
#define MODEM_UART_ERR_IRQhandler UART0_ERR_IRQHandler
#endif

/*
 * Types
 */


/*
 * Data
 */

/*
 * Interface functions
 */

uint8_t Modem_UART_GetCharsInRxBuf();
uint8_t Modem_UART_get_ch();

void Modem_init_serial(uint32_t instance, uint32_t baudRate );
uint32_t Modem_writeBlock(uint8_t *data, uint32_t len, uint32_t timeout);
void Modem_writeBlockAbort();

// these will become obsolete (use fixed 10ms/char timeout
bool Modem_put_ch(uint8_t c);
// void MODEM_put_ch_nb(uint8_t c);
bool Modem_put_s(char *str) ;
void setIgnoreLF(bool bIgnoreLF);



#endif /* SOURCES_MODEM_MODEMIO_H_ */


#ifdef __cplusplus
}
#endif