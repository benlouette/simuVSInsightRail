#ifdef __cplusplus
extern "C" {
#endif

/*
 * PassRailDSP_NEW.h
 *
 *  Created on: 30 May 2016
 *      Author: Bart Willemse
 *              ********** INITIAL MINIMUM VIABLE PRODUCT VERSION **************
 */

#ifndef PASSRAILDSP_NEW_H_
#define PASSRAILDSP_NEW_H_

#include <stdint.h>
#include <stdbool.h>
#include "Resources.h"

//******************************************************************************
#ifdef PASSRAIL_DSP_NEW
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

#ifdef PASSRAIL_DSP_NEW
    DECIMCHAIN_VIB_1280,
    DECIMCHAIN_VIB_2560,
    DECIMCHAIN_VIB_5120,

    DECIMCHAIN_WFLATS_256,
    DECIMCHAIN_WFLATS_512,
    DECIMCHAIN_WFLATS_1280

#else // NOT PASSRAIL_DSP_NEW

    //DECIM_FACTOR10,
    DECIM_FACTOR20,
    //DECIM_FACTOR40
#endif // NOT PASSRAIL_DSP_NEW

} DecimChainEnum;

//..............................................................................

bool PassRailDsp_Init(EnveloperEnum EnveloperID, DecimChainEnum DecimChainID);
void PassRailDsp_ProcessBlock(int32_t *pSampleBlockIn,
                              int32_t *pSampleBlockOut,
                              uint32_t *pNumOutputSamples);

// Test functions
void PassRailDsp_TEST(void);
void getMeanValues( int32_t * meanp, uint32_t * meanCountp);

//..............................................................................

//******************************************************************************
#endif // PASSRAIL_DSP_NEW
//******************************************************************************


#endif // PASSRAILDSP_NEW_H_


#ifdef __cplusplus
}
#endif