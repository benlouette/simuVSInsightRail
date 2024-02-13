#ifdef __cplusplus
extern "C" {
#endif

/*
 * serialize.h
 *
 * functions to serialize/unserialize possibly bad aligned for the
 * processor architecture data for types larger than one byte
 * stolen from the livingston 'main.h'
 *
 *  Created on: Jun 27, 2014
 *      Author: George de Fockert
 *
 */

#ifndef SERIALIZE_H_
#define SERIALIZE_H_

/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>

/*
 * Functions
 */
uint32_t GetUInt32(const uint8_t* abyBuff);
void SetUInt32(uint8_t* abyBuff, uint32_t nValue);
uint16_t GetUInt16(const uint8_t* abyBuff);
void SetUInt16(uint8_t* abyBuff, uint16_t nValue);
void SetFloat(uint8_t* abyBuff, float nValue);
float GetFloat(uint8_t* abyBuff);
bool GetBool(uint8_t* abyBuff);
void SetBool(uint8_t* abyBuff, bool bValue);


#endif /* SERIALIZE_H_ */


#ifdef __cplusplus
}
#endif