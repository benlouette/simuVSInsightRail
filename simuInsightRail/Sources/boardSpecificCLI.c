#ifdef __cplusplus
extern "C" {
#endif

/*
 * boardSpecificCLI.c
 *
 *  Created on: Oct 30, 2015
 *      Author: George de Fockert
 */

#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <portmacro.h>

#include <Resources.h>
#include <xTaskDefs.h>

/*-------------------------------------------------------------------------------------*
 |                                                                                     |
 *-------------------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <math.h>

#include "configFeatures.h"
#include "CLIio.h"
#include "CLIcmd.h"
#include "printgdf.h"

//
// board/project specific commands
//

#include "fsl_gpio_driver.h"
#include "configPinNames.h"

#include "fsl_adc16_driver.h"
//#include "adc1.h"

#include "boardSpecificCLI.h"
#include "configCLI.h"
#include "Log.h"

#include "PowerControl.h"
#include "PinConfig.h"
#include "PinDefs.h"

#include "AD7766_Intrpt.h"
#include "AD7766_DMA.h"

#include "configModem.h"
#include "Modem.h"
#include "NvmConfig.h"
#include "NvmData.h"
#include "NvmCalib.h"
#include "DataStore.h"
#include "Eventlog.h"

#include "configData.h"
#include "configIDEF.h"
#include "ISvcdata.h"
#include "configSvcData.h"

#include "xTaskApp.h"
#include "ExtFlash.h"

#include "xTaskAppRtc.h"

#include "xTaskMeasure.h"
#include "xTaskDevice.h"
#include "xTaskAppGNSS.h"

// for the scaling defines...
#include "xTaskAppCommsTest.h"

#include "DrvTmp431.h"
#include "ds137n.h"
#include "lis3dh.h"
#include "rtc.h"
#include "hdc1050.h"
#include "PassRailAnalogCtrl.h"

#include "PassRailDSP.h"

#include "fsl_rtc_driver.h"
#include "PassRailMeasure.h"
#include "TestUART.h"

#include "PowerControl.h"
#include "PowerOffFailsafe.h"
#include "DacWaveform.h"
#include "xTaskAppOta.h"
#include "Vbat.h"

#include "schedule.h"
#include "selfTest.h"
#include "nfc.h"
#include "nfc_cli.h"
#include "EnergyMonitor.h"
#include "pmic.h"

#ifdef FCC_TEST_BUILD
#include "FCCTest/FccTest.h"
#endif

#include "./PMIC/pmic.h"

//#include "strings.h"
#include "image.h"
#include "./cunit_tests/UnitTest.h"
#include "temperature.h"

#include "Measurement.h"

// List the subsystem types.
typedef enum
{
	MODEM_SUBSYSTEM		= 0,
	GNSS_SUBSYSTEM		= 1,
	ANALOG_SUBSYSTEM	= 2
} subSystemTypes_t;

// TODO: next have to find a better place (inside the measure sources, but now is in the xtaskappcommstest.c
bool cliExtHelpSimulAmSignal(uint32_t argc, uint8_t * argv[], uint32_t * argi);
bool cliSimulAmSignal( uint32_t args, uint8_t * argv[], uint32_t * argi);

//..............................................................................

bool gSuspendApp = false;		// suspend the MVP application from running
bool gSuspendMK24Shutdown = false;	// prevent MK24 from sending shutdown message to PMIC.
#ifdef EXT_WDG_DS1374_WAKEUP_TEST
bool g_bTriggerExtWatchdogWakeUp = false;
#endif


extern bool cliSelftestHelp(uint32_t argc, uint8_t * argv[], uint32_t * argi);
//..............................................................................
// Local function prototypes
static void cliSubsystemNotPoweredOnError(char *SubsystemStr);
static bool cliDs137nSetSleepPeriod( uint32_t args, uint8_t * argv[], uint32_t * argi);
static bool cliDs137nReadCounter( uint32_t args, uint8_t * argv[], uint32_t * argi);
static bool cliSetRtcTime( uint32_t args, uint8_t * argv[], uint32_t * argi);
static bool cliSetRtcDate( uint32_t args, uint8_t * argv[], uint32_t * argi);
static bool cliGetRtcTime( uint32_t args, uint8_t * argv[], uint32_t * argi);
static bool cliCheckRtcAlarm( uint32_t args, uint8_t * argv[], uint32_t * argi);
static bool cliReadLIS3DH( uint32_t args, uint8_t * argv[], uint32_t * argi);
static bool cliLis3dhMonitorMovement( uint32_t args, uint8_t * argv[], uint32_t * argi);
static bool cliMemsSelfTest( uint32_t args, uint8_t * argv[], uint32_t * argi);
static bool cliHumidity( uint32_t args, uint8_t * argv[], uint32_t * argi);
static bool cliReadTemp( uint32_t args, uint8_t * argv[], uint32_t * argi);
static bool cliReadBoardVersion( uint32_t args, uint8_t * argv[], uint32_t * argi);



static bool cliGnssRotnSpeedChangeTest( uint32_t args, uint8_t * argv[], uint32_t * argi);
static bool cliDoMeasure(uint32_t args, uint8_t * argv[], uint32_t * argi);
static void cliAD7766SendResultsToCliAndTestUart(uint32_t NumSamples,
                                                 uint32_t SamplesPerSec);
static bool cliSuspendApp( uint32_t args, uint8_t * argv[], uint32_t * argi);
static bool cliSuspendShutDown( uint32_t args, uint8_t * argv[], uint32_t * argi);
static bool cliUseAcceleratedScheduling( uint32_t args, uint8_t * argv[], uint32_t * argi);
#ifdef EXT_WDG_DS1374_WAKEUP_TEST
static bool cliTrigExtWdgDs1374NodeWakeupTest( uint32_t args, uint8_t * argv[], uint32_t * argi);
#endif
//..............................................................................

/*
 * cliLog
 *
 * log command
 */
static bool cliLog( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    uint32_t logIdx;
	bool rc_ok = true;

	if (args == 0) {
		 printf("log %d\n", dbg_logging);

		 // Show active levels
		 for ( logIdx = 0; logIdx < 32; logIdx++ ) {
		     // Print level strings and active
		     if ((0x1<<logIdx) & LOG_LEVEL_ALL) {
		         printf(" %10d  %d  %s\n", 0x1<<logIdx, ((0x1<<logIdx) & dbg_logging) ? 1 : 0,
		                 LOG_LEVELTOSTRING(0x1<<logIdx) );
		     }
		 }

	} else {
		if (args >= 1) {
			dbg_logging = argi[0];
			gNvmCfg.dev.general.dbg_logging = dbg_logging;
		} else {
			rc_ok = false;
		}
	}

	return rc_ok;
}


/*
 * TODO I don't think this function should be here
 */
bool deviceFirstConfigInitialisation()
{
    LOG_DBG(LOG_LEVEL_MODEM,"Setting modem at default baud rate.\n");
    (void)Modem_powerup(20000) ;
    (void)Modem_terminate( 0 ); //service profile does not care, ignore the error

    // wait until power is really down for the modem
    uint32_t wait_iters = 5;
    while ((GPIO_DRV_ReadPinInput(EHS5E_POWER_IND) == 0) && (wait_iters > 0))
    {
    	LOG_DBG(LOG_LEVEL_MODEM,"EHS5E_POWER_IND input pin sgnals that the modem is not yet powerdown, wait some more !\n");
        vTaskDelay(5000/portTICK_PERIOD_MS);// some time to let the voltage  drop before switching on again
        wait_iters--;
    }

    // modem baudrate setting was ok, so now we can retrieve some info
    uint32_t maxWaitMs = 30000;// max waittime in ms
    uint8_t providerProfile = gNvmCfg.dev.modem.provider % MODEM_PROVIDERPROFILES;
    char imei[MODEM_IMEI_LEN];
    char iccid[MODEM_ICCID_LEN];

    bool rc_ok = Modem_readCellularIds(
                        gNvmCfg.dev.modem.radioAccessTechnology,
                        gNvmCfg.dev.modem.providerProfile[providerProfile].simpin,
                        (uint8_t*)imei,
                        (uint8_t*)iccid,
                        maxWaitMs
                    );
    if (rc_ok)
    {
		// store it in the config
		strncpy((char*)gNvmCfg.dev.modem.imei, imei, sizeof(gNvmCfg.dev.modem.imei));
		strncpy((char*)gNvmCfg.dev.modem.iccid, iccid, sizeof(gNvmCfg.dev.modem.iccid));
		LOG_DBG(LOG_LEVEL_MODEM,"imei:  %s\n"
								"iccid: %s\n", imei, iccid);

		// If hardware has NFC present, write IMEI and ICCID to tag
#ifdef CONFIG_PLATFORM_NFC
		if((Device_GetHardwareVersion() >= MINIMUM_NFC_HARDWARE_LEVEL) && !Device_HasPMIC())
		{
			int len = NFC_BuildNDEF(eNDEF_SHIPMODE);
			if(len > 0)
			{
				NFC_WriteNDEF(len);
			}
		}
#endif
		// now construct the device id to be used by mqtt (and later idef)
		sprintf((char *)gNvmCfg.dev.mqtt.deviceId, "IMEI%s", (char *) gNvmCfg.dev.modem.imei);

        rc_ok = NvmConfigWrite(&gNvmCfg);
        if(!rc_ok)
        {
        	LOG_DBG(LOG_LEVEL_MODEM,"NvmConfigDefaults: write failed\n");
        }
    }

    return rc_ok;
}

static bool cliNvmWrite( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	bool rc_ok = true;

	if(args == 0)
	{
		NvmPrintConfig();// just here for lazy debug purposes, did not want to add another command for this
		NvmPrintData();// just here for the same lazy reason
		NvmPrintCalib();// this one too
		rc_ok = false;
	}
	else
	{
		if(argi[0] == 1)
		{
		    gNvmCfg.dev.general.timestamp = ConfigSvcData_GetIDEFTime();
			if (false == NvmConfigWrite(&gNvmCfg))
			{
				printf("NvmConfigWrite: failed\n");
			}
			else
			{
				if(Device_HasPMIC())
				{
					PMIC_ScheduleConfigUpdate();
				}
			}
		} else
		{
			if(argi[0] == -1)
			{
			    printf("Initialise configuration with defaults.. please wait\n");
			    NvmConfigDefaults(&gNvmCfg);
			    rc_ok = deviceFirstConfigInitialisation();
			    if(Device_HasPMIC())
			    {
			    	PMIC_ScheduleConfigUpdate();
			    }
			}
			else
			{
				printf("to reduce accidental writes, argument must be 1 or -1 for initialise with defaults\n");
			}
		}
	}
	return rc_ok;
}

static bool cliExtHelpTask(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	struct taskList {
		char *txt;
		WAKEUP_CAUSES_t cause;
	} taskLists[] =
	{
		{ "'DS137N' mode", WAKEUP_CAUSE_POWER_ON_FAILSAFE },
		{ "'RTC' mode", WAKEUP_CAUSE_RTC },
		{ "'Therm Alert' mode", WAKEUP_CAUSE_THERMOSTAT },
		{ "'Magnet' mode", WAKEUP_CAUSE_MAGNET },
		{ "'RTC' mode forcing a temperature cycle", WAKEUP_CAUSE_RTC_FORCED_TEMPERATURE },
		{ "'RTC' mode forcing a measurement cycle", WAKEUP_CAUSE_RTC_FORCED_MEASUREMENT },
		{ "'RTC' mode forcing a data upload cycle", WAKEUP_CAUSE_RTC_FORCED_DATAUPLOAD },
		{ "'Cold Boot' mode", WAKEUP_CAUSE_COLD_BOOT },
		{ NULL, 0 }
	};
    printf("task info\t: print list of tasks and stack usage.\n");
    printf("task shutdown\t: gracefully shuts down (first writing back changed config/data to flash etc.)\n");
    printf("task run [app|ota] <param>\t: start application task  or firmware update task, with optional parameter\n");
    printf("examples:\n");
    for(int i = 0; taskLists[i].txt; i++)
    {
    	printf("task run app %2d\t\t: starts application in %s\n", taskLists[i].cause, taskLists[i].txt);
    }
    //printf("task run app %d\t: starts application in communication test mode where 'enableapp 15' simulates a communication cycle\n",WAKEUP_CAUSE_UNKNOWN);

    return true;
}

#if 0
// test purpose settling behavior : control wehat value  vib_self_test_en is set in PowerControl.c (needs uncommenting stuff there also
static bool cli_vib_self_test_en( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    if (args==0) {
        printf("vib_self_test_en %d\n",vib_self_test_en);
    } else {
        vib_self_test_en = argi[0];
    }
    return true;
}
#endif
/*
 * cliTask
 *
 * task command
 */
static bool cliTask( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	bool rc_ok = false;

	if ((args==0) || (strcmp((const char*)argv[0], "info") == 0))
	{
	    Resources_PrintTaskInfo();
	    rc_ok = true;
	}
	else
	{
	    // args >=1
        if (strcmp((const char*)argv[0], "shutdown") == 0)
        {
            xTaskDeviceShutdown(false);
            rc_ok = true;
        }
        else if (strcmp((const char*)argv[0], "run") == 0)
        {
			if( (args >= 3) &&
				(0 == strcmp((const char*)argv[1], "app")) &&
				((argi[2] >= WAKEUP_CAUSE_POWER_ON_FAILSAFE && argi[2] <= WAKEUP_CAUSE_MAGNET) ||
				 (argi[2] >= WAKEUP_CAUSE_RTC_FORCED_TEMPERATURE && argi[2] <= WAKEUP_CAUSE_COLD_BOOT)) )
			{
				uint8_t wakeupReason = argi[2];
				printf("executing  startApplicationTask(%d)\n",wakeupReason);
				xTaskApp_startApplicationTask(wakeupReason);
				rc_ok = true;
			}
			else
			{
				printf("cmd format 'task run app #'\n");
            }
        }
	}
	return rc_ok;
}


static bool cliExtHelpSetCalib(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
    printf("Set the calibration constants:\n");
    printf("setCalib raw <value>\n");
    printf("setCalib bearingEnv3 <value>\n");
    printf("setCalib wheelFlat <value>\n");
    printf("setCalib\t\twith no parameter for listing the current values\n");
    return true;
}

static bool cliSetCalib( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    bool rc_ok = false;

    // Check number of parameters
    if (args == 0) {
        printf("setCalib raw         %f\n",gNvmCalib.dat.scaling.raw);
        printf("setCalib bearingEnv3 %f\n",gNvmCalib.dat.scaling.bearingEnv3);
        printf("setCalib wheelFlat   %f\n",gNvmCalib.dat.scaling.wheelFlat);
        rc_ok = true;
    } else {
        if (args==2) {
            if  (strcmp((const char*)argv[0], "raw") == 0) {
                gNvmCalib.dat.scaling.raw = atof((char *)argv[1]);
                gNvmCfg.dev.measureConf.Scaling_Raw = gNvmCalib.dat.scaling.raw;
                rc_ok=true;
            }
            if  (strcmp((const char*)argv[0], "bearingEnv3") == 0) {
                gNvmCalib.dat.scaling.bearingEnv3 = atof((char *)argv[1]);
                gNvmCfg.dev.measureConf.Scaling_Bearing = gNvmCalib.dat.scaling.bearingEnv3;
                rc_ok=true;
            }
            if  (strcmp((const char*)argv[0], "wheelFlat") == 0) {
                gNvmCalib.dat.scaling.wheelFlat = atof((char *)argv[1]);
                gNvmCfg.dev.measureConf.Scaling_Wheel_Flat = gNvmCalib.dat.scaling.wheelFlat;
                rc_ok=true;
            }
            if (rc_ok==true) {
                rc_ok = NvmCalibWrite(&gNvmCalib);
                if (rc_ok==false) {
                    printf("Update calibration flash failed !\n");
                }
            }
        } else {
            rc_ok=false;
        }
    }

    return rc_ok;
}

static bool cliSetTempHiLowLimits( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    bool rc_ok = false;

    // Check number of parameters
    if (args == 0)
    {
        printf("Temp Upper Limit: %f\n",gNvmCfg.dev.measureConf.Temperature_Alarm_Upper_Limit);
        printf("Temp Lower Limit: %f\n",gNvmCfg.dev.measureConf.Temperature_Alarm_Lower_Limit);
        rc_ok = true;
    }
    else
    {
        if (args == 2)
        {
            if  (strcmp((const char*)argv[0], "upper") == 0)
            {
                gNvmCfg.dev.measureConf.Temperature_Alarm_Upper_Limit = atof((char *)argv[1]);
                rc_ok=true;
            }
            else if  (strcmp((const char*)argv[0], "lower") == 0)
            {
                gNvmCfg.dev.measureConf.Temperature_Alarm_Lower_Limit = atof((char *)argv[1]);
                rc_ok=true;
            }
            else
            {
            	printf("Incorrect parameters\n");
            }
            if (false == NvmConfigWrite(&gNvmCfg))
            {
            	printf("NvmConfigWrite: failed\n");
			}
        }
        else if(strcmp((const char*)argv[0], "pmic") == 0)
        {
        	if(Device_HasPMIC())
        	{
        		if(args == 3)
				{
        			gNvmCfg.dev.measureConf.Temperature_Alarm_Upper_Limit = atof((char *)argv[1]);
        			gNvmCfg.dev.measureConf.Temperature_Alarm_Lower_Limit = atof((char *)argv[2]);
				}
        		params_t stcParams =
        		{
        	    		.group = PARAMS_TEMP_ALARMS,
        	    		.params.config.tempAlarms.lowerLimit = (uint8_t)gNvmCfg.dev.measureConf.Temperature_Alarm_Lower_Limit,
        	    		.params.config.tempAlarms.upperLimit = (uint8_t)gNvmCfg.dev.measureConf.Temperature_Alarm_Upper_Limit
        		};
        		PMIC_sendMK24ParamUpdateMessage(stcParams);

        		rc_ok = true;
        	}
        	else
        	{
        		printf("Unsupported Hardware\n");
        	}
        }
        else
        {
            rc_ok=false;
        }
    }

    return rc_ok;
}


/*
 * cliDcLevel
 *
 * dclevel     show raw 16-bit ADC value of DC-Level pin
 *
 *
 */
static bool cliDcLevel( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    float adcValue = 0x00;
	EnergyMonitor_ReadDCLevel_v(&adcValue);

    // Report
    printf("Vd = %f V\n", adcValue);
    printf("Ed = %f J\n", 0.5 * adcValue * adcValue * 232);		// capacitor 232 Farads

    return true;
}


// syntax printsamples type <count>
static bool cliPrintSampleBuf( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    bool rc_ok = true;
    bool integerdump = false;
    bool floatdump = false;
    uint32_t i;
    float scaling = 1.0;
    float dt=1.0;
    uint32_t count=2048;

    extern int32_t __sample_buffer[];// defined in the linker file, used for transfering the waveforms

    if (args>=1) {

        if (strcmp((const char*)argv[0], "env3") == 0) {
            scaling = gNvmCfg.dev.measureConf.Scaling_Bearing * MEASURE_BEARING_ENV3_SCALING;
            count = gNvmCfg.dev.measureConf.Samples_Bearing;
            dt = gNvmCfg.dev.measureConf.Sample_Rate_Bearing;
            printf(" Dump with Bearing Env3 parameters SampleRate= %g, scaling = %g, samples = %d\n\n",dt,scaling,count);
        }
        if (strcmp((const char*)argv[0], "raw") == 0) {
            scaling = gNvmCfg.dev.measureConf.Scaling_Raw * MEASURE_RAW_SCALING;
            count = gNvmCfg.dev.measureConf.Samples_Raw;
            dt = gNvmCfg.dev.measureConf.Sample_Rate_Raw;
            printf(" Dump with raw parameters SampleRate= %g, scaling = %g, samples = %d\n\n",dt,scaling,count);
        }
        if (strcmp((const char*)argv[0], "wheel") == 0) {
            scaling = gNvmCfg.dev.measureConf.Scaling_Wheel_Flat * MEASURE_WHEELFLAT_SCALING;
            count = gNvmCfg.dev.measureConf.Samples_Wheel_Flat;
            dt = gNvmCfg.dev.measureConf.Sample_Rate_Wheel_Flat;
            printf(" Dump with Wheel flat parameters SampleRate= %g, scaling = %g, samples = %d\n\n", dt,scaling,count);
        }

        if (args==2) count = argi[1];
        // safeguard agains division by zero
         if (dt!=0) {
             dt=1/dt;
         } else {
             dt = 1.0;
         }
         if (count>32768) count=32768;//TODO should use define or so

        if (strcmp((const char*)argv[0], "idata") == 0) {
            integerdump = true;
            printf(" Dump as integer data SampleRate=%f scaling = %f, samples = %d\n\n",dt,scaling,count);
        }
        if (strcmp((const char*)argv[0], "fdata") == 0) {
            dt=1;
            scaling=1;
            floatdump = true;
            printf(" Dump as float data SampleRate=%f scaling = %f, samples = %d\n\n",dt,scaling,count);
        }


        for (i=0; i<count; i++) {
#if 0
            // snprint does not do floats (needs linker option, startup initialization ? and probably increased stack and heap)
            char buf[40];
            if (integerdump || floatdump) {
                if (integerdump) {
                    snprintf(buf, sizeof(buf), "%5d %9d\n", i,  __sample_buffer[i]);
                } else {
                    snprintf(buf, sizeof(buf),"%5d %9f\n", i,  ((float *) __sample_buffer)[i]);
                }
            } else {
                snprintf(buf, sizeof(buf),"%f %f\n", dt*i,  __sample_buffer[i] * scaling);
            }
            printf(buf);
#else
            if (integerdump || floatdump) {
                if (integerdump) {
                    printf("%5d %9d\n", i,  __sample_buffer[i]);
                } else {
                    printf("%5d %9f\n", i,  ((float *) __sample_buffer)[i]);
                }
            } else {
                printf("%f %f\n", dt*i,  __sample_buffer[i] * scaling);
            }
#endif
        }

    }

    return rc_ok;
}

static void CliControlModemPower(uint16_t turnOn)
{
	bool cmdExeSuccess = true;

	if (turnOn)
	{	// Is the Modem Off?
		if (powerModemIsOn() == false)
		{	// Now switch ON the modem.
			if (powerModemOn() == false)
			{
				printf("powerModemOn() returned false!\n");
				cmdExeSuccess = false;
			}
		}
		else
		{
			printf("\nModem is already powered ON!!!\n");
			cmdExeSuccess = false;
		}
	}
	else
	{	// Switch OFF the modem.
		powerModemOff();
	}

	if (cmdExeSuccess)
	{
		// Report the Modem power status.
		if (powerModemIsOn() == true)
		{
			printf("pwrctrl 0 1 - OK\n");
		}
		else
		{
			printf("pwrctrl 0 0 - OK\n");
		}
	}
}

static void CliControlGNSSPower(uint16_t turnOn)
{
	bool cmdExeSuccess = true;

	if (turnOn)
	{	// Is the GNSS Off?
		if (powerGNSSIsOn() == false)
		{	// Now switch ON the GNSS.
			powerGNSSOn();
		}
		else
		{
			printf("\nGNSS is already powered ON!!!\n");
			cmdExeSuccess = false;
		}
	}
	else
	{	// Switch OFF the GNSS.
		powerGNSSOff();
	}

	if (cmdExeSuccess)
	{
		// Report the Modem power status.
		if(powerGNSSIsOn() == true)
		{
			printf("pwrctrl 1 1 - OK\n");
		}
		else
		{
			printf("pwrctrl 1 0 - OK\n");
		}
	}
}

static void CliControlAnalogPower(uint16_t turnOn)
{
	bool cmdExeSuccess = true;

	if (turnOn)
	{	// Is the Analog power Off?
		if (powerAnalogIsOn() == false)
		{	// Now switch ON the Analog Power.
			powerAnalogOn();
		}
		else
		{
			printf("\nAnalog circuitry is already powered ON!!!\n");
			cmdExeSuccess = false;
		}
	}
	else
	{	// Switch OFF the Analog Power.
		powerAnalogOff();
	}

	if (cmdExeSuccess)
	{
		// Report the Modem power status.
		if (powerAnalogIsOn() == true)
		{
			printf("pwrctrl 2 1 - OK\n");
		}
		else
		{
			printf("pwrctrl 2 0 - OK\n");
		}
	}
}

static bool cliPowerControl( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    bool rc_ok = true;

    if (args==2)
    {
    	switch ((subSystemTypes_t)argi[0])
    	{
    		case MODEM_SUBSYSTEM:
    			CliControlModemPower(argi[1]);
    			break;

    		case GNSS_SUBSYSTEM:
    			CliControlGNSSPower(argi[1]);
    			break;

    		case ANALOG_SUBSYSTEM:
    			CliControlAnalogPower(argi[1]);
    			break;

#if 0		// the following have been disabled for now, as they result in additional complication around power rail switching
    		// See Barts email thread 20160727
    	case 3:
    	    if (argi[1]) {
    	    	powerPSU_ENABLE_ON();
    	    } else {
    	    	powerPSU_ENABLE_OFF();
    	    }
    		break;
    	case 4:	// switch gnss, modem and analog all on/off
    		if (argi[1]) {
    			powerModemOn();
    			powerGNSSOn();
				powerAnalogOn();
				powerPSU_ENABLE_ON();
			} else {
				powerModemOff();
    			powerGNSSOff();
				powerAnalogOff();
				powerPSU_ENABLE_OFF();
			}
			break;
#endif

    	default:
    		rc_ok = false;
    		break;
    	}
    }
    else
    {
    	rc_ok = false;
    	printf("\nIncorrect no.of parameters entered\n");
    }

    return rc_ok;
}



static bool cliCommsTest( uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
    uint32_t funcNr = 0;
    uint32_t repeatCount = 1;
	// enable the application task

	if(argc>=1)
	{
	    funcNr = argi[0];

		if(argc>=2)
		{
			repeatCount = argi[1];
		}

		if(false == xTaskApp_commsTest( funcNr,  repeatCount))
		{
		    printf("xTaskApp_commsTest failed\n");
		}
	}

	return true;
}


static bool cliEnterSleep( uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	char *s = "";

	// a mechanism for powering down the node without clearing
	// the unexpected shutdown flag is required so pass 1 to
	// not clear the flag  'entersleep 1'
	// changed to a bit field:
	// Bit 0: If set do not clear the flag
	// Bit 1: If set clear the LVD reset flag and do a reboot

	printf("PMC_LVDSC1: 0x%02X\n", PMC_RD_LVDSC1(PMC));
	if(PMC_RD_LVDSC1_LVDRE(PMC))
	{
		PMC_WR_LVDSC1_LVDRE(PMC, 0);
		printf("PMC_LVDSC1: 0x%02X\n", PMC_RD_LVDSC1(PMC));
	}

	if(argc && (argi[0] & 2))
	{
		printf("Resetting...\n");
		vTaskDelay(20/portTICK_PERIOD_MS);
		NVIC_SystemReset();
	}

	if(!(argc && (argi[0]) & 1))
	{
		Vbat_ClearFlag(VBATRF_FLAG_UNEXPECTED_LAST_SHUTDOWN);
		s = ", cleared flag";
	}

	// before powering down log reason
	LOG_EVENT(0, LOG_NUM_APP, ERRLOGINFO, "Node powerdown by entersleep%s", s);

	while(1)
	{
		printf("Power off\n");
		vTaskDelay(1000/portTICK_PERIOD_MS);

		if(Device_HasPMIC())
		{
			PMIC_Powerdown();
		}
		else
		{
			GPIO_DRV_SetPinOutput(nPWR_ON);
		}
	}

	return true;
}


static bool cliPowerOffFailsafeNow(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
    printf("Triggering processor reset via TMR-POWER-OFF# line...(will probably power off node)\n");
    if (Device_GetHardwareVersion() == HW_PASSRAIL_REV1)
    {
        printf("NOTE: Will NOT work on this rev 1 board hardware, but the power-off control signal will still appear on TP21\n");
    }

    // 1-second delay to allow CLI message to be sent
    vTaskDelay(1000/portTICK_PERIOD_MS);

    GPIO_DRV_ClearPinOutput(TMR_POWER_OFFn);

    // Kinetis should now reset at this point

    return true;
}


static bool cliPowerOffFailsafeStart(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
    uint16_t NumSecs;
    bool bOK;

    if (!PowerOffFailsafe_IsRunning())
    {
        NumSecs = (uint16_t)argi[0];
        if ((NumSecs > 0) && (NumSecs <= POWEROFFFAILSAFE_MAX_SECS))
        {
            bOK = PowerOffFailsafe_Start(NumSecs);
            if (bOK)
            {
                printf("Power-off failsafe timer will switch off node power in %d seconds\n", NumSecs);
                if (Device_GetHardwareVersion() == HW_PASSRAIL_REV1)
                {
                    printf("NOTE: Power-off is NOT SUPPORTED on this rev 1 board hardware, but the power-off control signal will still appear on TP21\n");
                }
            }
            else
            {
                printf("ERROR: Power-off failsafe starting failed (reason unknown)\n");
            }
        }
        else
        {
            printf("ERROR: Secs must be in range 1 - %d (max duration is limited by internal counter range)\n", POWEROFFFAILSAFE_MAX_SECS);
        }
    }
    else
    {
        printf("ERROR: Power-off failsafe timer is already running\n");
    }

    return true;
}

static bool cliPowerOffFailsafeStop(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
    if (PowerOffFailsafe_IsRunning())
    {
        PowerOffFailsafe_Stop();
        printf("Power-off failsafe timer has been stopped\n");
    }
    else
    {
        printf("ERROR: Power-off failsafe timer is already stopped\n");
    }

    return true;
}

static bool cliAnalogGain(uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    bool rc_ok = true;
    char *pOnOrOffStr;

    if (args == 1)
    {
        if (powerAnalogIsOn())
        {
            pOnOrOffStr = (char *)argv[0];

            if (strcmp(pOnOrOffStr, "on") == 0)
            {
                AnalogGainSelect(true);
            }
            else if (strcmp(pOnOrOffStr, "off") == 0)
            {
                AnalogGainSelect(false);
            }
            else
            {
                printf("ERROR: Parameter is not 'on' or 'off'\n");
                rc_ok = false;
            }
        }
        else
        {
            cliSubsystemNotPoweredOnError("Analog");
        }
    }
    else
    {
        rc_ok = false;
    }

    return rc_ok;
}

static bool cliAnalogFilt(uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    bool rc_ok = true;
    char *pFiltSelectStr;

    if (args == 1)
    {
        if (powerAnalogIsOn())
        {
            pFiltSelectStr = (char *)argv[0];

            if (strcmp(pFiltSelectStr, "raw") == 0)
            {
                AnalogFiltSelect(ANALOGFILT_RAW);
            }
            else if (strcmp(pFiltSelectStr, "vib") == 0)
            {
                AnalogFiltSelect(ANALOGFILT_VIB);
            }
            else if (strcmp(pFiltSelectStr, "wflats") == 0)
            {
                if ((Device_GetHardwareVersion() == HW_PASSRAIL_REV1) && (!Device_HasPMIC()))
                {
                    printf("ERROR: Wheel-flat measurements are not available on this rev 1 board (no suitable hardware filter chain)\n");
                }
                else
                {
                    AnalogFiltSelect(ANALOGFILT_WFLATS);
                }
            }
            else
            {
                printf("ERROR: Filter selection parameter is not valid\n");
                rc_ok = false;
            }
        }
        else
        {
            cliSubsystemNotPoweredOnError("Analog");
        }
    }
    else
    {
        rc_ok = false;
    }

    return rc_ok;
}

//******************************************************************************
#ifdef VIB_SELF_TEST
//******************************************************************************
// TODO: This needs to be implemented in future, probably for Rev 3 boards.
static bool cliAnalogSelfTest(uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    bool rc_ok = true;
    char *pOnOrOffStr;

    if (args == 1)
    {
        if (powerAnalogIsOn())
        {
            pOnOrOffStr = (char *)argv[0];

            if (strcmp(pOnOrOffStr, "on") == 0)
            {
                AnalogSelfTestSelect(true);
            }
            else if (strcmp(pOnOrOffStr, "off") == 0)
            {
                AnalogSelfTestSelect(false);
            }
            else
            {
                printf("ERROR: Parameter is not 'on' or 'off'\n");
                rc_ok = false;
            }
        }
        else
        {
            cliSubsystemNotPoweredOnError("Analog");
        }
    }
    else
    {
        rc_ok = false;
    }

    return rc_ok;
}

//******************************************************************************
#endif // DAC_NEW
//******************************************************************************
#if 0
//
static bool cliGNSSUseExtConn(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
    powerGNSSOn();

    // Float the GNSS Tx and Rx pins, so that can connect external UART
    // to GNSS via J15
    pinConfigDigitalIn(GNSS_TX, kPortMuxAsGpio, false, kPortPullDown, kPortIntDisabled);
    pinConfigDigitalIn(GNSS_RX, kPortMuxAsGpio, false, kPortPullDown, kPortIntDisabled);

    return true;
}

static bool cliGNSSReqFWVersion(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
    // TODO: This hacky function currently only sends a command to the GNSS to
    // request the FW version - need to check for the response by monitoring
    // the GNSS Tx comms line using a PC terminal. This is just for hardware
    // comms checking for now. Need to expand & tidy up this functionality
    // in future.

    if (powerGNSSIsOn())
    {
        GNSSRequestFWVersion();
    }
    else
    {
        cliSubsystemNotPoweredOnError("GNSS");
    }
    return true;
}
#endif


#if 0
// Currently disabled, but might be resurrected in future
static bool cliADCExtReadRegs(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
    uint32_t AD7766RegValue;

    if (powerAnalogIsOn())
    {
        AD7766Intrpt_SpiInit();
        AD7766RegValue = AD7766Intrpt_ReadSampleRegisters();
        printf("24-bit external ADC register value is %06x hex\n", AD7766RegValue);
        AD7766Intrpt_SpiDeinit();
    }
    else
    {
        cliSubsystemNotPoweredOnError("Analog");
    }

    return true;
}
#endif // 0


// AD7766 sampling
static volatile bool g_bCliSamplingIsComplete = false;

void cliMeasureIsCompleteCallback(void)
{
    g_bCliSamplingIsComplete = true;
}

/*
 * cliAD7766Sample
 *
 * @desc
 *
 * @param
 *
 * @returns
 */
static bool cliAD7766Sample(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
    uint32_t SamplesPerSecMin = 99, SamplesPerSecMax = 999; // Dummy values
    char *pIntrOrDmaStr;
    uint32_t NumSamples;
    uint32_t SamplesPerSec;
    bool bIntrptSamplingSuccess;
    enum
    {
        AD7766SAMPLINGMODE_UNDEFINED = 0,
        AD7766SAMPLINGMODE_INTR,
        AD7766SAMPLINGMODE_DMA
    } SamplingMode;


    if (argc == 3)
    {
        pIntrOrDmaStr = (char *)argv[0];
        NumSamples = argi[1];
        SamplesPerSec = argi[2];

        // Determine sampling mode (interrupt or DMA) and define the minimum and
        // maximum samples per second for that mode
        if (strcmp(pIntrOrDmaStr, "intr") == 0)
        {
            SamplesPerSecMin = AD7766_INTR_MIN_SAMPLES_PER_SEC;
            SamplesPerSecMax = AD7766_INTR_MAX_SAMPLES_PER_SEC;
            SamplingMode = AD7766SAMPLINGMODE_INTR;
        }
        else if (strcmp(pIntrOrDmaStr, "dma") == 0)
        {
            // TODO: Needs more refinement eventually - e.g. define in terms
            // of maximum raw AD7766 input sample rate, and elsewhere also
            // validating the maximum OUTPUT sampling rates for particular DSP
            // chain setups, taking into account the real-time DSP performance
            // capabilities etc
            SamplesPerSecMin = AD7766_DMA_DRIVER_MIN_SAMPLES_PER_SEC; // TODO: NOT YET TESTED
            // Max rate for AD7766-1 16x oversampling variant is 64ksps (as
            // specified in the datasheet). The AD7766 DMA driver itself
            // seems to run OK at at least 100ksps (see
            // AD7766_DMA_DRIVER_MAX_SAMPLES_PER_SEC)
            SamplesPerSecMax = 64000;
            SamplingMode = AD7766SAMPLINGMODE_DMA;
        }
        else
        {
            printf("ERROR: First parameter is not recognised\n");
            SamplingMode = AD7766SAMPLINGMODE_UNDEFINED;
        }

        // Do further checks and then perform sampling
        if ((SamplingMode == AD7766SAMPLINGMODE_INTR) ||
            (SamplingMode == AD7766SAMPLINGMODE_DMA))
        {
            if (NumSamples > AD7766_MAX_OUTPUT_SAMPLES)
            {
                printf("ERROR: Max number of samples allowed is %lu\n", AD7766_MAX_OUTPUT_SAMPLES);
            }
            else if ((SamplesPerSec < SamplesPerSecMin) ||
                     (SamplesPerSec > SamplesPerSecMax))
            {
                printf("ERROR: Samples/sec for %s mode is outside allowed range of %lu to %lu\n",
                       pIntrOrDmaStr, SamplesPerSecMin, SamplesPerSecMax);
            }
            else
            {
                printf("Performing sampling....\n");

                bIntrptSamplingSuccess = false;

                //..............................................................
                // Interrupt-driven sampling
                if (SamplingMode == AD7766SAMPLINGMODE_INTR)
                {
                    // Perform interrupt-driven sampling
                    // NOTE: Requires analog subsystem to be powered on from
                    // here
                    powerAnalogOn();
                    // This delay is now moved inside the powerAnalogOn(), so that
                    // the delay required by a functionality is self contained.
                    //vTaskDelay(200 /  portTICK_PERIOD_MS);

                    // Note: This is a blocking call
                    bIntrptSamplingSuccess = AD7766Intrpt_PerformSampleBurst(NumSamples, SamplesPerSec);

                    powerAnalogOff();

                    if (!bIntrptSamplingSuccess)
                    {
                        printf("************ ERROR: Problem during sampling - do not trust the data ***************\n");
                    }
                    printf("Transmitting sample data on test UART....\n");
                    AD7766_SendSamplesToTestUart(NumSamples, SamplesPerSec,
                                                 g_pSampleBuffer);
                    printf("Done\n");
                }

                //..............................................................
                // DMA-driven sampling
                else if (SamplingMode == AD7766SAMPLINGMODE_DMA)
                {
                    // Perform DMA-driven sampling
                    // Perform RAW ADC sampling here - NOTE: Does NOT switch the
                    // analog power on and off, or configure the hardware gain
                    // and filtering etc (needs to be done using separate CLI
                    // commands). So the power MUST already be on here, otherwise
                    // sampling will stall!
                    if (powerAnalogIsOn())
                    {
                        g_bCliSamplingIsComplete = false;
                        Measure_Start(true,             // Specify raw ADC sampling
                                      MEASID_UNDEFINED, // Undefined because raw ADC sampling
                                      NumSamples,
                                      SamplesPerSec,    // Raw ADC samples per second
                                      cliMeasureIsCompleteCallback);

                        // Block until complete - TODO: put proper FreeRTOS timeout here -
                        // BUT TIMEOUT WILL DEPEND UPON SAMPLING RATE + NUMBER OF SAMPLES
                        while(!g_bCliSamplingIsComplete) vTaskDelay(100/portTICK_PERIOD_MS);// if we do this kind of active polling loops, at least give lower priority tasks some room

                        cliAD7766SendResultsToCliAndTestUart(NumSamples, SamplesPerSec);
                    }
                    else
                    {
                        cliSubsystemNotPoweredOnError("Analog");
                    }
                }

                //..............................................................
			}
        }
        else
        {
            // Error, but already indicated to CLI above
        }
    }
    else
    {
        printf("ERROR: Incorrect number of parameters\n");
    }

    return true;
}

/*
 * cliAD7766PrepareTimeoutTest
 *
 * @desc
 *
 * @param
 *
 * @returns -
 */
static bool cliAD7766PrepareTimeoutTest(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
    uint16_t BlockNum;

    if (argc == 1)
    {
        BlockNum = argi[0];
        if (BlockNum > 0)
        {
            AD7766_PrepareMclkStoppageTest(BlockNum);
            printf("Test enabled - next sampling burst should fail due to timeout in raw ADC block %d (N.B. %d samples/block)\n",
                    BlockNum, ADC_SAMPLES_PER_BLOCK);
        }
        else
        {
            printf("ERROR: blocknum must be greater than 0\n");
        }
    }
    else
    {
        printf("ERROR: Incorrect number of parameters\n");
    }

    return true;
}

/*
 * cliSample
 *
 * @desc    Performs a complete DMA-based sampling sequence:
 *            - Analog hardware power-on
 *            - Analog hardware gain & filtering setup for correct signal type
 *            - AD7766 sampling and real-time DSP
 *            - Analog hardware power-off
 *            - Outputting to test UART
 *
 * @param   The usual CLI parameters.
 *
 * @returns -
 */
static bool cliSample(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
    uint8_t i;
    bool bOK;
    uint32_t NumSamples;
    char *pMeasIdStr;
    tMeasId MeasId;
    static const struct // Static const so that doesn't go onto stack
    {
        tMeasId MeasId;
        char *pStr;
    } MeasIdStrs[] =
    {
#ifdef PASSRAIL_DSP_NEW
        {MEASID_RAWACCEL_25600SPS, "raw25600"},

        {MEASID_VIB_1280SPS, "vib1280"},
        {MEASID_VIB_2560SPS, "vib2560"},
        {MEASID_VIB_5120SPS, "vib5120"},

        {MEASID_WFLATS_256SPS, "wflats256"},
        {MEASID_WFLATS_512SPS, "wflats512"},
        {MEASID_WFLATS_1280SPS, "wflats1280"}
#else // NOT PASSRAIL_DSP_NEW
        {MEASID_RAWACCEL_25600SPS, "raw25600"},
        {MEASID_VIB_2560SPS, "vib2560"},
        {MEASID_WFLATS_1280SPS, "wflats1280"}
#endif // NOT PASSRAIL_DSP_NEW
    };

    bOK = true;

    // Check number of parameters
    if (argc != 2)
    {
        printf("ERROR: Incorrect number of parameters\n");
        bOK = false;
    }

    // Get parameters
    NumSamples = argi[0];
    pMeasIdStr = (char *)argv[1];

    // Check number of samples specified
    if (bOK)
    {
        if (NumSamples > SAMPLE_BUFFER_SIZE_WORDS)
        {
            printf("ERROR: Number of samples is greater than maximum of %ld\n",
                   SAMPLE_BUFFER_SIZE_WORDS);
            bOK = false;
        }
    }

    // Check measurement ID
    MeasId = MEASID_UNDEFINED;
    if (bOK)
    {
        // TODO: DOES THE FOLLOWING SIZEOF STUFF WORK HERE?? (because of the variable string lengths in the structure)
    	// Yes should be fine, const data
        bOK = false;
        for (i = 0; i < (sizeof(MeasIdStrs) / sizeof(MeasIdStrs[0])); i++)
        {
            if (strcmp(pMeasIdStr, MeasIdStrs[i].pStr) == 0)
            {
                MeasId = MeasIdStrs[i].MeasId;
                bOK = true;
                break;
            }
        }
        if (!bOK)
        {
            printf("ERROR: Measurement type not recognised\n");
        }
    }

    // If on rev 1 board then wheel-flat measurements aren't supported
    if (bOK && (Device_GetHardwareVersion() == HW_PASSRAIL_REV1) &&
    	(!Device_HasPMIC()) &&
		(PassRailMeasure_GetAnalogFiltForMeasId(MeasId) == ANALOGFILT_WFLATS))
    {
        printf("ERROR: Wheel-flat measurements are not available on this rev 1 board (no suitable hardware filter chain)\n");
        bOK = false;
    }

    // Perform sampling
    if (bOK)
    {
        g_bCliSamplingIsComplete = false;
        Measure_Start(false,              // Normal sampling
                      MeasId,
                      NumSamples,
                      0,                  // Dummy - not applicable for normal sampling
                      cliMeasureIsCompleteCallback);

        // Block until complete - TODO: put proper FreeRTOS timeout here -
        // BUT TIMEOUT WILL DEPEND UPON SAMPLING RATE + NUMBER OF SAMPLES
        while(!g_bCliSamplingIsComplete) vTaskDelay(100/portTICK_PERIOD_MS);// if we do this kind of active polling loops, at least give lower priority tasks some room

        cliAD7766SendResultsToCliAndTestUart(NumSamples,
                                             PassRailMeasure_GetAdcSpsForMeasId(MeasId));
    }

    return true;
}

/*
 * cliAD7766SendResultsToCliAndTestUart
 *
 * @desc
 *
 * @param
 *
 * @returns -
 */
static void cliAD7766SendResultsToCliAndTestUart(uint32_t NumSamples,
                                                 uint32_t SamplesPerSec)
{
    bool bMeasureErrors;
    MeasureErrorInfoType MeasureErrorInfo;
    static const char CliTestUARTSamplingErrorStr[] =
                "SAMPLING ERROR OCCURRED - See CLI output for more error information\n\n";

    bMeasureErrors = Measure_GetErrorInfo(&MeasureErrorInfo);
    if (!bMeasureErrors)
    {
        int32_t mean;
        uint32_t meanCount;

        getMeanValues(&mean, &meanCount);
        // CLI output
        printf("Sampling completed OK - results will be output to test UART\n");
        printf("Determined DC offset over the first %d samples = %d\n", meanCount, mean);

        // Test UART output - send samples
        AD7766_SendSamplesToTestUart(NumSamples,
                                     SamplesPerSec,
                                     g_pSampleBuffer);
    }
    else
    {
        // CLI output
        printf("*** SAMPLING ERROR OCCURRED ***: Measurement error code=%d, AD7766 error code=%d, raw ADC block number=%d\n",
               MeasureErrorInfo.MeasureError,
               MeasureErrorInfo.AD7766Error,
               MeasureErrorInfo.ErrorBlockNum);
        // Test UART output - send error message, then samples, then error message again
        TestUARTSendBytes((uint8_t *)CliTestUARTSamplingErrorStr,
                          strlen(CliTestUARTSamplingErrorStr));
        AD7766_SendSamplesToTestUart(NumSamples,
                                     SamplesPerSec,
                                     g_pSampleBuffer);
        TestUARTSendBytes((uint8_t *)CliTestUARTSamplingErrorStr,
                          strlen(CliTestUARTSamplingErrorStr));
    }
}

/*
 * cliDacOn
 *
 * @desc	Turns ON the DAC module. This is a low level function useful for
 * 			developers.
 *
 * @param	The usual CLI parameters.
 *
 * @returns
 */
static bool cliDacOn(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	uint16_t nOffset_mV = 0;
    if (powerAnalogIsOn())
    {
    	if(argc == 1)
    	{
    		nOffset_mV = (uint16_t)(argi[0]);
    	}
    	if(IsRequestOffsetValid(&nOffset_mV))
    	{
    		AnalogSelfTestSelect(false);
    		Dac_On(nOffset_mV);
    }
    else
    {
    		printf("Max possible Offset(mV)= %d\n", nOffset_mV);
    	}
    }
    else
    {
        cliSubsystemNotPoweredOnError("Analog");
    }

    return true;
}

/*
 * cliDacOff
 * @desc	cliDacOff Turns OFF the DAC and the PDB system.
 * 			This is a low level function useful for developers.
 *
 * @param	The usual CLI parameters.
 *
 * @returns
 */
static bool cliDacOff(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
    if (powerAnalogIsOn())
    {
        Dac_Off();
    }
    else
    {
        cliSubsystemNotPoweredOnError("Analog");
    }

    return true;
}

/*
 * cliDacTrigger
 *
 * @desc	Starts generating the Sine wave at the requested frequency using the
 * 			DAC. This is a low level function useful for developers.
 *
 * @param	The usual CLI parameters.
 *
 * @returns
 */
static bool cliDacTrigger(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	uint16_t nOutFreq_Hz = 0;
	if (IsDacSystemEn())
    {
		powerAnalogOn();
		if(argc == 1)
        {
			nOutFreq_Hz = (uint16_t)argi[0];

			if(nOutFreq_Hz < 0xFFFF)
            {
				Dac_SetSwTrigger(nOutFreq_Hz);
            }
            else
            {
				printf("Freq. Out of Range !!");
        }
        }
    }
    else
    {
		printf("Enable the DAC system first, refer the cliCmd \"dac-on\"\n");
    }

    return true;
}

#ifdef PDB_DEBUG_EN
static bool cliGetPDBTimerValue(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	printf("\nPDB Timer Value:%ld\n", GetPDBTimerValue());
	return true;
}

static bool cliGetPDBSCValue(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	printf("\nPDB SC Value:0x%lx\n", GetPDBSCValue());
	return true;
}

static bool cliGetPDBMODValue(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	printf("\nPDB MOD Value:0x%lx\n", GetPDBMODValue());
	return true;
}
#endif

static void cliSubsystemNotPoweredOnError(char *SubsystemStr)
{
    printf("ERROR: %s subsystem is not powered on - use pwrctrl command first\n", SubsystemStr);
}


static bool cliUnitTest(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{

	if (argc >= 1)
    {
		if(strcmp((char*)argv[0], "-ea") == 0)
		{
			CU_set_error_action(argi[1]);
		}
		else if(strcmp((char*)argv[0], "-h") == 0)
		{
			// help them people out there
			RunUnitTestsHelp();
		}
		else
		{
			IS25_ReadProductIdentity(NULL);
			RunUnitTests((char*)argv[0]);
		}
    }
    else
    {
        printf("ERROR: ut command requires a minimum of 1 arguments\n");
        RunUnitTestsHelp();
    }

    return true;
}

static bool cliSetWakeup(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	const int taskFlags[] = {0,
			NODE_WAKEUP_TASK_TEMPERATURE_MEAS,
			NODE_WAKEUP_TASK_MEASURE,
			NODE_WAKEUP_TASK_UPLOAD
	};

    if(argc)
    {
        uint32_t mins = argi[0];

        // do measure/upload?
    	if(((argc == 2) || (argc == 3)) && ((argi[1] >= 1) && (argi[1] <= 3)))
		{
			gNvmData.dat.schedule.bDoMeasurements = (argi[1] == 3) ? false : true;
			NvmDataWrite(&gNvmData);
			Vbat_SetNodeWakeupTask(taskFlags[argi[1]]);
		}

    	// set wakeup times
    	xTaskDeviceSetWakeUptime(mins, mins + POFS_MIN);

		// go to sleep now?
		if((argc == 3) && (argi[2] == 1))
		{
			// before powering down log reason and clear flag
			LOG_EVENT(0, LOG_NUM_APP, ERRLOGINFO, "Node powerdown by setwakeup");
		    Vbat_ClearFlag(VBATRF_FLAG_UNEXPECTED_LAST_SHUTDOWN);
			vTaskDelay(3000/portTICK_PERIOD_MS);
			if(Device_HasPMIC())
			{
				xTaskDeviceShutdown(true);
			}
			else
			{
				GPIO_DRV_SetPinOutput(nPWR_ON);
			}
		}
    }

    return true;
}

#ifdef FCC_TEST_BUILD

static bool cliStartFccTest( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    uint32_t samplesPerSec = 25600;		// Default raw sample rate.

    bool powerModem = true;
    bool powerGNSS = true;

    static bool testRunning = false;

    /*
     * Process the options
     */
    if((args > 0) && (args < 5))
    {
		if((argi[0] > 500) && (argi[0] < 65000))	// simple range check
		{
			samplesPerSec = argi[0];
		}
    	for(int i = 0; i < args; i++)
    	{
			if(strcasecmp((const char*)argv[i], "-p") == 0)
			{
				FccTest_PrintSamples(true);
			}
			else if(strcasecmp((const char*)argv[i], "-m") == 0)
			{
				powerModem = false;
			}
			else if(strcasecmp((const char*)argv[i], "-g") == 0)
			{
				powerGNSS = false;
			}
			else if(strcasecmp((const char*)argv[i], "-x") == 0)
			{
				powerGNSS = false;
				powerModem = false;
				testRunning = false;
			}
    	}
    }

    if(testRunning)
    {
    	printf("Test is running\n");
    }

    if(powerModem)
    {
    	if(!powerModemIsOn())
    	{
			Modem_powerup(20000);
			printf("Please wait.. starting Modem.\n");
			while(!powerModemIsOn())
			{
			}
			printf("Modem Powered.\n");
    	}
    }
    else
    {
    	if(powerModemIsOn())
    	{
    		printf("Shutting Modem down.\n");
    		Modem_terminate(0);
    	}
    }

    /*
     * Handle GNSS power
     */
    if(powerGNSS)
    {
    	if(!powerGNSSIsOn())
    	{
    		printf("Powering GNSS\n");
    		powerGNSSOn();
    	}
    }
    else
    {
    	if(powerGNSSIsOn())
    	{
    		printf("Unpowering GNSS\n");
    		powerGNSSOff();
    	}
    }

    if(!testRunning)
    {
    	testRunning = true;
		powerAnalogOn();
		pinConfigDigitalOut(SYSTEM_STATUS_LED1, kPortMuxAsGpio, 0, false);
		printf("Test Running\n");

		Measure_Start(true,             // Specify raw ADC sampling
					  MEASID_UNDEFINED, // Undefined because raw ADC sampling
					  1,				// Don't care, the test should continue forever.
					  samplesPerSec,	// Raw ADC samples per second
					  cliMeasureIsCompleteCallback);
    }

	return true;

}

static bool cliFccTestHelp(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
    printf("Set the node in FCC TEST MODE:\n");
    printf("The optional parameters are: <SampleRate> "
    		"<-m for Modemoff> <-g for gps off> <-p for printsamples>\n");
    printf("e.g StartFccTest 25600 -m -g : "
    		"\nStarts the FCC test, with sample rate as 25600sps, modem off and gps off\n");
    return true;
}
#endif

static bool energySaveBackup( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	printf("\nCalling PMIC_sendStoreEnergyUse()\n");
	if( PMIC_sendStoreEnergyUse( ) )
	{
		printf("Energy store: Ok\n");
	}
	else
	{
		printf("Energy store: Error\n");
	}
	

	return true;
}


#if 0
bool cliBlinkLed( uint32_t args, uint8_t * argv[], uint32_t * argi);
bool cliExtHelpBlinkLed(uint32_t argc, uint8_t * argv[], uint32_t * argi);
#endif
#include "configGnss.h"


static const struct cliCmd boardSpecificCommands[] = {
		{"log","value [port]\t\tlogging bitmask",cliLog, NULL},
		{"configwrite","val	\tval=1: write ram copy of the device config to flash, val=-1:initialise ramcopy and flash with defaults",cliNvmWrite,NULL},
		{"task", "\t\t\t\ttask list and stack usage", cliTask, cliExtHelpTask },
		{"datastore", "\t<id>\t\tData dictionary read id=1..n, or name", cliDataStore, cliExtHelpDataStore },
		{"image", "\t<command>\timage management commands.",image_cliImage, image_cliHelp},
		{"setCalib", "\t<param> <value>\tset calibration parameter, param=[raw, bearingEnv3,wheelFlat]", cliSetCalib, cliExtHelpSetCalib },
#ifdef CONFIG_PLATFORM_EVENT_LOG
		{"eventlog", "\t<id>\t\tError Log value id=1 2 or 3", cliEventLog, cliExtHelpEventLog },
#endif
//		{"set", "(NOT CURRENTLY WORKING) pin val\tset pin to logic level val [0,1]", cliSet, NULL },
//		{"get", "(NOT CURRENTLY WORKING) pin\tget pin value", cliGet, NULL },
//		{"low", "\t\tGo to low power mode (LLS)", cliLow, NULL }
		{"dclevel", "\t\t\tget DC-LEVEL raw ADC value 16-bits unsigned", cliDcLevel, NULL },
        {"pwrctrl", "<subsystem> <val>\tSubsystem power on/off. <subsystem>: 0=modem, 1=GNSS, 2=Analog  <val>: 1=on, 0=off", cliPowerControl, NULL },
        {"printsamplebuf", "wavetype <cnt>\tDump sample buffer, optionally overruling number of samples, types : env3, raw, wheel, idata, fdata)", cliPrintSampleBuf, NULL },
#if 0
        {"blinkLed", "\t<pattern> <timeUnitMs>\tblink the led according the specified pattern and time.", cliBlinkLed, cliExtHelpBlinkLed },
#endif

        {"commstest","<funcnr> <repeat>\tcommunication test functions.", cliCommsTest, NULL},
		{"entersleep","\t\t\tunlatch the power switch FOR TEST ONLY", cliEnterSleep, NULL},
		{"settemplimits","\t\t\tSets the node temperature limits (upper, lower)", cliSetTempHiLowLimits, NULL},
		{"powerofffailsafenow", "\t\tTrigger immediate processor reset and power-off by bringing TMR-POWER-OFF# line low", cliPowerOffFailsafeNow, NULL},
		// N.B. The 524 secs in the powerofffailsafestart command below corresponds to POWEROFFFAILSAFE_MAX_SECS
        {"powerofffailsafestart", "<secs>\tStart power-off failsafe timer to power off node after <secs> (max 524 secs)", cliPowerOffFailsafeStart, NULL},
        {"powerofffailsafestop", "\t\tStop power-off failsafe timer", cliPowerOffFailsafeStop, NULL},

		DS1374_CLI,

		// external counter DS137n
		{"ds137nReadCounter", "\t\tRead counter from DS137n", cliDs137nReadCounter, NULL},
		{"ds137nSetSleepPeriod", "\t\tSet DS137n to alarm mode [mins]", cliDs137nSetSleepPeriod, NULL},
#ifdef EXT_WDG_DS1374_WAKEUP_TEST
		{"ExtWdgTest", "Node woken up by Ext Watchdog Ds1374", cliTrigExtWdgDs1374NodeWakeupTest, NULL},
#endif

		// internal RTC
		{"setrtctime", "\t\t\tSet RTC to new time hr min sec", cliSetRtcTime, NULL},
		{"setrtcdate", "\t\t\tSet RTC to new date yr month day", cliSetRtcDate, NULL},
		{"getrtctime", "\t\t\tRead the current RTC time", cliGetRtcTime, NULL},
		{"checkrtcalarm", "\t\t\tCheck RTC alarm working correctly", cliCheckRtcAlarm, NULL},

		// Mems sensor
		{"readlis3dh", "<samples> <dump>\tRead LIS3DH X, Y and Z, samples : nr of samples to take, dump=1 for raw data dump", cliReadLIS3DH, NULL},
		{"monitorMovement","<treshold> <duration>\tmems motion detection (threshold[g]  duration[secs])", cliLis3dhMonitorMovement, NULL},
		{"memsSelfTest", "\t\t\tLIS3DH self test", cliMemsSelfTest, NULL},

		// HDC1080 Humidity sensor
		{"readhumidity", "\t\t\tRead Humidity Sensor", cliHumidity, NULL},

		// TMP431 temperature sensor
		{"tmp431ReadTemp", "\t\tRead temperatures from TMP431", cliReadTemp, NULL},

		// Read board version
		{"readBoardVer", "\t\t\tRead the board version", cliReadBoardVersion, NULL},

		// IS25 external flash
        EXTFLASH_CLI,
		TEMP_MEAS_STORAGE_CLI,	// Cli for Temperature Measurements
		// gnss
		GNSS_CLI,
		MT3333_CLI,

		{"gnssRotnSpeedChangeTest", "\t<Spd 1>..<Spd n> Set the Speed profile (knots) pass no values to reset", cliGnssRotnSpeedChangeTest, NULL},

        // Analog control and sampling commands
        {"analog-gain", "<on|off> \t\tAnalog hardware gain on/off", cliAnalogGain, NULL},
        {"analog-filt", "<raw|vib|wflats> \tAnalog hardware filter selection: Raw, vibration or wheel-flats", cliAnalogFilt, NULL},
#ifdef VIB_SELF_TEST
        {"analog-selftest", "<on|off> \tAnalog hardware self-test on/off", cliAnalogSelfTest, NULL},
#endif
        {"ad7766-samp", "<intr|dma> <numsamples> <samples/sec>\tPerform AD7766 sampling, output to test UART @ 115kbaud", cliAD7766Sample, NULL},
        {"ad7766-preparetimeouttest", "<blocknum>\tPrepare block timeout test for next sampling burst, to trigger for raw ADC blocknum (1=first block, 128 ADC samples/block)", cliAD7766PrepareTimeoutTest, NULL},
        {"dac-on", "<Offset in mV, should be <= 500mV> \tEnable DAC converter, with its output set to .7V", cliDacOn, NULL},
        {"dac-off", "\t\t\tDisable DAC converter", cliDacOff, NULL},
		{"dac-trig", "Trigger<Output Frequency> the buffer value to the DAC output(Vout)", cliDacTrigger, NULL},
		{"dac-selftest", " <1/0>\tPerform the DAC Self test and send the Output to test UART if option 1", cliDacSTStart, NULL},
#ifdef PDB_DEBUG_EN
		{"pdb", "Get the PDB Timer Value", cliGetPDBTimerValue, NULL},
		{"pdb-sc", "Get the PDB SC Value", cliGetPDBSCValue, NULL},
		{"pdb-mod", "Get the PDB MOD Value", cliGetPDBMODValue, NULL},
#endif

        {"simulAmSignal", "<am> <carrierFreq> <depth> <amfreq> <dcoffset>\toverwrites ADC values with simulated AM signal ", cliSimulAmSignal, cliExtHelpSimulAmSignal },

#ifdef PASSRAIL_DSP_NEW
        {"sample", "<numsamples> <raw25600 | vib[1280|2560|5120] | wflats[256|512|1280]>\tPerform sampling burst with full hardware control & DSP, output to test UART @ 115kbaud", cliSample, NULL},
#else
        {"sample", "<numsamples> <raw25600|vib2560|wflats1280>\tPerform sampling burst with full hardware control & DSP, output to test UART @ 115kbaud", cliSample, NULL},
#endif

		{"ut", "<groupname> <test #> [<params>]\tPerform unit test <groupname> -> <test #>, with optional params", cliUnitTest, NULL},

		// misc commands
		{"suspendApp", "\t\t\tSuspend the mvp application", cliSuspendApp, NULL},
		{"suspendShutdown", "\t\t\tSuspend MK24 from sending Shutdown msg to PMIC", cliSuspendShutDown, NULL},
		{"selftest", "\t\t\tRun the self test routine", cliSelfTest, cliSelftestHelp},
		{"useAcceleratedScheduling", "\tUse accelerated scheduling 1 or 0", cliUseAcceleratedScheduling, NULL},
		{"setwakeup", "\t\t\t<t> <m[0|1|2|3]> <r[0|1]> Set RTC wakeup time (mins), options: force temp/fdsmeasure/upload and reboot", cliSetWakeup, NULL},
		{"measure", "\t\t\tPerform a measurement cycle", cliDoMeasure, NULL},
		{"schedule", "\t\t\tTest scheduler", cliSchedule, cliScheduleHelp},
#ifdef CONFIG_PLATFORM_NFC
		{"nfc", "\t\t\t\tNFC access", NFC_CLI_cli, NFC_CLI_Help},
#endif
		{"vbat", "\t\t\t\tVBAT register contents access", cliVbat, cliHelpVbat},
		{"otaTest", " <subcommand>\t\tOTA test commands.",ota_cliOta, ota_cliHelp},
		{"energySaveBackup", " \tPmic save energy to backup mem",energySaveBackup, NULL},

#ifdef FCC_TEST_BUILD
		{"startFccTest", "\tStarts the FCC test <SampleRate(optional)> <1 = Printoutput(optional)>", cliStartFccTest, cliFccTestHelp },
#endif
//        {"vib_self_test_en", "val   \tSet the value of vib_self_test_en during normal measurement", cli_vib_self_test_en, NULL},
};

bool boardSpecificCliInit()
{
	return cliRegisterCommands(boardSpecificCommands , sizeof(boardSpecificCommands)/sizeof(*boardSpecificCommands));
}

static bool cliHumidity( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	float temp = 0x00;
	HDC1050_ReadHumidityTemperature(&commsRecord.params.Humidity, &temp);

	printf("Humidity = %f%%RH, Temperature = %f degC\n", commsRecord.params.Humidity, temp);

	return true;
}

static bool cliReadTemp( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	float Temperature_Pcb, Temperature_External;
	int count = ((args >= 3) ? argi[2] : 1);
	bool ok = true;

	const SimulatedTemperature_t const* pSimulated = Vbat_CheckForSimulatedTemperatures();
	const bool simulated_temps_are_in_force = (pSimulated->flags & VBATRF_local_is_simulated) || (pSimulated->flags & VBATRF_remote_is_simulated);

	if(simulated_temps_are_in_force)
	{
		printf("Simulated temperatures are present:\n");
	}
	if(pSimulated->flags & VBATRF_local_is_simulated)
	{
		printf("local temp = %d.0 degC ", pSimulated->local);
	}
	if(pSimulated->flags & VBATRF_remote_is_simulated)
	{
		printf("remote temp = %d.0 degC", pSimulated->remote);
	}
	if(simulated_temps_are_in_force)
	{
		printf("\n");
	}

	/* argi[0] = minimum temperature
	 * argi[1] = maximum temperature
	 * argi[2] = no of loops
	 * argi[3] = flags - 1 = turn power on; 2 - turn power off
	 */
	for(int i = 0; ok && (i < count);)
	{
		if(argi[3] & 1)
		{
		   	pinConfigDigitalOut(PSU_ENABLE, kPortMuxAsGpio, 1, false);
		 	vTaskDelay(50/portTICK_PERIOD_MS);
		}
		ok = ReadTmp431Temperatures(&Temperature_Pcb, &Temperature_External);
		if(ok)
		{
			// are we limit checking?
			if(args >= 2)
			{
				if((Temperature_External < (float)argi[0]) || (Temperature_External > (float)argi[1]))
				{
					ok = false;
				}
			}

			// if it's a singleton or a failure display result
			if(!ok || (1 == count))
			{
				printf("local temp = %8.4f degC remote temp = %8.4f degC\n",
					Temperature_Pcb, Temperature_External);
			}

			// display progress
			i++;
			if(ok && (count > 1))
			{
				if(0 == (i % 10000))
				{
					printf("*");
					if(0 == (i % 50000))
						printf("\n");
				} else if(0 == (i % 1000))
					printf(".");
			}

			// error
			if(!ok)
			{
				printf("failed temperature limits\n");
			}
			if(argi[3] & 2)
			{
			   	pinConfigDigitalOut(PSU_ENABLE, kPortMuxAsGpio, 0, false);
			 	vTaskDelay(50/portTICK_PERIOD_MS);
			}
		}
		else
		{
			printf("failed to read TMP431 temperatures\n");
		}
	}

   	pinConfigDigitalOut(PSU_ENABLE, kPortMuxAsGpio, 1, false);
	printf("\n");	// tidy up the screen
	return ok;
}

static bool cliReadBoardVersion(uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	printf("%s\n", Device_GetHardwareVersionString(Device_GetHardwareVersion()));

	return true;
}

static bool cliGnssRotnSpeedChangeTest(uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	if (args==3)
	{
		gnssRotnSpeedChangeTestEn((bool)argi[0]);	// Enable / Disable
		gnssSetPreMeasurementSpeed_Knots(atof((char*)argv[1]));
		gnssSetPostMeasurementSpeed_Knots(atof((char*)argv[2]));
	}
	else
	{
		gnssRotnSpeedChangeTestEn(false);	// Disable
	}
	printf("\nRotn Speed Change Test:%d, Speed1:%f[Knots], Speed2:%f[Knots]\n",
			gnssIsRotnSpeedChangeTestEn(), gnssGetPreMeasurementSpeed_Knots(), gnssGetPostMeasurementSpeed_Knots());

	return true;
}


static bool cliDoMeasure(uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	Measurement_DoMeasurements(false);
	extern void print_measurement_record(void);
	print_measurement_record();
	return true;
}


static bool cliSuspendApp( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	printf("Suspend application running\n");
	gSuspendApp = true;

	return true;
}

static bool cliSuspendShutDown( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	if(Device_HasPMIC())
	{
		printf("Shutdown message won't be sent to PMIC...\n");
		gSuspendMK24Shutdown = true;
	}
	return true;
}

static bool cliUseAcceleratedScheduling( uint32_t args, uint8_t * argv[], uint32_t * argi)
{

	if (args==1)
	{
		gNvmData.dat.schedule.bDoAcceleratedScheduling = argi[0];
		NvmDataWrite(&gNvmData);

		if(gNvmData.dat.schedule.bDoAcceleratedScheduling == true)
		{
			LOG_DBG( LOG_LEVEL_CLI, "Accelerated scheduling is ON\n");
		}
		else
		{
			LOG_DBG( LOG_LEVEL_CLI, "Accelerated scheduling is OFF\n");
		}
	}

	return true;
}





static bool cliDs137nSetSleepPeriod( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
//#ifdef PLATFORM_APPLICATION
	uint32_t value = 0x00;

	DS137n_ReadCounter(&value);
	printf("Seconds since cold boot %d\n", value);

	DS137n_SetAlarm(argi[0] * 60);

	DS137n_ReadAlarm(&value);
	printf("Next DS137n wakeup in %d seconds\n", value);

	printf("Send poweroff command seperately\n");
	vTaskDelay(50/portTICK_PERIOD_MS);
//#endif
	return true;
}

static bool cliDs137nReadCounter( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	uint32_t count = 0x00;

	bool result = DS137n_ReadCounter(&count);
	if(result != true)
	{
		return false;
	}

	printf("DS1371 counter = %d seconds\n", count);

	return true;
}



// Read the LIS3DH accelerometer
static bool cliReadLIS3DH( uint32_t args, uint8_t * argv[], uint32_t * argi)
{

	extern int32_t __sample_buffer[];// defined in the linker file, used for transfering the waveforms
	extern uint32_t __sample_buffer_size[];
	uint32_t samples = LIS3DH_SAMPLEFREQ; // 1 sec
    tLis3dAcc * acc3dp = (tLis3dAcc *) &__sample_buffer[0];
    tLis3dAccf mean,std;
    bool dump = false;

	if (args) {
	    samples = argi[0];
	}
	if (acc3dp && (samples*sizeof(*acc3dp) > (uint32_t)__sample_buffer_size ) ) {
	    samples = ((uint32_t)__sample_buffer_size) /sizeof(*acc3dp);
	    printf("Too many samples for buffer, adjusted to %d\n",samples);
	}
	if (args>1) {
	    dump = argi[1]!=0;
	}
    if (lis3dh_read(samples, dump ? acc3dp : NULL, &mean, &std)) {
        printf("Samples %d\n",samples);
        printf(" dir    mean   (std) :\n x= %9.6f (%9.6f)\n y= %9.6f (%9.6f)\n z= %9.6f (%9.6f)\n",
                mean.x, std.x,
                mean.y, std.y,
                mean.z, std.z
                );
#if 1
        // non essential fun
        {
            float r,incl,az;
#ifndef M_PI
#define M_PI        3.14159265358979323846
#endif
            r = sqrt(mean.x*mean.x + mean.y*mean.y + mean.z*mean.z);
            incl= acos(mean.z/r);
            az = atan2(mean.y, mean.x);
            printf("radius = %9.6f inclination = %9.6f (deg) azimuth = %9.6f (deg)\n", r, incl * 180 / M_PI , az * 180 / M_PI);
        }
#endif
        if (dump) {
            printf("LIS3DH unscaled integer values:\n");
            for (int i=0; i<samples; i++) {
                printf("%3d %6d %6d %6d\n",i, acc3dp[i].x, acc3dp[i].y, acc3dp[i].z);
            }
        }
    } else {
        printf("ReadLis3bh() failed\n");
    }

	return true;
}

static bool cliMemsSelfTest( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    uint32_t passed = 0, failed = 0, loopcount = 0;

    LOG_DBG( LOG_LEVEL_CLI, "MEMS self test\n");
    uint32_t samples = 0;
    if (args == 1)
    {
        samples = argi[0];
    } else if((args == 2) && (strcmp((char *) argv[0], "-l") == 0))
    {
        loopcount = argi[1];
        if(loopcount == 0) loopcount = 1;
        else if(loopcount > 100) loopcount = 100;
    }

    if(loopcount)
    {
        do
        {
            if(lis3dh_selfTest(10) == 0)
                failed++;
            else
                passed++;
            LOG_DBG( LOG_LEVEL_CLI, "MEMS self test: passed=%d, failed=%d\n", passed, failed);
        } while(--loopcount);
    } else {
        if (lis3dh_selfTest(samples)== false) {
            LOG_DBG( LOG_LEVEL_CLI, "MEMS self test FAILED\n");
        }
    }

    return true;
}

static bool cliLis3dhMonitorMovement( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    float threshold = gNvmCfg.dev.measureConf.Acceleration_Threshold_Movement_Detect;
    uint32_t duration = gNvmCfg.dev.measureConf.Acceleration_Avg_Time_Movement_Detect;
    bool moving=false;

    if (args >0 ) {
        threshold = atof((char *) argv[0]);
    }
    if (args >1 ) {
        duration = argi[1];
    }
    if (false == lis3dh_movementMonitoring( threshold, duration , &moving)) {
        printf("movement detection returned an error.\n");
    }

    printf("moving = %d\n",moving);

	return true;
}


static bool cliSetRtcTime( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    rtc_datetime_t datetime;
    bool rc_ok;

    rc_ok = RtcGetTime(&datetime);
    if (rc_ok)
    {
    	// Update MK24 RTC only.
		if (strcmp((const char*)argv[0], "-mk24") == 0)
		{
			if(args == 4)
			{
				datetime.hour = argi[1];
				datetime.minute = argi[2];
				datetime.second = argi[3];
				printf("\nChanging the MK24 RTC Time to %02d:%02d:%02d\n\n", datetime.hour, datetime.minute, datetime.second);
				rc_ok = RTC_DRV_SetDatetime(RTC_IDX, &datetime);
			}
			else
			{
				printf("Incorrect input, enter in hh mm ss format\n");
			}
		}
		else
		{
			datetime.hour = argi[0];
			datetime.minute = argi[1];
			datetime.second = argi[2];
			rc_ok = RtcSetTime(&datetime);
		}
    }
    return rc_ok;
}


static bool cliSetRtcDate( uint32_t args, uint8_t * argv[], uint32_t * argi)
{

    rtc_datetime_t datetime;
    bool rc_ok;

    rc_ok = RtcGetTime(&datetime);

    if(rc_ok)
    {
    	// Update MK24 RTC only.
		if (strcmp((const char*)argv[0], "-mk24") == 0)
		{
			if(args == 4)
			{
				datetime.year = argi[1];
				datetime.month = argi[2];
				datetime.day = argi[3];
				printf("\nChanging the MK24 RTC Date to %d/%d/%d\n\n", datetime.day, datetime.month, datetime.year);
				rc_ok = RTC_DRV_SetDatetime(RTC_IDX, &datetime);
			}
			else
			{
				printf("Incorrect input, enter in yyyy mm dd format\n");
			}
		}
		else
		{
			datetime.year = argi[0];
			datetime.month = argi[1];
			datetime.day = argi[2];
			rc_ok = RtcSetTime(&datetime);
		}
    }
    return rc_ok;
}

static bool cliGetRtcTime( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    rtc_datetime_t datetime;
    bool rc_ok;

    rc_ok = RtcGetTime(&datetime);

    if (rc_ok) {
        printf("%02d/%02d/%02d %02d:%02d:%02d\n", datetime.year, datetime.month, datetime.day,
                                                datetime.hour, datetime.minute, datetime.second);
    } else {
        printf("Error retrieving date\n");
    }
    return rc_ok;

}

static bool cliCheckRtcAlarm( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    bool rc_ok = false;
    uint32_t rtcStart = 0;

    if( true == RtcGetDatetimeInSecs(&rtcStart) )
    {
        RtcClearAlarmFlag();
        printf("\nRTC Alarm set to trigger in 10 secs from now, wait until that...\n");
        RtcPerformAlarmTest(10);
        vTaskDelay(12000);
        if(RtcIsAlarmTriggered())
        {
            printf("\nRTC Alarm trigerred after %d secs\n",
                   RtcGetAlarmTrigAtSecs() - rtcStart);
            rc_ok = true;
        }
        RtcClearAlarmFlag();
    }

    return rc_ok;
}

#ifdef EXT_WDG_DS1374_WAKEUP_TEST
static bool cliTrigExtWdgDs1374NodeWakeupTest( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	g_bTriggerExtWatchdogWakeUp = true;
	return true;
}
#endif


#ifdef __cplusplus
}
#endif