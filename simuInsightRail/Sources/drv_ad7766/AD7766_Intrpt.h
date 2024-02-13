#ifdef __cplusplus
extern "C" {
#endif

/*
 * AD7766_Intrpt.h
 *
 *  Created on: Jan 20, 2016
 *      Author: Bart Willemse
 */

#ifndef AD7766_INTRPT_H_
#define AD7766_INTRPT_H_

#include <stdint.h>
#include <stdbool.h>
#include "AD7766_Common.h"

//..............................................................................
// Define AD7766 interrupt-driven minimum and maximum samples per second
#define AD7766_INTR_MIN_SAMPLES_PER_SEC     (100)
// Maximum sampling rate which works for interrupt-driven AD7766 has been
// tested - total sample cycle time as seen on scope (from DRDY# pulse to end of
// SPI ~CS) is about 290us, and 3000sps is 333us so this is a reasonable margin
#define AD7766_INTR_MAX_SAMPLES_PER_SEC     (3000)

//..............................................................................

// N.B. AD7766 I/O lines are configured in powerAnalogOn()

void AD7766Intrpt_SpiInit(void);
void AD7766Intrpt_SpiDeinit(void);
uint32_t AD7766Intrpt_ReadSampleRegisters(void);
bool AD7766Intrpt_PerformSampleBurst(uint32_t NumSamples, uint16_t SamplesPerSec);
void AD7766Intrpt_ADCIRQn_ISR(void);

//..............................................................................

#endif // AD7766_INTRPT_H_




#ifdef __cplusplus
}
#endif