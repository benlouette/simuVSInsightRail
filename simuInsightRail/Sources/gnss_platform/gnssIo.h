#ifdef __cplusplus
extern "C" {
#endif

/*
 * gnssIo.h
 *
 *  Created on: Feb 2, 2017
 *      Author: ka3112
 */

#ifndef SOURCES_APP_GNSSIO_H_
#define SOURCES_APP_GNSSIO_H_

/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>

#include "configGnss.h"

// uncomment the next line to get GNSS upgrade firmware code
//#define GNSS_UPGRADE_AVAILABLE

/*
 * Macros
 */
// official maximum length of these commands (including starting '$' and terminating checksum + <cr><lf> is 82 chars.
// then we could do with 83 if we add the terminating \0
#define MAXGNSS_MSGBUFSIZE (82)

struct gnss_buf_str {
    uint16_t idx;
    uint8_t buf[MAXGNSS_MSGBUFSIZE+1];// ping pong buffers with room for the '\0'
};
/*
 * Data
 */
/*
 * Interface functions
 */

uint8_t Gnss_UART_GetCharsInRxBuf();
uint8_t Gnss_UART_get_ch();
uint32_t gnssBaudrate;

void Gnss_init_serial(uint32_t instance, uint32_t baudRate );
//uint32_t Gnss_writeBlock(uint8_t *data, uint32_t len, uint32_t timeout);
//void Gnss_writeBlockAbort();


bool Gnss_put_ch(uint8_t c);
bool Gnss_put_s(char *str) ;
void GnssIo_setBinaryMode(bool mode);
uint32_t Gnss_writeBlock(uint8_t *data, uint32_t len, uint32_t timeout);

void GnssIo_print_info();

#endif /* SOURCES_APP_GNSSIO_H_ */


#ifdef __cplusplus
}
#endif