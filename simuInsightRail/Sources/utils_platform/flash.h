#ifdef __cplusplus
extern "C" {
#endif

/*
 * flash.h
 *
 *  Created on: Nov 17, 2014
 *      Author: George de Fockert
 */

#ifndef FLASH_H_
#define FLASH_H_

/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>



/*
 * Functions
 */
bool DrvFlashEraseSector(uint32_t *dst, uint32_t len);
bool DrvFlashProgram(uint32_t * dst, uint32_t *src, uint32_t len);
bool DrvFlashEraseProgramSector(uint32_t * dst, uint32_t *src, uint32_t len);
bool DrvFlashVerify(uint32_t * dst, uint32_t *src, uint32_t len, uint32_t margin);
bool DrvFlashCheckErased(int32_t *dst, uint32_t len, uint32_t margin);

#endif /* FLASH_H_ */


#ifdef __cplusplus
}
#endif