#ifdef __cplusplus
extern "C" {
#endif

/*
 * FccTest.h
 *
 *  Created on: May 25, 2018
 *      Author: Jayant Rampuria
 *
 */

#ifndef FCC_TEST_H_
#define FCC_TEST_H_

#include <stdint.h>

void FCCTest_Init();
void FccTest_PrintSamples(bool bOutputEn);
void FccTest_StoreSamplesToSampleBuf(uint32_t *pAdcBlock);



#endif /* FCC_TEST_H_ */


#ifdef __cplusplus
}
#endif