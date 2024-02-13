#ifdef __cplusplus
extern "C" {
#endif


#include "utils.h"

/**
 * utils.c includes threadsafe utility functions that can be used to call from
 * anywhere.
 */

/**
 * Convert GNSS UTC time to RTC time format
 *
 * @param[in]       gnssTime - gnss time, 'hhmmss.sss' format
 * @param[in]       gnssDate - gnss date, 'ddmmyy' format
 * @param[in,out]   p_rtcTime - pointer to rtc_datetime_t structure
 */
void ConvertGnssUtc2Rtc(float gnssTime, uint32_t gnssDate, rtc_datetime_t *p_rtcTime)
{
   uint32_t tmpi = 0;

   tmpi = gnssTime;// no rounding, makes it complex if 59 becomes 60

   p_rtcTime->second = tmpi % 100; tmpi /= 100;
   p_rtcTime->minute = tmpi % 100; tmpi /= 100;
   p_rtcTime->hour   = tmpi % 24;

   tmpi = gnssDate;
   if (0 == tmpi)
   {
      p_rtcTime->year   =  1970;
      p_rtcTime->month  =  1;
      p_rtcTime->day    =  1;
   }
   else
   {
      p_rtcTime->year   =  2000 + tmpi % 100; tmpi /= 100;
      p_rtcTime->month  =  tmpi % 100; tmpi /= 100;
      p_rtcTime->day    =  tmpi % 100;
   }
}


#ifdef __cplusplus
}
#endif