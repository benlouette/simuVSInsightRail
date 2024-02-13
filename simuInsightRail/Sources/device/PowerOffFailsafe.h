#ifdef __cplusplus
extern "C" {
#endif

/*
 * PowerOffFailsafe.h
 *
 *  Created on: 5 September 2016
 *      Author: Bart Willemse
 */

#ifndef POWEROFFFAILSAFE_H_
#define POWEROFFFAILSAFE_H_

//..............................................................................

#include <stdint.h>
#include <stdbool.h>

//..............................................................................

// POWEROFFFAILSAFE_MAX_SECS is calculated by the maximum FlexTimer counter
// value (0xFFFF) divided by 1/2 of the counter clock rate (due to using
// the up-down counter approach). This is 524 secs, or about 8.7 minutes.
#define POWEROFFFAILSAFE_MAX_SECS   ((uint16_t)(0xFFFF / (250 / 2)))

//..............................................................................

bool PowerOffFailsafe_Start(uint16_t Secs);
void PowerOffFailsafe_Stop(void);
bool PowerOffFailsafe_IsRunning(void);

//..............................................................................

#endif // POWEROFFFAILSAFE_H_


#ifdef __cplusplus
}
#endif