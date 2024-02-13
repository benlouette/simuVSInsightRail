#ifdef __cplusplus
extern "C" {
#endif

/*
 * AD7766_Common.h
 *
 *  Created on: Apr 29, 2016
 *      Author: Bart Willemse
 */

#ifndef AD7766_COMMON_H_
#define AD7766_COMMON_H_

#include <stdint.h>
#include <stdbool.h>
#include "fsl_dspi_hal.h"
#include "Resources.h"

//..............................................................................

// Define specific AD7766 chip variant characteristics, based upon whether
// AD7766, AD7766-1 or AD7766-2.
// *****************************************************************************
//              IMPORTANT - NEED TO SET FOR EXACT AD7766 VARIANT USED
//                 See AD7766 datasheet table 7 on page 17.
#define AD7766_OVERSAMPLING_RATIO       (16)    // AD7766-1 variant
#define AD7766_NUM_SETTLING_MCLKS       (1186)  // AD7766-1 variant

// *****************************************************************************

#define AD7766_NUM_BYTES_PER_SAMPLE     (3)

#define AD7766_MAX_OUTPUT_SAMPLES       (SAMPLE_BUFFER_SIZE_WORDS)

// AD7766 FlexTimer definitions
#define AD7766_FTM_INSTANCE             (FLEXTIMER_ALLOC_SAMPLING)
#define AD7766_FTM_CHANNEL              (4)

// AD7766 DSPI definitions
#define AD7766_DSPI_INSTANCE            (SPI1_IDX)
#define AD7766_DSPI_CHIP_SELECT         kDspiPcs0

typedef enum
{
    AD7766ERROR_NONE = 0,
    AD7766ERROR_INVALID_PORT_INDEX,
    AD7766ERROR_EDMA_CHANREQ_TX,
    AD7766ERROR_EDMA_CHANREQ_RX,
    AD7766ERROR_ALREADY_SAMPLING,
    AD7766ERROR_MCLK_START,
    AD7766ERROR_SPI_ECHO_BYTE_MISMATCH,
} AD7766ErrorEnum;

//..............................................................................

void AD7766_GetDataFormatConfig(uint32_t BitsPerFrame,
                                dspi_data_format_config_t *pDspiDataFormat);
void AD7766_PerformResetSequence(void);
bool AD7766_MclkStart(uint32_t FrequencyHz);
void AD7766_MclkStop(void);
int32_t AD7766_ConvertRawToSigned(uint32_t RawSampleVal);
void AD7766_SendSamplesToTestUart(uint32_t NumSamples,
                                  uint32_t SamplesPerSec,
                                  int32_t *pSampleBuf);

void AD7766_SetError(AD7766ErrorEnum AD7766Error);
bool AD7766_ErrorIsSet(void);
void AD7766_ClearError(void);
AD7766ErrorEnum AD7766_GetFirstError(uint16_t *pBlockNumRet);

// Test functions
bool AD7766_TestRawToSignedConversion(void);
void AD7766_TestIOLines(void);

//..............................................................................

#endif // AD7766_COMMON_H_




#ifdef __cplusplus
}
#endif