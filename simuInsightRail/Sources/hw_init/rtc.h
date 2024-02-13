#ifdef __cplusplus
extern "C" {
#endif

/*
 * rtc.h
 *
 *  Created on: 11 Jan 2016
 *      Author: BF1418
 */

#ifndef SOURCES_HW_INIT_RTC_H_
#define SOURCES_HW_INIT_RTC_H_

#include "fsl_rtc_hal.h"

#define RTC_CLOCK_STATUS_RUNNING 	(1)
#define RTC_CLOCK_STATUS_ACCURATE 	(2)
#define RTC_CLOCK_STATUS_YEAR_VALID (4)
#define PMIC_RTC_OK (RTC_CLOCK_STATUS_RUNNING | RTC_CLOCK_STATUS_ACCURATE | RTC_CLOCK_STATUS_YEAR_VALID)

#define MAX_ALLOWED_RTC_DIFF_SECONDS (60*60)

bool InitRtc(uint32_t);

bool RtcGetDatetimeInSecs( uint32_t *seconds );
bool RtcSetTime(rtc_datetime_t* pDatetime);
bool RtcGetTime(rtc_datetime_t* pDatetime);
bool RtcSetAlarm(uint32_t seconds);
bool GetRtcAlarmTime(rtc_datetime_t* pDatetime);
bool ConfigureRtcWakeup(uint32_t sleepPeriodMinutes);
void ConfigureRtcWakeupNS(uint32_t sleepPeriodMinutes);
bool RtcHasAlarmOccurred(bool* pbAlarm);
void DisableRtcCounterAlarmInt(uint32_t nRtcInstance);
int RtcGetAdjustmentSpan();
char *RtcUTCToString(uint32_t secs);
char *RtcDatetimeToString(rtc_datetime_t datetime);
void RtcClearAlarmFlag();
bool RtcIsAlarmTriggered();
uint32_t RtcGetAlarmTrigAtSecs();
void RtcPerformAlarmTest(uint32_t alarmInSecsFromNow);
void RtcCheckRTC(void);


#endif /* SOURCES_HW_INIT_RTC_H_ */


#ifdef __cplusplus
}
#endif