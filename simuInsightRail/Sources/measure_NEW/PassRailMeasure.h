#ifdef __cplusplus
extern "C" {
#endif

/*
 * PassRailMeasure.h
 *
 *  Created on: 11 July 2016
 *      Author: Bart Willemse
 */

#ifndef PASSRAILMEASURE_H_
#define PASSRAILMEASURE_H_

#include <stdint.h>
#include <stdbool.h>
#include "PassRailAnalogCtrl.h"


typedef enum
{
    MEASID_UNDEFINED = 0,

    //**********************
    // TODO: FOR EMC TESTING ONLY - REMOVE EVENTUALLY
    MEASID_EMC_RAWVIB_1024SPS,
    //**********************

    //......................................................
    // Passenger-rail-specific measurement IDs
    // Raw acceleration
    MEASID_RAWACCEL_25600SPS,

    // Vibration
    MEASID_VIB_1280SPS,
    MEASID_VIB_2560SPS,
    MEASID_VIB_5120SPS,

    // Wheel-flats
    MEASID_WFLATS_256SPS,
    MEASID_WFLATS_512SPS,
    MEASID_WFLATS_1280SPS

    //......................................................
} tMeasId;

bool PassRailMeasure_PrepareSampling(tMeasId MeasId,
                                     uint32_t *pAdcSamplesPerSecRet,
                                     uint32_t *pDspAdcSettlingSamplesRet);
void PassRailMeasure_PostSampling(void);
uint32_t PassRailMeasure_GetAdcSpsForMeasId(tMeasId MeasId);
AnalogFiltEnum PassRailMeasure_GetAnalogFiltForMeasId(tMeasId MeasId);

#endif // PASSRAILMEASURE_H_


#ifdef __cplusplus
}
#endif