#ifdef __cplusplus
extern "C" {
#endif

/*
 * convert_junit.c
 *
 *  Created on: 26 Jun 2019
 *      Author: RZ8556
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "printgdf.h"

/*
 * Converts and emits the input as JUnit formatted XML
 */

/*
 * Canonical JUnit XML:
 *
 * <?xml version="1.0" encoding="UTF-8"?>
 * <testsuite 	name=""
 * 				tests="" <!-- number of tests in the suite -->
 * >
 *
 * <!-- testcase can appear multiple times, see /testsuites/testsuite@tests -->
 * <testcase	name=""       <!-- Name of the test method, required. -->
 *				classname=""  <!-- Full class name for the class the test method is in. required -->
 * >
 *
 * <!-- Indicates that the test failed. A failure is a test which
 *	   the code has explicitly failed by using the mechanisms for
 *	   that purpose. For example via an assertEquals. Contains as
 *	   a text node relevant data for the failure, e.g., a stack
 *	   trace. optional -->
 * <failure message="" <!-- The message specified in the assert. -->
 * ></failure>
 *
 * </testsuite>
 */

#include "linker.h"

static char* xml_results = (char*)&__sample_buffer;

// if we see -1 as number of tests then something went wrong
static int m_test_count = -1;

void convert_JUnit_Item(const char* buf)
{
	const char const* str_test_count_marker = "UT:TESTS=";
	const int tcm_length = strlen(str_test_count_marker);

	if(strstr(buf, ":FAIL:"))
	{
		sprintf(&xml_results[strlen(xml_results)], "<failure message=\"%s\"\"></failure>\n\r", buf);
	}
	else
	{
		const char* ptr_test_count_marker = strstr(buf, str_test_count_marker);
		if(ptr_test_count_marker && isdigit((int)*(ptr_test_count_marker + tcm_length)))
		{
			m_test_count = atol(ptr_test_count_marker + tcm_length);
		}
	}
}

void convert_JUnit_Start()
{
	xml_results[0] = 0;
}

void convert_JUnit_Stop()
{
	printf("\n<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
			"<testsuite name=\"PMIC Tests\" tests=\"%d\">\n"
			"%s</testsuite>\n", m_test_count, xml_results);
}



#ifdef __cplusplus
}
#endif