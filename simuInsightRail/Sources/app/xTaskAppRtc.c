#ifdef __cplusplus
extern "C" {
#endif

/*
 * xTaskAppRtc.c
 *
 *  Created on: 24 Feb 2016
 *      Author: Rex Taylor
 *
 *  Application task interface definition specific to internal RTC
 *
 */

#include <FreeRTOS.h>
#include "string.h"


#include "fsl_rtc_driver.h"
#include "rtc.h"

#include "log.h"

#include "xTaskAppRtc.h"


// TODO move these two functions to schedule.c and delete xTaskAppRtc.*
void ConfigureRTC()
{
	RTC_Type *base = g_rtcBase[RTC_IDX];
	RTC_SET_CR(base, 0x0300);		// Clock output CLKO is not output to other peripherals
									// Oscillator Enable OSCE is enabled
	RTC_SET_CR(base, 0x0002);		// Wakeup Pin Enabled WPE is enabled
	//RTC_SET_IER(base, 0x0080);
	RTC_SET_IER(base, 0x0002);		// Time overflow Interrupt Enable TAIE does generate and interrupt
}


bool calculateTimeToNextWakeup(uint32_t nextWakeupMinutesSinceMidnight, uint16_t* minutesToWakeup)
{
	bool retval = true;
	uint32_t currentMinutesSinceMidnight = 0x00;
	rtc_datetime_t datetime;

	retval = RtcGetTime(&datetime);

	if (retval) {
	    printf("Current time is %02d:%02d:%02d\r\n", datetime.hour, datetime.minute, datetime.second);

	    currentMinutesSinceMidnight = (datetime.hour * 60) + datetime.minute;

	    if(currentMinutesSinceMidnight == nextWakeupMinutesSinceMidnight)
	    {
	        nextWakeupMinutesSinceMidnight += 5;
	    }

	    if(nextWakeupMinutesSinceMidnight > currentMinutesSinceMidnight)
	    {
	        *minutesToWakeup = nextWakeupMinutesSinceMidnight - currentMinutesSinceMidnight;
	    }
	    else
	    {
	        *minutesToWakeup = (1440 - currentMinutesSinceMidnight) + nextWakeupMinutesSinceMidnight;       // 1440 = 24 * 60
	    }

	}

	return retval;
}





#ifdef __cplusplus
}
#endif