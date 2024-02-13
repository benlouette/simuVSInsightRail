#ifdef __cplusplus
extern "C" {
#endif

/*
 * configCLI.h
 *
 *  Created on: Oct 29, 2015
 *      Author: D. van der Velde
 */

// define next one to use the GNU newlib-nano stdio routines, otherwise the homebrew printf will be used
// but fails at the moment (hardfault) probably something with malloc, have to review the freeRTOS heap config
//#define NEWLIB_NANO

#ifndef SOURCES_CONFIG_CONFIGCLI_H_
#define SOURCES_CONFIG_CONFIGCLI_H_


// Define here the mapping to the actual used UART port
// must be supplied by the project
#define CLI_UART_IDX (UART3_IDX)

// unfortunately, also the IRQ vectors must be set
#define CLI_UART_RX_TX_IRQhandler UART3_RX_TX_IRQHandler
#define CLI_UART_ERR_IRQhandler UART3_ERR_IRQHandler


// following defines are optional, when not defined some default is chosen

// if following define exists,  it will call the named function,
// feel free to name the function whatever you like for your application
// then the application should have the following function : int CliActivity(char * cmdbuf, uint8_t dbg_put_ch)
//#define CLI_NOTIFY_ACTIVITY_FUNCTION	application_keep_awake
//#define CLI_NOTIFY_ACTIVITY_FUNCTION	CliActivity
#ifdef CLI_NOTIFY_ACTIVITY_FUNCTION
#include <stdint.h>
int CliActivity(char * cmdbuf, uint8_t dbg_put_ch);
#endif

// can we have access over more than one port ?
#define MAX_DEBUG_PUT_CH (1)
#define CLI_DEBUG_PUT_CH (0)
// keep secondary the same as the primary, if we do not have a secundary
#define CLI_SECUNDARY_DEBUG_PUT_CH  (0)

// number of subcomponents which can add commands to the CLI with the cliRegisterCommands() function
// be aware, that the CLI itself also counts as a component
#define CLI_MAXCOMPONENTS (8)

// defines to configure some CLI parameters which can have consequences on ram usage
#define CLI_CMDLEN (80)
#define CLI_HISTORY (4)


// low level software fifo receive and transmit  buffer sizes
#define CLI_MAXRXBUF (32*3)
#define CLI_MAXTXBUF 32

//#define CLI_BAUDRATE (115200*8)

#endif /* SOURCES_CONFIG_CONFIGCLI_H_ */


#ifdef __cplusplus
}
#endif