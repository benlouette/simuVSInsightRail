#ifdef __cplusplus
extern "C" {
#endif

/*
 * Measurement.h
 *
 *  Created on: 8 Sep 2017
 *      Author: tk2319
 */

#ifndef MEASUREMENT_H_
#define MEASUREMENT_H_

#include "taskGnss.h"

typedef enum {
	eStopped			= 255,	// Train stopped
	eSpeedSlow			= 254,	// Speed too slow
	eSpeedFast			= 253,	// Speed too fast
	eSpeedDiff			= 252,	// accelerating/decelerating
	eNoFix				= 251,	// No GPS fix
	eMissingPMICdata	= 250,  // Missing data from PMIC
	eSpeedBDFS			= 247,	// slow/fast/diff ?????
	eSpeedBDF			= 246,  // fast/diff
	eSpeedBDS			= 245,  // slow/diff
	eSpeedBD			= 244,  // diff
	eSpeedBFS			= 243,  // slow/fast ?????
	eSpeedBF			= 242,  // fast
	eSpeedBS			= 241,  // slow
	eSpeedBits  		= 240,  // 0xF0 + 3 bits
	eSpeedBSlow 		= 1,    //
	eSpeedBFast 		= 2,	//
	eSpeedBDiff 		= 4,	//
} eGatedErrors_t;

typedef struct
{
    float lowFreq_Hz;
    float highFreq_Hz;
    float allowedVariationPercent;
} gnssMeasureSpeedRange_t;

typedef struct {
    float speed_Hz;
    uint32_t time_ms;
} speed_t;

typedef struct
{
    speed_t previous;
    speed_t current;
    uint32_t fixtime_ms;
} gnssMeasureSpeed_t;

bool Measurement_DoMeasurements(bool KeepGnssOn);

#endif /* MEASUREMENT_H_ */


#ifdef __cplusplus
}
#endif