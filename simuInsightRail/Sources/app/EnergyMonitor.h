#ifdef __cplusplus
extern "C" {
#endif

/*
 * EnergyMonitor.h
 *
 *  Created on: 12/02/2018
 *      Author: Jayant Rampuria
 */

#ifndef ENERGYMONITOR_H_
#define ENERGYMONITOR_H_

#define POWER_PACK_MAX_ENERGY												(83000.0f)		// Joules
#define CONVERT_ENERGY_USED_IN_JOULES_TO_ENERGY_REM_IN_PERCENT(EnJoules)	(((POWER_PACK_MAX_ENERGY - (EnJoules)) * 100.0f) / POWER_PACK_MAX_ENERGY)
#define	CONVERT_ENERGY_REM_IN_PERCENT_TO_ENERGY_USED_JOULES(EnPercent)		(POWER_PACK_MAX_ENERGY - (POWER_PACK_MAX_ENERGY * (EnPercent) / 100.0f))
#define DURATION_MSECS(s)													(EnergyMonitor_GetDurationMsecs(s))

bool EnergyMonitor_ReadDCLevel_v(float *pfVolts);
bool EnergyMonitor_GetEnergyConsumed_J(float* pfTotalEnergy, float* pfCurrentRun);
bool EnergyMonitor_GetEnergyConsumedThisCycle_J(float* pfJoules);
bool EnergyMonitor_Report(void);
uint32_t EnergyMonitor_GetDurationMsecs(uint32_t nStartTicks); //#TODO Move this generic functions to utils!

#endif /* SOURCES_APP_ENERGYMONITOR_H_ */


#ifdef __cplusplus
}
#endif