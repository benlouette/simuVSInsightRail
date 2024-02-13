#ifdef __cplusplus
extern "C" {
#endif

/*
 * xTaskApp.c
 *
 *  Created on: Sep 27, 2016
 *      Author: BF1418
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "printgdf.h"

#include "pmic.h"

#include "Timer.h"
#include "xTaskAppEvent.h"
#include "xTaskDefs.h"
#include "configLog.h"
#include "log.h"
#include "device.h"
#include "NvmConfig.h"
#include "pinconfig.h"

#include "Modem.h"

#include "xTaskApp.h"

#include "configFeatures.h"
#include "dataDef.h"
#include "configData.h"
#include "pinDefs.h"

#include "fsl_adc16_hal.h"
#include "fsl_adc16_driver.h"

#include "DrvTmp431.h"
#include "xTaskAppRtc.h"
#include "xTaskAppGNSS.h"
#include "ds137n.h"
#include "rtc.h"
#include "hdc1050.h"
#include "lis3dh.h"
#include "ExtFlash.h"

#include "PassRailAnalogCtrl.h"
#include "AD7766_Common.h"

#include "NvmData.h"
#include "xTaskAppCommsTest.h"
#include "configSvcData.h"

#include "configBootloader.h"
#include "DataStore.h"

#include "taskGnss.h"
#include "gnssMT3333.h"
#include "Vbat.h"

#include "schedule.h"

#include "boardSpecificCLI.h"
#include "selfTest.h"
#include "Measurement.h"
#include "nfc.h"
#include "EnergyMonitor.h"
#include "xTaskDevice.h"
#include "crc.h"
#include "xTaskAppOta.h"
#include "fsl_rtc_driver.h"           // Defines g_rtcBase
#include "temperature.h"
#include "binaryCLI.h"
#include "image.h"
#include "errorcodes.h"
#include "alarms.h"

// to skip the datastore dumps at start and end
//#define LIMIT_CLI_SPAMMING

// Wait for an OTA notification for a max of 3 minutes.
#define PWR_ON_FLSAFE_OTA_REQ_WAIT_TIME_MILLISECS	(3 * 60 * 1000)
#define PWR_ON_FLSAFE_RTC_WAKEUP_TIME_MINS			(120)
#define PWR_ON_FLSAFE_EXT_WDG_WAKEUP_TIME_MINS		(90)
#define EVENTQUEUE_NR_ELEMENTS_APPLICATION  		(4)     //! Event Queue can contain this number of elements

// defines a margin in millisecs for the maximum sampling duration.
#define SAMPLING_TIME_MARGIN_MILLISECS				(1500)

#define MEMS_MIN_Y_VERTICAL							(0.8)
#define MEMS_AVG_DURATION							(10)
#define MEMS_AVG_SPS								(10)
#define MEMS_ADDITIONAL_10SECS						(10000)

#define CADENCE_250									(250)
#define CADENCE_750									(750)

#define ST_PASS_COUNT								(2)
#define ST_FAIL_COUNT								(15)
#define PFS_COUNT									(40)
#define RESTORE_ENERGY_REMAINING_PERCENT			(90)
static QueueHandle_t      EventQueue_Application;
extern uint32_t Firmware_Version;
extern bool bWakeupIsMAGSwipe;

////////////////////////////////////////////
// globals go here
extern bool gSuspendApp;

#ifdef EXT_WDG_DS1374_WAKEUP_TEST
extern bool g_bTriggerExtWatchdogWakeUp;
#endif

// Represents the last comms status. True indicates the last comms was successful.
extern bool lastCommsOk;

////////////////////////////////////////////
// private function prototypes go here
static void HandleColdBoot();
static void HandleThermalWakeup();
static void HandleSelfTestWakeup(WAKEUP_CAUSES_t wakeReason, ST_FLAGS_BIT_MASK_t selftestMask);
static void HandleRtcWakeup(bool overruleScheduler, bool doMeasurements);
static void EnterEngineeringMode(uint32_t engModeTimeout);
static void EngineeringMode();

static void wakeUp(WAKEUP_CAUSES_t reason);
static void comms(uint32_t funcNr, uint32_t repeatCount);

static void initAppNvmData();

static void AppMeasureIsCompleteCallback(void);
static void openFlashRetrieveMeasData();

///////////////////////////////////////////

// Sampling
volatile bool g_bAppSamplingIsComplete = false;


// data for communication with the extflash task
tExtFlashHandle extFlashHandle = { .EventQueue_extFlashRep=NULL};

// LED states
typedef enum {
	LED_OFF,
	LED_ON
}  tLedStates;

// Event descriptors
typedef enum {
    ApplicationEvt_Undefined = 0,
    ApplicationEvt_WakeUp,
    ApplicationEvt_Comms,
	ApplicationEvt_binaryCliOta
} tApplicationEventDescriptor;

// Event structure for application task
typedef struct
{
    // Event Descriptor
	tApplicationEventDescriptor Descriptor;
	union {
	    struct {
	        WAKEUP_CAUSES_t reason;
	    } wakeUp;
	    struct {
	        uint32_t funcNr;
	        uint32_t repeatCount;
	    } commsTest;
	} data;

} tApplicationEvent;

// Report structure - not static, shared by schedule.c
// TODO have an access function
report_t gReport;

// By default the node task is to upload for all wakeup causes, except on RTC wakeup.
// not static, shared by schedule.c
// TODO have an access function or factor out
uint8_t nodeTaskThisCycle = NODE_WAKEUP_TASK_UPLOAD;

TaskHandle_t   _TaskHandle_APPLICATION;		// not static because of easy print of task info in the CLI
// TODO: make it static and 'register' task handle with reporting function

// Create a semaphore for Sampling
static SemaphoreHandle_t gSemDoSample = NULL;

// These vars are used to calculate Node active / Up time.
static uint32_t nodeStartCounter_ticks = 0;
static uint32_t nodeStopCounter_ticks = 0;

static void logLifeCycle()
{
	float energyTotal_J = 0;
	if(!EnergyMonitor_GetEnergyConsumed_J(&energyTotal_J, NULL))
	{
		LOG_EVENT(eLOG_ENERGY_DATA, LOG_NUM_APP, ERRLOGMAJOR, "%s(): Failed to read energy from PMIC", __func__);
		return;
	}

	LOG_DBG(LOG_LEVEL_APP, "\nTotal Energy used so far: %.2fJ\n", energyTotal_J);
	int value = (int)((energyTotal_J)*100.0);
	// would've used %.2f but it doesn't seem to work!!!

	if(!Device_HasPMIC())
	{
		uint32_t dsCounter = 0;
		if(!DS137n_ReadCounter(&dsCounter))
		{
			LOG_EVENT(eLOG_DS374, LOG_NUM_APP, ERRLOGFATAL, "DS1374 read failed");
		}

		LOG_EVENT(eLOG_COLDBOOT, LOG_NUM_APP, ERRLOGDEBUG, "Energy used = %d.%02d J, Seconds since COLDBOOT = %u",
				value/100, value%100, (unsigned int)dsCounter);
	}
	else
	{
		LOG_EVENT(eLOG_NORMAL, LOG_NUM_APP, ERRLOGDEBUG, "Energy used = %d.%02d J", value/100, value%100);
	}
}

/*
 * SetStatusLed
 *
 * @desc    Set the status led count times ON/OFF
 *
 * @param   -	state - LED on/off
 *
 * @return -
 */
void xTaskApp_SetStatusLed(tLedStates state)
{
	uint32_t statusLed = SYSTEM_STATUS_LED1;

	if(Device_GetHardwareVersion() == HW_PASSRAIL_REV1)
	{	// Rev 1 hardware
		statusLed = SYSTEM_STATUS_LED2;
	}

	pinConfigDigitalOut(statusLed, kPortMuxAsGpio, 0, false);

	if (state == LED_ON)
	{
		GPIO_DRV_SetPinOutput( statusLed );
	}
	else
	{
		GPIO_DRV_ClearPinOutput( statusLed );
	}

	return;
}

// TODO only these next two functions should be in this file

/*!
 * xTaskApp_Init
 *
 * @brief      Initialize the xTaskAppe module before the scheduler is
 *             starting the actual task xTaskApp_xTaskApp().
 *
 * @param      pvParams
 */
void xTaskApp_Init()
{
	// Create event queue
	EventQueue_Application         = xQueueCreate( EVENTQUEUE_NR_ELEMENTS_APPLICATION, sizeof(tApplicationEvent));
    vQueueAddToRegistry(EventQueue_Application, "_EVTQ_APPLICATION");

    xTaskAppCommsTestInit();// TODO: remove double event queue's for the application task

    xTaskCreate( xTaskApp_xTaskApp,               			// Task function name
	                 "APPLICATION",                 // Task name string
					 STACKSIZE_XTASK_APP ,    		// Allocated stack size on FreeRTOS heap
	                 NULL,                   		// (void*)pvParams
					 PRIORITY_XTASK_APP,     		// Task priority
	                 &_TaskHandle_APPLICATION );    // Task handle

    gSemDoSample = xSemaphoreCreateBinary();
    memset((uint8_t*)&gReport, 0x00, sizeof(gReport));
    if(gSemDoSample != NULL)
    {
    	// Give the semaphore before it can be taken.
    	xSemaphoreGive(gSemDoSample);
    }
}

/*!
 * xTaskApp_xTaskApp
 *
 * @brief      Task function for the Application. This function is the task
 *             controlled by the RTOS and handles the APPLICATION task events.
 *
 * @param      pvParameters
 */
void xTaskApp_xTaskApp( void *pvParameters )
{
	tApplicationEvent applicationEvent;

	for (;;)
	{
		if (xQueueReceive(EventQueue_Application, &applicationEvent, portMAX_DELAY))
		{
			nodeStartCounter_ticks = xTaskGetTickCount();

			switch (applicationEvent.Descriptor)
			{
				case ApplicationEvt_Comms:
					comms(applicationEvent.data.commsTest.funcNr,
						  applicationEvent.data.commsTest.repeatCount);// added by george for testing communication stuff
				break;

				case ApplicationEvt_binaryCliOta:
					commsDoOtaOnly();
					otaRebootRequired();
				break;

				case ApplicationEvt_WakeUp:
					wakeUp(applicationEvent.data.wakeUp.reason);

					// always shutdown after normal application handling unless we got here from Eng mode!
					if(!Device_GetEngineeringMode())
					{
						xTaskDeviceShutdown(NODE_WAKEUP_TASK_UPLOAD & nodeTaskThisCycle);
					}
				break;

				default :
					LOG_EVENT(eLOG_SCHEDULE_WAKEUP, LOG_NUM_APP, ERRLOGMAJOR, "%s(): Unknown Event", __func__);
				break;
			}
		}
	}	// end of for (;;)
}

/*
 * wakeUp
 *
 * @desc normal application processing done here
 *
 * @params reason   the wakeup cause defines what we will do next.
 *
 */
static void wakeUp(WAKEUP_CAUSES_t reason)
{
    // Read the measurement and COMMS record from external flash.
	openFlashRetrieveMeasData();

	// Update the wakeup reason.
	commsRecord.params.Wakeup_Reason = reason;
	LOG_DBG(LOG_LEVEL_APP, "\n\n Wakeup reason: %d \n", reason);

	switch (reason)
	{
		case WAKEUP_CAUSE_COLD_BOOT:
			HandleColdBoot();
			break;

		case WAKEUP_CAUSE_POWER_ON_FAILSAFE:
        case WAKEUP_CAUSE_RTC:
            HandleRtcWakeup(false, false);
            break;

        case WAKEUP_CAUSE_RTC_FORCED_TEMPERATURE:
        case WAKEUP_CAUSE_RTC_FORCED_MEASUREMENT:
			{
				// check if VBAT has simulated temperatures
				const SimulatedTemperature_t const* pSimulated = Vbat_CheckForSimulatedTemperatures();
				if(!((pSimulated->flags & VBATRF_remote_is_simulated) && (pSimulated->flags & VBATRF_local_is_simulated)))
				{
					ReadTmp431Temperatures(&gReport.tmp431.localTemp, &gReport.tmp431.remoteTemp);
				}
				if(pSimulated->flags & VBATRF_remote_is_simulated)
				{
					gReport.tmp431.remoteTemp = pSimulated->remote;
					LOG_EVENT(eLOG_TMP431, LOG_NUM_APP, ERRLOGWARN, "Remote temperature is simulated! (%d)", pSimulated->remote);
				}
				if(pSimulated->flags & VBATRF_local_is_simulated)
				{
					gReport.tmp431.localTemp = pSimulated->local;
					LOG_EVENT(eLOG_TMP431, LOG_NUM_APP, ERRLOGWARN, "Local temperature is simulated! (%d)", pSimulated->local);
				}
				commsRecord.params.Wakeup_Reason = WAKEUP_CAUSE_RTC;
				HandleRtcWakeup(reason==WAKEUP_CAUSE_RTC_FORCED_MEASUREMENT, DO_MEASUREMENTS);
				// save the tracking data for reboots
				NvmDataUpdateIfChanged();
			}
            break;

        case WAKEUP_CAUSE_RTC_FORCED_DATAUPLOAD:
        	commsRecord.params.Wakeup_Reason = WAKEUP_CAUSE_RTC;
            HandleRtcWakeup(true, DO_DATA_UPLOADS);
            // save the tracking data for reboots
            NvmDataUpdateIfChanged();
            break;

		case WAKEUP_CAUSE_THERMOSTAT:
			HandleThermalWakeup();
			break;

		case WAKEUP_CAUSE_FULL_SELFTEST:
			if(Device_HasPMIC())
			{
				HandleSelfTestWakeup(reason, ST_NONE);
			}
			break;

		case WAKEUP_CAUSE_SELFTEST_REDUCED:
			if(Device_HasPMIC())
			{
				HandleSelfTestWakeup(reason, ST_REDUCED);
			}
			break;
		default:
			LOG_EVENT(eLOG_FATAL, LOG_NUM_APP, ERRLOGFATAL, "%s(): undefined event %d", __func__, reason);
			break;
	}

	nodeStopCounter_ticks = xTaskGetTickCount();

	// temperature measurement might be less than one second so ignore
	if(nodeStopCounter_ticks > nodeStartCounter_ticks)
	{
		// Update the node up-time.
		uint32_t nodeActiveTime_secs = ((nodeStopCounter_ticks - nodeStartCounter_ticks) * portTICK_PERIOD_MS) / 1000;
		commsRecord.params.Up_Time += nodeActiveTime_secs;
		Vbat_SetUpTime(commsRecord.params.Up_Time);
		LOG_DBG(LOG_LEVEL_APP, "\nNodeAge: %d[secs], Node age this cycle: %d[secs]\n", commsRecord.params.Up_Time, nodeActiveTime_secs);
	}
}


/*
 * UpdateCommsVoltages
 *
 * @desc 	Updates the voltage data in the comms record
 */
static void UpdateCommsVoltages()
{
	// get the final dc level
	if(EnergyMonitor_ReadDCLevel_v(&gReport.dcLevel.dcLevelVolts[DCLEVEL_AT_POWEROFF]))
	{
		LOG_DBG( LOG_LEVEL_APP, "SELFTEST V_2 = %f\n", gReport.dcLevel.dcLevelVolts[DCLEVEL_AT_POWEROFF]);
	}
	else
	{
		LOG_DBG( LOG_LEVEL_APP, "ERROR reading dc level - V_2 failed v = %f\n", gReport.dcLevel.dcLevelVolts[DCLEVEL_AT_POWEROFF]);
	}

	// dc levels and energy
	commsRecord.params.V_0 = gReport.dcLevel.dcLevelVolts[DCLEVEL_AT_START];		// supercap Voltage - at wake    	(previous cycle)
	commsRecord.params.V_1 = gReport.dcLevel.dcLevelVolts[DCLEVEL_BEFORE_MODEM];	// supercap Voltage - before modem  (previous cycle)
	commsRecord.params.V_2 = gReport.dcLevel.dcLevelVolts[DCLEVEL_AT_POWEROFF];		// supercap Voltage - at sleep    	(previous cycle)
}


/*
 * UpdateCommsEnergyUsage
 *
 * @desc 	Updates the (comms) energy usage data in the comms record and VBAT
 */
static void UpdateCommsEnergyUsage(const float energyTotal_J)
{
	if(!Vbat_AddEnergyUsedInCommsCycles(energyTotal_J))
	{
		LOG_EVENT(eLOG_ENERGY_DATA, LOG_NUM_APP, ERRLOGMAJOR, "%s(): Failed to update Modem energy", __func__);
		return;
	}

	float updatedEnergy;
	if(Vbat_GetEnergyUsedInCommsCycles(&updatedEnergy))
	{
		commsRecord.params.E_Modem_Cycle = updatedEnergy;
		LOG_DBG(LOG_LEVEL_APP, "\nNet Comms Energy Used: %.2fJ\n", commsRecord.params.E_Modem_Cycle);
	}
	else
	{
		LOG_EVENT(eLOG_ENERGY_DATA, LOG_NUM_APP, ERRLOGMAJOR, "%s(): Failed to retrieve Modem energy", __func__);
	}
}


/*
 * UpdateCommsTimeParams
 *
 * @desc 	Updates the modem related timing parameters in the comms record.
 *
 * @param 	pModemON_msecs - pointer to modem On duration in milli secs.
 * @param 	pUpload_msecs - pointer to Upload duration in milli secs.
 *
 */
static void UpdateCommsTimeParams(uint32_t *pModemON_msecs, uint32_t *pUpload_msecs)
{
	uint32_t modemOnDuration_msecs = 0;
	uint32_t providerConfigDone_msecs = 0;
	uint32_t dataUploadDuration_msecs = 0;
	// Record the modem info
	Modem_metrics_t *pModem_metrics = Modem_getMetrics();
	const uint32_t startTime = pModem_metrics->timeStamps.start;

	commsRecord.params.Time_Elap_TCP_Connect = 0;
	commsRecord.params.Time_Elap_TCP_Disconnect = 0;

	// If the timestamps are valid then extract the data.
	if(pModem_metrics->timeStamps.validTimestamps)
	{
		if(pModem_metrics->timeStamps.serviceConnectDone > startTime)
		{
			commsRecord.params.Time_Elap_TCP_Connect = (pModem_metrics->timeStamps.serviceConnectDone - startTime) * MSEC_TO_SEC_CONV_FACTOR;
		}

		if(pModem_metrics->timeStamps.serviceDisconnectDone > startTime)
		{
			commsRecord.params.Time_Elap_TCP_Disconnect = (pModem_metrics->timeStamps.serviceDisconnectDone - startTime) * MSEC_TO_SEC_CONV_FACTOR;
		}

		if(pModem_metrics->timeStamps.deregisterDone > pModem_metrics->timeStamps.serviceConnectDone)
		{
			dataUploadDuration_msecs = (pModem_metrics->timeStamps.deregisterDone - pModem_metrics->timeStamps.serviceConnectDone);
		}

		if(pModem_metrics->timeStamps.providerConfigDone > startTime)
		{
			providerConfigDone_msecs = (pModem_metrics->timeStamps.providerConfigDone - startTime);
		}
	}
	else
	{
#if 0
		LOG_EVENT(0, LOG_NUM_APP, ERRLOGFATAL, "start: %8u, poweron: %8u, sysstart: %8u",
					pModem_metrics->timeStamps.start,
					pModem_metrics->timeStamps.poweron,
					pModem_metrics->timeStamps.sysstart);
		LOG_EVENT(0, LOG_NUM_APP, ERRLOGFATAL,	"cellCfg: %8u, providerCfg: %8u, srvCfgDone: %8u",
					pModem_metrics->timeStamps.cellularConfigDone,
					pModem_metrics->timeStamps.providerConfigDone,
					pModem_metrics->timeStamps.serviceConfigDone);
		LOG_EVENT(0, LOG_NUM_APP, ERRLOGFATAL, "readSignalStrength: %8u, serviceConnectDone: %8u, serviceDisconnectDone: %8u",
					pModem_metrics->timeStamps.readSignalStrengthDone,
					pModem_metrics->timeStamps.serviceConnectDone,
					pModem_metrics->timeStamps.serviceDisconnectDone);
		LOG_EVENT(0, LOG_NUM_APP, ERRLOGFATAL,"deregisterDone: %8u, UHS5E_shutdown: %8u, poweroff: %8u",
					pModem_metrics->timeStamps.deregisterDone,
					pModem_metrics->timeStamps.UHS5E_shutdown,
					pModem_metrics->timeStamps.poweroff);
#else
		// simply report a list of csv
		LOG_EVENT(eLOG_CONNECTION_TIMES, LOG_NUM_APP, ERRLOGFATAL, "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
					pModem_metrics->timeStamps.start,
					pModem_metrics->timeStamps.poweron,
					pModem_metrics->timeStamps.sysstart,
					pModem_metrics->timeStamps.cellularConfigDone,
					pModem_metrics->timeStamps.providerConfigDone,
					pModem_metrics->timeStamps.serviceConfigDone,
					pModem_metrics->timeStamps.readSignalStrengthDone,
					pModem_metrics->timeStamps.serviceConnectDone,
					pModem_metrics->timeStamps.serviceDisconnectDone,
					pModem_metrics->timeStamps.deregisterDone,
					pModem_metrics->timeStamps.UHS5E_shutdown,
					pModem_metrics->timeStamps.poweroff);

#endif
	}

	// Determine modem ON duration
	if(pModem_metrics->timeStamps.poweroff > pModem_metrics->timeStamps.poweron)
	{
		modemOnDuration_msecs = (pModem_metrics->timeStamps.poweroff - pModem_metrics->timeStamps.poweron);
	}
	else
	{
		// ok, we may not have stopped the modem correctly so lets do a catch-all
		// cause the modem will be powered of when node turns off
		uint32_t stop = xTaskGetTickCount();
		if(stop > pModem_metrics->timeStamps.poweron)
		{
			modemOnDuration_msecs = (stop - pModem_metrics->timeStamps.poweron);
		}
	}

	commsRecord.params.Time_Elap_Registration = providerConfigDone_msecs * MSEC_TO_SEC_CONV_FACTOR;
	commsRecord.params.Time_Elap_Modem_On = modemOnDuration_msecs * MSEC_TO_SEC_CONV_FACTOR;// millisecs to seconds
	commsRecord.params.Time_Last_Wake = measureRecord.timestamp;

	*pModemON_msecs = modemOnDuration_msecs;
	*pUpload_msecs = dataUploadDuration_msecs;

	LOG_DBG(LOG_LEVEL_APP,
			"\n--------------------------------------------"
			"\nThis Cycle Data....."
			"\nRegistration: %d, TCP connected: %d"
			"\nTCP disconnected: %d, Modem on: %d"
			"\nTimingValidity: %d"
			"\nAccessTechnology: %dG"
			"\nCommsSignalQuality: %d"
			"\n--------------------------------------------\n",
			commsRecord.params.Time_Elap_Registration,
			commsRecord.params.Time_Elap_TCP_Connect,
			commsRecord.params.Time_Elap_TCP_Disconnect,
			commsRecord.params.Time_Elap_Modem_On,
			pModem_metrics->timeStamps.validTimestamps,
			pModem_metrics->accessTechnology + 1,
			commsRecord.params.Com_Signal_Quality);
}

/*
 * prepareForSleep
 *
 * @desc Prepares the node for the upcoming sleep period
 *
 * @params	bDoDataUpload - when true perform a data upload
 * @params	bMeasurement - when true perform some measurement functions
 * @params	bSleepLED - when true flash LED in sleep sequence
 *
 * @returns true
 */
static void prepareForSleep(bool bDoDataUpload, bool bMeasurement, bool bSleepLED)
{
	uint32_t modemOnDuration_msecs = 0;
	uint32_t dataUploadDuration_msecs = 0;

	if(bDoDataUpload)
	{
		// Perform a data upload to the network
		gNvmData.dat.schedule.noOfUploadAttempts++;
		gNvmData.dat.schedule.bDoMeasurements = DO_DATA_UPLOADS;
		if(gNvmData.dat.schedule.noOfUploadAttempts <= gNvmCfg.dev.commConf.Max_Upload_Retries)
		{
			// capture life cycle
			logLifeCycle();

			// TODO we should probably handle the returned boolean
			commHandling(15, false);

			if(gNvmData.dat.is25.noOfCommsDatasetsToUpload == 0x00)
			{	// upload successful
				// initAppNvmData();
				gNvmData.dat.schedule.noOfUploadAttempts =
				gNvmData.dat.schedule.noOfGoodMeasurements =
				gNvmData.dat.schedule.noOfMeasurementAttempts = 0x00;
			}
		}
	}

    if(bDoDataUpload || bMeasurement)
    {
    	// Update the comms record for energy usage
    	float energyTotal_J = 0;
		float energyThisCycle_J = 0;

		bool bEnergyTotalReadOk = EnergyMonitor_GetEnergyConsumed_J(&energyTotal_J, &energyThisCycle_J);
    	if(!bEnergyTotalReadOk)
    	{
    		LOG_EVENT(eLOG_ENERGY_DATA, LOG_NUM_APP, ERRLOGMAJOR, "%s(): Failed to read energy from PMIC", __func__);
    	}

		if(gNvmData.dat.schedule.bDoMeasurements != DO_MEASUREMENTS)
		{
			// data upload cycle - update comms related items
			UpdateCommsTimeParams(&modemOnDuration_msecs, &dataUploadDuration_msecs);
			if(bEnergyTotalReadOk)
			{
				UpdateCommsEnergyUsage(energyThisCycle_J);
			}
			UpdateCommsVoltages();
		}

		// retrieve our (possibly) updated modem total
		float energyModemTotal_J;

		bool bReadModemEok = Vbat_GetEnergyUsedInCommsCycles(&energyModemTotal_J);
		if(!bReadModemEok)
		{
			LOG_EVENT(eLOG_ENERGY_DATA, LOG_NUM_APP, ERRLOGMAJOR, "%s(): Failed to read modem energy from VBAT", __func__);
		}

		if(bReadModemEok && bEnergyTotalReadOk && (energyTotal_J > energyModemTotal_J))
		{
			// Update the total (minus modem)
			commsRecord.params.E_Previous_Wake_Cycle = energyTotal_J - energyModemTotal_J;
		}
		else
		{
			LOG_EVENT(eLOG_ENERGY_DATA, LOG_NUM_APP, ERRLOGMAJOR, "%s(): Could not update E_Previous_Wake_Cycle (ReadVBAT:%d ReadPMIC:%d E_diff: %.2f)",
					__func__, bReadModemEok, bEnergyTotalReadOk, energyTotal_J - energyModemTotal_J);
		}

		LOG_DBG(LOG_LEVEL_APP,	"\n----------------------------------------------------\n");

    	// We've done a data upload or a measurement so save the comms record for next time
    	storeCommsData();
    }

	// #659370 E_Previous_Wakeup
	// Make PMIC store measured energy-use before reboot to avoid loosing it
	//
	{
		int8_t tries = 3;
		while(tries)
		{
			tries--;
			// log try to terminal
			if( PMIC_sendStoreEnergyUse() )
			{
				// if message delivered and response OK - go on with reboot
				// log OK to terminal
				break;
			}
			else
			{
				// log ERR to terminal
			}
		}
	}


    // do we have an OTA reboot pending
    otaRebootRequired();

	schedule_CalculateNextWakeup();

	// Store the temperature record.
	Temperature_WriteRecord(gReport.tmp431.remoteTemp, RTC_HAL_GetSecsReg(g_rtcBase[RTC_IDX]));

#ifdef EXT_WDG_DS1374_WAKEUP_TEST
	// IMP_NOTE: This should NOT be enabled in the production S/W
	// TODO: This could be removed when UT are developed.
	if(g_bTriggerExtWatchdogWakeUp)
	{
		printf("\n Wakeup from POFS Next time. in 2mins... \n");
		// Set the RTC Wakeup time > External Watchdog time, so as to trigger
		// External watchdog DS1374 to wake up the node.
		scheduleNextWakeup(30, 2);
	}
#endif

    printf("\nPrepareForSleep()\n");

    EnergyMonitor_Report();

	if(bDoDataUpload && ((dbg_logging) & (LOG_LEVEL_CLI)))
	{
		DataStore_dumpDatastore();
	}

	if(bSleepLED)
	{
		// flash LED x40 in 20 secs
		xTaskApp_flashStatusLedCadence(PFS_COUNT, CADENCE_250, CADENCE_250);
	}

	// NOTE:- gNvmData is saved before node shutdown
}

static void comms(uint32_t funcNr, uint32_t repeatCount)
{
    uint32_t EnableApp= (funcNr & 0xffff) | (repeatCount << 16);// dirty trick to pass second argument

    printf("\nApplication task now running a comms test func=%d, repeat = %d!\n",funcNr, repeatCount);
    if ((EnableApp & 0xffff) >0)
    {
        commHandling(EnableApp, true);
    }
}

/*
 * xTaskApp_startApplicationTask
 *
 * @desc - wakeup the application
 *
 * @params - reason wakeup reason

 *
 */
bool xTaskApp_startApplicationTask(WAKEUP_CAUSES_t wakeupReason)
{
    tApplicationEvent applicationEvent =
    {
    	.Descriptor = ApplicationEvt_WakeUp,
    	.data.wakeUp.reason = wakeupReason
    };

    return (pdTRUE == xQueueSend(EventQueue_Application, &applicationEvent, 500/portTICK_PERIOD_MS));
}


/*
 * xTaskApp_commsTest
 *
 * @desc - quick workaround to get rid of the global enableapp and 'bricking the box'
 *
 * @params - testFuncNum
 * @params - repeatCount
 *
 */
bool xTaskApp_commsTest(uint32_t testFuncNum, uint32_t repeatCount)
{
    tApplicationEvent applicationEvent =
    {
    	.Descriptor = ApplicationEvt_Comms,
    	.data.commsTest.funcNr = testFuncNum,
    	.data.commsTest.repeatCount = repeatCount
    };

    return (pdTRUE == xQueueSend(EventQueue_Application, &applicationEvent, 500/portTICK_PERIOD_MS));
}

//------------------------------------------------------------------------------
/// Start a task to allow OTA via binaryCLI
///
/// @return bool true = Success, false = Failure
//------------------------------------------------------------------------------
bool xTaskApp_binaryCliOta()
{
	otaSetNotifyTaskTimeout(60000);

    tApplicationEvent applicationEvent =
    {
    	.Descriptor = ApplicationEvt_binaryCliOta,
    };

    return (pdTRUE == xQueueSend(EventQueue_Application, &applicationEvent, 500/portTICK_PERIOD_MS));
}

/*
 * initAppNvmDataIS25
 *
 * @desc - Initialise external flash NVM data
 *
 * @params
 *
 */
void initAppNvmDataIS25(void)
{
	gNvmData.dat.is25.is25CurrentDatasetNo = FIRST_DATASET;		// TODO assume for now all saved measurement have been uploaded
	gNvmData.dat.is25.noOfCommsDatasetsToUpload = 0x00;
	gNvmData.dat.is25.noOfMeasurementDatasetsToUpload = 0x00;
	gNvmData.dat.is25.measurementDatasetStartIndex = FIRST_DATASET;
}

/*
 * initAppNvmData
 *
 * @desc - Restore initial counters
 *
 * @params
 *
 */
static void initAppNvmData()
{
	// as a data upload has been done - even it it failed - clear everything to start again
	initAppNvmDataIS25();

	gNvmData.dat.schedule.noOfGoodMeasurements = 0x00;
	gNvmData.dat.schedule.noOfMeasurementAttempts = 0x00;
	gNvmData.dat.schedule.noOfUploadAttempts = 0x00;
}

/*
 * HandleThermalWakeup
 *
 * @desc Handles the high or low temp wakeup cycle - It is a requirement to attempt to upload measurement dataset
 * 			in this case
 *
 * @params
 *
 */
static void HandleThermalWakeup()
{
	if(Device_HasPMIC())
	{
		RtcCheckRTC();
	}

	// precaution again something going wrong and node not being able to wakeup
	schedule_ScheduleNextWakeup(30, 30 + POFS_MIN);
	LOG_DBG(LOG_LEVEL_APP, "In %s()\n",__func__);

#if defined(DISABLE_MEASURE_DURING_ALARM_RETRY)
	// no need to do more measurements if we are retrying to communicate
	if(!alarms_RetryInProgress())
	{
#endif
		Measurement_DoMeasurements(true);
#if defined(DISABLE_MEASURE_DURING_ALARm_RETRY)
	}
#endif

	PMIC_SendSelfTestReqMsg();
	selfTest(ST_REDUCED + ST_GNSS_ALREADY_ON + ST_LOG_TEMP_ALARM);

	// Do data upload and prepare the node for sleep
	prepareForSleep(true, false, false);
}


/*
 * HandleSelfTestWakeup
 *
 * @desc Handles the user-initiated self-test (via NFC)
 * 		 Harvester only.
 *
 * @params wakeReason - specify the wake reason.
 *		   selftestMask - Specify additional selftest masks, over and above
 *		   no ext flash re-init, include red/amber alarms & RTC alarm test.
 */
static void HandleSelfTestWakeup(WAKEUP_CAUSES_t wakeReason, ST_FLAGS_BIT_MASK_t selftestMask)
{
	RtcCheckRTC();
	PMIC_SendSelfTestReqMsg();

	bool bStartComms = false;
	/*
	 * Do self tests - don't re-init flash, include red/amber alarms and RTC alarm
	 * test.
	 */
	selfTest(ST_NO_EXTF_INIT + ST_LOG_TEMP_ALARM + ST_PERFORM_RTC_ALARM_TEST + selftestMask);

	if(dbg_logging & LOG_LEVEL_PMIC)
	{
		printf("Self-test results: ");
		for(int i = 0; i < MAXSTATUSSELFTESTLENGTH; i++)
		{
			printf("%02d ", commsRecord.params.Status_Self_Test[i]);
		}
		printf("\n");
	}

	if(WAKEUP_CAUSE_FULL_SELFTEST == wakeReason)
	{
		image_FirstTimePmicBackup();
		bStartComms = true;
	}

	// Do data upload and prepare the node for sleep
	prepareForSleep(bStartComms, false, false);
}


/*
 * xTaskApp_makeIs25ReadyForUse
 *
 * @desc - Ensure that the is25 is ready for use
 *
 */
bool xTaskApp_makeIs25ReadyForUse()
{
    bool retval = false;
    uint32_t nformatStartTick = 0;
    uint32_t nformatStopTick = 0;

    if(IS25_ReadProductIdentity(NULL) == false)
    {
    	LOG_EVENT(eLOG_FLASH_INIT, LOG_NUM_APP, ERRLOGFATAL, "Failed to Read ExtFlash ID");
    }
    // whatever we do, we will very likely need the external flash, so make sure the data and hardware is set up
	retval = extFlash_InitCommands(&extFlashHandle);
	if (false == retval)
	{
		LOG_EVENT(eLOG_FLASH_INIT, LOG_NUM_APP, ERRLOGFATAL, "Failed to initialise commands");
	}
	else
	{
		// external flash - the is25 system is not ready for use
		if(gNvmData.dat.is25.isReadyForUse == false)
		{
			extern TaskHandle_t   _TaskHandle_CLI;

			// in case the logging is not set
			uint32_t save = dbg_logging;
			dbg_logging |= LOG_LEVEL_CLI + LOG_LEVEL_APP;

			// disable the CLI while we format the flash
			suspendCli();

			LOG_DBG( LOG_LEVEL_APP, "\n***Preparing is25 for use, could take up to %d secs***\n\n", EXTFLASH_CHIP_ERASE_MAXWAIT_MS / 1000);
			nformatStartTick = xTaskGetTickCount();

			// if the commsRecord needs upgraded then do it now
			extFlash_commsRecordUpgrade();

			print_communication_record();

			// Format the IS25 external flash
			int errCode = extFlash_format(&extFlashHandle, EXTFLASH_CHIP_ERASE_MAXWAIT_MS);
			if(errCode == extFlashErr_noError)
			{
				nformatStopTick = xTaskGetTickCount();
				LOG_EVENT(eLOG_FLASH_FORMAT, LOG_NUM_APP, ERRLOGINFO, "IS25 format complete in %d secs", (nformatStopTick - nformatStartTick) / 1000);
				memset(&measureRecord, 0x00, sizeof(measureRecord));
				initAppNvmData();
				gNvmData.dat.schedule.bDoMeasurements = DO_DATA_UPLOADS;
				// This is required to update the totalNumberOfBytes for the measure &
				// comms record before it can be retrieved.
				retval = storeCommsData();
			}
			else
			{
				LOG_EVENT(eLOG_FLASH_FORMAT,
						  LOG_NUM_APP,
						  ERRLOGFATAL,
						  "extFlash_format(Timeout:%d ms) error %s\n",
						  EXTFLASH_CHIP_ERASE_MAXWAIT_MS,
						  extFlash_ErrorString(errCode));
			}
			dbg_logging = save;
			vTaskResume(_TaskHandle_CLI);

			gNvmData.dat.is25.isReadyForUse = true;
			NvmDataWrite(&gNvmData);
		}
	}
	return retval;
}

/*
 * HandleRtcWakeup
 *
 * @desc Handles the rtc wakeup - will either perform measurement routine
 * 		or attempt to uploads all measurement datasets stored in the external is25
 *
 * 	@params
 *
 */
static void HandleRtcWakeup(bool overruleScheduler, bool do_measurements)
{
	bool bDoDataUpload = false;
	bool bMeasurement = false;

	if(Device_HasPMIC())
	{
		RtcCheckRTC();
	}

	// Find out what the node should do now.
    if(true == Vbat_GetNodeWakeupTask(&nodeTaskThisCycle))
	{
    	LOG_DBG(LOG_LEVEL_APP,
    			"\n"
    			"--------------------\n"
    			"NodeTaskList: 0x%02X\n"
			    "--------------------\n",
				nodeTaskThisCycle);
	}
    else
	{
		LOG_EVENT(eLOG_CHECKSUM, LOG_NUM_APP, ERRLOGFATAL, "Node wakeup task Checksum Error");
	}

	if (!overruleScheduler)
	{
		do_measurements = gNvmData.dat.schedule.bDoMeasurements;
	}
	else
	{
		LOG_DBG( LOG_LEVEL_APP, "\nScheduler is overruled !\n\n");
	}

	if((nodeTaskThisCycle & NODE_WAKEUP_TASK_MEASURE) ||
	   (nodeTaskThisCycle & NODE_WAKEUP_TASK_UPLOAD) ||
	   (overruleScheduler == true))
	{
		// precaution again something going wrong and node not being able to wakeup
		schedule_ScheduleNextWakeup(30, 30 + POFS_MIN);

		// save the comms and measure records to is25.
		storeCommsData();

		// determine if this is a measurement cycle or data upload cycle
		if(do_measurements)
		{	// measurement cycle
			LOG_DBG( LOG_LEVEL_APP, "\n.....Measurement cycle.....\n\n");
			/*retval =*/ Measurement_DoMeasurements(false);
			gNvmData.dat.schedule.noOfMeasurementAttempts++;
			bMeasurement = true;
		}
		else
		{
			PMIC_SendSelfTestReqMsg();

			LOG_DBG( LOG_LEVEL_APP, "\n.....Comms cycle.....\n\n");
			// upload cycle
			if(WAKEUP_CAUSE_POWER_ON_FAILSAFE == commsRecord.params.Wakeup_Reason)
			{
				selfTest(ST_REDUCED + ST_PERFORM_RTC_ALARM_TEST);
			}
			else
			{
				selfTest(ST_REDUCED);
			}
			// Get the stop time to calculate the startup energy consumed.
			uint32_t preGnssOnStartupDuration_msecs = DURATION_MSECS(g_nStartTick);

			float fEnergyThisRun;
			if(EnergyMonitor_GetEnergyConsumed_J(NULL, &fEnergyThisRun))
			{
				LOG_DBG(LOG_LEVEL_APP,"\n\n===>StartUp Energy In Comms Cycle:%f, Duration:%d [msec]\n\n", //#TODO Change the text!
					fEnergyThisRun, preGnssOnStartupDuration_msecs);
			}

			bDoDataUpload = true;
		}
	}

	// If required, do data upload then prepare the node for sleep
	prepareForSleep(bDoDataUpload, bMeasurement, false);
}

/*
 * generate_default_records
 *
 * @desc	Initialise COMMS and measurement records
 *
 * @params
 *
 */
static void generate_default_records(int errcode)
{
	LOG_EVENT( errcode, LOG_NUM_APP, ERRLOGWARN, "Creating default COMM & Measurement record");

	// initialise the COMMS record
	memset(&commsRecord, 0, sizeof(commsRecord));
	// initialise non-zero values
    commsRecord.params.GNSS_NS_Com[0]       = ' ';
    commsRecord.params.GNSS_EW_Com[0]       = ' ';

	// initialise the MEASUREMENT record
    memset(&measureRecord, 0, sizeof(measureRecord));
	// initialise non-zero values
    measureRecord.params.GNSS_NS_1[0] = ' ';
    measureRecord.params.GNSS_EW_1[0] = ' ';
}

/*
 * cliSelftestHelp
 *
 * @desc - display help message for self-test
 *
 * @param - argc number of arguments passed to function
 *
 * @param - argv array of pointers to passed parameters
 *
 * @param - argi array of passed integers
 *
 * @return - true if successful, otherwise false
 */
bool cliSelftestHelp(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	const char text[] = "selftest <selfTestFlags>\n"
			            "   1 - GNSS already powered on and running\n"
			            "   2 - run reduced self-test\n"
			            "   4 - do not erase external flash\n"
						"   8 - Record temperature alarms, if detected\n";

	printf((char*)text);

	return true;
}



/*
 * HandlColdBoot
 *
 * @desc    Handles the wakeup cause COLDBOOT. A COLDBOOT is identified from the RTC year being 1970
 *
 * 			There is the option to enter Engineering mode
 * 			Otherwise a gnss fix will be sought - if successful the RTC year will be changed meaning coldboot is no longer possible
 * 			until power to the node is recycled.
 *
 * @param   -
 *
 * @return - void
 */
static void HandleColdBoot()
{
	bool gnssResult = true;
	tGnssCollectedData gnssData;// just a warning, this is a large structure

	commsRecord.params.EnergyRemaining_percent = 100.0f;

	// Clear the temperature measurement storage.
	if(false == Temperature_EraseAllRecords())
	{
		LOG_EVENT(eLOG_TEMP_RECORDS, LOG_NUM_APP, ERRLOGMAJOR, "%s: Failed to clear temperature records", __func__);
	}

	// reset the external DS137n counter so as to have a known starting point.
	if(false == DS137n_ResetCounter())
	{
		LOG_EVENT(eLOG_DS1374_RESET, LOG_NUM_APP, ERRLOGFATAL, "%s: Failed to reset DS1374", __func__);
	}

	// ensure that RTC cannot wake node anytime soon
	// ensure that DS1374 cannot wake node (watchdog function is disabled by writing all zeros)
	uint32_t tempTime = 3 * 365 * 24 * 60;		// set for 3 years in the future
	if(false == schedule_ScheduleNextWakeup(tempTime, 0))
	{
		LOG_EVENT(eLOG_SCHEDULE, LOG_NUM_APP, ERRLOGFATAL, "%s: Schedule next wakeup failed", __func__);
	}

	// provide an opportunity for the user to enter Engineering Mode - allows cli commands until wdog forces node OFF
	EnterEngineeringMode(10);	// timeout in seconds

	// if Engineering Mode has not been entered after a time delay the node will attempt to get a GNSS fix - and if successful
	// the RTC will be updated with the UTC time
	gnssResult = gnssCommand(GNSS_POWER_ON, 8000);

	if(gnssResult)
	{
		gnssResult = gnssCommand(GNSS_READ_RMC, 0);// nowait, because we also kick off the modem initial setup in parallel
	}

	// while the gnss can try to find the time, we can set the correct modem baud, and retrieve the modem ID numbers
	if(false == deviceFirstConfigInitialisation())
	{
	    // this is important, wait 10 seconds and lets try again !
	    vTaskDelay( 10000 /  portTICK_PERIOD_MS );
	    deviceFirstConfigInitialisation();
	}

	// hopefully we have now the modem data, now lets see if the gnss has the time
	if(gnssResult)
	{
		gnssResult = GNSS_WaitForCompletion((GNSS_maxSecondsToAquireValidData() * 1000) );
	}

	if(gnssResult)
	{
		gnssResult = gnssRetrieveCollectedData(&gnssData, 1000);// should not take longer than a second
	}

	if(gnssResult)
	{
        SetRTCFromGnssData(&gnssData);
		LOG_DBG(LOG_LEVEL_APP, "GNSS- Time to fix = %d [mSecs]\n", gnssData.firstFixTimeMs - gnssData.powerupTimeMs);
  	}
	else
	{
		LOG_EVENT(eLOG_GNSS, LOG_NUM_APP, ERRLOGDEBUG, "GNSS Timeout");
	}
	gnssCommand(GNSS_POWER_OFF, 1000);
}

/*
 *	EnterEngineeringMode
 *
 *	 @desc	Provide the user with a method of entering Engineering mode
 *
 *	 @params	engModeTimeout - timeout in seconds for user to select engineering mode
 *	 			Enginnering mode is selected by setting the gSuspendApp = true via the cli command 'suspendApp'.
 *
 */
static void EnterEngineeringMode(uint32_t engModeTimeout)
{
	LOG_DBG( LOG_LEVEL_APP, "\n"
							"\n"
							"FOR TEST PURPOSES ONLY\n"
							"\n"
							"Send cli command 'suspendApp' to suspend the app within the next %d seconds\n",
							engModeTimeout);

	for(uint8_t i = 0x00; i < engModeTimeout; i++)
	{
		if(gSuspendApp == true)
		{
			EngineeringMode();
		}

		vTaskDelay( 1000 /  portTICK_PERIOD_MS );
	}

	return;
}

/*
 *  EngineeringMode
 *
 *  @desc - Puts the node into engeering mode
 *
 *  @params
 *
 */
static void EngineeringMode()
{
	LOG_DBG( LOG_LEVEL_APP, "\n\nTG5 application has been suspended\n\n"
							"*******************************************************\n"
							"************ Welcome to Engineering Mode **************\n"
							"*******************************************************\n");

	while(gSuspendApp == true)
	{	// spin forever, for test purposes only
		vTaskDelay( 1000 /  portTICK_PERIOD_MS );
	}

	return;
}

/*
 * flashStatusLedCadence
 *
 * @desc    flash the status led count times with required cadence
 *
 * @param   -	count - number of times to flash status led
 * @param   -   OnMsec  - period that the LED is on
 * @param   -   OffMsec - period that the LED is off
 *
 * @return -
 */
void xTaskApp_flashStatusLedCadence(uint8_t count, uint16_t onMsec, uint16_t offMsec)
{
	uint32_t statusLed = SYSTEM_STATUS_LED1;

	if(Device_GetHardwareVersion() == HW_PASSRAIL_REV1)
	{	// Rev 1 hardware
		statusLed = SYSTEM_STATUS_LED2;
	}

	pinConfigDigitalOut(statusLed, kPortMuxAsGpio, 0, false);

	for(uint8_t i = 0x00; i<count; i++)
	{
		GPIO_DRV_SetPinOutput( statusLed );
		vTaskDelay( onMsec /  portTICK_PERIOD_MS );
		GPIO_DRV_ClearPinOutput( statusLed );
		vTaskDelay( offMsec /  portTICK_PERIOD_MS );
	}

	return;
}

/*
 * flashStatusLed
 *
 * @desc    flash the status led count times
 *
 * @param   -	count - number of times to flash status led
 *
 * @return -
 */
void xTaskApp_flashStatusLed(uint8_t count)
{
	xTaskApp_flashStatusLedCadence(count, 500, 500);
}

/**
 * @desc - Detect movemnet using lis3dh mems sensor
 *
 * Note: Only use this function for MEM connected to MK24.
 *
 * @params - pointer to bool which will be true when movement is detected
 *
 * @return  false when detecting movement encountered an error.
 */
bool xTaskApp_DetectMovement(bool * isMovementDetected_p)
{
    bool rc_ok = true;

    *isMovementDetected_p = false;

	rc_ok = lis3dh_movementMonitoring( gNvmCfg.dev.measureConf.Acceleration_Threshold_Movement_Detect,
										gNvmCfg.dev.measureConf.Acceleration_Avg_Time_Movement_Detect,
										isMovementDetected_p);
	if (rc_ok == false) {
		LOG_DBG( LOG_LEVEL_APP,"%s() : lis3d_movementMonitoring failed\n", __func__);
	}

	LOG_DBG( LOG_LEVEL_APP, "%s() : Movement %s detected\n", __func__,
			*isMovementDetected_p ? "IS" : "NOT");

    return rc_ok;
}


/*
 * xTaskApp_calculateWheelHz
 *
 * @desc convert gnss reported speed inknots to wheel Hz
 *
 * @params - reported gnss speed in knots
 * @params - wheel diameter in meter
 *
 *
 */
float xTaskApp_calculateWheelHz(float speed_knots, float Wheel_Diameter)
{
	float speed = 0;
	speed = speed_knots  * 1.852;// convert knots to km/h
	speed = speed * 1000;		// convert to meters / hour

	if(Wheel_Diameter == 0)
	{
        Wheel_Diameter = 0.9;
		LOG_DBG( LOG_LEVEL_APP, "Invalid wheel diameter, will use %f\n", Wheel_Diameter);
	}

	// convert to rpm and then to Hz
	speed = speed / (Wheel_Diameter * 3.1415926535);	// rotations per hr
	speed = speed / 60;													// rpm
	speed = speed / 60;													// revs/second
	return speed;
}

/*
 * xTaskApp_doSampling
 *
 * @desc - Control waveform sampling
 *
 * @param   bRawADCSampling (bool): false if wanting to use measurement ID, true
 * 			for raw ADC sampling WITHOUT power control etc
 * @param   eMeasId (tMeasId): Measurement ID of type tMeasId. Only used if
 * 			bRawADCSampling is false - specify the required samples per second
 * 			in ADCSamplesPerSecIfRawAdc
 * @param   nSampleLength (uint32_t): Number of output samples required
 * @param   ADCSamplesPerSecIfRawAdc (uint32_t): Samples per second,
 * 			for raw ADC sampling only. When a valid measurement ID is specified
 * 			then the sample rate is chosen based on the ID and this param is
 * 			don't care then.
 * @note	The param nAdcSamplesPerSecIfRawAdc is also used to determine the
 * 			sampling duration, irrespective whether it is Raw Adc Sampling
 * 			or not.
 * @return 	true  - if the sampling is complete,
 * 		 	false - otherwise.
 */
//bool xTaskApp_doSampling(uint32_t dataLength, tMeasId measId)
bool xTaskApp_doSampling(bool bRawAdcSampling,
                   	     tMeasId eMeasId,
						 uint32_t nSampleLength,
						 uint32_t nAdcSamplesPerSecIfRawAdc)
{
	bool retval = false;
	uint32_t nMaxSamplingTime_msec = 0;

    if((gSemDoSample != NULL) && (xSemaphoreTake(gSemDoSample, 0) != pdFALSE))
    {
    	g_bAppSamplingIsComplete = false;
    	retval = Measure_Start(bRawAdcSampling,          		// True for Raw Adc Sampling
    						   eMeasId,							// MEASID_RAWACCEL_25600SPS,
							   nSampleLength,
							   nAdcSamplesPerSecIfRawAdc,      // Dummy - not applicable for normal sampling
							   AppMeasureIsCompleteCallback);

    	// Determine max sampling time with the margin included.
   		nMaxSamplingTime_msec = (uint32_t)((((float)nSampleLength / (float)nAdcSamplesPerSecIfRawAdc) * 1000) + SAMPLING_TIME_MARGIN_MILLISECS);
    	//printf("\n Sampling Timeout: %ld [msec] \n", nMaxSamplingTime_msec);

    	// Block until timeout.
    	if(xSemaphoreTake(gSemDoSample, pdMS_TO_TICKS(nMaxSamplingTime_msec)) != pdFALSE)
    	{
        	if((retval != true) || (g_bAppSamplingIsComplete != true))
        	{
        		LOG_EVENT(eLOG_SAMPLING, LOG_NUM_APP, ERRLOGMAJOR, "****ERROR-Sampling, RetVal= %d, SampleComplete=%d", retval, g_bAppSamplingIsComplete);
        	}
    	}
    	else
    	{
    		LOG_EVENT(eLOG_SAMP_WAIT, LOG_NUM_APP, ERRLOGMAJOR, "***Semaphore Wait FAILED***");
    	}

    	// Release the Semaphore for the next sampling cycle.
    	xSemaphoreGive(gSemDoSample);
    }
    else
    {
    	LOG_EVENT(eLOG_SAMP_LOCK, LOG_NUM_APP, ERRLOGMAJOR,"***FAILED to Get Sampling Semaphore***");
    }

	return retval;
}

/*
 * AppMeasureIsCompleteCallback
 *
 * @desc - Callback function used exclusively by xTaskApp_doSampling.
 *
 * @return void.
 *
 */
static void AppMeasureIsCompleteCallback(void)
{
    bool bMeasureErrors;
    MeasureErrorInfoType MeasureErrorInfo;

    bMeasureErrors = Measure_GetErrorInfo(&MeasureErrorInfo);

    // If there are sampling errors, log the info.
    if (bMeasureErrors)
    {
        LOG_DBG( LOG_LEVEL_APP, "\n************** ERROR - Sampling\n");
        LOG_DBG( LOG_LEVEL_APP, "\ng_bAppSamplingIsComplete %d\n", g_bAppSamplingIsComplete);
        LOG_DBG( LOG_LEVEL_APP, "\nMeasure error code: %d\n", MeasureErrorInfo.MeasureError);
        LOG_DBG( LOG_LEVEL_APP, "\nAD7766 error code: %d\n", MeasureErrorInfo.AD7766Error);
        LOG_DBG( LOG_LEVEL_APP, "\nRaw ADC error block number: %d\n", MeasureErrorInfo.ErrorBlockNum);
    }
    // Sampling was OK, Indicate that sampling has completed.
    else
    {
        g_bAppSamplingIsComplete = true;
    }
#ifdef SAMPLING_EXEC_TIME_EN
    printf("\n Sampling Time: %ld [msec]\n", (g_nStopSamplingTick - g_nStartSamplingTick));
#endif
    // Release the Sampling Semaphore.
    xSemaphoreGive(gSemDoSample);
}

/*
 * openFlashRetrieveMeasData
 *
 * @brief	Open the external flash device and retrieve measurement and
 *          communication record from the external flash then populate
 *          the measurement & COMMS record structure with respective data.
 * @return	true - If the data read is successful,
 * 			false - otherwise.
 */
static void openFlashRetrieveMeasData()
{
    // first we have to open the external flash device and get a handle
    // then read the measurement data
    if(true == fetchCommsData(true))
    {
        if(Device_HasPMIC())
        {
        	PMIC_UpdateParamsInCommsRecord();
        }
    }
    else
    {
    	// set default values due to open/read failure
    	generate_default_records(eLOG_CM_0);
    }

	// retrieve Up_Time from RFVbat
	if(!Vbat_GetUpTime(&commsRecord.params.Up_Time))
	{
		commsRecord.params.Up_Time = 0;
	}

    print_measurement_record();
    print_communication_record();
#if 0
        	LOG_DBG(LOG_LEVEL_APP, 	"\n----------------------------"
        							"\nRead energy data:"
        							"\nRemaining: %f%%"
        							"\nModemCyclesSum: %f"
        							"\nMeasureCyclesSum: %f"
        							"\n----------------------------",
									commsRecord.params.EnergyRemaining_percent,
									commsRecord.params.E_Modem_Cycle,
									commsRecord.params.E_Previous_Wake_Cycle);
#endif
}


#ifdef __cplusplus
}
#endif