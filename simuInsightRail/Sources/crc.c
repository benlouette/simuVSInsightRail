#ifdef __cplusplus
extern "C" {
#endif

/*
 * crc.c
 *
 *  Created on: 20 Nov 2018
 *      Author: RZ8556
 */

#include "stdbool.h"
#include "MK24F12.h"
#include "crc.h"

#define CRC_POLYN 	0x04C11DB7
#define CRC_CTRL_INIT (0xA0000000 | CRC_CTRL_FXOR_MASK | CRC_CTRL_TCRC_MASK | CRC_CTRL_WAS_MASK)

/**
 * @desc	Setup for a CRC32 calculation using the NORMAL CRC polynomial
 *
 * @param	none
 *
 * @returns	none
 */
void crc32_start(void)
{
#ifndef _MSC_VER
    // turn on the CRC logic clock
    SIM_SCGC6 |= SIM_SCGC6_CRC_MASK;

    // Initialise the CRC logic
    CRC0->CTRL = CRC_CTRL_INIT;			/* 32-bit CRC */
    CRC0->DATA = 0xFFFFFFFF;			/* Seed */
	CRC0->CTRL &= ~CRC_CTRL_WAS_MASK;	/* Clear WAS */
	CRC0->GPOLY = CRC_POLYN;			/* Polynomial to match */
#endif
}

/**
 * @desc	Compute the CRC32 for the buffer of the given length
 *
 * @param	pBuff - pointer to a buffer
 * @param	length - length of the buffer in bytes
 *
 * @returns	none
 */
void crc32_calc(void *pBuff, uint32_t length)
{
#ifndef _MSC_VER
	int i;

	if(length & 3)
	{
		// need to do it byte wide
		uint8_t *p = (uint8_t*)pBuff;

		// perform the CRC calculation
		for(i = 0; i < length; i++)
		{
			CRC0->ACCESS8BIT.DATALL = *p++;
		}
	}
	else
	{
		// can do it 32 bit wide
		uint32_t *p = (uint32_t*)pBuff;

		// convert to # words
		length /= 4;

		// perform the CRC calculation
		for(i = 0; i < length; i++)
		{
			CRC0->DATA = *p++;
		}
	}
#endif
}

/**
 * @desc	shuts down the CRC logic and returns the calculated CRC
 *
 * @param	none
 *
 * @returns	32 bit calculated CRC
 */
uint32_t crc32_finish(void)
{
	// save the CRC
	uint32_t crc = 0;
#ifndef _MSC_VER
	// Make sure CRC clock is on (in case some muppet calls crc32_finish w/o calling crc32_start)
	if(SIM_SCGC6 & SIM_SCGC6_CRC_MASK)
	{
		crc = CRC0->DATA;
	}

	// now turn of the clock
	SIM_SCGC6 &= ~(SIM_SCGC6_CRC_MASK);
#endif
	return crc;
}

/**
 * @desc	Computes the CRC32 of the buffer of the given length
 *			using the NORMAL CRC polynomial
 *
 * @param	pBuff - pointer to buffer for the calculation
 * @param	length - size of the buffer in bytes
 *
 * @returns	calculated CRC32 value
 */
uint32_t crc32_hardware(void *pBuff, uint32_t length)
{
	crc32_start();
	crc32_calc(pBuff, length);
	return crc32_finish();
}


#ifdef __cplusplus
}
#endif