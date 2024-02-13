#ifdef __cplusplus
extern "C" {
#endif

/*
 * UT_pmic.c
 *
 *  Created on: 13 Jan 2020
 *      Author: KC2663
 */

#include "pmic.h"
#include "UnitTest.h"
#include "NvmConfig.h"
#include "NvmData.h"
#include "Vbat.h"

extern tNvmData gNvmData;

static uint8_t nStoredState;
static uint16_t nStoredFlags;

static int storeAlarmState(void)
{
	Vbat_GetFlags(&nStoredFlags);
	Vbat_GetAlarmState(&nStoredState);

	return 0;
}

static int restoreAlarmState(void)
{
	Vbat_SetFlag(nStoredFlags);
	Vbat_SetAlarmState(nStoredState);

	return 0;
}

void checkCMLowerLimit()
{
	const PMIC_TemperatureAlarm_t stcPmicTempAlarmMsg =
	{
			.cm_alarm = CM_TEMPERATURE_LOW,
			.pcb_alarm = PCB_TEMPERATURE_OK,
			.stcTemperatureData.local.high = 84,
			.stcTemperatureData.local.low = 0,
			.stcTemperatureData.remote.high = 20, //-44C
			.stcTemperatureData.remote.low = 0
	};

	CU_ASSERT(PMIC_checkTemperatureAlarm(stcPmicTempAlarmMsg) == true);
	CU_ASSERT(Vbat_IsFlagSet(VBATRF_FLAG_LOW_TEMPERATURE_LIMIT_TRIG) == true);
}

void checkPCBLowerLimit()
{
	const PMIC_TemperatureAlarm_t stcPmicTempAlarmMsg =
	{
			.pcb_alarm = PCB_TEMPERATURE_LOW,
			.cm_alarm = CM_TEMPERATURE_OK,
			.stcTemperatureData.local.high = 20, //-44C
			.stcTemperatureData.local.low = 0,
			.stcTemperatureData.remote.high = 84,
			.stcTemperatureData.remote.low = 0
	};

	CU_ASSERT(PMIC_checkTemperatureAlarm(stcPmicTempAlarmMsg) == true);
	CU_ASSERT(Vbat_IsFlagSet(VBATRF_FLAG_LOW_TEMPERATURE_LIMIT_TRIG) == true);
}

void checkCMUpperLimit()
{
	const PMIC_TemperatureAlarm_t stcPmicTempAlarmMsg =
	{
			.cm_alarm = CM_TEMPERATURE_HIGH,
			.pcb_alarm = PCB_TEMPERATURE_OK,
			.stcTemperatureData.local.high = 84,
			.stcTemperatureData.local.low = 0,
			.stcTemperatureData.remote.high = 155, //91C
			.stcTemperatureData.remote.low = 0
	};

	CU_ASSERT(PMIC_checkTemperatureAlarm(stcPmicTempAlarmMsg) == true);
	CU_ASSERT(Vbat_IsFlagSet(VBATRF_FLAG_HIGH_TEMPERATURE_LIMIT_TRIG) == true);
}

void checkPCBUpperLimit()
{
	const PMIC_TemperatureAlarm_t stcPmicTempAlarmMsg =
	{
			.pcb_alarm = PCB_TEMPERATURE_HIGH,
			.cm_alarm = CM_TEMPERATURE_OK,
			.stcTemperatureData.local.high = 155, //91C
			.stcTemperatureData.local.low = 0,
			.stcTemperatureData.remote.high = 84,
			.stcTemperatureData.remote.low = 0
	};

	CU_ASSERT(PMIC_checkTemperatureAlarm(stcPmicTempAlarmMsg) == true);
	CU_ASSERT(Vbat_IsFlagSet(VBATRF_FLAG_HIGH_TEMPERATURE_LIMIT_TRIG) == true);
}

void checkCMAmberAlarm()
{
	const PMIC_TemperatureAlarm_t stcPmicTempAlarmMsg =
	{
			.cm_alarm = CM_TEMPERATURE_AMBER,
			.pcb_alarm = PCB_TEMPERATURE_OK,
			.stcTemperatureData.local.high = 84,
			.stcTemperatureData.local.low = 0,
			.stcTemperatureData.remote.high = 129, //65C
			.stcTemperatureData.remote.low = 0
	};

	CU_ASSERT(PMIC_checkTemperatureAlarm(stcPmicTempAlarmMsg) == true);
	CU_ASSERT(Vbat_IsFlagSet(VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG) == true);
}

void checkCMRedAlarm()
{
	const PMIC_TemperatureAlarm_t stcPmicTempAlarmMsg =
	{
			.cm_alarm = CM_TEMPERATURE_RED,
			.pcb_alarm = PCB_TEMPERATURE_OK,
			.stcTemperatureData.local.high = 84,
			.stcTemperatureData.local.low = 0,
			.stcTemperatureData.remote.high = 139, //75C
			.stcTemperatureData.remote.low = 0
	};

	CU_ASSERT(PMIC_checkTemperatureAlarm(stcPmicTempAlarmMsg) == true);
	CU_ASSERT(Vbat_IsFlagSet(VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG) == true);
}

void checkInvalidCode()
{
	const PMIC_TemperatureAlarm_t stcPmicTempAlarmMsg =
	{
			.cm_alarm = 8,
			.stcTemperatureData.local.high = 155, //91C
			.stcTemperatureData.local.low = 0,
			.stcTemperatureData.remote.high = 84,
			.stcTemperatureData.remote.low = 0
	};

	CU_ASSERT(PMIC_checkTemperatureAlarm(stcPmicTempAlarmMsg) == false);
}

CUnit_suite_t UTpmic = {
	{ "PMIC", storeAlarmState, restoreAlarmState, CU_TRUE, "test pmic handling functions" },
	{
		{"check CM lower limit exceeded", checkCMLowerLimit},
		{"check PCB lower limit exceeded", checkPCBLowerLimit},
		{"check CM upper limit exceeded", checkCMUpperLimit},
		{"check PCB upper limit exceeded", checkPCBUpperLimit},
		{"check CM Amber Alarm", checkCMAmberAlarm},
		{"check CM Red Alarm", checkCMRedAlarm},
		{"check invalid code", checkInvalidCode},
		{ NULL, NULL }
	}
};


#ifdef __cplusplus
}
#endif