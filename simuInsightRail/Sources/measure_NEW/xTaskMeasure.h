#ifdef __cplusplus
extern "C" {
#endif

/*
 * xTaskMeasure.h
 *
 *  Created on: 26 jun. 2014
 *      Author: g100797, adapted by Bart Willemse
 */

#ifndef XTASKMEASURE_H_
#define XTASKMEASURE_H_

//..............................................................................
// Includes
#include <stdint.h>
#include <stdbool.h>
#include "PassRailMeasure.h"
#include "AD7766_Common.h"
#include "AdcApiDefs.h"

//..............................................................................
// Types
typedef enum
{
    MEASUREERROR_NONE = 0,
    MEASUREERROR_ADC_TO_SAMPLE_BLOCK_CONVERT,
    MEASUREERROR_PRE_SAMPLING,
    MEASUREERROR_STARTSAMPLING,
    MEASUREERROR_FINISHSAMPLING,
    MEASUREERROR_BLOCKTIMEOUT,
} MeasureErrorEnum;

typedef struct
{
    MeasureErrorEnum MeasureError;
    AD7766ErrorEnum AD7766Error;
    // TODO: Can add further error classes in here, e.g. DSP errors etc
    uint16_t ErrorBlockNum;
} MeasureErrorInfoType;

typedef void (*tMeasureCallback)(void);

//..............................................................................
// Functions

// Init must be called before the scheduler is started
void xTaskMeasure_Init(void);
void xTaskMeasure(void *pvParameters);

bool Measure_Start(bool bRawAdcSampling,
                   tMeasId MeasId,
                   uint32_t NumOutputSamples,
                   uint32_t AdcSamplesPerSecIfRawAdc,
                   tMeasureCallback pCallback);
bool Measure_GetErrorInfo(MeasureErrorInfoType *pMeasureErrorInfo);

//..............................................................................

#endif // XTASKMEASURE_H_


#ifdef __cplusplus
}
#endif