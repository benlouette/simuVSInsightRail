#ifdef __cplusplus
extern "C" {
#endif

/*
 * AdcApiDefs.h
 *
 *  Created on: May 25, 2016
 *      Author: Bart Willemse
 * Description: Defines the required ADC driver API interface.
 */

#ifndef ADCAPIDEFS_H_
#define ADCAPIDEFS_H_

// Define number of entries in an ADC sample input block buffer. This
// corresponds to e.g. one-half of the overall ping-pong buffer.
// CRITICALLY IMPORTANT: Do NOT change without also changing dependent code,
// because dependent code currently has various hard-coded size items.
#define ADC_SAMPLES_PER_BLOCK    (128)

typedef enum
{
    ADCBLOCKRESULT_DATAREADY = 0,   // ADC block trigger, sampling continues
    ADCBLOCKRESULT_DONE,            // ADC sampling done
} tAdcBlockResult;

typedef struct
{
    tAdcBlockResult AdcBlockResult;
    uint32_t *pSampleBlock;
} tAdcBlockData;

// Define ADC ISR callback function pointer type
typedef void (*tAdcISRCallback)(tAdcBlockData AdcBlockData);

#endif // ADCAPIDEFS_H_


#ifdef __cplusplus
}
#endif