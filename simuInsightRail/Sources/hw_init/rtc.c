#ifdef __cplusplus
extern "C" {
#endif

/*
 * rtc.c
 *
 *  Created on: 11 Jan 2016
 *      Author: BF1418
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"

#include "CLIio.h"
#include "CLIcmd.h"
#include "printgdf.h"

#include "fsl_rtc_driver.h"
#include "fsl_clock_manager.h"

#include "PowerControl.h"
#include "PinConfig.h"
#include "rtc.h"
#include "log.h"
#include "device.h"
#include "SelfTest.h"
#include "pmic.h"

#define MUTEX_MAXWAIT_MS (300)
#define DATETIME_FORMAT "YYYY/MM/DD HH:MM:SS"

#define MSEC_TO_TICK(msec)  (((uint32_t)(msec)+500uL/(uint32_t)configTICK_RATE_HZ) \
                             *(uint32_t)configTICK_RATE_HZ/1000uL)

#define MUTEX_MAXWAIT_TICKS (MSEC_TO_TICK(MUTEX_MAXWAIT_MS))

extern uint32_t g_nStartTick;

static SemaphoreHandle_t gRtcMutex = NULL;
static int g_nAdjustmentSpan = 0;
static volatile bool g_bRTCAlarmTriggered = false;
static volatile uint32_t g_nAlarmTrigSecs = 0;

static const char kRTCprefix[] = "MK24 RTC ";

static bool RtcSetTimeNoPMICupdate(rtc_datetime_t* pDatetime);

/*
** ===================================================================
**     Interrupt handler : RTC_IRQHandler
**
**     Description :
**         User interrupt service routine.
**     Parameters  : None
**     Returns     : Nothing
** ===================================================================
*/
void RTC_IRQHandler(void)
{
  RTC_DRV_AlarmIntAction(RTC_IDX);
  /* Write your code here ... */
  g_bRTCAlarmTriggered = true;
  g_nAlarmTrigSecs = RTC_HAL_GetSecsReg(g_rtcBase[RTC_IDX]);
}

void RtcClearAlarmFlag()
{
	g_bRTCAlarmTriggered = false;
}

bool RtcIsAlarmTriggered()
{
	return g_bRTCAlarmTriggered;
}

uint32_t RtcGetAlarmTrigAtSecs()
{
	return g_nAlarmTrigSecs;
}

bool InitRtc(uint32_t instance)
{
    // in case we call this again
    if(NULL == gRtcMutex)
    {
    	gRtcMutex = xSemaphoreCreateMutex();
    }

    if (NULL != gRtcMutex)
    {
        RTC_DRV_Init(instance);
        /* Enable seconds interrupt */
        // RTC_DRV_SetSecsIntCmd(instance, true);
        /* RTC counter(if it isn't already running) */
        if(RTC_DRV_IsCounterEnabled(instance) == false)
        {
            RTC_HAL_EnableCounter(g_rtcBase[instance], true);
        }

    	RTC_SET_CR(g_rtcBase[instance], 0x0300);		// CLK0, cllk not output to other peripherals, OSCE, oscillator enabled
    }
    else
    {
    	LOG_EVENT(0, LOG_NUM_APP, ERRLOGMAJOR, "%sinitialise error", kRTCprefix);
    }

    return (NULL != gRtcMutex);
}

/*
 * DisableRtcCounterAlarmInt
 *
 * @brief	Disable the RTC counter and Alarm Interrupt.
 *
 * @param	nRtcInstance (uint32_t) - RTC instance
 *
 * @return	void
 */
void DisableRtcCounterAlarmInt(uint32_t nRtcInstance)
{
	RTC_HAL_SetAlarmReg(g_rtcBase[nRtcInstance], 0x00);		// Clear the SR[TAF]
	RTC_DRV_SetAlarmIntCmd(nRtcInstance, false);			// Disable the Alarm Interrupt.
}

/*
 * RtcGetDatetimeInSecs
 *
 * @desc	gets the RTC time in seconds
 *
 * @returns status
 * 			time in seconds
 */
bool RtcGetDatetimeInSecs( uint32_t *seconds )
{
	uint32_t seconds2 = 0xffffffff;
	uint32_t tries = 0;

	do
	{
		tries++;
		if(tries > 3)
		{
			return false;
		}
		RTC_HAL_GetDatetimeInSecs(g_rtcBase[RTC_IDX], seconds);
		vTaskDelay( 1 /  portTICK_PERIOD_MS );
		RTC_HAL_GetDatetimeInSecs(g_rtcBase[RTC_IDX], &seconds2);
	}
	while( (*seconds != seconds2) );

	return true;
}

/**
 * Set MK24 RTC date/time, also updates PMIC RTC
 */
bool RtcSetTime(rtc_datetime_t* pDatetime)
{
	bool rc_ok = false;

	if(RtcSetTimeNoPMICupdate(pDatetime))
	{
		rc_ok = true;
		if(Device_HasPMIC())
	    {
			uint32_t seconds;
			RTC_HAL_ConvertDatetimeToSecs(pDatetime, &seconds);

			// we set alarm to 0 to indicate that PMIC should not touch alarm setting
			PMIC_sendMK24ParamUpdateMessage((params_t){.group = PARAMS_RTC, .params.rtc = {.time_s = seconds, .alarm_s = 0}});
	    }
	}
	return rc_ok;
}


bool RtcGetTime(rtc_datetime_t* pDatetime)
{
    bool rc_ok = (pdPASS == xSemaphoreTake(gRtcMutex, MUTEX_MAXWAIT_TICKS));
    if (rc_ok)
    {
		uint32_t seconds = 0;
		if( RtcGetDatetimeInSecs( &seconds ) )
		{
			//const uint32_t seconds = RTC_HAL_GetSecsReg(RTC);
			RTC_HAL_ConvertSecsToDatetime(&seconds, pDatetime);
			rc_ok = (pdPASS == xSemaphoreGive(gRtcMutex));
		}
		else
		{
			rc_ok = false;
			LOG_DBG(LOG_LEVEL_CLI,"\n%s:%d  Reading RTC seconds failed\n", __FILE__,__LINE__ );
		}
    }

	return rc_ok;
}


int RtcGetAdjustmentSpan()
{
	return g_nAdjustmentSpan;
}


bool GetRtcAlarmTime(rtc_datetime_t* pDatetime)
{
	bool rc_ok = (pdPASS == xSemaphoreTake(gRtcMutex, MUTEX_MAXWAIT_TICKS));
    if (rc_ok)
    {
        RTC_DRV_GetAlarm(RTC_IDX, pDatetime);
        rc_ok = (pdPASS == xSemaphoreGive(gRtcMutex));
    }

	return rc_ok;
}


bool RtcHasAlarmOccurred(bool* pbAlarm)
{
	bool rc_ok = (pdPASS == xSemaphoreTake(gRtcMutex, MUTEX_MAXWAIT_TICKS));
    if (rc_ok)
    {
        *pbAlarm = RTC_DRV_IsAlarmPending(RTC_IDX);
        rc_ok = (pdPASS == xSemaphoreGive(gRtcMutex));
    }

	return rc_ok;
}


// Program the RTC (internal) counter with new wake up times
// TODO - Look at ways of making sure they are valid wakeup times
bool ConfigureRtcWakeup(uint32_t sleepPeriodMinutes)
{
	bool rc_ok = (pdPASS == xSemaphoreTake(gRtcMutex, MUTEX_MAXWAIT_TICKS));
    if (rc_ok)
    {
    	uint32_t seconds = sleepPeriodMinutes * 60;
    	uint32_t datetimeSeconds = RTC_HAL_GetSecsReg(RTC);

		if(Device_HasPMIC())
		{
			// send PMIC RTC update message
			PMIC_sendMK24ParamUpdateMessage((params_t){.group = PARAMS_RTC,
			.params.rtc = {.time_s = datetimeSeconds, .alarm_s = datetimeSeconds + seconds}});
		}

		/* set alarm in seconds*/
		RTC_HAL_SetAlarmReg(RTC, datetimeSeconds + seconds);

		/* enable Alarm interrupt based */
		RTC_HAL_SetAlarmIntCmd(RTC, true);

		(void)(xSemaphoreGive(gRtcMutex)); // ignore result
	}

	return rc_ok;
}


// Program the RTC (internal) counter with new wake up times
// no semaphore lock
// TODO - Look at ways of making sure they are valid wakeup times
void ConfigureRtcWakeupNS(uint32_t sleepPeriodMinutes)
{
    uint32_t srcClock = CLOCK_SYS_GetRtcFreq(RTC_IDX);
    uint32_t seconds = RTC_HAL_GetSecsReg(RTC);

	if (srcClock != 32768U)
	{
		/* In case the input clock to the RTC counter is not 32KHz, the seconds register will not
		 * increment every second, therefore the seconds register value needs to be adjusted.
		 * to get actual seconds. We then add the prescaler register value to the seconds.
		 */
		uint64_t tmp = (uint64_t)seconds << 15U;
		tmp |= (uint64_t)(RTC_HAL_GetPrescaler(RTC) & 0x7FFFU);
		seconds = tmp / srcClock;
	}

	// add on seconds to next wakeup
	seconds += (sleepPeriodMinutes * 60);

    /* set alarm in seconds*/
    RTC_HAL_SetAlarmReg(RTC, seconds);

    /* Activate or deactivate the Alarm interrupt based on user choice */
    RTC_HAL_SetAlarmIntCmd(RTC, true);
}


/**
 * @brief   Format UTC seconds into a string
 *
 * @param[in]  seconds - UTC time in seconds
 *
 * @return - string in the format "YYYY/MM/DD HH:MM:SS"
 */
char *RtcUTCToString(uint32_t seconds)
{
	static char string[sizeof(DATETIME_FORMAT)];
	rtc_datetime_t datetime;

	RTC_HAL_ConvertSecsToDatetime(&seconds, &datetime);
	sprintf(string, "%04d/%02d/%02d %02d:%02d:%02d",
			datetime.year, datetime.month, datetime.day,
			datetime.hour, datetime.minute, datetime.second);
	return string;
}

/**
 * @brief    Format rtc_datetime_t to string
 *
 * @param[in]  datetime in rtc_datetime_t format
 *
 * @return - string in the format "YYYY/MM/DD HH:MM:SS"
 */
char *RtcDatetimeToString(rtc_datetime_t datetime)
{
	static char string[sizeof(DATETIME_FORMAT)];

	sprintf(string, "%04d/%02d/%02d %02d:%02d:%02d",
			datetime.year, datetime.month, datetime.day,
			datetime.hour, datetime.minute, datetime.second);
	return string;
}

/*
 * RTCPerformAlarmTest
 *
 * @desc    Checks if the RTC Alarm is functioning correctly
 *
 * @param   alarmInSecsFromNow - relative time (in secs) to trigger the alarm
 *
 * @return - void
 */
void RtcPerformAlarmTest(uint32_t alarmInSecsFromNow)
{
	RtcClearAlarmFlag();
	/* set alarm in seconds*/
	RTC_HAL_SetAlarmReg(RTC, RTC_HAL_GetSecsReg(RTC) + alarmInSecsFromNow);
	/* enable Alarm interrupt based */
	RTC_HAL_SetAlarmIntCmd(RTC, true);
}


/*
 * RtcCheckRTC
 *
 * @desc    Checks if the RTC appears to be ok,
 *          and fix from PMIC RTC if that is appropriate
 *
 * @param    none
 *
 * @return - void
 */
void RtcCheckRTC(void)
{
	bool bRTCok = true;
	rtc_datetime_t mk24Datetime;

	LOG_DBG(LOG_LEVEL_APP, "Checking RTC, MK24 vs PMIC\n");

	if(!RtcGetTime(&mk24Datetime))
	{
		LOG_DBG(LOG_LEVEL_APP, "Unable to get MK24 current datetime\n");
		return;
	}

	LOG_DBG(LOG_LEVEL_APP, "MK24 current datetime: %s\n", RtcDatetimeToString(mk24Datetime));
	PMIC_RTCstatus_t pmic_rtc = PMIC_GetPMICrtc();
	if (PMIC_RTC_OK == pmic_rtc.rtcStatus)
	{
		LOG_DBG(LOG_LEVEL_APP, "PMIC current datetime: %s\n", RtcUTCToString(pmic_rtc.rtcSeconds));
	}
	else
	{
		LOG_DBG(LOG_LEVEL_APP, "PMIC current datetime: %s, but error so cannot be used\n", RtcUTCToString(pmic_rtc.rtcSeconds));
	}
	
	if(mk24Datetime.year < 2020)
	{
		LOG_EVENT(0, LOG_NUM_APP, ERRLOGWARN, "%sInvalid year detected: %d", kRTCprefix, mk24Datetime.year);
		bRTCok = false;
	}

	if(abs((int)RTC_HAL_GetSecsReg(RTC) - (int)(pmic_rtc.rtcSeconds)) > MAX_ALLOWED_RTC_DIFF_SECONDS)
	{
		LOG_EVENT(0, LOG_NUM_APP, ERRLOGWARN, "%sdrift exceeded %d seconds", kRTCprefix, MAX_ALLOWED_RTC_DIFF_SECONDS);
		bRTCok = false;
	}

	// fix it, call version of time setting that DOES NOT update PMIC!
	if(!bRTCok && (pmic_rtc.rtcStatus == PMIC_RTC_OK))
	{
		LOG_EVENT(0, LOG_NUM_APP, ERRLOGWARN, "%sset to %s", kRTCprefix, RtcUTCToString(pmic_rtc.rtcSeconds));
		RTC_HAL_ConvertSecsToDatetime(&pmic_rtc.rtcSeconds, &mk24Datetime);
		(void)RtcSetTimeNoPMICupdate(&mk24Datetime);
	}
}


/*
 * Set date/time, but do not update PMIC
 */
static bool RtcSetTimeNoPMICupdate(rtc_datetime_t* pDatetime)
{
	bool rc_ok = (pdPASS == (xSemaphoreTake(gRtcMutex, MSEC_TO_TICK(MUTEX_MAXWAIT_MS))));
	if (rc_ok)
	{
		uint32_t oldSeconds = RTC_HAL_GetSecsReg(RTC);
	    RTC_DRV_SetDatetime(RTC_IDX, pDatetime);
	    uint32_t newSeconds = RTC_HAL_GetSecsReg(RTC);
	    rc_ok = (pdPASS == xSemaphoreGive(gRtcMutex));

		g_nAdjustmentSpan += (newSeconds - oldSeconds);

		if(abs(newSeconds - oldSeconds) >= (60 * 60))
		{
			char old[sizeof(DATETIME_FORMAT)], new[sizeof(DATETIME_FORMAT)];
			strcpy(old, RtcUTCToString(oldSeconds));
			strcpy(new, RtcUTCToString(newSeconds));
			LOG_EVENT(0, LOG_NUM_APP, ERRLOGWARN, "Large delta setting RTC, old=%s  new=%s", old, new);
		}
	}
	return rc_ok;
}





#ifdef __cplusplus
}
#endif