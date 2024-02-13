#ifdef __cplusplus
extern "C" {
#endif

/*
 * UT_Alarms.c
 *
 *  Created on: 19 Mar 2021
 *      Author: RZ8556
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include "UnitTest.h"

#include "alarms.h"
#include "vbat.h"
#include "DrvTmp431.h"
#include "NvmConfig.h"
#include "xtaskApp.h"
#include "NvmData.h"

extern bool cliSetLocalSimulatedTemp(uint32_t argc, uint8_t * argv[], uint32_t * argi);
extern bool cliSetRemoteSimulatedTemp(uint32_t argc, uint8_t * argv[], uint32_t * argi);
extern bool cliClearSimulatedTemps(uint32_t argc, uint8_t * argv[], uint32_t * argi);

static int testAlarmsInit();
static int testAlarmsCleanUp();

static void testAmberAlarmCommsOk();
static void testRedAlarmCommsOk();
static void testAmberAlarmNc();
static void testRedAlarmNc();
static void testAmberAlarmNcRedAlarmCommsOk();
static void testAmberAlarmCommsOkAmberWithinLockout();
static void testRedAlarmCommsOkRedWithinLockout();
static void testAmberAlarmCommsOkRedWithinLockout();
static void testRedAlarmCommsOkAmberWithinLockout();
static void testLowLimitChipCommsOk();
static void testHighLimitChipCommsOk();
static void testLowLimitExternalCommsOk();
static void testHighLimitExternalCommsOk();
static void testLowLimitChipNoComms();
static void testHighLimitChipNoComms();
static void testNoLimitAlarmsBadComms();

extern report_t gReport;
extern tNvmData gNvmData;

extern bool lastCommsOk;

static SimulatedTemperature_t m_SimulatedTemperatures;

/*
 * UTalarms unit test suite definition.
 */
CUnit_suite_t UTalarms =
{
	{ "Alarms", testAlarmsInit, testAlarmsCleanUp, CU_TRUE, ""},
	// Array of test functions.
	{
		{ "Amber alarm, comms ok",									testAmberAlarmCommsOk},
		{ "Red alarm comms ok",										testRedAlarmCommsOk},
		{ "Amber alarm no comms", 									testAmberAlarmNc},
		{ "Red alarm no comms", 									testRedAlarmNc},
		{ "Amber alarm, no comms, move to Red alarm comms ok", 		testAmberAlarmNcRedAlarmCommsOk},
		{ "Amber alarm comms ok, amber alarm within 24 hours", 		testAmberAlarmCommsOkAmberWithinLockout},
		{ "Red alarm comms ok, red alarm within 24 hours", 			testRedAlarmCommsOkRedWithinLockout},
		{ "Amber alarm comms ok, red alarm within 24 hours", 		testAmberAlarmCommsOkRedWithinLockout},
		{ "Red alarm comms ok, amber alarm within 24 hours",		testRedAlarmCommsOkAmberWithinLockout},
		{ "Low limit chip alarm comms ok", 							testLowLimitChipCommsOk},
		{ "High limit chip alarm comms ok", 						testHighLimitChipCommsOk},
		{ "Low limit ext alarm comms ok", 							testLowLimitExternalCommsOk},
		{ "High limit ext alarm comms ok",	 						testHighLimitExternalCommsOk},
		{ "Low limit chip alarm no comms", 							testLowLimitChipNoComms},
		{ "High limit chip alarm no comms",							testHighLimitChipNoComms},
		{ "No limit alarms no comms",								testNoLimitAlarmsBadComms},
		{ NULL, NULL }
	}
};

extern tNvmCfg gNvmCfg;
extern uint32_t dbg_logging;

static float LocalTemp, RemoteTemp;
static uint32_t save_debug;
static float savedAmberThreshold, savedRedThreshold;

static int testAlarmsInit()
{
	cliClearSimulatedTemps(0, NULL, NULL);
	save_debug = dbg_logging;
	dbg_logging = 0;
	alarms_ResetTemperatureAlarms();
	ReadTmp431Temperatures(&LocalTemp, &RemoteTemp);

	savedAmberThreshold = gNvmCfg.dev.measureConf.Temperature_Alarm_Lower_Limit;
	savedRedThreshold =	gNvmCfg.dev.measureConf.Temperature_Alarm_Upper_Limit;

	gNvmCfg.dev.measureConf.Temperature_Alarm_Lower_Limit = LocalTemp + 10.0f;
	gNvmCfg.dev.measureConf.Temperature_Alarm_Upper_Limit = gNvmCfg.dev.measureConf.Temperature_Alarm_Lower_Limit + 10.0f;

	return 0;
}


static int testAlarmsCleanUp()
{
	gNvmCfg.dev.measureConf.Temperature_Alarm_Lower_Limit = savedAmberThreshold;
	gNvmCfg.dev.measureConf.Temperature_Alarm_Upper_Limit = savedRedThreshold ;
	dbg_logging = save_debug;

	return 0;
}

/*
 * Condition Monitoring alarm tests
 */

// helper function for checking after alarm/limit is tested
static void testAfterCheck(VBATRF_FLAGS_BITMASK_t active_flag, VBATRF_FLAGS_BITMASK_t inactive_flag)
{
	// check flags
	uint16_t flags;
	CU_ASSERT_TRUE(Vbat_GetFlags(&flags));
	CU_ASSERT_FALSE(flags & inactive_flag);
	CU_ASSERT_TRUE(flags & active_flag);

	// check attempts
	uint8_t attempts;
	switch(active_flag)
	{
	case VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG:
	case VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG:
		CU_ASSERT_TRUE(Vbat_GetByte(VBAT_DATA_INDX_ALARM_ATTEMPTS, &attempts));
		break;

	case VBATRF_FLAG_LOW_TEMPERATURE_LIMIT_TRIG:
	case VBATRF_FLAG_HIGH_TEMPERATURE_LIMIT_TRIG:
		CU_ASSERT_TRUE(Vbat_GetByte(VBAT_DATA_INDX_LIMIT_ATTEMPTS, &attempts));
		break;

	default:
		CU_ASSERT_FATAL(0);
		break;
	}
	CU_ASSERT_EQUAL((attempts & 0b01111111), 0b00010001);

	// when successful comms, call this
	CU_ASSERT_TRUE(alarms_SetLockouts());

	// check alarm cleared
	CU_ASSERT_TRUE(Vbat_GetFlags(&flags));
	CU_ASSERT_FALSE(flags & active_flag);
	CU_ASSERT_FALSE(flags & inactive_flag);

	// check lockout is in place
	switch(active_flag)
	{
	case VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG:
		CU_ASSERT_TRUE(flags & VBATRF_FLAG_AMBER_MASKED);
		break;

	case VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG:
		CU_ASSERT_TRUE(flags & VBATRF_FLAG_RED_MASKED);
		break;

	case VBATRF_FLAG_LOW_TEMPERATURE_LIMIT_TRIG:
		CU_ASSERT_TRUE(flags & VBATRF_FLAG_LOW_TEMPERATURE_LIMIT_MASKED);
		break;

	case VBATRF_FLAG_HIGH_TEMPERATURE_LIMIT_TRIG:
		CU_ASSERT_TRUE(flags & VBATRF_FLAG_HIGH_TEMPERATURE_LIMIT_MASKED);
		break;

	default:
		CU_ASSERT_FATAL(0);
		break;
	}

	uint8_t counter;
	if((active_flag == VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG) || (active_flag == VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG))
	{
		CU_ASSERT_TRUE(Vbat_GetByte(VBAT_DATA_INDX_24_HOUR_COUNTER_ALARMS, &counter));
		CU_ASSERT_EQUAL(counter, CM_ALARM_LOCKOUT_COUNT);
	}
	else
	{
		CU_ASSERT_TRUE(Vbat_GetByte(VBAT_DATA_INDX_24_HOUR_COUNTER_LIMITS, &counter));
		CU_ASSERT_EQUAL(counter, LIMITS_ALARM_LOCKOUT_COUNT);
	}

}

// helper function for connection ok case
static void testAlarmOk(VBATRF_FLAGS_BITMASK_t active_flag, VBATRF_FLAGS_BITMASK_t inactive_flag)
{
	CU_ASSERT_FATAL(((active_flag == VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG) &&
					 (inactive_flag == VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG)) ||
					((inactive_flag == VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG) &&
					(active_flag == VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG)));

	// check for alarm generated
	CU_ASSERT_TRUE(alarms_IsTemperatureAlarm(&RemoteTemp, false));
	testAfterCheck(active_flag, inactive_flag);
}


static void testAmberAlarmCommsOk()
{
	float saved_threshold = gNvmCfg.dev.measureConf.Temperature_Alarm_Lower_Limit;
	gNvmCfg.dev.measureConf.Temperature_Alarm_Lower_Limit = RemoteTemp - 5.0F;

	testAlarmOk(VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG,
				VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG);

	// clear lockout for tidiness
	CU_ASSERT_TRUE(Vbat_SetByte(VBAT_DATA_INDX_24_HOUR_COUNTER_ALARMS, 0));
	gNvmCfg.dev.measureConf.Temperature_Alarm_Lower_Limit = saved_threshold;
}


static void testRedAlarmCommsOk()
{
	float saved_threshold = gNvmCfg.dev.measureConf.Temperature_Alarm_Upper_Limit;
	gNvmCfg.dev.measureConf.Temperature_Alarm_Upper_Limit = RemoteTemp - 5.0F;

	testAlarmOk(VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG,
				VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG);

	// clear lockout for tidiness
	CU_ASSERT_TRUE(Vbat_SetByte(VBAT_DATA_INDX_24_HOUR_COUNTER_ALARMS, 0));
	gNvmCfg.dev.measureConf.Temperature_Alarm_Upper_Limit = saved_threshold;
}


// helper function for no connection case
static void testAlarmNoConnect(VBATRF_FLAGS_BITMASK_t active_flag, VBATRF_FLAGS_BITMASK_t inactive_flag)
{
	CU_ASSERT_FATAL(((active_flag == VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG) &&
					(inactive_flag == VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG)) ||
					((inactive_flag == VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG) &&
					(active_flag == VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG)));

	// check for alarm generated
	CU_ASSERT_TRUE(alarms_IsTemperatureAlarm(&RemoteTemp, false));

	// check flags
	uint16_t flags;
	CU_ASSERT(Vbat_GetFlags(&flags));
	CU_ASSERT(!(flags & inactive_flag));
	CU_ASSERT(flags & active_flag);

	// check attempts
	uint8_t attempts;
	CU_ASSERT_TRUE(Vbat_GetByte(VBAT_DATA_INDX_ALARM_ATTEMPTS, &attempts));
	CU_ASSERT_EQUAL(attempts, 0b00010001);

	// do comms fail
	CU_ASSERT_TRUE(alarms_IsTemperatureAlarm(&RemoteTemp, false));

	CU_ASSERT_TRUE(Vbat_GetByte(VBAT_DATA_INDX_ALARM_ATTEMPTS, &attempts));
	CU_ASSERT_EQUAL(attempts, 0b00010000);

	// do comms fail again
	CU_ASSERT_TRUE(alarms_IsTemperatureAlarm(&RemoteTemp, false));

	CU_ASSERT_TRUE(Vbat_GetByte(VBAT_DATA_INDX_ALARM_ATTEMPTS, &attempts));
	CU_ASSERT_EQUAL(attempts, 0b10000000);

	// check retry setup correctly
	uint8_t counter;
	CU_ASSERT_TRUE(Vbat_GetByte(VBAT_DATA_INDX_24_HOUR_COUNTER_ALARMS, &counter));
	CU_ASSERT_EQUAL(counter, CM_ALARM_LOCKOUT_COUNT);

	// use the 24 hours
	for(uint8_t ten_minute = 1; ten_minute <= CM_ALARM_LOCKOUT_COUNT; ten_minute++)
	{
		//Check counter on 'wakeup'
		CU_ASSERT_TRUE(alarms_CheckCounters())

		CU_ASSERT_TRUE(Vbat_GetByte(VBAT_DATA_INDX_24_HOUR_COUNTER_ALARMS, &counter));
		CU_ASSERT_EQUAL(counter, (CM_ALARM_LOCKOUT_COUNT - ten_minute));

		if(ten_minute == CM_ALARM_LOCKOUT_COUNT)
		{
			CU_ASSERT_TRUE(alarms_IsTemperatureAlarm(&RemoteTemp, false));
		}
		else
		{
			CU_ASSERT_FALSE(alarms_IsTemperatureAlarm(&RemoteTemp, false));
		}
	}

	CU_ASSERT_TRUE(Vbat_GetByte(VBAT_DATA_INDX_24_HOUR_COUNTER_ALARMS, &counter));
	CU_ASSERT_EQUAL(counter, CM_ALARM_LOCKOUT_COUNT);

	// check it's still alarmed and then clear
	CU_ASSERT_TRUE(Vbat_GetFlags(&flags));
	CU_ASSERT_FALSE(flags & inactive_flag);
	CU_ASSERT_TRUE(flags & active_flag);
	CU_ASSERT_TRUE(Vbat_ClearFlag(active_flag));

	// clear counter
	CU_ASSERT(Vbat_SetByte(VBAT_DATA_INDX_24_HOUR_COUNTER_ALARMS, 0));
}


static void testAmberAlarmNc()
{
	float saved_threshold = gNvmCfg.dev.measureConf.Temperature_Alarm_Lower_Limit;

	// set alarm threshold below temp
	gNvmCfg.dev.measureConf.Temperature_Alarm_Lower_Limit = RemoteTemp - 5.0;
	testAlarmNoConnect(VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG, VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG);

	// restore alarm threshold
	gNvmCfg.dev.measureConf.Temperature_Alarm_Lower_Limit = saved_threshold;
}


static void testRedAlarmNc()
{
	float saved_threshold = gNvmCfg.dev.measureConf.Temperature_Alarm_Upper_Limit;

	// set alarm threshold below temp
	gNvmCfg.dev.measureConf.Temperature_Alarm_Upper_Limit = RemoteTemp - 5.0;
	testAlarmNoConnect(VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG, VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG);

	// restore alarm threshold
	gNvmCfg.dev.measureConf.Temperature_Alarm_Upper_Limit = saved_threshold;
}


// helper to clear down lockout flags
static void clearLockouts()
{
	uint8_t counter;

	CU_ASSERT_TRUE(Vbat_GetByte(VBAT_DATA_INDX_24_HOUR_COUNTER_ALARMS, &counter));
	for(int i=0; i<=counter; i++)
	{
		CU_ASSERT_TRUE(alarms_CheckCounters());
	}

	uint16_t flags;
	CU_ASSERT_TRUE(Vbat_GetFlags(&flags));
	CU_ASSERT_FALSE(flags & VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG);
	CU_ASSERT_FALSE(flags & VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG);
	CU_ASSERT_FALSE(flags & VBATRF_FLAG_RED_MASKED);
	CU_ASSERT_FALSE(flags & VBATRF_FLAG_AMBER_MASKED);
}


// checks that an Amber, while in retries, is 'upgraded' to a red
static void testAmberAlarmNcRedAlarmCommsOk()
{
	float saved_lower_limit = gNvmCfg.dev.measureConf.Temperature_Alarm_Lower_Limit;
	float saved_upper_limit = gNvmCfg.dev.measureConf.Temperature_Alarm_Upper_Limit;

	// set alarm threshold below temp
	gNvmCfg.dev.measureConf.Temperature_Alarm_Lower_Limit = RemoteTemp - 5.0;

	// check for amber alarm generated
	CU_ASSERT_TRUE(alarms_IsTemperatureAlarm(&RemoteTemp, false));

	// check flags
	uint16_t flags;
	CU_ASSERT_TRUE(Vbat_GetFlags(&flags));
	CU_ASSERT_FALSE(flags & VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG);
	CU_ASSERT_TRUE(flags & VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG);

	// check attempts
	uint8_t attempts;
	CU_ASSERT_TRUE(Vbat_GetByte(VBAT_DATA_INDX_ALARM_ATTEMPTS, &attempts));
	CU_ASSERT_EQUAL(attempts, 0b00010001);

	// do comms fail
	CU_ASSERT_TRUE(alarms_IsTemperatureAlarm(&RemoteTemp, false));

	CU_ASSERT_TRUE(Vbat_GetByte(VBAT_DATA_INDX_ALARM_ATTEMPTS, &attempts));
	CU_ASSERT_EQUAL(attempts, 0b00010000);

	// now change red limit
	gNvmCfg.dev.measureConf.Temperature_Alarm_Upper_Limit = RemoteTemp - 5.0;
	CU_ASSERT_TRUE(alarms_IsTemperatureAlarm(&RemoteTemp, false));

	// check Red alarm now
	CU_ASSERT_TRUE(Vbat_GetFlags(&flags));
	CU_ASSERT_TRUE(flags & VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG);
	CU_ASSERT_TRUE(flags & VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG);

	// check attempts
	CU_ASSERT_TRUE(Vbat_GetByte(VBAT_DATA_INDX_ALARM_ATTEMPTS, &attempts));
	CU_ASSERT_EQUAL(attempts, 0b00010001);

	// when successful comms, call this
	CU_ASSERT_TRUE(alarms_SetLockouts());

	// check alarm cleared
	CU_ASSERT_TRUE(Vbat_GetFlags(&flags));
	CU_ASSERT_FALSE(flags & VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG);
	CU_ASSERT_FALSE(flags & VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG);

	// check lockout is in place
	uint8_t counter;
	CU_ASSERT_TRUE(Vbat_GetByte(VBAT_DATA_INDX_24_HOUR_COUNTER_ALARMS, &counter));
	CU_ASSERT_EQUAL(counter, CM_ALARM_LOCKOUT_COUNT);

	// clear lockout for tidiness
	clearLockouts();

	gNvmCfg.dev.measureConf.Temperature_Alarm_Lower_Limit = saved_lower_limit;
	gNvmCfg.dev.measureConf.Temperature_Alarm_Upper_Limit = saved_upper_limit;
}


static void testAmberAlarmCommsOkAmberWithinLockout()
{
	float saved_lower_limit = gNvmCfg.dev.measureConf.Temperature_Alarm_Lower_Limit;
	gNvmCfg.dev.measureConf.Temperature_Alarm_Lower_Limit = RemoteTemp - 5.0F;

	// raise the Amber
	testAlarmOk(VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG, VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG);

	// check for alarm generated - should return flase as we are in alarm lockout
	CU_ASSERT_FALSE(alarms_IsTemperatureAlarm(&RemoteTemp, false));

	// check alarm flags still cleared
	uint16_t flags;
	CU_ASSERT_TRUE(Vbat_GetFlags(&flags));
	CU_ASSERT_FALSE(flags & VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG);
	CU_ASSERT_FALSE(flags & VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG);

	// clear lockout for tidiness
	clearLockouts();
	gNvmCfg.dev.measureConf.Temperature_Alarm_Lower_Limit = saved_lower_limit;
}


static void testRedAlarmCommsOkRedWithinLockout()
{
	float saved_upper_limit = gNvmCfg.dev.measureConf.Temperature_Alarm_Upper_Limit;
	gNvmCfg.dev.measureConf.Temperature_Alarm_Upper_Limit = RemoteTemp - 5.0F;

	// raise the Red
	testAlarmOk(VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG, VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG);

	// check for alarm generated - should return false as we are in alarm lockout
	CU_ASSERT_FALSE(alarms_IsTemperatureAlarm(&RemoteTemp, false));

	// check alarm flags still cleared
	uint16_t flags;
	CU_ASSERT_TRUE(Vbat_GetFlags(&flags));
	CU_ASSERT_FALSE(flags & VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG);
	CU_ASSERT_FALSE(flags & VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG);

	// clear lockout for tidiness
	clearLockouts();
	gNvmCfg.dev.measureConf.Temperature_Alarm_Upper_Limit = saved_upper_limit;
}


static void testAmberAlarmCommsOkRedWithinLockout()
{
	float saved_lower_limit = gNvmCfg.dev.measureConf.Temperature_Alarm_Lower_Limit;
	gNvmCfg.dev.measureConf.Temperature_Alarm_Lower_Limit = RemoteTemp - 5.0F;

	// create an Amber alarm
	testAlarmOk(VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG,
				VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG);

	// now do a Red alarm
	float saved_upper_limit = gNvmCfg.dev.measureConf.Temperature_Alarm_Upper_Limit;
	gNvmCfg.dev.measureConf.Temperature_Alarm_Upper_Limit = RemoteTemp - 5.0F;

	CU_ASSERT_TRUE(alarms_IsTemperatureAlarm(&RemoteTemp, false));

	// check we now have a red
	uint16_t flags;
	CU_ASSERT_TRUE(Vbat_GetFlags(&flags));
	CU_ASSERT_TRUE(flags & VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG);
	CU_ASSERT_FALSE(flags & VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG);

	// check attempts
	uint8_t attempts;
	CU_ASSERT_TRUE(Vbat_GetByte(VBAT_DATA_INDX_ALARM_ATTEMPTS, &attempts));
	CU_ASSERT_EQUAL((attempts & 0b01111111), 0b00010001);

	// when successful comms, call this
	CU_ASSERT_TRUE(alarms_SetLockouts());

	// check alarm cleared
	CU_ASSERT_TRUE(Vbat_GetFlags(&flags));
	CU_ASSERT_FALSE(flags & VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG);
	CU_ASSERT_FALSE(flags & VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG);

	// check lockout is in place
	uint8_t counter;
	CU_ASSERT_TRUE(Vbat_GetByte(VBAT_DATA_INDX_24_HOUR_COUNTER_ALARMS, &counter));
	CU_ASSERT_EQUAL(counter, CM_ALARM_LOCKOUT_COUNT);

	// clear lockout for tidiness
	clearLockouts();
	gNvmCfg.dev.measureConf.Temperature_Alarm_Lower_Limit = saved_lower_limit;
	gNvmCfg.dev.measureConf.Temperature_Alarm_Upper_Limit = saved_upper_limit;
}


static void testRedAlarmCommsOkAmberWithinLockout()
{
	float saved_upper_limit = gNvmCfg.dev.measureConf.Temperature_Alarm_Upper_Limit;
	gNvmCfg.dev.measureConf.Temperature_Alarm_Upper_Limit = RemoteTemp - 5.0F;

	// create a Red alarm
	testAlarmOk(VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG,
				VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG);

	// now try an Amber
	float saved_lower_limit = gNvmCfg.dev.measureConf.Temperature_Alarm_Lower_Limit;
	gNvmCfg.dev.measureConf.Temperature_Alarm_Lower_Limit = RemoteTemp - 5.0F;

	// we expect this one to not raise an alarm
	CU_ASSERT_FALSE(alarms_IsTemperatureAlarm(&RemoteTemp, false));

	// check neither alarm is asserted
	uint16_t flags;
	CU_ASSERT_TRUE(Vbat_GetFlags(&flags));
	CU_ASSERT_FALSE(flags & VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG);
	CU_ASSERT_FALSE(flags & VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG);

	// clear lockout for tidiness
	clearLockouts();
	gNvmCfg.dev.measureConf.Temperature_Alarm_Lower_Limit = saved_lower_limit;
	gNvmCfg.dev.measureConf.Temperature_Alarm_Upper_Limit = saved_upper_limit;
}


/*
 * High/low limit tests
 */

// we clone this from private vbat function, don't want anyone calling this by accident..
static void SetSimulatedTemperature()
{
	if(m_SimulatedTemperatures.flags & VBATRF_local_is_simulated)
	{
		CU_ASSERT_TRUE(Vbat_SetByte(VBAT_DATA_INDX_PCB_SIM_TEMP, m_SimulatedTemperatures.local));
		CU_ASSERT_TRUE(Vbat_SetByte(VBAT_DATA_INDX_PCB_SIM_TEMP + 1, ~m_SimulatedTemperatures.local));
	}

	if(m_SimulatedTemperatures.flags & VBATRF_remote_is_simulated)
	{
		CU_ASSERT_TRUE(Vbat_SetByte(VBAT_DATA_INDX_EXT_SIM_TEMP, m_SimulatedTemperatures.remote));
		CU_ASSERT_TRUE(Vbat_SetByte(VBAT_DATA_INDX_EXT_SIM_TEMP + 1, ~m_SimulatedTemperatures.remote));
	}
}


static void simulateInternal(const int8_t local)
{
	m_SimulatedTemperatures.remote = RemoteTemp;
	m_SimulatedTemperatures.local = local;
	m_SimulatedTemperatures.flags = VBATRF_local_is_simulated;
	SetSimulatedTemperature();

	const SimulatedTemperature_t const* pSimulated = Vbat_CheckForSimulatedTemperatures();
	CU_ASSERT_TRUE(pSimulated->flags & VBATRF_local_is_simulated);

	// overwrite any simulated temperatures
	gReport.tmp431.localTemp = pSimulated->local;
}


static void simulateExternal(const int8_t remote)
{
	m_SimulatedTemperatures.local = LocalTemp;
	m_SimulatedTemperatures.remote = remote;
	m_SimulatedTemperatures.flags = VBATRF_remote_is_simulated;
	SetSimulatedTemperature();

	const SimulatedTemperature_t const* pSimulated = Vbat_CheckForSimulatedTemperatures();
	CU_ASSERT_TRUE(pSimulated->flags & VBATRF_remote_is_simulated);

	// overwrite any simulated temperatures
	gReport.tmp431.remoteTemp = pSimulated->remote;
}


// helper function for connection ok case
static void testLimitAlarmOk(VBATRF_FLAGS_BITMASK_t active_flag, VBATRF_FLAGS_BITMASK_t inactive_flag)
{
	CU_ASSERT_FATAL(((active_flag == VBATRF_FLAG_LOW_TEMPERATURE_LIMIT_TRIG) &&
					 (inactive_flag == VBATRF_FLAG_HIGH_TEMPERATURE_LIMIT_TRIG)) ||
					((active_flag == VBATRF_FLAG_HIGH_TEMPERATURE_LIMIT_TRIG) &&
					(inactive_flag == VBATRF_FLAG_LOW_TEMPERATURE_LIMIT_TRIG)));

	// check for alarm generated
	testAfterCheck(active_flag, inactive_flag);
}


static void testLowLimitChipCommsOk()
{
	// make it too cold (chip sensor)
	simulateInternal(SENSOR_OPERATING_TEMP_LIMIT_LOW_DEG_C - 5);

	lastCommsOk = true;

	// check for limit alarm generated as part of wakeup detection logic
	CU_ASSERT_TRUE(alarms_NewLimitAlarm(&m_SimulatedTemperatures));

	testLimitAlarmOk(VBATRF_FLAG_LOW_TEMPERATURE_LIMIT_TRIG, VBATRF_FLAG_HIGH_TEMPERATURE_LIMIT_TRIG);

	// clear lockout for tidiness
	CU_ASSERT_TRUE(Vbat_SetByte(VBAT_DATA_INDX_24_HOUR_COUNTER_LIMITS, 0));

	// exit sim mode - limits
	Vbat_ClearSimulatedTemperatures();
}


static void testHighLimitChipCommsOk()
{
	// make it too hot (chip sensor)
	simulateInternal(SENSOR_OPERATING_TEMP_LIMIT_HIGH_DEG_C + 5);

	lastCommsOk = true;

	// check for limit alarm generated as part of wakeup detection logic
	CU_ASSERT_TRUE(alarms_NewLimitAlarm(&m_SimulatedTemperatures));

	testLimitAlarmOk(VBATRF_FLAG_HIGH_TEMPERATURE_LIMIT_TRIG, VBATRF_FLAG_LOW_TEMPERATURE_LIMIT_TRIG);

	// clear lockout for tidiness
	CU_ASSERT_TRUE(Vbat_SetByte(VBAT_DATA_INDX_24_HOUR_COUNTER_LIMITS, 0));

	// exit sim mode - limits
	Vbat_ClearSimulatedTemperatures();
}


static void testLowLimitExternalCommsOk()
{
	// make it too cold (ext sensor)
	simulateExternal(SENSOR_OPERATING_TEMP_LIMIT_LOW_DEG_C - 5);

	lastCommsOk = true;

	// check for limit alarm generated as part of wakeup detection logic
	CU_ASSERT_TRUE(alarms_NewLimitAlarm(&m_SimulatedTemperatures));

	testLimitAlarmOk(VBATRF_FLAG_LOW_TEMPERATURE_LIMIT_TRIG, VBATRF_FLAG_HIGH_TEMPERATURE_LIMIT_TRIG);

	// clear lockout for tidiness
	CU_ASSERT_TRUE(Vbat_SetByte(VBAT_DATA_INDX_24_HOUR_COUNTER_LIMITS, 0));

	// exit sim mode - limits
	Vbat_ClearSimulatedTemperatures();
}


static void testHighLimitExternalCommsOk()
{
	// make it too hot (ext sensor)
	simulateExternal(SENSOR_OPERATING_TEMP_LIMIT_HIGH_DEG_C + 5);

	lastCommsOk = true;

	// check for limit alarm generated as part of wakeup detection logic
	CU_ASSERT_TRUE(alarms_NewLimitAlarm(&m_SimulatedTemperatures));

	testLimitAlarmOk(VBATRF_FLAG_HIGH_TEMPERATURE_LIMIT_TRIG, VBATRF_FLAG_LOW_TEMPERATURE_LIMIT_TRIG);

	// clear lockout for tidiness
	CU_ASSERT_TRUE(Vbat_SetByte(VBAT_DATA_INDX_24_HOUR_COUNTER_LIMITS, 0));

	// exit sim mode - limits
	Vbat_ClearSimulatedTemperatures();
}


// helper function for no comms case
static void testLimitAlarmFailComms(VBATRF_FLAGS_BITMASK_t active_flag, VBATRF_FLAGS_BITMASK_t inactive_flag)
{
	// check flags
	uint16_t flags;
	CU_ASSERT(Vbat_GetFlags(&flags));
	CU_ASSERT(!(flags & inactive_flag));
	CU_ASSERT(flags & active_flag);

	// check attempts
	uint8_t attempts;
	CU_ASSERT_TRUE(Vbat_GetByte(VBAT_DATA_INDX_LIMIT_ATTEMPTS, &attempts));
	CU_ASSERT_EQUAL(attempts, 0b00010001);

	// do comms fail
	SimulatedTemperature_t* pSimulated = Vbat_CheckForSimulatedTemperatures();
	CU_ASSERT_TRUE(alarms_NewLimitAlarm(pSimulated));

	CU_ASSERT_TRUE(Vbat_GetByte(VBAT_DATA_INDX_LIMIT_ATTEMPTS, &attempts));
	CU_ASSERT_EQUAL(attempts, 0b00010000);

	// do comms fail again
	CU_ASSERT_TRUE(alarms_NewLimitAlarm(pSimulated));

	CU_ASSERT_TRUE(Vbat_GetByte(VBAT_DATA_INDX_LIMIT_ATTEMPTS, &attempts));
	CU_ASSERT_EQUAL(attempts, 0b10000000);

	// check retry setup correctly
	uint8_t counter;
	CU_ASSERT_TRUE(Vbat_GetByte(VBAT_DATA_INDX_24_HOUR_COUNTER_LIMITS, &counter));
	CU_ASSERT_EQUAL(counter, 144);

	// use the 24 hours
	for(uint8_t ten_minute = 0; ten_minute <= LIMITS_ALARM_LOCKOUT_COUNT; ten_minute++)
	{
		CU_ASSERT_TRUE(Vbat_GetByte(VBAT_DATA_INDX_24_HOUR_COUNTER_LIMITS, &counter));
		CU_ASSERT_EQUAL(counter, (LIMITS_ALARM_LOCKOUT_COUNT - ten_minute));
		if(ten_minute == LIMITS_ALARM_LOCKOUT_COUNT)
		{
			CU_ASSERT_TRUE(alarms_NewLimitAlarm(pSimulated));
		}
		else
		{
			CU_ASSERT_FALSE(alarms_NewLimitAlarm(pSimulated));
			CU_ASSERT_TRUE(alarms_CheckCounters());
		}
	}

	CU_ASSERT_TRUE(Vbat_GetByte(VBAT_DATA_INDX_24_HOUR_COUNTER_LIMITS, &counter));
	CU_ASSERT_EQUAL(counter, LIMITS_ALARM_LOCKOUT_COUNT);

	// check it's still alarmed and then clear
	CU_ASSERT_TRUE(Vbat_GetFlags(&flags));
	CU_ASSERT_FALSE(flags & inactive_flag);
	CU_ASSERT_TRUE(flags & active_flag);
	CU_ASSERT_TRUE(Vbat_ClearFlag(active_flag));

	// clear counter
	CU_ASSERT(Vbat_SetByte(VBAT_DATA_INDX_24_HOUR_COUNTER_LIMITS, 0));
}


static void testLowLimitChipNoComms()
{
	Vbat_ClearFlag(VBATRF_FLAG_LOW_TEMPERATURE_LIMIT_TRIG + VBATRF_FLAG_HIGH_TEMPERATURE_LIMIT_TRIG);

	// make it too cold (chip sensor)
	simulateInternal(SENSOR_OPERATING_TEMP_LIMIT_LOW_DEG_C - 5);

	lastCommsOk = true;

	// check for limit alarm generated as part of wakeup detection logic
	CU_ASSERT_TRUE(alarms_NewLimitAlarm(&m_SimulatedTemperatures));

	lastCommsOk = false;
	testLimitAlarmFailComms(VBATRF_FLAG_LOW_TEMPERATURE_LIMIT_TRIG, VBATRF_FLAG_HIGH_TEMPERATURE_LIMIT_TRIG);

	// exit sim mode - limits
	Vbat_ClearSimulatedTemperatures();
}


static void testHighLimitChipNoComms()
{
	Vbat_ClearFlag(VBATRF_FLAG_LOW_TEMPERATURE_LIMIT_TRIG + VBATRF_FLAG_HIGH_TEMPERATURE_LIMIT_TRIG);

	// make it too hot (chip sensor)
	simulateInternal(SENSOR_OPERATING_TEMP_LIMIT_HIGH_DEG_C + 5);

	lastCommsOk = true;

	// check for limit alarm generated as part of wakeup detection logic
	CU_ASSERT_TRUE(alarms_NewLimitAlarm(&m_SimulatedTemperatures));

	lastCommsOk = false;
	testLimitAlarmFailComms(VBATRF_FLAG_HIGH_TEMPERATURE_LIMIT_TRIG, VBATRF_FLAG_LOW_TEMPERATURE_LIMIT_TRIG);

	// exit sim mode - limits
	Vbat_ClearSimulatedTemperatures();
}


static void testNoLimitAlarmsBadComms()
{
	Vbat_ClearFlag(VBATRF_FLAG_LOW_TEMPERATURE_LIMIT_TRIG + VBATRF_FLAG_HIGH_TEMPERATURE_LIMIT_TRIG);

	lastCommsOk = false;

	// check for no limit alarm generated
	CU_ASSERT_FALSE(alarms_NewLimitAlarm(NULL));
}




#ifdef __cplusplus
}
#endif