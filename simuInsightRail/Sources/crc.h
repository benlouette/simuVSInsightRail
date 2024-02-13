#ifdef __cplusplus
extern "C" {
#endif

/*
 * crc.h
 *
 *  Created on: 20 Nov 2018
 *      Author: RZ8556
 */

#include <stdint.h>

#ifndef SOURCES_CRC_H_
#define SOURCES_CRC_H_

#define CRCTARGETVALUE (0x12345678)

void crc32_start(void);
void crc32_calc(void *pBuff, uint32_t length);
uint32_t crc32_finish(void);
uint32_t crc32_hardware(void *pBuff, uint32_t length);


#endif /* SOURCES_CRC_H_ */


#ifdef __cplusplus
}
#endif