#ifdef __cplusplus
extern "C" {
#endif

/*
 * CLIio.h
 *
 *  Created on: 14 oct. 2015
 *      Author: D. van der Velde
 */

#ifndef SOURCES_CLI_CLIIO_H_
#define SOURCES_CLI_CLIIO_H_

/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>

// include file which contains project configurable defines
#include "configCLI.h"

/*
 * Macros
 */
#if 0
// these really should be in the project file 'configCLI.h', highly dependant on the hardware setup, and no fallback to defaults advisable !

// so, if we define on a high level :
#define CLI_UART_IDX (UART4_IDX)
// unfortunately, also the IRQ vectors must be set
#define CLI_UART_RX_TX_IRQhandler UART4_RX_TX_IRQHandler
#define CLI_UART_ERR_IRQhandler UART4_ERR_IRQHandler
#endif


/*
 * Types
 */
#ifndef CLI_CMDLEN
// can be overruled in the project delivered config file 'configCLI.h
#define CLI_CMDLEN (40)
#endif

struct cmdLine_str {
	uint8_t pos;			// cursor position
	uint8_t len;			// len of buffer
	char buf[CLI_CMDLEN];	// command line buffer
};

// structure used for the CLI I/O, routines to send characters over the uart
// so for the put_ch, get_ch, put_s stuff (printf uses these functions)
struct cliIo_str {
	uint8_t (* getCharsInRxBuf)();
	uint8_t  (* getChar)();
	void (*putChar)(uint8_t c);
	void (*putCharNB)(uint8_t c);// non blocking putchar
	char *const notifyText;
	struct cmdLine_str * cmdline;
};


#ifndef MAX_DEBUG_PUT_CH
// if the project did not change defined these, we will do it here for one port
// can we have access over more than one port ?
#define MAX_DEBUG_PUT_CH (1)
#define CLI_DEBUG_PUT_CH (0)
// keep secondary the same as the primary, if we do not have a secundary
#define CLI_SECUNDARY_DEBUG_PUT_CH  (0)

#endif

extern  const struct cliIo_str *const cliIo[MAX_DEBUG_PUT_CH]; // pointer of the current used input and output routines for the commandline (CLI)

/*
 * Data
 */

/*
 * Interface functions
 */

// these go directly to the main cli uart
uint8_t CLI_UART_GetCharsInRxBuf();
uint8_t CLI_UART_get_ch();
void CLI_put_ch(uint8_t c);
void CLI_flushTx();
void CLI_put_ch_nb(uint8_t c);
void CLI_init_serial(uint32_t instance, uint32_t baudRate );


// these can be redirected to an alternate port, if configured correctly
uint8_t get_ch();
void put_ch(uint8_t c);
void put_ch_nb(uint8_t c);
int put_s(char *str);

//
void CLIio_setOutputUart(uint8_t uartId);
uint8_t CLIio_getOutputUart();

#endif /* SOURCES_CLI_CLIIO_H_ */


#ifdef __cplusplus
}
#endif