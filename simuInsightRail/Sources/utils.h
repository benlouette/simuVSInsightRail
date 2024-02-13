#ifdef __cplusplus
extern "C" {
#endif


#ifndef UTILS_H_
#define UTILS_H_

#include "stdint.h"
#include "fsl_rtc_hal.h"

void ConvertGnssUtc2Rtc(float, uint32_t, rtc_datetime_t*);

#endif /* UTILS_H_ */


#ifdef __cplusplus
}
#endif