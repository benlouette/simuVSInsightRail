#ifdef __cplusplus
extern "C" {
#endif

/*
 * xTaskAppRtc.h
 *
 *  Created on: 24 Feb 2016
 *      Author: Rex Taylor
 *
 *  Application task interface definition specific to internal RTC
 *
 */

#ifndef SOURCES_APP_XTASKAPPRTC_H_
#define SOURCES_APP_XTASKAPPRTC_H_

//bool GetRtcTime();
//bool ConfigureRtcWakeup(uint32_t sleepPeriodMinutes);
void ConfigureRTC();
//bool ConfigureRtcSetTime();
bool calculateTimeToNextWakeup(uint32_t nextWakeupMinutesSinceMidnight, uint16_t* minutesToWakeup);

#endif	/* SOURCES_APP_XTASKAPPRTC_H_*/



#ifdef __cplusplus
}
#endif