#ifdef __cplusplus
extern "C" {
#endif

/*
 * UT_externalFlash.c
 *
 *  Created on: 20 Jun 2019
 *      Author: RZ8556
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "FreeRTOS.h"

#include "linker.h"
#include "UnitTest.h"
#include "drv_is25.h"

// 16 pages per sector, 0x100 bytes per page
#define BYTES_PER_PAGE	(0x100)
#define PAGES_PER_SECTOR (16)
#define TEST_BLOCK_SIZE_SECTORS(x) (BYTES_PER_PAGE * PAGES_PER_SECTOR * (x))
#define WORST_CASE_TRANSFER_RATE (16000) // approximate value from spreadsheet

static const int testSize = TEST_BLOCK_SIZE_SECTORS(32);
static const int startAddress = 0;
static const char testValue = 0x55;

/*
 * NOTE! These tests WILL corrupt the flash device contents.
 * Also, there may be interdependence across tests so don't change the test suite calling order
 */

static int find_last_match(const char* s1, const char c, const int max)
{
	int i;
	for(i=0; i < max; i++)
	{
		if(*(s1 + i) != c)
		{
			break;
		}
	}
	return i;
}

/*
 * test1
 *
 * @desc	perform a speed test. Limited to sample_buffer size ()
 *
 * @param	none
 *
 * @returns	none
 */
static void test1()
{
	char* buf = (char*)__sample_buffer;

	if(testSize > (uint32_t)__sample_buffer_size)
	{
		CU_FAIL("Requested size exceeds buffer");
		return;
	}

	memset(buf, testValue, testSize);
	int last_char = find_last_match(buf, testValue, testSize);
	CU_ASSERT(last_char == testSize);

	bool retval = IS25_PerformSectorErase(startAddress, testSize);
	CU_ASSERT(retval);

	// get tick count
	TickType_t startTicks = xTaskGetTickCount();
	retval = IS25_WriteBytes(startAddress, (uint8_t*)buf, testSize);
	CU_ASSERT(xTaskGetTickCount() > startTicks);
	TickType_t total_ticks = xTaskGetTickCount() - startTicks;

	if(retval)
	{
		char buf[30];
		sprintf(buf, "%ld bytes per second", (1000*testSize)/(total_ticks*portTICK_PERIOD_MS));
		CU_ASSERT((1000*testSize)/(total_ticks*portTICK_PERIOD_MS) > WORST_CASE_TRANSFER_RATE);
	}
	else
	{
		CU_FAIL("Error writing test pattern");
	}
}


/*
 * test2
 *
 * @desc	perform a read test
 *
 * @param	none
 *
 * @returns	none
 */
static void test2()
{
	char* buf = (char*)__sample_buffer;
	int address = startAddress;

	if(testSize > (uint32_t)__sample_buffer_size)
	{
		CU_FAIL("Requested size exceeds buffer");
		return;
	}

	bool b_ok = IS25_ReadBytes(address, (uint8_t*)buf, testSize + startAddress);

	CU_ASSERT(b_ok && (find_last_match(buf, testValue, testSize) == testSize));
	buf[testSize/2] = ~testValue;
	CU_ASSERT(b_ok && (find_last_match(buf, testValue, testSize) != testSize));
}


CUnit_suite_t UTexternalFlash = {
	{ "FLASH", NULL, NULL, CU_TRUE, "test external flash functions" },
	{
		{"write speed test", test1},
		{"read test", test2},
		{ NULL, NULL }
	}
};



#ifdef __cplusplus
}
#endif