#ifdef __cplusplus
extern "C" {
#endif

/*
 *  Simple example of a CUnit unit test.
 * 
 * Output:- Junit XML format
 * 
 *<?xml version="1.0" encoding="UTF-8"?>
 *<testsuite tests="2" name="Suite_1">
 *       <testcase classname=".Suite_1" name="test of strcmp()" time="0">
 *           <failure message="" type="Failure">
 *                    Condition: 0 &gt; strcmp("Hi","Hello")
 *                    File     : ../Sources/cunit_tests/TestDecimationFilters.c
 *                    Line     : 61
 *           </failure>
 *       </testcase>
 *       <testcase classname=".Suite_1" name="test of strlen()" time="0">
 *          <failure message="" type="Failure">
 *                    Condition: 10 == strlen("SKF Insight")
 *                    File     : ../Sources/cunit_tests/TestDecimationFilters.c
 *                    Line     : 70
 *           </failure>
 *       </testcase>
 * </testsuite>
 *
 * <CunitRunSummaryRecord>
 *    <Suites total="1" run="1" failed="0" inactive="0"> </Suites>
 *
 *    <TestCases total="2" run="2" success="0" failed="2" inactive="0"> </TestCases>
 *
 *    <Assertions total="5" run="5" success="3" failed="2" inactive="n/a"> </Assertions>
 *
 * </CunitRunSummaryRecord>
 */

#include <stdio.h>
#include "CUnit/Basic.h"
#include "UnitTest.h"
#include "device.h"

extern void CU_automated_enable_junit_xml(CU_BOOL);
extern void CU_automated_run_tests(void);
extern void CU_automated_package_name_set(const char *pName);

extern CUnit_suite_t UTeventlog;
extern CUnit_suite_t UTcrc;
extern CUnit_suite_t UTbasic;
extern CUnit_suite_t UTjson;
extern CUnit_suite_t UTds1374;
extern CUnit_suite_t UTtemp;
extern CUnit_suite_t UTexternalFlash;
extern CUnit_suite_t UTnfc;
extern CUnit_suite_t UTNVMConfig;
extern CUnit_suite_t UTpmic;
extern CUnit_suite_t UTbinaryCLI;
extern CUnit_suite_t UTalarms;

CUnit_suite_t *suites[] = {
	&UTbasic,
	&UTcrc,
	&UTeventlog,
	&UTjson,
	&UTds1374,
	&UTtemp,
	&UTexternalFlash,
	//&UTnfc,
	&UTNVMConfig,
	&UTpmic,
	&UTbinaryCLI,
	&UTalarms,
	NULL
};

void setInactiveTests()
{
	if(Device_HasPMIC())
	{
		UTds1374.suite.fActive = CU_FALSE;
		UTalarms.suite.fActive = CU_FALSE;	// remove when new alarms ported to PMIC variant
	}
	else
	{
		UTnfc.suite.fActive = CU_FALSE;
		UTpmic.suite.fActive = CU_FALSE;
	}
}

void RunUnitTestsHelp(void)
{
	setInactiveTests();

	printf("  ut -h - display help\n"
		   "  ut all - run all tests\n");

	for(int i = 0; suites[i]; i++)
	{
	   CUnit_suite_t *s = suites[i];

	   if(s->suite.fActive)
	   {
		   if(!s->suite.help || strlen(s->suite.help) == 0)
		   {
			   printf("  ut %s\n", s->suite.name);
		   }
		   else
		   {
			   printf("  ut %s - %s\n",
					   s->suite.name,
					   s->suite.help);
		   }
	   }
	}
}

/* The main() function for setting up and running the tests.
 * Returns a CUE_SUCCESS on successful running, another
 * CUnit error code on failure.
 */
int RunUnitTests(char *suite)
{
   CU_pSuite pSuite = NULL;

   setInactiveTests();

   CU_automated_package_name_set("Application");

   /* initialize the CUnit test registry */
   if (CUE_SUCCESS != CU_initialize_registry())
      return CU_get_error();

   for(int i = 0; suites[i]; i++)
   {
	   CUnit_suite_t *s = suites[i];

	   if(!(strcmp(suite, "all") == 0 || strcmp(suite, s->suite.name) == 0))
		   continue;

	   /* add a suite to the registry */
	   pSuite = CU_add_suite(s->suite.name, s->suite.init, s->suite.clean);

	   if (NULL == pSuite)
	   {
	      CU_cleanup_registry();
	      return CU_get_error();
	   }

	   pSuite->fActive = s->suite.fActive;

	   for(int j = 0; s->tests[j].test; j++)
	   {
		   /* add the tests to the suite */
		   if (NULL == CU_add_test(pSuite, s->tests[j].name, s->tests[j].test))
		   {
			  CU_cleanup_registry();
			  return CU_get_error();
		   }
	   }
   }

   /* Run tests using the Automated interface */
   CU_automated_enable_junit_xml(CU_TRUE);
   CU_automated_run_tests();
   
    /* Clean up registry and return */
   CU_cleanup_registry();
   return CU_get_error();  
}


#ifdef __cplusplus
}
#endif