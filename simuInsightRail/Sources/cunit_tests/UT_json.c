#ifdef __cplusplus
extern "C" {
#endif

/*
 * UT_json.c
 *
 *  Created on: 28 Jan 2019
 *      Author: RZ8556
 */

#include <stdio.h>
#include "UnitTest.h"

extern void ut_json();

static void testJSON();

/*
 * JSON unit test suite definition.
 * Does not require setup or teardown functions so pass as NULLs
 */
CUnit_suite_t UTjson = {
	{ "json", 0, 0, CU_TRUE, ""},
	{
		{ "test of JSON parsing", testJSON },
		{ NULL, NULL }
	}
};

#include "image.h"
static void testJSON()
{
	ut_json();
}


#ifdef __cplusplus
}
#endif