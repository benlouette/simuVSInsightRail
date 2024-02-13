#ifdef __cplusplus
extern "C" {
#endif

/*
 * alarms.h
 *
 *  Created on: 16 Mar 2021
 *      Author: RZ8556
 */

#ifndef SOURCES_APP_ALARMS_H_
#define SOURCES_APP_ALARMS_H_

#include "Vbat.h"

#define SENSOR_OPERATING_TEMP_LIMIT_LOW_DEG_C		(-35.0f)
#define SENSOR_OPERATING_TEMP_LIMIT_HIGH_DEG_C		(80.0f)

#define CM_ALARM_LOCKOUT_COUNT (144)
#define LIMITS_ALARM_LOCKOUT_COUNT (144)

bool alarms_SetLockouts();
bool alarms_RetryInProgress();
bool alarms_CheckCounters();
bool alarms_NewLimitAlarm(SimulatedTemperature_t* pSimulated);
bool alarms_IsTemperatureAlarm(float *remoteTemp_degC, const bool simulatedTemp);
void alarms_ResetTemperatureAlarms(void);

#endif /* SOURCES_APP_ALARMS_H_ */


#ifdef __cplusplus
}
#endif