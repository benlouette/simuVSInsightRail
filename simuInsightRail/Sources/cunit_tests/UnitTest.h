#ifdef __cplusplus
extern "C" {
#endif

#ifndef UNITTEST_H_
#define UNITTEST_H_

#include "CUnit/CUnit.h"

/*
 * structure for defining a unit test suite
 */
typedef struct {
	struct {
		char *name;
		CU_InitializeFunc init;
		CU_CleanupFunc clean;
		CU_BOOL           fActive;          /**< Flag for whether suite is executed during a run. */
		char *help;
	} suite;	// define suite here
	struct {
		char *name;
		CU_TestFunc test;
	} tests[];	// array of tests here
} CUnit_suite_t;

int RunUnitTests(char *suite);
void RunUnitTestsHelp(void);

#endif /* UNITTEST_H_ */


#ifdef __cplusplus
}
#endif