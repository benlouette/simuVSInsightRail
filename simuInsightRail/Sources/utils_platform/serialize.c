#ifdef __cplusplus
extern "C" {
#endif

/*
 * serialize.c
 *
 * * functions to serialize/unserialize possibly bad aligned for the
 * processor architecture data for types larger than one byte
 * stolen from the livingston 'main.c'
 *
 *  Created on: Jun 27, 2014
 *      Author: George de Fockert
 *
 */

/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>

#include "serialize.h"

/*
 * Functions
 */

//==============================================================================
/// GetBool
///
/// @desc   Return a bool from array buffer.
///
/// @param  [in] abyBuff - Buffer array.
///
/// @return bool - Converted value
///
bool GetBool(uint8_t* abyBuff)
{
    return !(abyBuff[0] == 0);
}

//==============================================================================
/// SetBool
///
/// @desc   Set content of an array address to a value.
///
/// @param  [in] abyBuff - Buffer array.
/// @param  [in] bValue - Value to set.
///
/// @return void
///
void SetBool(uint8_t* abyBuff, bool bValue)
{
    abyBuff[0] = (bValue == false ? 0 : 1);
}

//==============================================================================
/// GetUInt16
///
/// @desc   Return a Word from array buffer.
///
/// @param  [in] abyBuff - Buffer array.
///
/// @return uint16_t - Converted value
///
uint16_t GetUInt16(const uint8_t* abyBuff)
{
    return ((abyBuff[0] << 8) | abyBuff[1]);
}


//==============================================================================
/// SetUInt16
///
/// @desc   Set content of an array address to a value.
///
/// @param  [in] abyBuff - Buffer array.
/// @param  [in] nValue - Value to set.
///
/// @return void
///
void SetUInt16(uint8_t* abyBuff, uint16_t nValue)
{
    abyBuff[0] = (uint8_t)(nValue >> 8);
    abyBuff[1] = (uint8_t)(nValue & 0xff);
}

//==============================================================================
/// GetFloat
///
///
/// @desc   Return a float from HART format array buffer.
///         this routine assumes IEEE754 big endian float storage (sign bit in fourth byte)
///         The HART standard uses little endian, so the four bytes are swapped
///
/// @param  [in] abyBuff - Buffer array.
///
/// @return float - Converted value
///
float GetFloat(uint8_t* abyBuff)
{
    union {
        uint8_t u8[4];
        float fl;
    } cvt;

    cvt.u8[3]= *abyBuff++;
    cvt.u8[2]= *abyBuff++;
    cvt.u8[1]= *abyBuff++;
    cvt.u8[0]= *abyBuff++;

    return cvt.fl;
}

//==============================================================================
/// SetFloat
///
/// @desc   Set content of a HART format array address to a float value.
///         this routine assumes IEEE754 big endian float storage (sign bit in fourth byte)
///         The HART standard uses little endian, so the four bytes are swapped
///
/// @param  [in] abyBuff - Buffer array.
/// @param  [in] nValue - Value to set.
///
/// @return void
///
void SetFloat(uint8_t* abyBuff, float nValue)
{
    union {
        uint8_t u8[4];
        float fl;
    } cvt;
    cvt.fl = nValue;
    *abyBuff++ = cvt.u8[3];
    *abyBuff++ = cvt.u8[2];
    *abyBuff++ = cvt.u8[1];
    *abyBuff++ = cvt.u8[0];


}


//==============================================================================
/// GetUInt32
///
/// @desc   Return a Word from array buffer.
///
/// @param  [in] abyBuff - Buffer array.
/// @param  [out] pnRetval - Converted value.
///
/// @return uint32_t - Converted value
///
uint32_t GetUInt32(const uint8_t* abyBuff)
{
    uint32_t abyBuff32[4];
    abyBuff32[0] = abyBuff[0];
    abyBuff32[1] = abyBuff[1];
    abyBuff32[2] = abyBuff[2];
    abyBuff32[3] = abyBuff[3];
    return (abyBuff32[0] << 24) | (abyBuff32[1] << 16) | (abyBuff32[2] << 8) | abyBuff32[3];
}


//==============================================================================
/// SetUInt32
///
/// @desc   Set content of an array address to a value.
///
/// @param  [in] abyBuff - Buffer array.
/// @param  [in] nValue - Value to set.
///
/// @return void
///
void SetUInt32(uint8_t* abyBuff, volatile uint32_t nValue)
{
    abyBuff[3] = (uint8_t)(nValue & 0xff);
    nValue = nValue >> 8;
    abyBuff[2] = (uint8_t)(nValue & 0xff);
    nValue = nValue >> 8;
    abyBuff[1] = (uint8_t)(nValue & 0xff);
    nValue = nValue >> 8;
    abyBuff[0] = (uint8_t)(nValue & 0xff);
}


#ifdef __cplusplus
}
#endif