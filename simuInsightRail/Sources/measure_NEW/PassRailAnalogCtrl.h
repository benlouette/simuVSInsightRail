#ifdef __cplusplus
extern "C" {
#endif

/*
 * PassRailAnalogCtrl.h
 *
 *  Created on: July 1, 2016
 *      Author: Bart Willemse
 */

#ifndef PASSRAILANALOGCTRL_H_
#define PASSRAILANALOGCTRL_H_

#include <stdint.h>
#include <stdbool.h>
#include "PinDefs.h"
#include "Resources.h"

//..............................................................................

typedef enum
{
    ANALOGFILT_UNDEFINED = 0,
    ANALOGFILT_RAW,
    ANALOGFILT_VIB,
    ANALOGFILT_WFLATS   // NOTE: Not available on rev 1 board
} AnalogFiltEnum;

//..............................................................................

void AnalogGainSelect(bool bGainOn);
bool AnalogFiltSelect(AnalogFiltEnum FiltSelect);
void AnalogSelfTestSelect(bool bSelfTestOn);

//..............................................................................

#endif // PASSRAILANALOGCTRL_H_




#ifdef __cplusplus
}
#endif