#ifdef __cplusplus
extern "C" {
#endif

/*
 * xTaskApp.h
 *
 *  Created on: Sep 27, 2016
 *      Author: BF1418
 */

#ifndef SOURCES_APP_XTASKAPP_H_
#define SOURCES_APP_XTASKAPP_H_

#include "xTaskMeasure.h"

//#define EXT_WDG_DS1374_WAKEUP_TEST

#define DCLEVEL_AT_START        (0)
#define DCLEVEL_BEFORE_MODEM    (1)
#define DCLEVEL_AT_POWEROFF     (2)
#define DCLEVEL_MAX             (3)

#define MSEC_TO_SEC_CONV_FACTOR							(.001f)		// 1msec = .001 secs
#define AVG_POWER_CONSUMED_PRE_GNSS_ON_WATTS			(0.06337f)	// E = P * duration
#define AVG_POWER_CONSUMED_DURING_GNSS_ON_WATTS			(0.198f)	// E = P * duration
#define AVG_DELTA_POWER_CONSUMED_MEASUREMENT_WATTS		(0.244f)	// E = P * duration
#define AVG_POWER_CONSUMED_MODEM_ON_WATTS				(0.345f)	// E = P * duration
#define AVG_POWER_CONSUMED_DATA_UPLOAD_WATTS			(0.325f)	// E = P * duration
#define AVG_POWER_USED_IN_TEMPERATURE_MEASUREMENT_WATTS	(0.1f)		// E = P * duration(REV3_HW_TEMPERATURE_MEASUREMENT_MILLISECS)
#define REV3_HW_TEMPERATURE_MEASUREMENT_MILLISECS		(1000)		// Fixed duration, for legacy support.

/**************************************************************************/
/******************** Report structures ***********************************/
/**************************************************************************/
// IMP NOTE: These enumerator values are based on the UploadedParameters.xls
// list and they should always be in SYNC.
typedef enum
{
    WAKEUP_CAUSE_POWER_ON_FAILSAFE  		=  0,   // WAKEUP_CAUSE_DS137N
    WAKEUP_CAUSE_RTC                		=  1,   // Normal RTC wakeup.
    WAKEUP_CAUSE_THERMOSTAT         		=  2,   // WAKEUP_CAUSE_TMP431_THERM_ALERT
    WAKEUP_CAUSE_MAGNET             		=  3,   // WAKEUP_CAUSE_MAG_SWITCH
    WAKEUP_CAUSE_NFC                		=  4,   // WAKEUP_CAUSE_NFC
	WAKEUP_CAUSE_FULL_SELFTEST				=  5,	// Harvester only - selftest activated
	WAKEUP_CAUSE_SELFTEST_REDUCED			=  6,	// Harvester only - Daily selftest, no GPS test
    WAKEUP_CAUSE_RTC_FORCED_TEMPERATURE  	= 96,   // for testing and not been frustrated by the unpredictable scheduler
    WAKEUP_CAUSE_RTC_FORCED_MEASUREMENT  	= 97,   // for testing and not been frustrated by the unpredictable scheduler
    WAKEUP_CAUSE_RTC_FORCED_DATAUPLOAD  	= 98,   // for testing and not been frustrated by the unpredictable scheduler
    WAKEUP_CAUSE_COLD_BOOT          		= 99,   // Not present in the list, but retained as it was in the code.
    WAKEUP_CAUSE_UNKNOWN            		= 100,  // Default value, not present in the list.
}WAKEUP_CAUSES_t;

// List of the task the node performs. Each task is assigned a bit position.
// The task list is a byte long and can store upto 8 tasks, one for each bit.
typedef enum
{
    NODE_WAKEUP_TASK_TEMPERATURE_MEAS  	=  1,   // Temperature Measurement
    NODE_WAKEUP_TASK_SELF_TEST      	=  2,   // Self Test
	NODE_WAKEUP_TASK_UPLOAD         	=  4,   // Data Upload
	NODE_WAKEUP_TASK_MEASURE        	=  8,   // Measurement
	NODE_WAKEUP_TASK_SLEEP				= 16,	// Relax
}NODE_WAKEUP_TASKS_t;

typedef struct
{
    float dcLevelVolts[DCLEVEL_MAX];
    float dcLevelEnergy[DCLEVEL_MAX];
}DCLEVEL_data_t;

// formerly in configDefaultSettings.h, but what has it to do with default settings ?
// TODO move these two typedefs to temp monitoring code module
typedef struct
{
    float localTemp;
    float remoteTemp;
}TMP431_data_t;

typedef struct Report
{
    TMP431_data_t   tmp431;
    DCLEVEL_data_t dcLevel;
}report_t;


// Add a minute offset, so as to adjust node wake up for temp. if the measurement / upload is
// within this minute offset from the temperature schedule.
#define TEMP_MEAS_SCHEDULE_MINS						(9 + 1)

void xTaskApp_Init();
void xTaskApp_xTaskApp( void *pvParameters );
bool xTaskApp_AddStatusArrayCode(uint8_t code);
float xTaskApp_calculateWheelHz(float speed_knots, float Wheel_Diameter);
void xTaskApp_flashStatusLedCadence(uint8_t count, uint16_t onMsec, uint16_t offMsec);
void xTaskApp_flashStatusLed(uint8_t count);

bool xTaskApp_DetectMovement( bool * moving);

bool xTaskApp_doSampling(bool bRawAdcSampling,
                   	     tMeasId eMeasId,
						 uint32_t nNumOutputSamples,
						 uint32_t nAdcSamplesPerSecIfRawAdc);

bool xTaskApp_startApplicationTask(uint8_t wakeupReason);
bool xTaskApp_commsTest(uint32_t testFuncNum, uint32_t repeatCount);
bool xTaskApp_binaryCliOta();

bool xTaskApp_calculateNextWakeup(bool bWakeupEvent);

bool xTaskApp_scheduleNextWakeup(uint32_t rtcSleepTime_mins, uint32_t ds137nSleepTime_mins);
bool xTaskApp_PerformDACSelfTest();

bool xTaskApp_makeIs25ReadyForUse();

#endif /* SOURCES_APP_XTASKAPP_H_ */


#ifdef __cplusplus
}
#endif