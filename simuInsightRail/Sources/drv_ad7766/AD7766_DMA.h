#ifdef __cplusplus
extern "C" {
#endif

/*
 * AD7766_DMA.h
 *
 *  Created on: Apr 27, 2016
 *      Author: Bart Willemse
 */

#ifndef AD7766_DMA_H_
#define AD7766_DMA_H_

#include <stdint.h>
#include <stdbool.h>
#include "AD7766_Common.h"
#include "AdcApiDefs.h"

//..............................................................................
// Define AD7766 DMA ** DRIVER ** minimum and maximum samples per second.
// Note that this is separate from the actual sampling rate supported by the
// chips themselves - the AD7766, AD7766-1 and AD7766-2 device variants support
// up to 128, 64 and 32ksps respectively.
// TODO: Rough guesses only for now - need to check and test these limits.
// The lower limit depends upon the lowest possble MCLK which can be generated,
// with the actual corresponding raw sampling rate determined by the AD7766
//  variant's oversampling rate.
#define AD7766_DMA_DRIVER_MIN_SAMPLES_PER_SEC  (100)
// For max value, 100ksps has been quickly tested, and the AD7766 DSPI EDMA
// driver, ping-pong buffering and xTaskMeasure task's real-time sample
// handling seem to work OK at this rate. Have NOT tested at 110ksps yet.
#define AD7766_DMA_DRIVER_MAX_SAMPLES_PER_SEC  (110000U)

//..............................................................................

void AD7766_Init(void);
void AD7766_InstallISRCallback(tAdcISRCallback pAdcISRCallback);
void AD7766_PrepareMclkStoppageTest(uint16_t BlockNum);
bool AD7766_StartSampling(uint32_t SamplesPerSec);
bool AD7766_FinishSampling(void);
uint16_t AD7766_GetCurrentBlockNum(void);
bool AD7766_PreProcessRawSpiIntoSampleVal(uint32_t AdcRawValIn,
                                          int32_t *pSampleValOut);
uint32_t AD7766_MaxBlockMillisecs(uint32_t ADCSamplesPerSec);

//..............................................................................

#endif // AD7766_DMA_H_




#ifdef __cplusplus
}
#endif