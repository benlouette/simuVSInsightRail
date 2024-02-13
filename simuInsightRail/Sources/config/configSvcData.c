#ifdef __cplusplus
extern "C" {
#endif

/*
 * configSvcData.c
 *
 *  Created on: 28 mrt. 2016
 *      Author: Daniel van der Velde
 */

/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

//#include "rtcTimer.h"                      // Defines rtc_IDX
#include "fsl_rtc_driver.h"           // Defines g_rtcBase
#include "fsl_rtc_hal.h"              // For RTC register access

#include "log.h"
#include "NvmConfig.h"

/*
 * Macros
 */

/*
 * Functions
 */

/**
 * ConfigSvcData_GetIDEFTime
 *
 * @return IDEF datetime-stamp in 100ns counts since UNIX epoch in UTC.
 */
uint64_t ConfigSvcData_GetIDEFTime(void)
{
    // Retrieve seconds from RTC register
    RTC_Type *rtcBase = g_rtcBase[RTC_IDX];
    uint32_t seconds = rtcBase->TSR;
    uint32_t ms = rtcBase->TPR;

    uint64_t t = (uint64_t)seconds  * 10000000ULL; 	// seconds to 100-nanoseconds
    t += ((uint64_t)((((ms & 0x7fff) / 32) * 1000) / 1024) * 10000);	// not really ms but next best thing

    return t;
}

/*
 * printf out the IDEF time to the CLI, in a format (ISO 8601) which is reasonable universal: YYYY-MM-DDTHH:MM:SS.sssZ
 * This is a Z means GMT/UT
  *
 */
void ConfigSvcData_PrintIDEFTime(uint64_t idefTime)
{
    time_t unixTimeSeconds = idefTime/10000000ULL;
    struct tm result;

    gmtime_r (&unixTimeSeconds, &result );
    printf("%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
            result.tm_year+1900,
            result.tm_mon+1,
            result.tm_mday,
            result.tm_hour,
            result.tm_min,
            result.tm_sec,
            (idefTime % 10000000ULL) /(10000)
            );
}
/**
 * ConfigSvcData_GetSourceId
 * @return Pointer to fixed 16 bytes holding the sourceId
 */
uint8_t *ConfigSvcData_GetSourceId(void)
{
    // TODO Get sourceId from Data Store
    //static uint8_t sourceId[16] = {'S','O','U','R','C','E','I','D','F','R','D','M','K','6','4','F'};

    //return sourceId;
    return gNvmCfg.dev.mqtt.deviceId;
}

#include <time.h>

#define SECS_PER_DAY   (24 * 60 * 60)
#define SECS_PER_HOUR  (60 * 60)
#define SECS_PER_MIN   (60)
#define EPOCH_WDAY     (4)  // Thursday
#define DAYS_PER_WEEK (7)
#define EPOCH_YDAY  (0)
#define EPOCH_LEAP_YEARS  (1970 / 4 - 1)
#define EPOCH_YEAR 1970
#define TM_YEAR_BASE 1900
#define LEAP_YEAR(year) ((year) % 4 == 0 && ((year) % 100 != 0 || (year) % 400 == 0))

static const int __mon_lengths[2][12] = {
    {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},  // Non-leap year
    {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}   // Leap year
};

int gmtime_r(const time_t* timep, struct tm* result) {
    time_t t = *timep;
    long days, rem, y;
    int yday, leap;
    const int* ip;
    int timezone = 0;
    /* Normalize time to seconds since the epoch */
    t -= timezone;  // Adjust for local timezone
    days = t / SECS_PER_DAY;  // Number of days since the epoch
    rem = t % SECS_PER_DAY;   // Remaining seconds in the current day
    if (rem < 0) {
        rem += SECS_PER_DAY;
        days--;
    }
    result->tm_hour = (int)(rem / SECS_PER_HOUR);
    rem %= SECS_PER_HOUR;
    result->tm_min = (int)(rem / SECS_PER_MIN);
    result->tm_sec = (int)(rem % SECS_PER_MIN);
    /* January 1, 1970 was a Thursday */
    result->tm_wday = (int)((EPOCH_WDAY + days) % DAYS_PER_WEEK);
    if (result->tm_wday < 0) result->tm_wday += DAYS_PER_WEEK;

    yday = EPOCH_YDAY + days - (EPOCH_YEAR * 365 + EPOCH_LEAP_YEARS);
    leap = LEAP_YEAR(result->tm_year);
    for (y = EPOCH_YEAR; y <= result->tm_year; y++) {
        yday += LEAP_YEAR(y) ? 366 : 365;
    }
    result->tm_yday = yday;
    ip = __mon_lengths[leap];
    for (result->tm_mon = 0; yday >= ip[result->tm_mon]; result->tm_mon++) {
        yday -= ip[result->tm_mon];
    }
    result->tm_mday = (int)(yday + 1);
    result->tm_year += EPOCH_YEAR - TM_YEAR_BASE;
    result->tm_isdst = 0;
    return 0;
}

#if 0
// currently not used, and need adaptation to different subscribe and publish topic prefixes
/**
 * ConfigSvcData_GetTopicPrefix
 * @return Pointer to
 */
uint8_t *ConfigSvcData_GetTopicPrefix(void)
{

    return gNvmCfg.dev.mqtt.subTopicPrefix;
}
#endif



#ifdef __cplusplus
}
#endif