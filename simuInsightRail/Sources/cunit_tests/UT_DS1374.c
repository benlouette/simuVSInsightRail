#ifdef __cplusplus
extern "C" {
#endif

/*
 * UT_DS1374.c
 *
 *  Created on: 5 April 2019
 *      Author: John McRobert TK2319 (Livingston)
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "FreeRTOS.h"

#include "Device.h"
#include "ds137n.h"
#include "UnitTest.h"

/*
 * test1
 *
 * @desc	perform a counter test
 *
 * @param	none
 *
 * @returns	none
 */
static void test1()
{
	uint32_t count;

	// reset the counter
	CU_ASSERT(true == DS137n_ResetCounter());
	CU_ASSERT(true == DS137n_ReadCounter(&count));
	CU_ASSERT(0 == count);
	vTaskDelay(4100);	// delay 4secs plus some headway
	CU_ASSERT(true == DS137n_ReadCounter(&count));
	CU_ASSERT(4 == count);
}

/*
 * test2
 *
 * @desc	perform alarm testing
 *
 * @param	none
 *
 * @returns	none
 */
static void test2()
{
	uint32_t count;
	uint8_t abyte;
	const uint32_t counts[] = { 0, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 0xffffff, 0 };

	// reset the counter
	for(int i = 0; i < sizeof(counts)/sizeof(uint32_t); i++)
	{
		// the alarm register and check the result
		CU_ASSERT(true == DS137n_SetAlarm(counts[i]));
		CU_ASSERT(true == DS137n_ReadAlarm(&count));
		CU_ASSERT(counts[i] == count);
		// check the control register
		CU_ASSERT(true == DS137n_ReadControl(&abyte));
		CU_ASSERT((counts[i] ? 0x49 : 0) == abyte);
	}

	// set the alarm and make sure there's no interrupt
	CU_ASSERT(true == DS137n_SetAlarm(100));
	CU_ASSERT(true == DS137n_ReadStatus(&abyte));
	CU_ASSERT(0 == abyte);
	// delay 5 seconds
	vTaskDelay(5000);
	CU_ASSERT(true == DS137n_ReadAlarm(&count));
	CU_ASSERT(95 == count);

	// set the alarm and make sure there's an interrupt
	CU_ASSERT(true == DS137n_SetAlarm(5));
	// delay 5 seconds and change
	vTaskDelay(5100);
	CU_ASSERT(true == DS137n_ReadStatus(&abyte));
	CU_ASSERT(1 == abyte);
	// reading the status clears it so check it has been
	CU_ASSERT(true == DS137n_ReadStatus(&abyte));
	CU_ASSERT(0 == abyte);
	// counter should reload the 5 secs
	CU_ASSERT(true == DS137n_ReadAlarm(&count));
	CU_ASSERT(5 == count);

	// stop the alarm
	CU_ASSERT(true == DS137n_SetAlarm(0));
}

CUnit_suite_t UTds1374 = {
	{ "DS1374", NULL, NULL, CU_TRUE, "test DS1374 functions" },
	{
		{"counter test", test1},
		{"alarm test",   test2},
		{ NULL, NULL }
	}
};


#ifdef __cplusplus
}
#endif