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