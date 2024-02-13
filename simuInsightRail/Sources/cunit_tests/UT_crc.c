#ifdef __cplusplus
extern "C" {
#endif

/*
 * UT_crc.c
 *
 *  Created on: 24 Jan 2019
 *      Author: tk2319
 */

#include "UnitTest.h"
#include "crc.h"
#include "linker.h"

void testCRCbuffer(void);
void testCRCimage(void);

CUnit_suite_t UTcrc = {
	{ "crc", NULL, NULL, CU_TRUE, "test crc functions"},
	{
		{ "test memory CRC", testCRCbuffer },
		{ "test image CRC", testCRCimage },
		{ NULL, NULL }
	}
};

/*
 * Test the CRC code
 */
void testCRCbuffer(void)
{
	struct {
		uint8_t value;
		uint32_t crc;
	} tests[] = {
		{ 0x00, 0x7EE8CDCD },
		{ 0xFF, 0x154803CC },
		{ 0x55, 0xEEA78A0D },
		{ 0xAA, 0x8507440C },
	};

	// test CRC calculation with ram buffer
	for(int i = 0; i < sizeof(tests)/sizeof(tests[0]); i++)
	{
		memset(__sample_buffer, tests[i].value, (uint32_t)__sample_buffer_size);
		CU_ASSERT(tests[i].crc == crc32_hardware((void *)__sample_buffer, (uint32_t)__sample_buffer_size));
	}
}

void testCRCimage(void)
{
	// check the application CRC
	CU_ASSERT(CRCTARGETVALUE == crc32_hardware((void *)__app_origin, (uint32_t)__app_image_size));
}



#ifdef __cplusplus
}
#endif