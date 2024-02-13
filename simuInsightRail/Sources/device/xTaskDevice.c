#ifdef __cplusplus
extern "C" {
#endif

/*
 * xTaskDevice.c
 *
 *  Created on: 16 jun. 2014
 *      Author: g100797
 */

/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>

#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>

#include "xTaskDefs.h"
#include "Resources.h"
#include "xTaskDevice.h"
#include "xTaskDeviceEvent.h"
#include "Device.h"
#include <string.h>
#include "printgdf.h"

#include "fsl_wdog_driver.h"
#include "fsl_interrupt_manager.h"

#include "log.h"
#include "CRC.h"

#include "PinConfig.h"

#include "configBootloader.h"
#include "xTaskAppOta.h"
#include "xTaskApp.h"
#include "Vbat.h"

#include "configLog.h"
#include "NvmConfig.h"
#include "NvmData.h"
#include "schedule.h"
#include "EnergyMonitor.h"

#include "pmic.h"
#include "boardSpecificCLI.h"
#include "drv_is25.h"
#include "DataStore.h"

#ifdef FCC_TEST_BUILD
#include "FCCTest/FccTest.h"
#endif

/*
 * Macros
 */

/*
 * Types
 */

/*
 * Data
 */


// data for freeRTOS related resources
#ifndef EVENTQUEUE_NR_ELEMENTS_DEVICE
#define EVENTQUEUE_NR_ELEMENTS_DEVICE  4     //! Event Queue can contain this number of elements
#endif

extern bool gSuspendMK24Shutdown;

/*
 * FreeRTOS local resources
 */
TaskHandle_t   _TaskHandle_Device;// global to easy list in the cli tasks command
//static QueueHandle_t  _EventQueue_Device;

QueueHandle_t  _EventQueue_Device;

static void PrepareShutdown(bool bIsCommsCycle);
static void startUpMessage();

#ifdef FCC_TEST_BUILD
void xTaskDevice_FccTest();
#endif

/*!
 * xTaskDevice_Init
 *
 * @brief      Initialize the xTaskDevice module before the scheduler is
 *             starting the actual task xTaskDevice().
 *
 * @param      pvParams
 */
void xTaskDevice_Init( void )
{
    /*
     * Setup task resources
     */

    // Create event queue
    _EventQueue_Device    = xQueueCreate( EVENTQUEUE_NR_ELEMENTS_DEVICE, sizeof( tDeviceEvent));
    vQueueAddToRegistry(_EventQueue_Device, "_EVTQ_DEVICE");

#ifdef FCC_TEST_BUILD
    // Create task
    xTaskCreate( xTaskDevice_FccTest,       // Task function name
                 "FCC_DEVICE",              // Task name string
				 STACKSIZE_XTASK_DEVICE,    // Allocated stack size on FreeRTOS heap
                 NULL,                   	// (void*)pvParams
				 PRIORITY_XTASK_DEVICE,     // Task priority
                 &_TaskHandle_Device );     // Task handle

    //vTaskSuspend(_TaskHandle_Device);

#else
    // Create task
    xTaskCreate( xTaskDevice,       		// Task function name
                 "DEVICE",                  // Task name string
				 STACKSIZE_XTASK_DEVICE,    // Allocated stack size on FreeRTOS heap
                 NULL,                   	// (void*)pvParams
				 PRIORITY_XTASK_DEVICE,     // Task priority
                 &_TaskHandle_Device );     // Task handle
#endif
    /*
     * Initialize
     */
}

#ifdef FCC_TEST_BUILD
void xTaskDevice_FccTest()
{
	tDeviceEvent rxEvent;
	//FCCTest_Init();
	if(Device_HasPMIC())
	{
		PMIC_SendMK24PowerUpStatus();
	}
	while(1)
	{
		if (xQueueReceive( _EventQueue_Device, &rxEvent, portMAX_DELAY))
		{
			//printf("\nUnexpected device event received.\n");
		}
	}
}
#endif

#define USEWATCHDOG

#define WDOG_TIMEOUT_VALUE_SECS          (10)

// Configure watchdog.
static const wdog_config_t wdogConfig =
{
    .wdogEnable		= true,						// Watchdog mode
    .timeoutValue	= WDOG_TIMEOUT_VALUE_SECS*1000,        //2048U,// Watchdog overflow time is about 10s (1khz source, divider=1)
	.intEnable		= true,						// enable interrupt
    .winEnable		= false,					// Disable window function
    .windowValue	= 0,						// Watchdog window value
    .prescaler   = kWdogClkPrescalerDivide1,	// Watchdog clock prescaler
    .updateEnable	= true,						// Update register enabled
    .clkSrc			= kWdogLpoClkSrc, 			// Watchdog clock source is LPO 1KHz
#if FSL_FEATURE_WDOG_HAS_WAITEN
    .workMode.kWdogEnableInWaitMode  = true,	// Enable watchdog in wait mode
#endif
    .workMode.kWdogEnableInStopMode  = true,	// Enable watchdog in stop mode
    .workMode.kWdogEnableInDebugMode = false,	// Disable watchdog in debug mode
};



static HwVerEnum supportedHardware[] = {
        HW_PASSRAIL_REV3,
        HW_PASSRAIL_REV4,
		HW_PASSRAIL_REV5,
		HW_PASSRAIL_REV6,
};

static bool checkSupportedHardware(HwVerEnum HwVer)
{
	if(Device_HasPMIC())
	{
		return true;
	}
    for (uint16_t i=0; i<sizeof(supportedHardware)/sizeof(*supportedHardware); i++) {
            if (HwVer == supportedHardware[i]) return true;
    }
    return false;
}


/*
 * PutNodeToSleep
 *
 * @desc    Attempt to switch off VD power domain (leaving RTC domain active)
 *
 * @param   nShutdownRetryCount - the attempt number
 *
 * @return false (if it returns at all!)
 */
static bool PutNodeToSleep(uint16_t nShutdownRetryCount)
{
	if(dbg_logging)
	{
		vTaskDelay(50/portTICK_PERIOD_MS);
	}

	// Disable interrupts, which also stops task switching
	INT_SYS_DisableIRQGlobal();

	// switch off Reset-on-LVD detect in case we don't die gracefully..
	PMC_WR_LVDSC1_LVDRE(PMC, 0);

    pinConfigDigitalOut(nPWR_ON, kPortMuxAsGpio, 1, false); // maybe this helps on nodes having problems shutting down ?
    GPIO_DRV_SetPinOutput(nPWR_ON);

    // since we have killed tasking, we need a way of creating a delay
	#define APPROX_500_MS (6000000)
    for(volatile int i=0; i<APPROX_500_MS; i++);

    INT_SYS_EnableIRQGlobal();
    LOG_DBG(LOG_LEVEL_APP, "\nHELP - Shutdown failed..... Trying again, RetryCount:%d\n", nShutdownRetryCount);

    return false; // shutdown failed !!
}

static bool getNewAppProgStatus()
{
	bool retval = false;
	if((gBootCfg.cfg.FromApp.NewAppAddress == 0) &&
		(gBootCfg.cfg.ImageInfoFromLoader.newAppProgStatus != 0))
	{
		switch(gBootCfg.cfg.ImageInfoFromLoader.newAppProgStatus)
		{

		case LOADER_NEWAPP_PROG_SUCCESS:
			LOG_EVENT(0, LOG_NUM_APP, ERRLOGINFO, "App v \"%s\", Woken On Successful Update",
					  getFirmwareFullSemVersionMeta(__app_version));
			break;

		case LOADER_NEWAPP_INCOMPATIBLE_STATUS:
			LOG_EVENT( 0, LOG_NUM_APP, ERRLOGWARN, "Incompatible App... Now schedule NextWakeUp");
			break;

		case LOADER_NEWPACKAGE_PROG_SUCCESS:
			LOG_EVENT(0, LOG_NUM_APP, ERRLOGINFO, "Package Update successful, App woken On Successful Update");
			break;

		case LOADER_NEWPACKAGE_FLASH_FAIL_STATUS:
			LOG_EVENT( 0, LOG_NUM_APP, ERRLOGWARN, "Failed to perform update from package");
			break;

		default:
			break;
		}

		// Reset the App prog success var.
		gBootCfg.cfg.ImageInfoFromLoader.newAppProgStatus = LOADER_NEWAPP_NONE;
		retval = bootConfigWriteFlash(&gBootCfg);
	}
	return retval;
}


static TickType_t m_maxNodeOnTicks;
static TickType_t m_elapsedTicks;

/*!
 * xTaskDevice_TicksRemaining
 *
 * @brief      Return number of ticks before we are shutdown
 *
 * @param      none
 */
TickType_t xTaskDevice_TicksRemaining()
{
	if(m_elapsedTicks > m_maxNodeOnTicks)
	{
		return 0;
	}
	return m_maxNodeOnTicks - m_elapsedTicks;
}

/*!
 * xTaskDevice
 *
 * @brief      Task function for the Device component. This function is the task
 *             controlled by the RTOS and handles the DEVICE task events.
 *
 * @param      pvParams
 */
void xTaskDevice(void *pvParameters)
{
    tDeviceEvent rxEvent;
    enum eStartupMode {
        SUapplication,
        SUfirmwareUpdate,
        SUengineering,
    } startupMode = SUapplication ;

    bool engineeringMode = false;
    bool bFirmwareUpdateMode = false;

    uint32_t APPmaxOnTimeMin = 10;
    HwVerEnum HwVer;
    TickType_t startTicks = xTaskGetTickCount();

    m_maxNodeOnTicks = 1 *60 *1000 / portTICK_PERIOD_MS; // 1 minute OK ? Will be set to a new value when we know what woke us up.
    g_nStartTick = xTaskGetTickCount();

    // send a status to PMIC if we are a harvester
	if(Device_HasPMIC())
	{
		/*
		 * we can do this even if flash is still being formatted before PMIC task runs..
		 * we don't expect a reply
		 */
		PMIC_SendMK24PowerUpStatus();
	}
	else
	{
		// Make IS25 ready for use.
		xTaskApp_makeIs25ReadyForUse();
	}

	startUpMessage();

	/*
	 * Init
	 */
    // check for a brown-out
    uint8_t bRCM_SRS0 = RCM->SRS0;
    uint8_t bRCM_SRS1 = RCM->SRS1;
    if((bRCM_SRS0 & (RCM_SRS0_POR_MASK + RCM_SRS0_LVD_MASK)) == RCM_SRS0_LVD_MASK)
    {
        LOG_EVENT( 0, LOG_NUM_APP, ERRLOGFATAL, "brown-out reset detected");
    }
    // now look for unusual reasons
    if((bRCM_SRS0 & (RCM_SRS0_WDOG_MASK + RCM_SRS0_LOL_MASK + RCM_SRS0_LOC_MASK)) != 0)
    {
        LOG_EVENT( 0, LOG_NUM_APP, ERRLOGFATAL, "invalid reset detected RCM_SRS0=%02X", bRCM_SRS0);
    }
    // don't flag the software reset RCM_SRS1_SW_MASK
    if((bRCM_SRS1 & (RCM_SRS1_JTAG_MASK + RCM_SRS1_LOCKUP_MASK + RCM_SRS1_MDM_AP_MASK + RCM_SRS1_EZPT_MASK + RCM_SRS1_SACKERR_MASK)) != 0)
    {
        LOG_EVENT( 0, LOG_NUM_APP, ERRLOGFATAL, "invalid reset detected RCM_SRS1=%02X", bRCM_SRS1);
    }

    // if a watchdog is detected this function will log where from
    Vbat_CheckForWatchdog();

    if (Vbat_IsFlagSet(VBATRF_FLAG_STACK_OVERFLOW))
    {
    	uint8_t indx;
    	Vbat_GetStackOverflowTaskIndx(&indx);
    	LOG_EVENT( 0, LOG_NUM_APP, ERRLOGFATAL, "STACK OVERFLOW detected, Task(%d): %s", indx, Resources_GetTaskName(indx));
    	// now clear it
        Vbat_ClearFlag(VBATRF_FLAG_STACK_OVERFLOW);
    }

    if (Vbat_IsFlagSet(VBATRF_FLAG_HEAP_OVERFLOW))
    {
        LOG_EVENT( 0, LOG_NUM_APP, ERRLOGFATAL, "HEAP Overflow detected");
    	// now clear it
        Vbat_ClearFlag(VBATRF_FLAG_HEAP_OVERFLOW);
    }

    if (Vbat_IsFlagSet(VBATRF_FLAG_HARD_FAULT))
    {
        LOG_EVENT( 0, LOG_NUM_APP, ERRLOGFATAL, "HARD FAULT detected");
    	// now clear it
        Vbat_ClearFlag(VBATRF_FLAG_HARD_FAULT);
    }

    // read nvmdata from flash (already done in resources.c
    // if 'lastpowerdown_unexpected' log_event('last shutdown unexpected')
    if (Vbat_IsFlagSet(VBATRF_FLAG_UNEXPECTED_LAST_SHUTDOWN))
    {
    	const char*msg[] =
    	{
    			"",
				" - modem active",
				" - GNSS active",
				" - modem & GNSS active"
    	};
    	int cause = 9990, other = 0;
        if (Vbat_IsFlagSet(VBATRF_FLAG_MODEM))
        {
        	other |= 1;
        }
        if (Vbat_IsFlagSet(VBATRF_FLAG_GNSS))
        {
        	other |= 2;
        }
    	Vbat_ClearFlag(VBATRF_FLAG_GNSS + VBATRF_FLAG_MODEM);
        LOG_EVENT( cause+other, LOG_NUM_APP, ERRLOGDEBUG, "last shutdown unexpected%s", msg[other & 3]);

        // Switch to a temperature reading to restart the scheduler
        // there's little chance of an unexpected shutting down during it.
        // Most of the time this occurs when there's not enough
        // charge in the supercap resulting in waking up/shutting
        // down every 30 minutes so let's break the cycle
        Vbat_SetNodeWakeupTask(NODE_WAKEUP_TASK_TEMPERATURE_MEAS);
    }
    else
    {
    	// OK, not set so prepare for an unexpected shutdown
        Vbat_SetFlag(VBATRF_FLAG_UNEXPECTED_LAST_SHUTDOWN);
    }

#ifdef USEWATCHDOG
    // init the watchdog
    // configure the watchdog
    NVIC_SetPriority(WDOG_EWM_IRQn, 0);
    NVIC_EnableIRQ(WDOG_EWM_IRQn);
    WDOG_DRV_Init(&wdogConfig);

    // Restart the watchdog so we have the full time to do some stuff.
    WDOG_DRV_Refresh();
#endif
    // init the max on time to something default

    // before calling all kind of code, check if we should, do we run on supported hardware ?
    HwVer = Device_GetHardwareVersion();

    if(checkSupportedHardware(HwVer) == false)
    {
        // hopefully the CLI serial port is the same
        printf("\nUnsupported Hardware version (%d) detected, not starting anything, just staying in engineering mode !\n\n", HwVer);
        engineeringMode = true;
    }
    else
    {
        engineeringMode = Device_GetEngineeringMode();
        bFirmwareUpdateMode = getNewAppProgStatus();
    }

	if(engineeringMode)
	{
          startupMode = SUengineering;
          m_maxNodeOnTicks =  5 * 1000 / portTICK_PERIOD_MS;// 5 seconds on after pulling the engineering mode plug
          printf("\nEngineering mode detected, node will stay 'on'.\n");
	}
	else
	{
		if(bFirmwareUpdateMode)
		{
			startupMode = SUfirmwareUpdate;
			m_maxNodeOnTicks =  1 *60 *1000 / portTICK_PERIOD_MS; // 60 sec should be enough to schedule the node for next wakeup?
		}
		else
		{
			// It's neither FW update or engineering so must be Application mode
			startupMode = SUapplication;
		}
    }

    switch (startupMode)
    {
    case SUapplication:
    	m_maxNodeOnTicks =  APPmaxOnTimeMin * 60 * 1000 / portTICK_PERIOD_MS;
        break;

    case SUengineering:
        // nothing to start
    	if(Device_HasPMIC() && bFirmwareUpdateMode)
    	{
    		PMIC_Reboot();
    	}
        break;

    case SUfirmwareUpdate:
    	if(!Device_HasPMIC())
    	{
			// Set up the schedule and sleep.
			schedule_CalculateNextWakeup();
#ifdef USEWATCHDOG
			WDOG_DRV_Refresh();
#endif
			PrepareShutdown(true);
    	}
    	else
    	{
    		PMIC_Reboot();
    	}
    	break;

    default:
        break;
    }

	if(Device_HasPMIC())
	{
		if(!PMIC_IsMetadataRcvd())
		{
			WDOG_DRV_Refresh();
			metadataDisplay(titlePMIC_Application, (char*)PMIC_getMetadata(), true);
		}
		else
		{
			metadataDisplay(titlePMIC_Application, PMIC_GetMetadataResult(), true);
		}
		metadataDisplayLoader(true);
		metadataDisplay(titleApplication, __app_version, true);
		printf("\n");
	}

    /*
	 * Device main loop
	 */
    bool bShutdownRequested = false;

	for (;;)
	{
        // Restart the watchdog so it doesn't reset.
#ifdef USEWATCHDOG
	    WDOG_DRV_Refresh();
#endif

	    if(!bShutdownRequested)
	    {
	    	// also shutdown the node when the maximum on time is passed
	    	m_elapsedTicks = xTaskGetTickCount() - startTicks; // we are not guarding against tick timer overflow, because that happens after about 50 day's ?
			if(engineeringMode)
			{
				if (!Device_GetEngineeringMode())
				{
					engineeringMode = false;
					bShutdownRequested = true;
					LOG_EVENT(0, LOG_NUM_APP, ERRLOGDEBUG, "Node shutdown after removing engineering mode jumper");
					vTaskDelay(10/portTICK_PERIOD_MS);

					// we were in engineering mode, and someone pulled the plug
					// send message to self 'shutdown'
					xTaskDeviceShutdown(false);
				}

#ifdef REPEAT_COMMAND_HACK
		    	/*
		    	 * hack for repeated command invocation
		    	 */
				static uint8_t p = 99;
				static TickType_t cliTicks = 0;

				if(xTaskGetTickCount() - cliTicks > (3000 / portTICK_PERIOD_MS))
				{
					if(cliTicks > 0)
					{
						extern QueueHandle_t  _EventQueue_CLI;
						xQueueSend(_EventQueue_CLI, &p , portMAX_DELAY);
					}
					cliTicks = xTaskGetTickCount();
				}
#endif

			} else {

				if(m_elapsedTicks > m_maxNodeOnTicks)
				{
					bShutdownRequested = true;
					// shutdown the node
					// logevent 'node shutdown by exceeding maximum on time'
					LOG_EVENT( 0, LOG_NUM_APP, ERRLOGDEBUG, "Node shutdown by exceeding max on time");
					vTaskDelay(10/portTICK_PERIOD_MS);

					// send message to self 'shutdown'
					xTaskDeviceShutdown(false);
				}
			}
	    }

        /*
         * TODO add a new message to allow us to start the application task (from PMIC task)
         */
		if (xQueueReceive( _EventQueue_Device, &rxEvent, (WDOG_TIMEOUT_VALUE_SECS*1000/3) /portTICK_PERIOD_MS ))
		{
#ifdef USEWATCHDOG
		    // Restart the watchdog so we have the full time to do some stuff.
            WDOG_DRV_Refresh();
#endif
			switch (rxEvent.Descriptor)
			{
                case DeviceEvt_SetWakeUpTime:
                	schedule_ScheduleNextWakeup(rxEvent.Data.wakeUpTime.rtcSleepTime,
					rxEvent.Data.wakeUpTime.ds137nSleepTime);
                	break;

                case DeviceEvt_Shutdown:
                	if ((engineeringMode) && (!Device_HasPMIC()))
                	{
						vTaskDelay(500/ portTICK_PERIOD_MS);
						printf("\n !! Shutdown called in engineering mode, node stays active !!\n");
					}
                	else
                	{
                		// gracefully bring down the node
                		PrepareShutdown(rxEvent.Data.bIsThisCommsCycle);
                	}
					break;

	            default:
	            	LOG_EVENT( 0, LOG_NUM_APP, ERRLOGDEBUG,  "Unexpected event %d", rxEvent.Descriptor );
					break;
			}
		}
	}
}

/*
 * functions, to be called from the external task, which take care of the event structure filling
 */
// send message to device task to bring the node down
bool xTaskDeviceSetWakeUptime(uint32_t rtcSleepTime, uint32_t ds137nSleepTime)
{
    tDeviceEvent evt =
    {
    	.Descriptor = DeviceEvt_SetWakeUpTime,
    	.Data.wakeUpTime.rtcSleepTime = rtcSleepTime,
    	.Data.wakeUpTime.ds137nSleepTime = ds137nSleepTime
    };

    return (pdTRUE == xQueueSend(_EventQueue_Device, &evt , portMAX_DELAY ));
}



// send message to device task to bring the node down
bool xTaskDeviceShutdown(bool bCommsCycle)
{
    tDeviceEvent evt =
    {
    	.Descriptor = DeviceEvt_Shutdown,
    	.Data.bIsThisCommsCycle = bCommsCycle
    };

    return (pdTRUE == xQueueSend(_EventQueue_Device, &evt , portMAX_DELAY ));
}

/*
 * PrepareShutdown
 *
 * @brief	Performs some pre-shutdown activities like:
 * 			1. Clear the Unexpected shutdown flag
 *			2. Update Config & Data in the NVM, if they have changed.
 * 			3. Update the energy used.Updates the total energy consumed by the
 * 			   sensor in comms cycle.
 *
 * @param	bIsCommsCycle - Indicate whether it is a COMMS cycle.
 *
 * @return  void
 */
static void PrepareShutdown(bool bIsCommsCycle)
{
	uint16_t retrycount = 5;

    // store that we did do a planned shutdown for detecting crashes next boot
    Vbat_ClearFlag(VBATRF_FLAG_UNEXPECTED_LAST_SHUTDOWN);

    // If the NVM Data has changed update the NVM with the RAM copy.
    NvmDataUpdateIfChanged();

    // If the CFG data has changed, update the NVM with the RAM copy.
    NvmConfigUpdateIfChanged(false);

    if(!Device_HasPMIC() && (PutNodeToSleep(retrycount) == false))
    {
    	LOG_EVENT( 0, LOG_NUM_APP, ERRLOGDEBUG, "Node powerdown failed, RetryLeft:%d",retrycount - 1);
		while (retrycount--)
		{
			PutNodeToSleep(retrycount);
		}
	}
    else
    {
    	//if(PMIC_ConfigUpdateScheduled())
		if (1) //always update config param in PMIC
    	{
			LOG_DBG(LOG_LEVEL_APP, "Send MK24 configuration parameter values to PMIC\n");
    		PMIC_sendMK24ParamUpdateMessage((params_t)
    		{
    			.group = PARAMS_CONFIG,
    			.params.config.selftest.tempDelta = gNvmCfg.dev.selftest.maxSelfTestTempDelta,
				.params.config.motionDetect.isGatingEnabled = gNvmCfg.dev.measureConf.Is_Moving_Gating_Enabled,
				.params.config.tempAlarms = {.lowerLimit = (uint8_t)gNvmCfg.dev.measureConf.Temperature_Alarm_Lower_Limit,
									.upperLimit = (uint8_t)gNvmCfg.dev.measureConf.Temperature_Alarm_Upper_Limit},
				.params.config.mems = {.threshold_mg = (uint32_t)(gNvmCfg.dev.measureConf.Acceleration_Threshold_Movement_Detect*1000),
								.duration_s = gNvmCfg.dev.measureConf.Acceleration_Avg_Time_Movement_Detect},
			});
			
			vTaskDelay(100/portTICK_PERIOD_MS);
		}

    	if(false == gSuspendMK24Shutdown)
    	{
    		// This should not return
    		PMIC_Powerdown();
    		gSuspendMK24Shutdown = false;
    	}
    }
}


/*
 * startUpMessage
 *
 * @desc Prepare node for running - show some helpful? information to the user
 *
 * @params
 *
 */
static void startUpMessage()
{
#ifndef LIMIT_CLI_SPAMMING
	// show people how to start logging
	printf("Please use the following cli command 'log' + the module to be logged or any combination, followed by configwrite 1 to save to nvm if required\n");
	printf("%d for APPlication logging\n", LOG_LEVEL_APP);
	printf("%d for GNSS logging\n", LOG_LEVEL_GNSS);
	printf("%d for NET logging\n", LOG_LEVEL_NET);
	printf("%d for MODEM logging\n", LOG_LEVEL_MODEM);
	printf("%d for CLI logging\n", LOG_LEVEL_CLI);
	printf("%d for I2C logging\n", LOG_LEVEL_I2C);
	printf("%d for COMM logging\n", LOG_LEVEL_COMM);
	printf("%d for PMIC logging\n", LOG_LEVEL_PMIC);

	printf("\n\n");
	printf("ENGINEERING MODE can now be invoked by starting the node with TEST_IO1 pin tied to +Vd\n\n");
#endif
	vTaskDelay( 100 /  portTICK_PERIOD_MS );

	if ((dbg_logging) & (LOG_LEVEL_CLI))
	{
		DataStore_dumpDatastore();
	}
}







#ifdef __cplusplus
}
#endif