#ifdef __cplusplus
extern "C" {
#endif

/*
 * printgdf.h
 *
 *  Created on: Jun 25, 2014
 *      Author: George de Fockert
 */

#ifndef PRINTGDF_H_
#define PRINTGDF_H_

/*
 * printf      uses blocking write calls to ensure chars are sent to output
 * printf_nb   uses non-blocking write calls used for debugging logs in performance critical tasks
 */
// for the put_ch and put_ch_nb externals
#include "xTaskCLI.h"

#ifndef NEWLIB_NANO

// use homebrew printf
void printgdf(void (*put_c)(uint8_t c), const char *format, ...);
void suspendCli();

#define printf(...)	     printgdf(put_ch, __VA_ARGS__)
#define printf_nb(...)   printgdf(put_ch_nb, __VA_ARGS__)

#endif

//#define LOG_EVENT(...) do { printf("%s (%d) :",__FILE__,__LINE__); printf(__VA_ARGS__); } while (0);

#endif /* PRINTGDF_H_ */


#ifdef __cplusplus
}
#endif