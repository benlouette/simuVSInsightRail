#ifdef __cplusplus
extern "C" {
#endif

/*
 * UT_basic.c
 *
 *  Created on: 24 Jan 2019
 *      Author: tk2319
 */

#include <stdio.h>
#include "fsl_rtc_hal.h"
#include "UnitTest.h"

int init_suite1(void);
int clean_suite1(void);
void testSTRCMP(void);
void testSTRLEN(void);
void testDelay(void);
void testConvertGnssUtc2Rtc(void);

CUnit_suite_t UTbasic = {
	{ "basic", init_suite1, clean_suite1, CU_TRUE, ""},
	{
		{ "test of strcmp()", testSTRCMP },
		{ "test of strlen()", testSTRLEN },
		{ "test of vTaskDelay()", testDelay },
		{ "test of ConvertGnssUtc2Rtc()", testConvertGnssUtc2Rtc },
		{ NULL, NULL }
	}
};

/* The suite initialization function.
  * Returns zero on success, non-zero otherwise.
 */
int init_suite1(void)
{
   return 0;

}
/* The suite cleanup function.
 * Returns zero on success, non-zero otherwise.
 */
int clean_suite1(void)
{
      return 0;
}

/* Simple test of String Compare strcmp().
 */
void testSTRCMP(void)
{
  CU_ASSERT(0 == strcmp("test","test"));
  CU_ASSERT(0 > strcmp("Hello","Hi"));
  CU_ASSERT(0 < strcmp("Hi","Hello"));

}

/* Simple test of string lenth strlen().
 */
void testSTRLEN(void)
{
  CU_ASSERT(11 == strlen("SKF Insight"));
  CU_ASSERT(10 != strlen("SKF Insight"));
}

/* Simple test of vTaskDelay.
 */
void testDelay(void)
{
	extern void vTaskDelay(unsigned int);
	extern unsigned int xTaskGetTickCount(void);
	unsigned int count, start = xTaskGetTickCount();
	vTaskDelay(2005);
	count = xTaskGetTickCount() - start;
	CU_ASSERT(count > 2000);
}

void testConvertGnssUtc2Rtc(void)
{
	extern void ConvertGnssUtc2Rtc(float, uint32_t, rtc_datetime_t*);
	
	rtc_datetime_t datetime = {};
	ConvertGnssUtc2Rtc(105736.232960, 210722, &datetime);

	CU_ASSERT(2022 == datetime.year);
	CU_ASSERT(7 == datetime.month);
	CU_ASSERT(21 == datetime.day);
	CU_ASSERT(10 == datetime.hour);
	CU_ASSERT(57 == datetime.minute);
	CU_ASSERT(36 == datetime.second);
	
	memset(&datetime, 0, sizeof(datetime));
	ConvertGnssUtc2Rtc(235958.987654, 280222, &datetime);

	CU_ASSERT(2022 == datetime.year);
	CU_ASSERT(2 == datetime.month);
	CU_ASSERT(28 == datetime.day);
	CU_ASSERT(23 == datetime.hour);
	CU_ASSERT(59 == datetime.minute);
	CU_ASSERT(58 == datetime.second);
}

#ifdef __cplusplus
}
#endif