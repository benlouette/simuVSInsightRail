#ifdef __cplusplus
extern "C" {
#endif

/*
 * configGnss.h
 *
 *  Created on: Feb 2, 2017
 *      Author: ka3112
 */

#ifndef SOURCES_APP_CONFIGGNSS_H_
#define SOURCES_APP_CONFIGGNSS_H_

// Define here the mapping to the actual used UART port for the gnss
#define GNSS_UART_IDX (UART2_IDX)

// unfortunately, also the IRQ vectors must be set
#define GNSS_UART_RX_TX_IRQhandler UART2_RX_TX_IRQHandler
#define GNSS_UART_ERR_IRQhandler UART2_ERR_IRQHandler

// Cli functions
bool cliGnssLongHelp( uint32_t args, uint8_t * argv[], uint32_t * argi);
bool cliGnss( uint32_t args, uint8_t * argv[], uint32_t * argi);
#define GNSS_CLI {"gnss", " [action] [param(s)]\tGnss actions", cliGnss, cliGnssLongHelp }

bool cliMT3333LongHelp( uint32_t args, uint8_t * argv[], uint32_t * argi);
bool cliMT3333( uint32_t args, uint8_t * argv[], uint32_t * argi);
#define MT3333_CLI {"mt3333", " [action] [param(s)]\tMT3333 Gnss actions", cliMT3333, cliMT3333LongHelp }

#endif /* SOURCES_APP_CONFIGGNSS_H_ */


#ifdef __cplusplus
}
#endif