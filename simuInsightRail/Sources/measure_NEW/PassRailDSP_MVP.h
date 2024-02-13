#ifdef __cplusplus
extern "C" {
#endif

/*
 * PassRailDSP_MVP.h
 *
 *  Created on: 30 May 2016
 *      Author: Bart Willemse
 *              ********** INITIAL MINIMUM VIABLE PRODUCT VERSION **************
 */

#ifndef PASSRAILDSP_MVP_H_
#define PASSRAILDSP_MVP_H_

#include <stdint.h>
#include <stdbool.h>
#include "Resources.h"

//******************************************************************************
#ifndef PASSRAIL_DSP_NEW    // N.B. NOT PASSRAIL_DSP_NEW
//******************************************************************************

//..............................................................................

typedef enum
{
    ENV_NONE = 0,
    ENV_VIB,
    ENV_WFLATS
} EnveloperEnum;

typedef enum
{
    DECIMCHAIN_NONE = 0,

    // TODO: Temporary for MVP
    // TODO: Modify decimation filtering to use the decimation factors
    // eventually (assuming that vibration and wheel-flats can share the same
    // filtering)
    DECIMCHAIN_VIB_FACTOR16_MVP,
    DECIMCHAIN_WFLATS_FACTOR8_MVP,
#if 0
    DECIM_FACTOR8,
    DECIM_FACTOR16,
    DECIM_FACTOR32
#endif // 0
} DecimChainEnum;

//..............................................................................

bool PassRailDsp_Init(EnveloperEnum Enveloper, DecimChainEnum Decimation);
void PassRailDsp_ProcessBlock(int32_t *pSampleBlockIn,
                              int32_t *pSampleBlockOut,
                              uint32_t *pNumOutputSamples);

//..............................................................................

//******************************************************************************
#endif // NOT PASSRAIL_DSP_NEW
//******************************************************************************


#endif // PASSRAILDSP_MVP_H_


#ifdef __cplusplus
}
#endif