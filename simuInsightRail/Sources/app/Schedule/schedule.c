#ifdef __cplusplus
extern "C" {
#endif

/*
 * schedule.c
 *
 *  Created on: 16 Aug 2017
 *      Author: RZ8556
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "fsl_rtc_hal.h"

// TODO: 'recursive' include
#include "xTaskApp.h"

#include "xTaskAppRtc.h"
#include "xTaskAppOta.h"
#include "Vbat.h"

#include "CLIcmd.h"
#include "Log.h"

// TODO only included for Temperature stuff - can be hidden when Temp monitoring moved to own file
// i.e. TemperatureEraseAllRecords, it's kind of irrelevant that this uses Ext Flash...
// #include "temperatureMonitor.h"
#include "ExtFlash.h"

#include "Device.h"
#include "rtc.h"
#include "ds137n.h"
#include "NvmConfig.h"
#include "NvmData.h"
#include "configData.h"
#include "schedule.h"
#include "pmic.h"

/*================================================================================*
 |                                   LOCAL DEFINES                                |
 *================================================================================*/

// Upload spreading.
#define MINS_IN_A_DAY			(24 * 60)
#define DAYS_IN_WEEK			(7)
#define	NODES_SUPPORTED			(1000)

#define UPLOAD_FIXED_OVERHEAD_MINS (10)
#define MEASURE_FIXED_OVERHEAD_MINS (3)

/*================================================================================*
 |                              GLOBAL VARIABLE PROTOTYPES                        |
 *================================================================================*/

extern tNvmCfg gNvmCfg;
extern tNvmData gNvmData;
extern RTC_Type * const g_rtcBase[RTC_INSTANCE_COUNT];

extern uint8_t nodeTaskThisCycle;

/*================================================================================*
 |                            LOCAL FUNCTION DEFINES                              |
 *================================================================================*/

static uint32_t convertToMinutes(uint32_t time);
static uint32_t convertToTime(uint32_t time, uint32_t mins);
static uint32_t calculateUploadOffset(uint32_t);
static uint32_t calculateAvailableMinutes(bool);
static bool convertStringToNumber(const char* str, const uint32_t width, uint32_t *result);

static bool cliInfo(uint32_t args, uint8_t * argv[], uint32_t * argi);
static bool cliUnitTest(uint32_t args, uint8_t * argv[], uint32_t * argi);
static bool cliUploadDayOffset(uint32_t args, uint8_t * argv[], uint32_t * argi);

static void overallTest(uint32_t);
static uint8_t calculateUploadDayOffset();
static bool CheckSwitchToUploadCycle();
/*================================================================================*
 |                                 IMPLEMENTATION                                 |
 *================================================================================*/

/*
 * schedule_ScheduleNextWakeup
 *
 * @desc    schedule the next node wakeup - by programming the RTC and DS1374
 *
 * @param   - rtcSleepTime_mins: set processor RTC to wake up after this delay
 * 			- ds137nSleepTime_mins: external 'watchdog' wake up - generally set higher than RTC
 *
 * @return -
 */
bool schedule_ScheduleNextWakeup(uint32_t rtcSleepTime_mins, uint32_t ds137nSleepTime_mins)
{
	bool retval = true;
	LOG_DBG(LOG_LEVEL_APP, "Schedule next node wakeup\n");
    rtc_datetime_t datetime;

    // get the current time
    if(true == RtcGetTime(&datetime))
	{
		LOG_DBG(LOG_LEVEL_APP, "Current time %02d/%02d/%02d %02d:%02d:%02d\n", datetime.day,
																datetime.month,
																datetime.year,
																datetime.hour,
																datetime.minute,
																datetime.second);
	}
	else
	{
		// continue on and try to set RTC/EXTERNAL wake up
		LOG_EVENT(0, LOG_NUM_APP, ERRLOGFATAL, "failed RtcGetTime");
		retval = false;
	}


	// set up the RTC to assert RTC-WAKEUP low on alarm interrupt
	ConfigureRTC();
	if(true == ConfigureRtcWakeup(rtcSleepTime_mins))
	{
		LOG_DBG(LOG_LEVEL_APP, "RTC sleeptime = %d [mins]\n", rtcSleepTime_mins);
	}
	else
	{
		// continue to set EXTERNAL alarm wake up
		LOG_EVENT(0, LOG_NUM_APP, ERRLOGFATAL, "failed ConfigureRtcWakeup");
		retval = false;
	}

	// now set backup DS137n wakeup
	if(!Device_HasPMIC())
	{
		if(true == DS137n_SetAlarm(ds137nSleepTime_mins * 60))
		{
			LOG_DBG( LOG_LEVEL_APP, "DS137n sleeptime = %d [mins]\n", ds137nSleepTime_mins);
		}
		else
		{	// TODO 	- what do we do about this case
			LOG_EVENT(0, LOG_NUM_APP, ERRLOGFATAL, "failed DS137n_SetAlarm");
			retval = false;
		}
	}

	return retval;
}




/*
 * schedule_CalculateNextWakeup
 *
 * @desc Calculate the next scheduled wakeup based on the schedule
 *
 * @params None
 *
 */
bool schedule_CalculateNextWakeup(void)
{
	bool retval = true;
	uint32_t nextWakeupMinutesSinceMidnight = 0x00;
	uint16_t minutesToWakeup = 0x00;
	uint32_t rtcInSecs = 0x00;
	uint8_t nodeWakeupTask = NODE_WAKEUP_TASK_TEMPERATURE_MEAS;
	uint32_t uploadOffset_mins = 0;
	uint32_t uploadOffset_time = 0;

	LOG_DBG(LOG_LEVEL_APP, "\n>>>> calculateNextWakeup()\n");

	uint32_t uploadInterval_s = gNvmCfg.dev.commConf.Upload_repeat;

	if(gNvmCfg.dev.measureConf.Is_Upload_Offset_Enabled)
	{
		uint32_t availableMinutes = calculateAvailableMinutes(false);
		// interval from start of 'upload' time window (has been offset by IMEI)
		uploadOffset_mins = calculateUploadOffset(availableMinutes);

		uint32_t measureEnd = ((gNvmCfg.dev.measureConf.Max_Daily_Meas_Retries-1) *
								(gNvmCfg.dev.measureConf.Retry_Interval_Meas)) / 60;
		measureEnd += convertToMinutes(gNvmCfg.dev.measureConf.Time_Start_Meas);
		measureEnd += gNvmCfg.dev.measureConf.Max_Daily_Meas_Retries * MEASURE_FIXED_OVERHEAD_MINS;

		uploadOffset_mins += measureEnd;
		uploadOffset_time = convertToTime(0, uploadOffset_mins);

		if(NvmConfig_HasMovedToMultiDayUpload() && (uploadInterval_s > MIN_UPLOAD_REPEAT_SECS))
		{
			uint8_t calculatedUploadDayOffset = calculateUploadDayOffset();
			//Schedule has been updated so calculate an offset between 1-7 days based on IMEI sensor so all sensors don't upload on same day
			Vbat_SetUploadDayOffset(calculatedUploadDayOffset);
			LOG_DBG(LOG_LEVEL_APP, "\nNew Upload Day Offset, %d day(s)", calculatedUploadDayOffset);
		}

		uint8_t uploadDayOffset = Vbat_GetUploadDayOffset();

		if(uploadDayOffset > 0)
		{
			uploadInterval_s = uploadDayOffset * MIN_UPLOAD_REPEAT_SECS;
		}
	}

	#ifdef DEBUG
	if(dbg_logging & LOG_LEVEL_APP)
	{
		printf("\nnoOfMeasurementDatasetsToUpload %d\n", gNvmData.dat.is25.noOfMeasurementDatasetsToUpload);
		printf("Required_Daily_Meas %d\n", gNvmCfg.dev.measureConf.Required_Daily_Meas);
		printf("noOfMeasurementAttempts %d\n", gNvmData.dat.schedule.noOfMeasurementAttempts);
		printf("noOfGoodMeasurements %d\n", gNvmData.dat.schedule.noOfGoodMeasurements);
		printf("Max_Daily_Meas_Retries %d\n", gNvmCfg.dev.measureConf.Max_Daily_Meas_Retries);
		printf("Retry_Interval_Meas %d mins\n", gNvmCfg.dev.measureConf.Retry_Interval_Meas / 60);
		printf("noOfCommsDatasetsToUpload %d\n", gNvmData.dat.is25.noOfCommsDatasetsToUpload);
		printf("noOfUploadAttempts %d\n", gNvmData.dat.schedule.noOfUploadAttempts);
		printf("Time_Start_Meas %02d:%02d\n", (gNvmCfg.dev.measureConf.Time_Start_Meas/100),
																gNvmCfg.dev.measureConf.Time_Start_Meas -((gNvmCfg.dev.measureConf.Time_Start_Meas/100)*100));
		if(gNvmCfg.dev.measureConf.Is_Upload_Offset_Enabled)
		{
			printf("Upload Offset enabled, Offset(mins from 'now'): %d\n", uploadOffset_mins);
			printf("New Upload Time = %02d:%02d\n", (uploadOffset_time / 100), uploadOffset_time - ((uploadOffset_time/100)*100));
		}
		else
		{
			printf("Scheduled Upload_Time %02d:%02d\n",
					(gNvmCfg.dev.commConf.Upload_Time / 100), gNvmCfg.dev.commConf.Upload_Time - ((gNvmCfg.dev.commConf.Upload_Time / 100)*100));
		}
		printf("Retry_Interval_Upload %d mins\n", gNvmCfg.dev.commConf.Retry_Interval_Upload / 60);
		printf("Max_Upload_Retries %d\n", gNvmCfg.dev.commConf.Max_Upload_Retries);
		printf("Number_Of_Upload %d\n", gNvmCfg.dev.commConf.Number_Of_Upload);
		printf("scheduleStartSecs %ld\n", gNvmData.dat.schedule.scheduleStartSecs);
		printf("\n");
	}
	#endif

	switch(gNvmData.dat.schedule.bDoMeasurements)
	{
	case DO_DATA_UPLOADS:

		if((gNvmData.dat.schedule.noOfUploadAttempts >= gNvmCfg.dev.commConf.Max_Upload_Retries) ||
		   ((gNvmData.dat.is25.noOfCommsDatasetsToUpload == 0x00) &&
		    (nodeTaskThisCycle & NODE_WAKEUP_TASK_UPLOAD) &&
			Vbat_IsFlagSet(VBATRF_FLAG_LAST_COMMS_OK)))
		{	// switch to measurements
			LOG_DBG(LOG_LEVEL_APP, "\nSwitching to DO_MEASUREMENTS\n\n");
			gNvmData.dat.schedule.bDoMeasurements = DO_MEASUREMENTS;
			gNvmData.dat.schedule.noOfMeasurementAttempts = 0x00;
			gNvmData.dat.schedule.noOfGoodMeasurements = 0x00;
			gNvmData.dat.schedule.noOfUploadAttempts = 0x00;

			// this is the measurement start schedule time
			gNvmData.dat.schedule.scheduleStartSecs = RTC_HAL_GetSecsReg(g_rtcBase[RTC_IDX]);

			LOG_DBG(LOG_LEVEL_APP, "Updated scheduleStartSecs: %d\n", gNvmData.dat.schedule.scheduleStartSecs);
		}
		break;

	case DO_MEASUREMENTS:

		if(gNvmData.dat.schedule.noOfMeasurementAttempts == gNvmCfg.dev.measureConf.Max_Daily_Meas_Retries ||
		   gNvmData.dat.schedule.noOfGoodMeasurements == gNvmCfg.dev.measureConf.Required_Daily_Meas)
		{
			//Adjust the Schedule start time for any changes in RTC time e.g. On the upload scheduleStartSecs is set
			//to a time in 1970 then on measurement got a fix and updated the RTC, this adjusts the schedule start time accordingly
			gNvmData.dat.schedule.scheduleStartSecs += RtcGetAdjustmentSpan();

			if(CheckSwitchToUploadCycle())
			{
				LOG_DBG(LOG_LEVEL_APP, "\nSwitching to DO_DATA_UPLOADS\n\n");
				gNvmData.dat.schedule.bDoMeasurements = DO_DATA_UPLOADS;
				gNvmData.dat.schedule.noOfMeasurementAttempts = 0x00;
			}
			else
			{
				LOG_DBG(LOG_LEVEL_APP, "\nNo daily upload staying as DO_MEASUREMENTS, " "Current Secs:%ld, ScheduleStartSecs:%ld\n",
						RTC_HAL_GetSecsReg(g_rtcBase[RTC_IDX]), gNvmData.dat.schedule.scheduleStartSecs);

				gNvmData.dat.schedule.bDoMeasurements = DO_MEASUREMENTS;
				gNvmData.dat.schedule.noOfMeasurementAttempts = 0x00;
				gNvmData.dat.schedule.noOfGoodMeasurements = 0x00;
				gNvmData.dat.schedule.noOfUploadAttempts = 0x00;
			}
		}
		break;
	}

	if(gNvmCfg.dev.measureConf.Required_Daily_Meas == 0x00)
	{
		// Override any decisions on schedule type above and schedule for an upload time within 24 hours:
		// No daily measurements set (TODO: Is this right for move to weekly uploads intended for battery saving)
		gNvmData.dat.schedule.bDoMeasurements = DO_DATA_UPLOADS;
	}

	// Establish if measurements or data upload
	if(gNvmData.dat.schedule.bDoMeasurements == DO_MEASUREMENTS)
	{	// do measurements - so set next wakeup time

		rtcInSecs = RTC_HAL_GetSecsReg(g_rtcBase[RTC_IDX]);

		LOG_DBG(LOG_LEVEL_APP, "\n>>>>Seconds until upload schedule can be started: %ld, CurrentSecs:%ld, ScheduleStartSecs:%ld\n",
				(uploadInterval_s - (rtcInSecs - gNvmData.dat.schedule.scheduleStartSecs)),
				rtcInSecs,
				gNvmData.dat.schedule.scheduleStartSecs);

		// if accelerated scheduling needed
		if(gNvmData.dat.schedule.bDoAcceleratedScheduling == true)
		{
		    rtc_datetime_t datetime;
		    bool rc_ok;

			LOG_DBG(LOG_LEVEL_APP, "Schedule type = ACCELERATED -- DO_MEASUREMENTS %d mins\n", (gNvmCfg.dev.measureConf.Retry_Interval_Meas / 60));
			rc_ok = RtcGetTime(&datetime);
			if (!rc_ok)
			{
			    LOG_DBG(LOG_LEVEL_APP, "xTaskApp_calculateNextWakeup(): get time failed\n");
			}

			// set measure wake to be (gNvmCfg.dev.measureConf.Retry_Interval_Meas/60) from now
			nextWakeupMinutesSinceMidnight = (datetime.hour * 60) + datetime.minute + (gNvmCfg.dev.measureConf.Retry_Interval_Meas / 60);
		}
		else
		{
			LOG_DBG(LOG_LEVEL_APP, "Schedule type = DO_MEASUREMENTS\n");

			// convert to 'number of minutes after midnight' and add possible retry offset
			nextWakeupMinutesSinceMidnight = convertToMinutes(gNvmCfg.dev.measureConf.Time_Start_Meas);
			nextWakeupMinutesSinceMidnight += ((gNvmCfg.dev.measureConf.Retry_Interval_Meas / 60) * gNvmData.dat.schedule.noOfMeasurementAttempts);
		}
	}
	else
	{
		// if accelerated scheduling needed
		if(gNvmData.dat.schedule.bDoAcceleratedScheduling == true)
		{
            rtc_datetime_t datetime;
            bool rc_ok;

			LOG_DBG(LOG_LEVEL_APP, "Schedule type = ACCELERATED --  DO_DATA_UPLOADS %d mins\n", (gNvmCfg.dev.commConf.Retry_Interval_Upload / 60));
            rc_ok = RtcGetTime(&datetime);
            if (!rc_ok)
            {
                LOG_DBG(LOG_LEVEL_APP, "xTaskApp_calculateNextWakeup(): get time failed\n");
            }

            // set upload wake to be (gNvmCfg.dev.commConf.Retry_Interval_Upload/60) from now
			nextWakeupMinutesSinceMidnight = (datetime.hour * 60) + datetime.minute + (gNvmCfg.dev.commConf.Retry_Interval_Upload / 60);
		}
		else
		{	// do data upload - so set next wakeup time
			LOG_DBG(LOG_LEVEL_APP, "Schedule type = DO_DATA_UPLOADS\n");

			// Use Upload offset when enabled.
			if(gNvmCfg.dev.measureConf.Is_Upload_Offset_Enabled)
			{
				nextWakeupMinutesSinceMidnight = convertToMinutes(uploadOffset_time);
			}
			else
			{
				nextWakeupMinutesSinceMidnight = convertToMinutes(gNvmCfg.dev.commConf.Upload_Time);
			}

			// convert to 'number of minutes after midnight' and add possible retry offset
			nextWakeupMinutesSinceMidnight +=
					(gNvmCfg.dev.commConf.Retry_Interval_Upload * gNvmData.dat.schedule.noOfUploadAttempts) / 60;
		}
	}

	// If we wrap around midnight we have to subtract a day
	nextWakeupMinutesSinceMidnight %= MINS_IN_A_DAY;

	calculateTimeToNextWakeup(nextWakeupMinutesSinceMidnight, &minutesToWakeup);

	if((minutesToWakeup > TEMP_MEAS_SCHEDULE_MINS) && !Device_HasPMIC())
	{
		LOG_DBG(LOG_LEVEL_APP,"Temperature Measure wakeup in: %d [mins], Next %s wakeup in: %d[mins]\n",
				TEMP_MEAS_SCHEDULE_MINS, gNvmData.dat.schedule.bDoMeasurements?"Measurement":"Upload",
						minutesToWakeup);
		minutesToWakeup = TEMP_MEAS_SCHEDULE_MINS;
	}
	else
	{
		// Set schedule time to 10 mins here if temperature alarm retry
		if(Device_HasPMIC() &&
		  !Vbat_IsFlagSet(VBATRF_FLAG_LAST_COMMS_OK) &&
		  (Vbat_IsFlagSet(VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG +
				  	  	  VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG +
						  VBATRF_FLAG_LOW_TEMPERATURE_LIMIT_TRIG +
						  VBATRF_FLAG_HIGH_TEMPERATURE_LIMIT_TRIG)))
		{
			Vbat_SetAlarmState(TEMPERATURE_ALARM_RETRY);
		}

		// Do measurement
		if(gNvmData.dat.schedule.bDoMeasurements)
		{
			nodeWakeupTask |= NODE_WAKEUP_TASK_MEASURE;
		}
		// Upload
		else
		{
			nodeWakeupTask |= NODE_WAKEUP_TASK_UPLOAD;
		}

		LOG_DBG(LOG_LEVEL_APP,"Next %s wakeup in: %d[mins]\n",
				gNvmData.dat.schedule.bDoMeasurements?"Measurement":"Upload",
						minutesToWakeup);
	}
	// Set the node wakeup task for next cycle.
	Vbat_SetNodeWakeupTask(nodeWakeupTask);

	// If more than one day interval, leave minutesToWakeup as is
	if(uploadInterval_s <= (MINS_IN_A_DAY * 60))
	{
		// If we are NOT doing multi-day upload intervals force this to be less than a day
		if(minutesToWakeup > MINS_IN_A_DAY)
		{
			LOG_DBG(LOG_LEVEL_APP, "minutesToWakeup changed from %d to %d\r\n", minutesToWakeup, minutesToWakeup %  MINS_IN_A_DAY);
			minutesToWakeup %= MINS_IN_A_DAY;
		}
	}

	LOG_DBG(LOG_LEVEL_APP, "minutesToWakeup = %d, NodeWakeupEvent:0x%x\n", minutesToWakeup, nodeWakeupTask);

	// JAY: TBC: The wakeup time for the Ext watchdog should be (minutesToWakeup + 10)  * 60 ???
    // YES!
    schedule_ScheduleNextWakeup(minutesToWakeup, (24 * 60) + 10);		// ensure failsafe wakeup does not occur before rtc wakeup
	return retval;
}


/*
 * CheckSwitchToUploadCycle
 *
 * @desc Decide whether to switch from taking measurements to doing doing an upload cycle. You want to switch to an upload cycle if:
 *       1. OTA is in progress and retries haven't yet been exhausted
 *       2. The day offset defined by 'UploadDayOffset' has been reached
 *       3. The upload repeat frequency has been reached. A value of MIN_UPLOAD_REPEAT_SECS (86400)
 *       is subtracted from the upload_repeat to determine if an upload is required for the current day.
 *
 * @params None
 *
 * @return - true, should switch to upload cycle or false, should remain in measurement cycle
 */

static bool CheckSwitchToUploadCycle()
{
	bool bSwitchToUploadCycle = false;

	uint8_t uploadDayOffset = Vbat_GetUploadDayOffset();
	uint32_t timeSinceLastUpload_s = (RTC_HAL_GetSecsReg(g_rtcBase[RTC_IDX]) - gNvmData.dat.schedule.scheduleStartSecs);

	if((OtaProcess_GetImageSize() > 0)  && (OtaProcess_GetRetryCount() > 0))
	{
		//In Progress OTA hasn't completed all attempts yet so switch to upload
		LOG_DBG(LOG_LEVEL_APP, "OTA still in progress, retrying within 24 hrs\r\n");

		bSwitchToUploadCycle = true;
	}
	else if((gNvmCfg.dev.measureConf.Is_Upload_Offset_Enabled) && (uploadDayOffset > 0))
	{
		//If set we should use the uploadDayOffset value to decide when to do an upload
		if(timeSinceLastUpload_s > ((uploadDayOffset * MIN_UPLOAD_REPEAT_SECS) - MIN_UPLOAD_REPEAT_SECS))
		{
			bSwitchToUploadCycle = true;

			//Reset UploadDayOffset
			Vbat_SetUploadDayOffset(0);
		}
	}
	else
	{
		if(timeSinceLastUpload_s > (gNvmCfg.dev.commConf.Upload_repeat - MIN_UPLOAD_REPEAT_SECS))
		{
			bSwitchToUploadCycle = true;
		}
	}
	return bSwitchToUploadCycle;
}
/*================================================================================*
 |                                 CLI FUNCTIONS                                  |
 *================================================================================*/

// Schedule CLI commands.
struct cliSubCmd scheduleSubCmds[] =
{
		{"info",				cliInfo},
		{"ut",					cliUnitTest},
		{"uploadDayOffset",		cliUploadDayOffset},

};

static const char scheduleHelp[] = {
		" Sub commands:\r\n"
		" info -- show scheduling info\r\n"
		" ut   -- run unit test\r\n"
		" uploadDayOffset  -- print calculated upload day offset based on IMEI\r\n"
};


bool cliScheduleHelp(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	printf((char*)scheduleHelp);

	return true;
}


bool cliSchedule(uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    bool rc_ok = true;

    if(args)
    {
		rc_ok = cliSubcommand(args,argv,argi, scheduleSubCmds, sizeof(scheduleSubCmds)/sizeof(*scheduleSubCmds));
    }
    else
    {
        printf("Try typing: help schedule\n");
    }

    return rc_ok;
}

static bool cliUploadDayOffset(uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	printf("Upload Day Offset is: %d\r\n", calculateUploadDayOffset());

	return true;
}

static bool cliInfo(uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	printf("Measure wake up time is: %d\r\n", gNvmCfg.dev.measureConf.Time_Start_Meas);

	return true;
}

static bool	cliUnitTest(uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	#define FIXED_OVERHEAD (UPLOAD_FIXED_OVERHEAD_MINS + MEASURE_FIXED_OVERHEAD_MINS)

	#define SCHEDULE_TESTS 3
	const struct
	{
		const uint32_t measureStart;
		const uint32_t retryInterval;
		const uint32_t totalAttempts;

		const uint32_t commsRetryInterval;
		const uint32_t commsAttempts;

		const uint32_t expectedMins;
		const uint32_t expectedAvailable;

	} schedule_test[SCHEDULE_TESTS] =
	{
		{1025, 60, 2,
		 30, 2,
		 10*60+25,
				// mins in day - (meas retry  meas overhead                   	comms retry     comms overhead)
				MINS_IN_A_DAY - (60*(2-1) 	+ (2*MEASURE_FIXED_OVERHEAD_MINS)	+ (30*(2-1)		+ (2*UPLOAD_FIXED_OVERHEAD_MINS) + FIXED_OVERHEAD))	},

		{1600, 20, 4,
		 40, 3,
		 16*60,
				MINS_IN_A_DAY - (20*(4-1) 	+ (4*MEASURE_FIXED_OVERHEAD_MINS)	+ (40*(3-1)		+ (3*UPLOAD_FIXED_OVERHEAD_MINS) + FIXED_OVERHEAD))	},

		{ 100, 30, 1,
		  60, 1,
		  60,
				MINS_IN_A_DAY - (30*(1-1)   + (1*MEASURE_FIXED_OVERHEAD_MINS)	+ (60*(1-1)		+ (1*UPLOAD_FIXED_OVERHEAD_MINS) + FIXED_OVERHEAD))	}
	};

	const struct
	{
		const char* str;
		const int result;
		const bool expected_result;
	} convert_test_result[] =
	{
		{"000",		0,	 	true},
		{"008",		8,	 	true},
		{"064",		64, 	true},
		{"012", 	12, 	true},	// handle leading zeros
		{"795", 	795, 	true},
		{"456",		456,	true},
		{"12",		0, 		false},	// string too short
		//{"1234", 	0, 		false},	// string too long
		{"1z2", 	0, 		false},	// invalid char in string
		{"", 		-1,		true}	// end of test!
	};

	const struct
	{
		const uint32_t base_time;
		const int32_t minutes;
		const uint32_t expected_time;
	} time_convert_test[] =
	{
			{1000, 	 10,	1010},
			{2300, 	120,	 100},
			{1800,	600,	 400},
			{0, 	 -1, 	 0}
	};

	printf(	"##########################################\n\r"
			"      Schedule Calculation Unit Test\n\r"
			"##########################################\n\r");

	uint32_t result;
	bool stn_pass;
	bool stn_overall_pass = true;

	// test valid formats
	int i=0;
	while(convert_test_result[i].result != -1)
	{
		stn_pass = convertStringToNumber(convert_test_result[i].str, 3, &result);

		if(convert_test_result[i].expected_result != stn_pass)
		{
			printf("Test case %d failed..\r\n", i+1);
			stn_overall_pass = false;
		}
		else
		{
			if(stn_pass)
			{
				if(result != convert_test_result[i].result)
				{
					printf("Test case %d failed..\r\n", i+1);
					stn_overall_pass = false;
				}
			}
		}
		++i;
	}
	printf("%s converting string to number\r\n", stn_overall_pass?("PASSED"):("FAILED"));

	// test time conversion functions
	i = 0;
	bool tc_pass = true;
	while(time_convert_test[i].minutes > 0)
	{
		if(convertToTime(time_convert_test[i].base_time, time_convert_test[i].minutes) != time_convert_test[i].expected_time)
		{
			tc_pass = false;
			printf("Time conversion - Test case %d failed..\r\n", i+1);
		}
		++i;
	}
	printf("%s converting to minutes\r\n", tc_pass?("PASSED"):("FAILED"));

	uint32_t savedMeasureStartTime = gNvmCfg.dev.measureConf.Time_Start_Meas;
	uint32_t savedRetryInterval = gNvmCfg.dev.measureConf.Retry_Interval_Meas;
	uint32_t savedDailyRetries = gNvmCfg.dev.measureConf.Max_Daily_Meas_Retries;
	uint32_t savedCommsRetryInterval = gNvmCfg.dev.commConf.Retry_Interval_Upload;
	uint32_t savedUploadRetries = gNvmCfg.dev.commConf.Max_Upload_Retries;

	uint32_t fromIMEI = (uint32_t)(strtol((char*)&gNvmCfg.dev.modem.imei[11], NULL, 10) / 10);
	stn_pass = convertStringToNumber((char*)&gNvmCfg.dev.modem.imei[11], 3, &result);
	if(!stn_pass || (result != fromIMEI))
	{
		printf("IMEI to number discrepancy\r\n");
	}

	for(int i = 0; i < SCHEDULE_TESTS; i++)
	{
		gNvmCfg.dev.measureConf.Time_Start_Meas = schedule_test[i].measureStart;
		gNvmCfg.dev.measureConf.Retry_Interval_Meas = 60*schedule_test[i].retryInterval;
		gNvmCfg.dev.measureConf.Max_Daily_Meas_Retries = schedule_test[i].totalAttempts;

		gNvmCfg.dev.commConf.Retry_Interval_Upload = 60*schedule_test[i].commsRetryInterval;
		gNvmCfg.dev.commConf.Max_Upload_Retries = schedule_test[i].commsAttempts;

		// run functions to be checked
		uint32_t timeMinutes = convertToMinutes(gNvmCfg.dev.measureConf.Time_Start_Meas);
		uint32_t availableMinutes = calculateAvailableMinutes(false);
		uint32_t uploadOffset_mins = calculateUploadOffset(availableMinutes);
		uint32_t uploadDayOffset = calculateUploadDayOffset();

		printf("Start: %04d Meas ivl: %3d(%d) Comms Ivl %3d(%d) -- ",
				gNvmCfg.dev.measureConf.Time_Start_Meas,
				gNvmCfg.dev.measureConf.Retry_Interval_Meas/60,
				gNvmCfg.dev.measureConf.Max_Daily_Meas_Retries,
				gNvmCfg.dev.commConf.Retry_Interval_Upload/60,
				gNvmCfg.dev.commConf.Max_Upload_Retries
				);

		uint32_t expectedOffset = (fromIMEI * availableMinutes) / NODES_SUPPORTED;
		uint32_t expectedDayOffset = (fromIMEI % DAYS_IN_WEEK) + 1;

		if((timeMinutes == schedule_test[i].expectedMins) &&
		   (availableMinutes == schedule_test[i].expectedAvailable) &&
 		   (uploadOffset_mins == expectedOffset) &&
		   (uploadDayOffset == expectedDayOffset))
		{
			printf("PASS!\r\n");
		}
		else
		{
			printf("FAIL: expected (%4d,%4d,%4d) got (%4d,%4d,%4d)\r\n",
				schedule_test[i].expectedMins,
				schedule_test[i].expectedAvailable,
				expectedOffset,

				timeMinutes,
				availableMinutes,
				uploadOffset_mins);
		}
	}

	overallTest(447);

	gNvmCfg.dev.measureConf.Time_Start_Meas = savedMeasureStartTime;
	gNvmCfg.dev.measureConf.Retry_Interval_Meas = savedRetryInterval;
	gNvmCfg.dev.measureConf.Max_Daily_Meas_Retries = savedDailyRetries;
	gNvmCfg.dev.commConf.Retry_Interval_Upload = savedCommsRetryInterval;
	gNvmCfg.dev.commConf.Max_Upload_Retries = savedUploadRetries;

	return true;
}


/*================================================================================*
 |                                LOCAL FUNCTIONS                                 |
 *================================================================================*/

static void overallTest(uint32_t imei)
{
	/*
	 * This test is used to compare against spreadsheet calculator
	 * Change the numbers below as required
	 */
	#define MEASURE_RETRIES 		(3)
	#define MEASURE_RETRY_INTERVAL	(600)	// seconds
	#define UPLOAD_RETRIES 			(4)
	#define	UPLOAD_RETRY_INTERVAL	(9900)	// seconds
	#define MEASURE_START_TIME 		(920)		// 9:20

	gNvmCfg.dev.measureConf.Max_Daily_Meas_Retries = MEASURE_RETRIES;
	gNvmCfg.dev.measureConf.Retry_Interval_Meas = MEASURE_RETRY_INTERVAL;
	gNvmCfg.dev.commConf.Max_Upload_Retries = UPLOAD_RETRIES;
	gNvmCfg.dev.commConf.Retry_Interval_Upload = UPLOAD_RETRY_INTERVAL;
	gNvmCfg.dev.measureConf.Time_Start_Meas = MEASURE_START_TIME;

	uint32_t availableMinutes = calculateAvailableMinutes(true);

	uint32_t measureLength = ((gNvmCfg.dev.measureConf.Max_Daily_Meas_Retries - 1) *
							(gNvmCfg.dev.measureConf.Retry_Interval_Meas)) / 60;
	// interval in seconds

	measureLength += gNvmCfg.dev.measureConf.Max_Daily_Meas_Retries * MEASURE_FIXED_OVERHEAD_MINS;
	measureLength += convertToMinutes(gNvmCfg.dev.measureConf.Time_Start_Meas);
	printf("ME: %d\r\n", measureLength*60);

	// interval from start of 'upload' time window (has been offset by IMEI)
	// for this test we provide 'IMEI'
	uint32_t uploadOffset_mins = ((imei * availableMinutes) / NODES_SUPPORTED) % MINS_IN_A_DAY;

	uint32_t measureEnd = ((gNvmCfg.dev.measureConf.Max_Daily_Meas_Retries-1) *
						(gNvmCfg.dev.measureConf.Retry_Interval_Meas)) / 60;
	// interval in seconds

	measureEnd += convertToMinutes(gNvmCfg.dev.measureConf.Time_Start_Meas);
	measureEnd += gNvmCfg.dev.measureConf.Max_Daily_Meas_Retries * MEASURE_FIXED_OVERHEAD_MINS;

	uploadOffset_mins += measureEnd;
	uint32_t uploadOffset_time = convertToTime(0, uploadOffset_mins);

	printf("Upload Time is: %04d\r\n", uploadOffset_time);

	uint32_t uploadDayOffset = (imei % DAYS_IN_WEEK) + 1;

	printf("Upload in: %d day(s) from now\r\n", uploadDayOffset);
}


/*
 * convertToTime
 * @desc Convert passed parameter to clock time
 *       offset from 'time' parameter
 */
static uint32_t convertToTime(uint32_t time, uint32_t mins)
{
	uint32_t minutes;
	uint32_t time_hours;
	uint32_t time_mins;

	minutes = convertToMinutes(time);
	minutes += mins;

	// get 'excess' after midnight
	minutes %= MINS_IN_A_DAY;
	time_hours = minutes / 60;
	time_mins = minutes % 60;

	return (time_hours*100 + time_mins);
}

/*
 * convertToMinutes
 * @desc Convert passed parameter to minutes
 */
static uint32_t convertToMinutes(uint32_t time)
{
	uint32_t timeMins = (time/100)*60 + time % 100;
	return timeMins;
}

/*
 * calculateAvailableMinutes
 * @desc Calculate window in minutes available for uploading
 *
 */
static uint32_t calculateAvailableMinutes(bool debug)
{
	uint32_t available_mins;
	uint32_t measureLength;
	uint32_t commsLength;

	if(gNvmCfg.dev.measureConf.Max_Daily_Meas_Retries == 0)
	{
		measureLength = MEASURE_FIXED_OVERHEAD_MINS;	// 3 mins allowed for measure duration
	}
	else
	{
		measureLength = ((gNvmCfg.dev.measureConf.Max_Daily_Meas_Retries - 1) * gNvmCfg.dev.measureConf.Retry_Interval_Meas) / 60;
		// interval in seconds

		measureLength += gNvmCfg.dev.measureConf.Max_Daily_Meas_Retries * MEASURE_FIXED_OVERHEAD_MINS;	// 3 mins allowed for measure duration
	}

	if(debug)
	{
		printf("MP: %d\r\n", measureLength*60);
	}

	if(gNvmCfg.dev.commConf.Max_Upload_Retries == 0)
	{
		commsLength = UPLOAD_FIXED_OVERHEAD_MINS;	// 10 mins allowed for comms duration
	}
	else
	{
		commsLength = ((gNvmCfg.dev.commConf.Max_Upload_Retries - 1) * gNvmCfg.dev.commConf.Retry_Interval_Upload) / 60;
		// interval in seconds

		commsLength += (gNvmCfg.dev.commConf.Max_Upload_Retries * UPLOAD_FIXED_OVERHEAD_MINS);	// 10 mins allowed for each comms duration
	}

	if(debug)
	{
		printf("UP: %d\r\n", commsLength*60);
	}

	if((measureLength + commsLength) > (MINS_IN_A_DAY - MEASURE_FIXED_OVERHEAD_MINS - UPLOAD_FIXED_OVERHEAD_MINS))
	{
		// oops..
		available_mins = 0;
	}
	else
	{
		available_mins = MINS_IN_A_DAY - measureLength - commsLength - MEASURE_FIXED_OVERHEAD_MINS - UPLOAD_FIXED_OVERHEAD_MINS;
	}

	if(debug)
	{
		printf("US: %d\r\n", available_mins*60);
	}

	return available_mins;
}

/*
 * calculateUploadOffset
 *
 * @desc Calculate a 'random' offset (based on IMEI) used to determine upload schedule time
 *       Uses measure settings to determine the start of the upload window
 *
 * @return - Upload offset in minutes
 */
static uint32_t calculateUploadOffset(uint32_t availableMinutes)
{
	uint32_t uploadOffset_mins;

	/*
	 * Calculate start point of available upload time 'block'
	 */

	/*
	 * Now use the last 3 of first 14 digits of IMEI (as 15th digit is not unique)
	 * to calculate an offset into available time window
	 */
	 if(!convertStringToNumber((char*)&gNvmCfg.dev.modem.imei[11], 3, &uploadOffset_mins))
	 {
		LOG_DBG(LOG_LEVEL_APP, "%s: Failed to convert IMEI string to number\n", __func__);
		 return 0;
	 }
	 uploadOffset_mins = ((uploadOffset_mins * availableMinutes) / NODES_SUPPORTED) % MINS_IN_A_DAY;

	return (uploadOffset_mins);
}

/*
 * calculateUploadDayOffset
 *
 * @desc Calculate a 'random' offset (based on IMEI) used to determine upload schedule day
 *       Uses upload repeat as maximum number of seconds offset
 *
 * @return - Number of days offset
 *
 */
static uint8_t calculateUploadDayOffset()
{
	uint32_t uploadDayOffset;
	/*
	 * Now use the last 3 of first 14 digits of IMEI (as 15th digit is not unique)
	 * to calculate an offset into available day window
	 */
	 if(!convertStringToNumber((char*)&gNvmCfg.dev.modem.imei[11], 3, &uploadDayOffset))
	 {
		LOG_DBG(LOG_LEVEL_APP, "%: Failed to convert IMEI string to number\n", __func__);
		 return 0;
	 }
	 uploadDayOffset %= DAYS_IN_WEEK;

	 return (uint8_t)(uploadDayOffset + 1);
}

static bool convertStringToNumber(const char* str, const uint32_t width, uint32_t *result)
{
	int i;

	*result = 0;
	for(i=0; i<width; i++)
	{
		// string too short?
		if(!str[i])
		{
			*result = 0;
			return false;
		}

		if(isdigit((int)str[i]))
		{
			*result *= 10;
			*result += (str[i]-'0');
		}
		else // bad character
		{
			*result = 0;
			return false;
		}
	}

#if 0
	// string too long?
	if(str[i])
	{
		*result = 0;
		return false;
	}
#endif

	return true;
}




#ifdef __cplusplus
}
#endif