#ifdef __cplusplus
extern "C" {
#endif

/*
 * DacWaveform.h
 *
 *  Created on: 17 Aug 2016
 *      Author: Bart Willemse
 */

#ifndef DACWAVEFORM_H_
#define DACWAVEFORM_H_

#include <stdint.h>
#include <stdbool.h>
#include "Resources.h"

bool Dac_Enable(void);
void Dac_Disable(void);
void Dac_On(uint16_t nOffset_mV);
void Dac_Off(void);
bool IsDacSystemEn();
bool IsRequestOffsetValid(uint16_t* pnOffset_mV);
void Dac_SetSwTrigger(uint16_t nExpOutFreq_Hz);

#ifdef PDB_DEBUG_EN
uint32_t GetPDBTimerValue();
uint32_t GetPDBSCValue();
uint32_t GetPDBMODValue();
#endif

#endif // DACWAVEFORM_H_


#ifdef __cplusplus
}
#endif