#ifdef __cplusplus
extern "C" {
#endif

/*
 * selfTest.h
 *
 *  Created on: 17 Aug 2017
 *      Author: RZ8556
 */

#ifndef SOURCES_APP_SELFTEST_H_
#define SOURCES_APP_SELFTEST_H_

#include "pmic.h"

// Enum to define the Self Test flags. Each enumerator represents a unique
// bit position.
typedef enum
{
	ST_NONE						= 0x00,		// No flags passed
	ST_GNSS_ALREADY_ON			= 0x01,		// The GNSS has already been powered on before call to self-test
	ST_REDUCED					= 0x02,		// Do not perform a GNSS fix
	ST_NO_EXTF_INIT				= 0x04,		// Do not erase external flash and format
	ST_LOG_TEMP_ALARM			= 0x08,		// Logs red / amber temperature alarms when detected
	ST_NO_PMIC					= 0x10,		// use this for CLI
	ST_PERFORM_RTC_ALARM_TEST	= 0x20,		// use this for performing RTC Alarm self test.
}ST_FLAGS_BIT_MASK_t;

typedef enum
{
    SELFTEST_PASS                = 1,       // pass all self test stages pass within test limits
    SELFTEST_GNSS_TIME_TO_FIX    = 2,       // GNSS time to fix Time to fix must be < 40 s for self-test pass
    SELFTEST_GNSS_SNR_LOW        = 3,       // GNSS SNR low At least 5 satellites must have SNR > 10
    SELFTEST_CELLULAR_CSQ_LOW    = 4,       // cellular signal quality low  Pass if 9 or greater
    SELFTEST_TEMP_DIFF           = 5,       // temperature sensor difference between PCB mounted temperature chip and remote sensor on vibration sensor pcb < 1 degree C
    SELFTEST_RAW_ACCEL_FAILED    = 6,       // raw acceleration test signal fail    peak to peak of sampled applied tone is out of range
    SELFTEST_ENV3_FAILED         = 7,       // env3 test signal fail    peak to peak of sampled applied tone is out of range
    SELFTEST_WHEELFLAT_FAILED    = 8,       // wheel flat test signal fail  peak to peak of sampled applied tone is out of range
    SELFTEST_HUMIDITY_HIGH       = 9,       // humidity high    above threshold set in LVS0217 Design Description section 4.11.14.  Record as pass if humidity sensor not fitted - so no fail code reported
    SELFTEST_MEMS_SENSOR_FAIL    = 10,      // MEMS sensor fail MEMS built in self test feature.  Electrostatic deflection of MEMS sensor with measured response
    SELFTEST_SENSOR_MOVING       = 11,      // MEMS moving  MEMS sensor has a movement detect feature.  It is not essential to be stationary for MEMS built in test feature
                                            // but we should report if the vehicle is moving as a co-incidental shock could give a faulse MEMS self test fail
    SELFTEST_BATTERY_VOLTAGE_LOW = 12,      // battery Voltage low  reports low Voltage if Vd < 2.6 V.  Modem must not be on during Voltage measurement
    SELFTEST_RTC_TIME_DIFF       = 13,      // RTC time difference between RTC and new time recovered from GNSS before synchronising.  Difference must be < 10 s*/
	SELFTEST_GNSS_NO_SAT         = 14,      // GNSS no satellites detected
    SELFTEST_GNSS_COMMS          = 15,      // GNSS connection error
    SELFTEST_GNSS_PWR_ON         = 16,      // GNSS failed to power on
    SELFTEST_GNSS_VER            = 17,      // GNSS failed to read version
    SELFTEST_RAW_FAILED          = 18,      // RAW test signal fail    peak to peak of sampled applied tone is out of range, used to settle sample

    // power on failsafe status OnTime Notification: Defect ID [#14876]
    SELFTEST_POWER_ON_FAILSAFE_ENABLED  = 20,   // gNvmCfg.dev.measureConf.Is_Power_On_Failsafe_Enabled = true
    SELFTEST_POWER_ON_FAILSAFE_DISABLED = 21,   // gNvmCfg.dev.measureConf.Is_Power_On_Failsafe_Enabled = false

    // additional errors/warnings
    SELFTEST_BATTERY_READ_ERROR   = 30,     	// Failed to read battery level
    SELFTEST_RTC_ERROR            = 31,     	// Error reading RTC
    SELFTEST_GNSS_FAILED_TO_START = 32,     	// Error failed to start GNSS
    SELFTEST_TMP431_ERROR         = 33,     	// Error reading TMP431
    SELFTEST_DS1374_ERROR         = 34,     	// Error reading DS1374
    SELFTEST_LIS3DH_ERROR         = 35,     	// Error reading LS3DH
    SELFTEST_HDC1050_ERROR        = 36,     	// Error reading HDC1050
    SELFTEST_IS25LP128_ERROR 	  = 37,     	// Error reading IS25LP128
    SELFTEST_IS25LP512_ERROR      = 38,     	// Error reading IS25LP512
	SELFTEST_NT3H1101_ERROR       = 39,         // Error Reading NT3H1101
	SELFTEST_ADXL372_ERROR        = 40,         // Error Reading ADXL372
	SELFTEST_PMIC_RX_ERROR		  = 42,			// Selftest data not received from PMIC
	SELFTEST_PMIC_RTC_ERROR 	  = 43,			// Error reading PMIC RTC
	SELFTEST_PMIC_IS25LP128_ERROR = 44,			// Error reading PMIC IS25LP128
    SELFTEST_PMIC_DS1374_ERROR    = 45,         // Error reading PMIC DS1374
	SELFTEST_PMIC_TMP431_ERROR    = 46,         // Error reading PMIC TMP431

    // permanent self test code
    SELFTEST_HIGH_TEMP_LIMIT_EXCEEDED = 100,	// PCB/External sensor temperature has went above the fixed PCB high temperature limit
    SELFTEST_LOW_TEMP_LIMIT_EXCEEDED  = 101,	// PCB/External sensor temperature has went below the fixed PCB low temperature limit

	SELFTEST_RED_TEMP_THRESHOLD_EXCEEDED 	= 102,		// External Sensor has exceeded temperature defined by Temperature_Alarm_Upper_Limit
	SELFTEST_AMBER_TEMP_THRESHOLD_EXCEEDED 	= 103,		// External Sensor has exceeded temperature defined by Temperature_Alarm_Lower_Limit

	SELFTEST_COMMS_RECORD_READ_ERROR		= 200,		// Read the commsRecord from external flash with an error
}tSelfTestStatusCodes;

extern bool selfTest(uint8_t selfTestFlags);
extern void SelfTestResetRedTemperatureAlarm();
extern void SelfTestResetAmberTemperatureAlarm();

void SelfTest_ResetAlarm(tSelfTestStatusCodes alarmCode);
void AddStatusArrayCode(uint8_t code);
void sendSelfTestMessage(SelfTestData stcSelfTestData);
void selftest_InitiateRTCAlarmTest(uint32_t alarmInSecsFromNow);
/*================================================================================*
 |                                 CLI FUNCTIONS                                  |
 *================================================================================*/

extern bool cliDacSTStart(uint32_t argc, uint8_t * argv[], uint32_t * argi);
extern bool cliSelfTest( uint32_t args, uint8_t * argv[], uint32_t * argi);


#endif /* SOURCES_APP_SELFTEST_H_ */


#ifdef __cplusplus
}
#endif