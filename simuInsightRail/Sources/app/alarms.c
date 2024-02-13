#ifdef __cplusplus
extern "C" {
#endif

/*
 * alarms.c
 *
 *  Created on: 16 Mar 2021
 *      Author: RZ8556
 */

#include <stdbool.h>
#include <stdint.h>

#include "errorcodes.h"
#include "selftest.h"
#include "log.h"
#include "DrvTmp431.h"
#include "NvmConfig.h"
#include "NvmData.h"
#include "xTaskApp.h"
#include "vbat.h"
#include "alarms.h"

extern tNvmCfg gNvmCfg;
extern tNvmData gNvmData;
extern report_t gReport;
extern bool lastCommsOk;

static void resetAttemptsCounters(const VBAT_DATA_INDEX_t attempts_index);
static bool checkAlarmRetries(const VBAT_DATA_INDEX_t attempts_index, const VBAT_DATA_INDEX_t day_counter_index);

/*
 * alarms_CheckCounters
 *
 * @desc    Called every 10 minute wake
 *
 * @param   none
 *
 * @return  true if no vbat errors
 */
bool alarms_CheckCounters()
{
	uint8_t count;
	bool bOk_Alarms;

	if((bOk_Alarms = Vbat_GetByte(VBAT_DATA_INDX_24_HOUR_COUNTER_ALARMS, &count)))
	{
		if(count > 0)
		{
			bOk_Alarms &= Vbat_SetByte(VBAT_DATA_INDX_24_HOUR_COUNTER_ALARMS, --count);
		}
		else
		{
			bOk_Alarms &= Vbat_ClearFlag(VBATRF_FLAG_AMBER_MASKED + VBATRF_FLAG_RED_MASKED);
		}
	}

	bool bOk_Limits;
	if((bOk_Limits = Vbat_GetByte(VBAT_DATA_INDX_24_HOUR_COUNTER_LIMITS, &count)))
	{
		if(count > 0)
		{
			bOk_Limits &= Vbat_SetByte(VBAT_DATA_INDX_24_HOUR_COUNTER_LIMITS, --count);
		}
		else
		{
			bOk_Limits &= Vbat_ClearFlag(VBATRF_FLAG_LOW_TEMPERATURE_LIMIT_MASKED + VBATRF_FLAG_HIGH_TEMPERATURE_LIMIT_MASKED);
		}
	}

	return bOk_Alarms && bOk_Limits;
}


/*
 * alarms_SetLockouts
 *
 * @desc    Called when comms is ok - reset any alarms and initiate lockout timer(s)
 *
 * @param   none
 *
 * @return  true if no vbat error
 */
bool alarms_SetLockouts()
{
	// set lockout counter(s) based on which alarm it was
	uint16_t flags;
	if(!Vbat_GetFlags(&flags))
	{
		return false;
	}

	bool bOk = true;
	if((flags & VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG) || (flags & VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG))
	{
		bOk = Vbat_SetByte(VBAT_DATA_INDX_24_HOUR_COUNTER_ALARMS, CM_ALARM_LOCKOUT_COUNT);

		if(flags & VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG)
		{
			bOk &= Vbat_SetFlag(VBATRF_FLAG_RED_MASKED);
		}

		if(flags & VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG)
		{
			bOk &= Vbat_SetFlag(VBATRF_FLAG_AMBER_MASKED);
		}

		// Clear the temperature alarm flags.
		bOk &= Vbat_ClearFlag(VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG + VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG);
	}

	if((flags & VBATRF_FLAG_LOW_TEMPERATURE_LIMIT_TRIG) || (flags & VBATRF_FLAG_HIGH_TEMPERATURE_LIMIT_TRIG))
	{
		bOk &= Vbat_SetByte(VBAT_DATA_INDX_24_HOUR_COUNTER_LIMITS, LIMITS_ALARM_LOCKOUT_COUNT);

		if(flags & VBATRF_FLAG_LOW_TEMPERATURE_LIMIT_TRIG)
		{
			bOk &= Vbat_SetFlag(VBATRF_FLAG_LOW_TEMPERATURE_LIMIT_MASKED);
		}

		if(flags & VBATRF_FLAG_HIGH_TEMPERATURE_LIMIT_TRIG)
		{
			bOk &= Vbat_SetFlag(VBATRF_FLAG_HIGH_TEMPERATURE_LIMIT_MASKED);
		}

		// Clear the temperature limit flags.
		bOk &= Vbat_ClearFlag(VBATRF_FLAG_LOW_TEMPERATURE_LIMIT_TRIG + VBATRF_FLAG_HIGH_TEMPERATURE_LIMIT_TRIG);
	}

	(void)Vbat_GetFlags(&flags);
	LOG_DBG(LOG_LEVEL_APP,"NodeTempAlarmStatus: %d (0 - No temp alarms)\n", (flags & (VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG |
																					  VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG |
																					  VBATRF_FLAG_LOW_TEMPERATURE_LIMIT_TRIG |
																					  VBATRF_FLAG_HIGH_TEMPERATURE_LIMIT_TRIG)));

	return bOk;
}


/*
 * alarms_NewLimitAlarm
 *
 * @desc    Check if a temperature limit alarm should be generated.
 *
 * @param   pointer to simulated temp (NULL if none)
 *
 * @return - true if an alarm needs to be sent
 */
bool alarms_NewLimitAlarm(SimulatedTemperature_t* pSimulated)
{
	bool bIsLocalTempSimulated = false, bIsRemoteTempSimulated = false;
	bool bTrigLimitAlarm = false;

	if(pSimulated)
	{
		bIsLocalTempSimulated = pSimulated->flags & VBATRF_local_is_simulated;
		bIsRemoteTempSimulated = pSimulated->flags & VBATRF_remote_is_simulated;
	}

	// is condition monitoring lockout in effect? (either 24 hour delay to resend or blank, does not matter)
	uint8_t val = 0;
	(void)Vbat_GetByte(VBAT_DATA_INDX_24_HOUR_COUNTER_LIMITS, &val);
	if (val > 0)
	{
		return false;
	}

	uint16_t flags;
	bool flagsReadOk = Vbat_GetFlags(&flags);

	if(((gReport.tmp431.localTemp >= SENSOR_OPERATING_TEMP_LIMIT_HIGH_DEG_C) &&
	    (bIsLocalTempSimulated || tmp431Valid(TMP431_LOCAL, &gReport.tmp431.localTemp))) ||
		((gReport.tmp431.remoteTemp >= SENSOR_OPERATING_TEMP_LIMIT_HIGH_DEG_C) &&
		(bIsRemoteTempSimulated || tmp431Valid(TMP431_REMOTE, &gReport.tmp431.remoteTemp))))
	{
		LOG_DBG(LOG_LEVEL_APP,"No previous temp. high limit alarms..\n");

		// new alarm?
		if(!(flags & VBATRF_FLAG_HIGH_TEMPERATURE_LIMIT_TRIG) || !flagsReadOk)
		{
			if(Vbat_SetFlag(VBATRF_FLAG_HIGH_TEMPERATURE_LIMIT_TRIG))
			{
				bTrigLimitAlarm = true;
			    LOG_EVENT(eLOG_TEMP_HIGH_EXCEEDED, LOG_NUM_APP, ERRLOGWARN, ">>>> HIGH limit Temperature Alarm");
			}
			// setup retries in case we need them
			resetAttemptsCounters(VBAT_DATA_INDX_LIMIT_ATTEMPTS);
		}
	}

	if(((gReport.tmp431.localTemp <= SENSOR_OPERATING_TEMP_LIMIT_LOW_DEG_C) &&
		(bIsLocalTempSimulated || tmp431Valid(TMP431_LOCAL, &gReport.tmp431.localTemp))) ||
		((gReport.tmp431.remoteTemp <= SENSOR_OPERATING_TEMP_LIMIT_LOW_DEG_C) &&
		(bIsRemoteTempSimulated || tmp431Valid(TMP431_REMOTE, &gReport.tmp431.remoteTemp))))
	{
		LOG_DBG(LOG_LEVEL_APP,"No previous temp. low limit alarms..\n");

		// new alarm?
		if(!(flags & VBATRF_FLAG_LOW_TEMPERATURE_LIMIT_TRIG)  || !flagsReadOk)
		{
			if(Vbat_SetFlag(VBATRF_FLAG_LOW_TEMPERATURE_LIMIT_TRIG))
			{
				bTrigLimitAlarm = true;
				LOG_EVENT(eLOG_TEMP_LOW_EXCEEDED, LOG_NUM_APP, ERRLOGWARN, ">>>> LOW limit Temperature Alarm");
			}
			resetAttemptsCounters(VBAT_DATA_INDX_LIMIT_ATTEMPTS);
		}
	}

	// If there is an outstanding alarm condition, which we did not communicate last time, check for retry
	if((lastCommsOk == false) && (flags & (VBATRF_FLAG_HIGH_TEMPERATURE_LIMIT_TRIG | VBATRF_FLAG_LOW_TEMPERATURE_LIMIT_TRIG)))
	{
		if((bTrigLimitAlarm = checkAlarmRetries(VBAT_DATA_INDX_LIMIT_ATTEMPTS, VBAT_DATA_INDX_24_HOUR_COUNTER_LIMITS)))
		{
			LOG_EVENT(eLOG_TEMP_COMMS_RETRY, LOG_NUM_APP, ERRLOGWARN, "Last comms failed, reattempting temp limit alarm");
		}
		else
		{
			uint8_t wakes_remaining;
			(void)Vbat_GetByte(VBAT_DATA_INDX_24_HOUR_COUNTER_LIMITS, &wakes_remaining);

			LOG_DBG(LOG_LEVEL_APP, "\nLast comms failed, next temp alarm attempt in %d wakes\n", wakes_remaining);		}
	}

	if(bTrigLimitAlarm)
	{
		if(bIsRemoteTempSimulated)
		{
			LOG_EVENT(eLOG_WAKEUP + WAKEUP_CAUSE_THERMOSTAT, LOG_NUM_APP, ERRLOGWARN, "Remote temperature is simulated! (%d)", pSimulated->remote);
		}
		if(bIsLocalTempSimulated)
		{
			LOG_EVENT(eLOG_WAKEUP + WAKEUP_CAUSE_THERMOSTAT, LOG_NUM_APP, ERRLOGWARN, "Local temperature is simulated! (%d)", pSimulated->local);
		}
	}

	return bTrigLimitAlarm;
}


/*
 * alarms_IsTemperatureAlarm
 *
 * @desc    Check if the CM temperature alarm should be generated.
 *
 * @param   float *remoteTemp_degC: pointer to remote temperature (may be overwritten if there is a problem with sensor read)
 * 			const bool simulatedTemp: pass in true if we are using simulated remote temp and therefore should not check chip
 *
 * @return - true if an alarm needs to be sent
 */
bool alarms_IsTemperatureAlarm(float *remoteTemp_degC, const bool bIsSimulatedTemp)
{
	bool bTrigTempAlarm = false;
	uint16_t flags;

	(void)Vbat_GetFlags(&flags);
	const bool bIsRedTempAlarm = flags & VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG;
	const bool bIsAmberTempAlarm = flags & VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG;

	// If this is a new alarm (and also consider case where we are in 24 hour lockout)
	if((bIsRedTempAlarm == false) && (bIsAmberTempAlarm == false))
	{
		// is condition monitoring lockout in effect?
		uint8_t val = 0;
		(void)Vbat_GetByte(VBAT_DATA_INDX_24_HOUR_COUNTER_ALARMS, &val);
		if (val == 0)
		{
			LOG_DBG(LOG_LEVEL_APP,"No previous temp. alarms..\n");

			if((*remoteTemp_degC >= gNvmCfg.dev.measureConf.Temperature_Alarm_Lower_Limit) &&
			   (*remoteTemp_degC < gNvmCfg.dev.measureConf.Temperature_Alarm_Upper_Limit) &&
			   (bIsSimulatedTemp || tmp431Valid(TMP431_REMOTE, remoteTemp_degC)))
			{
				 if(Vbat_SetFlag(VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG))
				 {
					 bTrigTempAlarm = true;
					 // NOTE LOG_EVENT uses vsnprintf to format the string. Using the %f format does
					 // not work so round up the temperature to an integer and use that for formatting
					 LOG_EVENT(eLOG_TEMP_AMBER, LOG_NUM_APP, ERRLOGWARN, ">>>> AMBER Temperature Alarm @ %d degC", (int)*remoteTemp_degC);
				 }

			}
			else if((*remoteTemp_degC >= gNvmCfg.dev.measureConf.Temperature_Alarm_Upper_Limit) &&
					(bIsSimulatedTemp || tmp431Valid(TMP431_REMOTE, remoteTemp_degC)))
			{
				if(Vbat_SetFlag(VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG))
				{
					bTrigTempAlarm = true;
					LOG_EVENT(eLOG_TEMP_RED1, LOG_NUM_APP, ERRLOGWARN, ">>>> RED Temperature Alarm Set @ %d degC", (int)*remoteTemp_degC);
				}
			}

			// setup retries in case we need them
			resetAttemptsCounters(VBAT_DATA_INDX_ALARM_ATTEMPTS);
		}
		else
		{
			// if we are masked we only have to consider case that an Amber alarm did the masking
			if((flags & VBATRF_FLAG_AMBER_MASKED) && !(flags & VBATRF_FLAG_RED_MASKED))
			{
				if(*remoteTemp_degC >= gNvmCfg.dev.measureConf.Temperature_Alarm_Upper_Limit)
				{
					if(Vbat_SetFlag(VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG))
					{
						bTrigTempAlarm = true;
						LOG_EVENT(eLOG_TEMP_RED2, LOG_NUM_APP, ERRLOGWARN, ">>>> RED Temperature Alarm Set @ %d degC", (int)*remoteTemp_degC);
					}
					resetAttemptsCounters(VBAT_DATA_INDX_ALARM_ATTEMPTS);
					(void)Vbat_ClearFlag(VBATRF_FLAG_AMBER_MASKED);
				}
			}
		}
	}
	else
	// we have an alarm already - check if Amber now becomes Red
	{
		LOG_DBG(LOG_LEVEL_APP,"\nPrevious temp. alarm Status, Red:%d, Amber:%d\n", bIsRedTempAlarm, bIsAmberTempAlarm);

		if ((*remoteTemp_degC >= gNvmCfg.dev.measureConf.Temperature_Alarm_Upper_Limit) &&
			(bIsRedTempAlarm == false) &&
			(bIsSimulatedTemp || tmp431Valid(TMP431_REMOTE, remoteTemp_degC)))
		{
			if(Vbat_SetFlag(VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG))
			{
				bTrigTempAlarm = true;
				LOG_EVENT(eLOG_TEMP_RED2, LOG_NUM_APP, ERRLOGWARN, ">>>> RED Temperature Alarm Set @ %d degC", (int)*remoteTemp_degC);
			}
			resetAttemptsCounters(VBAT_DATA_INDX_ALARM_ATTEMPTS);
		}

		// trigger an alarm if the last comms failed and if we are due to retry
		else if (lastCommsOk == false)
		{
			if(checkAlarmRetries(VBAT_DATA_INDX_ALARM_ATTEMPTS, VBAT_DATA_INDX_24_HOUR_COUNTER_ALARMS))
			{
				LOG_EVENT(eLOG_TEMP_COMMS_RETRY, LOG_NUM_APP, ERRLOGWARN, "Last comms failed, reattempting temp alarm");
				bTrigTempAlarm = true;
			}
			else
			{
				uint8_t wakes_remaining;
				(void)Vbat_GetByte(VBAT_DATA_INDX_24_HOUR_COUNTER_ALARMS, &wakes_remaining);

				LOG_DBG(LOG_LEVEL_APP, "\nLast comms failed, next temp alarm attempt in %d wakes\n", wakes_remaining);
			}
		}
	}

	return bTrigTempAlarm;
}


/*
 * alarms_RetryInProgress
 *
 * @desc    Return status of alarm retries
 *
 * @param   none
 *
 * @return  true if alarm retries are pending
 */
bool alarms_RetryInProgress()
{
	uint8_t attempts;
	(void)Vbat_GetByte(VBAT_DATA_INDX_ALARM_ATTEMPTS, &attempts);

	return attempts != 0;
}


/*
 *  alarm retries are packed into byte:
 *  7 6 5 4 3 2 1 0
 *  ===============
 *          x x x x   Attempt 2
 *    x x x           Attempt 3
 *  x				  Attempts Used
 */

#define SECOND_ATTEMPT(x) 	(x & 0x0F)
#define THIRD_ATTEMPT(x) 	((x >> 4) & 0x07)

#define DECREMENT_SECOND(x) (x -= (1<<0))
#define DECREMENT_THIRD(x) 	(x -= (1<<4))
#define ATTEMPTS_USED_FLAG 	(0x80)
#define ATTEMPTS_USED(x)	((x & ATTEMPTS_USED_FLAG) == ATTEMPTS_USED_FLAG)

#define SECOND_ATTEMPT_INIT  (0b0001)
#define THIRD_ATTEMPT_INIT 	(0b10000)

#define DAILY_RETRY_COUNT	(144) // 24 hours for 10 minute wakes

// call on 'new' alarm
static void resetAttemptsCounters(const VBAT_DATA_INDEX_t attempts_index)
{
	(void)Vbat_SetByte(attempts_index, SECOND_ATTEMPT_INIT | THIRD_ATTEMPT_INIT);
}

/*
 * Resets the temperature alarms
 */
void alarms_ResetTemperatureAlarms()
{
	(void)Vbat_SetByte(VBAT_DATA_INDX_24_HOUR_COUNTER_ALARMS, 0);
	(void)Vbat_SetByte(VBAT_DATA_INDX_24_HOUR_COUNTER_LIMITS, 0);
	Vbat_ClearFlag(VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG + VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG +
					VBATRF_FLAG_LOW_TEMPERATURE_LIMIT_TRIG + VBATRF_FLAG_HIGH_TEMPERATURE_LIMIT_TRIG);
}

/*
 * This function returns true if it is time to try another alarm or limit commms attempt
 */
static bool checkAlarmRetries(const VBAT_DATA_INDEX_t attempts_index, const VBAT_DATA_INDEX_t day_counter_index)
{
	uint8_t attempts;
	if(!Vbat_GetByte(attempts_index, &attempts))
	{
		return false;
	}

	bool doAlarm = false;

	// only check counters if we haven't already used them up
	if(!ATTEMPTS_USED(attempts))
	{
		// check 'attempt' counters
		if(SECOND_ATTEMPT(attempts) > 0)
		{
			DECREMENT_SECOND(attempts);
			if(SECOND_ATTEMPT(attempts) == 0)
			{
				doAlarm = true;
			}
		} else
		{
			if(THIRD_ATTEMPT(attempts) > 0)
			{
				DECREMENT_THIRD(attempts);
				if(THIRD_ATTEMPT(attempts) == 0)
				{
					doAlarm = true;
					attempts = ATTEMPTS_USED_FLAG;
					// set daily retry
					(void)Vbat_SetByte(day_counter_index, DAILY_RETRY_COUNT);
				}
			}
		}
		// update the counters
		(void)Vbat_SetByte(attempts_index, attempts);
	}
	else
	{
		uint8_t wakes_remaining;
		if(false == Vbat_GetByte(day_counter_index, &wakes_remaining))
		{
			return false;
		}

		// check daily counter
		if(wakes_remaining == 0)
		{
			(void)Vbat_SetByte(day_counter_index, DAILY_RETRY_COUNT);
			doAlarm = true;
		}
	}

	return doAlarm;
}



#ifdef __cplusplus
}
#endif