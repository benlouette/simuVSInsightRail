#ifdef __cplusplus
extern "C" {
#endif

/*
 * selfTest.c
 *
 *  Created on: 18 Aug 2017
 *      Author: TK2319
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "CLIcmd.h"
#include "Log.h"
#include "printgdf.h"

#include "NvmConfig.h"
#include "NvmData.h"

#include "ConfigData.h"
#include "ConfigSvcData.h"

#include "rtc.h"
#include "DrvTmp431.h"
#include "drv_is25.h"
#include "lis3dh.h"
#include "hdc1050.h"
#include "ds137n.h"

#include "PassRailMeasure.h"

#include "PowerControl.h"
#include "DacWaveform.h"

#include "ExtFlash.h"

#include "resources.h"

#include "xTaskApp.h"
#include "xTaskAppGnss.h"
#include "xTaskAppCommsTest.h"
#include "taskGnss.h"
#include "selfTest.h"
#include "Vbat.h"
#include "device.h"
#include "EnergyMonitor.h"
#include "Measurement.h"
#include "PMIC.h"
#include "xTaskDevice.h"
#include "schedule.h"

#include "errorcodes.h"
#include "log.h"
static void logSTStarted(char* strDevice);

/****************************** defines ****************************/

#define ADC_SAMPLES_FOR_DAC_SETTLE					(256)
#define ADC_SAMPLE_RATE_FOR_DAC_SETTLE				(2048)
#define ADC_SAMPLES_FOR_DAC_SELF_TEST				(2048)
#define ADC_SAMPLE_RATE_FOR_DAC_SELF_TEST			(2048)

// The following limits have been fed by the hardware team.

const struct
{
	const unsigned int lower;
	const unsigned int upper;
} kEnvelope3_SelfTestLimits[] =
{
	{2743256, 4963336},
	{1088158, 1813597}
};

const struct
{
	const unsigned int lower;
	const unsigned int upper;
} kWheelFlat_SelfTestLimits[] =
{
	{3248000, 4760000},
	{1525478, 2542463}
};

const struct
{
	const unsigned int lower;
	const unsigned int upper;
} kRaw_SelfTestLimitsRev12 =
{
	1973601, 3289335
};

#define DAC_ST_LIMIT_TOLERANCE_PERCENT				(5)
#define RTC_ALARM_SELF_TEST_SECS					(2)

// index counter for the self-test results array
static uint8_t sacIndex = 0x00;

// Flag to enable printing the ADC Samples of the DAC self test to TEST UART.
static bool bDacSamplesToTestUART = false;

static const char msgMinMax[] =  "\n%s Min = %d, Max = %d, AvgVpk = %d, MinVpkLimit = %d, MaxVpkLimit = %d\n";

/****************************** externals **************************/

extern volatile bool g_bAppSamplingIsComplete;
extern RTC_Type * const g_rtcBase[RTC_INSTANCE_COUNT];
extern report_t gReport;
extern tNvmCfg gNvmCfg;
extern bool xTaskApp_doSampling(bool bRawAdcSampling,
                   	     tMeasId eMeasId,
						 uint32_t nSampleLength,
						 uint32_t nAdcSamplesPerSecIfRawAdc);

/*================================================================================*
 |                                 IMPLEMENTATION                                 |
 *================================================================================*/

/*
 * updateGnssCommsRecord
 *
 * @desc - Updates comms record with gnss data
 *
 * @params
 *
 */
static bool updateGnssCommsRecord()
{
    bool rc_ok = true;

    tGnssCollectedData data;// just a warning, this is a large structure
    rc_ok = gnssRetrieveCollectedData(&data, 1000);// should not take longer than a second

    if (rc_ok) {

        commsRecord.params.GNSS_Lat_Com =  data.latitude;
        commsRecord.params.GNSS_NS_Com[0] = data.NS;
        commsRecord.params.GNSS_NS_Com[1] = '\0';
        commsRecord.params.GNSS_Long_Com =  data.longitude;
        commsRecord.params.GNSS_EW_Com[0] =  data.EW;
        commsRecord.params.GNSS_EW_Com[1] = '\0';

        commsRecord.params.GNSS_Speed_To_Rotation_Com = xTaskApp_calculateWheelHz(data.speed_knots, gNvmCfg.dev.assetConf.Wheel_Diameter);
    }

	LOG_DBG( LOG_LEVEL_APP, "GNSS_Speed_To_Rotation_Com %f Hz\n", commsRecord.params.GNSS_Speed_To_Rotation_Com);

	/*measureRecord.params.Is_Speed_Within_Env = false;
	if((measureRecord.params.GNSS_Speed_To_Rotation_1 >= gNvmCfg.dev.measureConf.Min_Hz) &&
			(measureRecord.params.GNSS_Speed_To_Rotation_1 <= gNvmCfg.dev.measureConf.Max_Hz))
	{
		measureRecord.params.Is_Speed_Within_Env = true;
	}*/

	return rc_ok;
}


/*
 * DACSTSamplesIsInLimits
 *
 * @desc    Checks if the DAC Self test samples are within the limits specified
 *
 * @param	nMinLimit_Vpeak - Contains the Lower Limit for he Vpeak sample.
 *
 * @param	nMaxLimit_Vpeak - Contains the Upper Limit for the Vpeak sample.
 *
 * @return	true  - If the Vpeak sample is within the limits,
 * 			false - Otherwise.
 */
static bool DACSTSamplesIsInLimits(char *label, int32_t nMinLimit_Vpeak, int32_t nMaxLimit_Vpeak)
{
	uint32_t i = 0;
	bool bRetVal = false;
	int32_t nMin = g_pSampleBuffer[0];
	int32_t nMax = g_pSampleBuffer[0];
	int32_t nAvg_Vpeak = 0;

	if(ADC_SAMPLES_FOR_DAC_SELF_TEST <= SAMPLE_BUFFER_SIZE_WORDS)
	{
		for(i = 0; i < ADC_SAMPLES_FOR_DAC_SELF_TEST; i++)
		{
			// Got a lower value.
			if(g_pSampleBuffer[i] < nMin)
			{
				nMin = g_pSampleBuffer[i];
			}
			// Got a higher value.
			else if(g_pSampleBuffer[i] > nMax)
			{
				nMax = g_pSampleBuffer[i];
			}
		}
	}
	else
	{
		LOG_EVENT(0, LOG_NUM_APP, ERRLOGMAJOR, "*** Incorrect No.of Samples could have lead to Buffer Overflow***");
		return false;
	}

	// Calculate the Vpeak-peak average.
	nAvg_Vpeak = (nMax + abs(nMin)) / 2;
	// Check if the samples are within Limits.
	if((nAvg_Vpeak < nMaxLimit_Vpeak) && (nAvg_Vpeak > nMinLimit_Vpeak))
	{
		// do not use/delete or modify as this line is used by production test
		LOG_DBG(LOG_LEVEL_APP, msgMinMax, label, nMin, nMax, nAvg_Vpeak, nMinLimit_Vpeak, nMaxLimit_Vpeak);
		bRetVal = true;
	}
	else
	{
		LOG_EVENT(0, LOG_NUM_APP, ERRLOGWARN, "%s Min = %d, Max = %d , AvgVpk = %d, MinVpkLimit = %d, MaxVpkLimit = %d",
				  label, nMin, nMax, nAvg_Vpeak, nMinLimit_Vpeak, nMaxLimit_Vpeak);
	}

	return bRetVal;
}

/*
 * DACSelfTestStartSampling
 *
 * @desc    Starts the raw sampling of the DAC O/P sine waveform.
 *
 * @param   eFiltSelect, which filter should be switched in
 *
 * @return	true  - If the Adc sampling has been successfully completed,
 * 			false - If the ADC sampling has timed out or if DAC system Init
 * 			has failed.
 */
static bool DACSelfTestStartSampling(AnalogFiltEnum eFiltSelect)
{
	bool bTestResult = true;

	// Now select the relevant filtering.
    AnalogFiltSelect(eFiltSelect);

    // Start the sampling
	bTestResult = xTaskApp_doSampling(true,
									  MEASID_UNDEFINED,
									  ADC_SAMPLES_FOR_DAC_SELF_TEST,
									  ADC_SAMPLE_RATE_FOR_DAC_SELF_TEST);

	// If sampling still not complete, fail the test.
	if((g_bAppSamplingIsComplete == false) || (bTestResult == false))
	{
		LOG_DBG( LOG_LEVEL_APP, "\nDAC-ST: Sampling Failed\n");
		bTestResult = false;
	}

#ifdef CONFIG_PLATFORM_CLI
    if(bDacSamplesToTestUART)
    {
    	printf("\n ------------------------------------------------------- \n"
    			" ADCSamples:%d, SampleRate:%d sps\n",
    			ADC_SAMPLES_FOR_DAC_SELF_TEST, ADC_SAMPLE_RATE_FOR_DAC_SELF_TEST);
        AD7766_SendSamplesToTestUart(ADC_SAMPLES_FOR_DAC_SELF_TEST,
        							 ADC_SAMPLE_RATE_FOR_DAC_SELF_TEST,
                                     g_pSampleBuffer);
        bDacSamplesToTestUART = false;
    }
#endif
	return bTestResult;
}

/*
 * PerformRawTest
 *
 * @desc    Performs a raw test to determine when the DAC has settled>
 *
 * @return	true  - If the test passes,
 * 			false - Otherwise.
 */
static bool PerformRawTest()
{
    int32_t nMaxLimit_Vpeak;
	int32_t nMinLimit_Vpeak;
	int32_t nMin = 0;
	int32_t nMax = 0;
	int32_t nAvg_Vpeak = 0;

	bool bTestResult = true;
	uint32_t attempts = gNvmCfg.dev.selftest.adcSettleTime * 8; //

	if(Device_GetHardwareVersion() < HW_PASSRAIL_REV12)
	{
		nMaxLimit_Vpeak = gNvmCfg.dev.selftest.adcSettleMax;
		nMinLimit_Vpeak = gNvmCfg.dev.selftest.adcSettleMin;
	}
	else
	{
		nMaxLimit_Vpeak = kRaw_SelfTestLimitsRev12.upper;
		nMinLimit_Vpeak = kRaw_SelfTestLimitsRev12.lower;
	}

	while(bTestResult && (attempts != 0))
	{
		// Now select the vibration filtering.
	    AnalogFiltSelect(ANALOGFILT_RAW);

	    // Start the sampling
		bTestResult = xTaskApp_doSampling(true,
										  MEASID_UNDEFINED,
										  ADC_SAMPLES_FOR_DAC_SETTLE,
										  ADC_SAMPLE_RATE_FOR_DAC_SETTLE);
		// If sampling still not complete, fail the test.
		if((g_bAppSamplingIsComplete == false) || (bTestResult == false))
		{
			LOG_EVENT(0, LOG_NUM_APP, ERRLOGMAJOR, "Raw-ST: Sampling Failed");
			bTestResult = false;
		}

		if(bTestResult != false)
		{
			nMin = g_pSampleBuffer[0];
			nMax = g_pSampleBuffer[0];
			nAvg_Vpeak = 0;

			for(int i = 0; i < ADC_SAMPLES_FOR_DAC_SETTLE; i++)
			{
				// Got a lower value.
				if(g_pSampleBuffer[i] < nMin)
				{
					nMin = g_pSampleBuffer[i];
				}
				// Got a higher value.
				else if(g_pSampleBuffer[i] > nMax)
				{
					nMax = g_pSampleBuffer[i];
				}
				//printf("%3d: %d\n", i, g_pSampleBuffer[i]);
			}

			// Calculate the Vpeak-peak average.
			nAvg_Vpeak = (nMax + abs(nMin)) / 2;
			// Check if the samples are within Limits.
			//LOG_DBG(LOG_LEVEL_APP, "\nRaw:  Max = %ld, Min = %ld , Avg = %ld \n", nMax, nMin, nAvg_Vpeak);
			if((nAvg_Vpeak < nMaxLimit_Vpeak) && (nAvg_Vpeak > nMinLimit_Vpeak))
			{
				// do not use/delete or modify as this line is used by production test
				LOG_DBG(LOG_LEVEL_APP, msgMinMax, "Raw:  ", nMin, nMax, nAvg_Vpeak, nMinLimit_Vpeak, nMaxLimit_Vpeak);
				return true;
			}
		}
		attempts--;
	}

	LOG_EVENT(0, LOG_NUM_APP, ERRLOGWARN, "Raw ST fail, Min=%d, Max=%d, AvgVpk=%d, MinVpkLimit=%d, MaxVpkLimit=%d",
			  nMin, nMax, nAvg_Vpeak, nMinLimit_Vpeak, nMaxLimit_Vpeak);
	return false;
}

/*
 * PerformEnv3Test
 *
 * @desc    Performs the ENV3 bearing filter test using raw adc sampling.
 *
 * @return	true  - If the test passes,
 * 			false - Otherwise.
 */
static bool PerformEnv3Test()
{
	bool bTestResult = DACSelfTestStartSampling(ANALOGFILT_VIB);
    if(bTestResult != false)
	{
    	const int select_limits = (Device_GetHardwareVersion() < HW_PASSRAIL_REV12)?0:1;

		// Compute the Vpeak value
		bTestResult = DACSTSamplesIsInLimits("Env3: ",
											kEnvelope3_SelfTestLimits[select_limits].lower,
											kEnvelope3_SelfTestLimits[select_limits].upper);
    }
	return bTestResult;
}

/*
 * PerformWheelFlatTest
 *
 * @desc    Performs the Wheel Flat test using raw adc sampling.
 *
 * @return	true  - If the test passes,
 * 			false - Otherwise.
 */
static bool PerformWheelFlatTest()
{
	bool bTestResult = DACSelfTestStartSampling(ANALOGFILT_WFLATS);
    if(bTestResult != false)
	{
    	const int select_limits = (Device_GetHardwareVersion() < HW_PASSRAIL_REV12)?0:1;

		// Compute the Vpeak value
		bTestResult = DACSTSamplesIsInLimits("WFlat:",
											kWheelFlat_SelfTestLimits[select_limits].lower,
											kWheelFlat_SelfTestLimits[select_limits].upper);
    }
	return bTestResult;
}

/*
 * PerformDACSelfTest
 *
 * @desc    Performs the DAC self test.
 *          Due to an issue running this test in a cold/humid environment,
 *          PerformRawTest was introduced. It's function is to allow the
 *          DAC waveform generation to settle due to saturation issues
 *          caused by the environmental conditions.
 *          The tests involves 2 sub-tests one for
 * 			ENV3 bearing filter and the other for Wheel Flats. The DAC system
 * 			is enabled before starting these sub-tests and disabled once all
 * 			these sub-tests are completed.
 *
 * @return	true  - If all the sub-tests have passed,
 * 			false - Otherwise.
 */
static bool PerformDACSelfTest()
{
	bool analogWasOff = false;

	logSTStarted("DAC");

	// Enable the DAC system.
	if(!Dac_Enable())
	{
		LOG_EVENT(0, LOG_NUM_APP, ERRLOGMAJOR, "ERROR: DAC System Init Failed");
		AddStatusArrayCode(SELFTEST_ENV3_FAILED);
		return false;
	}

    if(powerAnalogIsOn() != true)
    {
		// Turn ON the analog circuit.
        powerAnalogOn();
        analogWasOff = true;// remember that is was already on when we started, so we won't switch it off at the end.
    }

    // Set the VIB_SELF_TEST_EN to LOW so that the DAC Self test is enabled.
    AnalogSelfTestSelect(false);

	// REV4 hardware requires double the gain
	AnalogGainSelect(Device_GetHardwareVersion() >= HW_PASSRAIL_REV4 || Device_HasPMIC());

	// 1. Perform ENV3 test
	bool bEnv3TestResult = PerformEnv3Test();
	if(bEnv3TestResult != true)
	{
		LOG_DBG(LOG_LEVEL_APP, "SELFTEST ERROR - ENV3\n");
		AddStatusArrayCode(SELFTEST_ENV3_FAILED);
	}

	// 2. Perform Wheel Flat test
	bool bWheelFlatTestResult = PerformWheelFlatTest();
	if(bWheelFlatTestResult != true)
	{
		LOG_DBG(LOG_LEVEL_APP, "SELFTEST ERROR - WFlat\n");
		AddStatusArrayCode(SELFTEST_WHEELFLAT_FAILED);
	}

	// 3. Perform RAW test
	bool bRawTestResult = PerformRawTest();
	if(bRawTestResult != true)
	{
		LOG_DBG(LOG_LEVEL_APP, "SELFTEST ERROR - RAW\n");
		AddStatusArrayCode(SELFTEST_RAW_FAILED);
	}

	// Disable the DAC system.
	Dac_Disable();

	if (analogWasOff == true)
	{
	    // turn it off, because it was off when we started.
	    powerAnalogOff();
	}

	return (bRawTestResult && bEnv3TestResult &&  bWheelFlatTestResult);
}

/*
 * SelfTest_Hdc1080
 *
 * @desc - Self-test for the HDC1080 humidity sensor chip
 *
 * @param - selfTestGood, set to false on a failure
 *
 * @return - void
 */
static void SelfTest_Hdc1080(bool *selfTestGood)
{
	if((Device_GetHardwareVersion() < HW_PASSRAIL_REV2) && Device_HasPMIC())
	{
		// Harvester Rev1: I2C resistors missing! So just Pass..
		*selfTestGood = true;
		return;
	}

	float hdc1050temp = 0x00;

	logSTStarted("Humidity Sensor");
	/* Read humidity sensor if fitted.
	 * We intend to use the humidity sensor during early manufacturing phase to prove the capability of the housing seal process.
	 * Fail if humidity > 30%.
	 * We may then only fit the humidity sensor on a sample basis so the code must deal with the humidity sensor not fitted and report as a no fit.
	 */
	if(true == HDC1050_ReadHumidityTemperature(&commsRecord.params.Humidity, &hdc1050temp))
	{
		LOG_DBG(LOG_LEVEL_APP, "Humidity %f, Temp %f\n", commsRecord.params.Humidity, hdc1050temp);
	}
	else
	{
		LOG_DBG(LOG_LEVEL_APP, "SELFTEST WARNING - reading HDC1050\n");
		*selfTestGood = false;
		AddStatusArrayCode(SELFTEST_HDC1050_ERROR);
	}
}


/*
 * SelfTest_GnssStart
 *
 * @desc - Start the GNSS Selftest
 *
 * @param - selfTestGood, set to false on a failure
 *
 * @param - selfTestFlags set of flags to control self-test functionality
 *
 * @return - whether or not the function started GNSS
 */
static bool SelfTest_GnssStart(bool *selfTestGood, uint8_t selfTestFlags)
{
	bool retval = true;
	bool gnssStarted = false;

	logSTStarted("GNSS");

	if (false == (selfTestFlags & ST_GNSS_ALREADY_ON))
	{
	    retval = gnssCommand(GNSS_POWER_ON,8000);

	    if(false == retval)
	    {
			LOG_DBG( LOG_LEVEL_APP, "SELFTEST ERROR - GNSS comms\n");
			*selfTestGood = false;
			AddStatusArrayCode(SELFTEST_GNSS_PWR_ON);
	    }
	}

	/* If we're still OK, check the version command */
	if (true == retval)
	{
		/* Send the version command and check response */
		retval = gnssCommand(GNSS_VERSION,1000);
		if(false == retval)
		{
			LOG_DBG( LOG_LEVEL_APP, "SELFTEST ERROR - GNSS version\n");
			*selfTestGood = false;
			AddStatusArrayCode(SELFTEST_GNSS_VER);
		}
	}

	/* If we're still OK and fix required, get a fix */
	if(true == retval)
	{
		/* Skip getting a fix if it's reduced testing */
		if(false == (selfTestFlags & ST_REDUCED))
		{
			if (true == retval)
			{
				retval = gnssCommand(GNSS_READ_RMC,0);
			}
			if(false == retval)
			{
				LOG_DBG( LOG_LEVEL_APP, "SELFTEST WARNING - Failed to start GNSS\n");
				*selfTestGood = false;
				AddStatusArrayCode(SELFTEST_GNSS_FAILED_TO_START);
			} else {
				gnssStarted = true;
			}
		}
		else
		{
			LOG_DBG( LOG_LEVEL_APP, "SELFTEST WARNING - GNSS fix not triggered. Running Reduced mode or in Test Box mode\n");
		}
	}

	return gnssStarted;
}

/*
 * SelfTest_GnssComplete
 *
 * @desc - Check GNSS results
 *
 * @param - gnssStared set if the self-test started the GNSS
 *
 * @return - void
 */
static void SelfTest_GnssComplete(bool gnssStarted)
{
	//4.11.20	Measure power consumption over a communication cycle

	//4.11.21	Report cellular signal quality.   Pass if 9 or greater

	// TODO - talk with Mark about the following
	//4.11.22	Report real time clock time.
	//          Report time difference between RTC and new time recovered from GNSS before synchronising.
	//          Difference must be < 10 s.  This could cause a potential self-test fail at installation if the sensor
	//          has been in storage for a long (months) period before installation. For this reason do not link this parameter
	//          as a self-test pass criteria.

	// the GNSS task was kicked of earlier - we need to wait for it to complete
	if(gnssStarted)
	{
		bool gnssResult;
		int gnss_Timeout = GNSS_maxSecondsToAquireValidData();
		LOG_DBG(LOG_LEVEL_APP, "xTaskApp_selfTest(): Wait on GPS fix (max %d secs)\n", gnss_Timeout);
		gnssResult = GNSS_WaitForCompletion((gnss_Timeout * 1000) / portTICK_PERIOD_MS);// TODO: proper timeout value !

		tGnssCollectedData gnssData;// just a warning, this is a large structure

		if(gnssResult)
		{
			gnssResult = gnssRetrieveCollectedData(&gnssData, 1000);// should not take longer than a second
		}

		if(gnssResult)
		{
			updateGnssCommsRecord();

			SetRTCFromGnssData(&gnssData);
		}
		else
		{
			LOG_DBG(LOG_LEVEL_APP, "xTaskApp_selfTest(): GNSS Timeout\n");
			AddStatusArrayCode(SELFTEST_GNSS_TIME_TO_FIX);
			if( (gnssStatus.gpsSat.numsat == 0) && (gnssStatus.glonassSat.numsat == 0) )
			{
				LOG_DBG(LOG_LEVEL_APP, "xTaskApp_selfTest(): GNSS no satellites detected\n");
				AddStatusArrayCode(SELFTEST_GNSS_NO_SAT);
			}
		}
	}
	gnssCommand(GNSS_POWER_OFF, 1000);
}

/*
 * selfTest_ExtFlash
 *
 * @desc - Simple test for the Flash device
 *         checks the SPI interface and chip can be accessed
 *
 * @return - true if ok
 */
static bool selfTest_ExtFlash()
{
	logSTStarted("External Flash");

	uint8_t id = 0;
	bool rc_ok  = IS25_ReadProductIdentity(&id);
	if(rc_ok && (id != IS25_128_MBIT_PRODUCT_ID) && (id != IS25_512_MBIT_PRODUCT_ID))
	{
		rc_ok = false;
		LOG_DBG( LOG_LEVEL_APP, "SELFTEST ERROR - IS25LPX, ID: %d\n", id);
	}

	return rc_ok;
}

/*
 * SelfTest_ResetAlarm
 *
 * @desc  - Reset flag corresponding to code passed in
 *
 * @param - tSelfTestStatusCodes alarmCode: code to be reset
 *
 * @return	none
 */
void SelfTest_ResetAlarm(tSelfTestStatusCodes alarmCode)
{
	for(int i = 0; i < MAXSTATUSSELFTESTLENGTH; i++)
	{
		if(commsRecord.params.Status_Self_Test[i] == alarmCode)
		{
			commsRecord.params.Status_Self_Test[i] = 0;
			break;
		}
	}
}

static void logSTStarted(char* strDevice)
{
	LOG_DBG( LOG_LEVEL_APP, "MK24 - Running %s Test(s)\n", strDevice);
}

static bool PerformRTCAlarmTest()
{
    bool bRetVal = false;
    uint32_t rtcStart = 0;
    if( true == RtcGetDatetimeInSecs(&rtcStart) )
    {
        RtcPerformAlarmTest(RTC_ALARM_SELF_TEST_SECS);

        // Wait for the alarm self test time to elapse + some delta (1 sec)
        vTaskDelay((RTC_ALARM_SELF_TEST_SECS + 1) * 1000);

        // Check if the RTC alarm has triggered
        if(RtcIsAlarmTriggered())
        {
            LOG_DBG( LOG_LEVEL_APP,"RTC Alarm triggered after %d secs\n",
                     RtcGetAlarmTrigAtSecs() - rtcStart);
            bRetVal = true;
        }
        else
        {
            LOG_DBG( LOG_LEVEL_APP, "SELFTEST ERROR - RTCAlarm (rtcStart: %d)\n", rtcStart);
            AddStatusArrayCode(SELFTEST_RTC_ERROR);
        }
        // restore the precautionary wakeup for something going wrong
        // and node not being able to wakeup
        schedule_ScheduleNextWakeup(30, 30 + POFS_MIN);
    }
    return bRetVal;
}


/*
 * selfTest
 *
 * @desc - Perform node self test prior to communications
 *
 * @param - SelfTestFlags which modify function behaviour
 *
 * @return - true if successful, otherwise false
 */
bool selfTest(uint8_t selfTestFlags)
{
	const char star60[] = "************************************************************";
	bool retval = true;
	bool selfTestGood = true;
	bool gnssStarted = false;
	bool ds137ok = true;
	uint32_t nSelfStopTick = 0, nSelfStartTick = 0;
	uint32_t ds137Start = 0;
	uint32_t ds137Stop = 0;
	uint32_t rtcStart = 0;
	uint32_t rtcStop = 0;

	LOG_DBG( LOG_LEVEL_APP, "\n%s\n"
	                        "\n\nStarting self test routine (flags=0x%02X)\n", star60, selfTestFlags);

	// clear out the old values, let's get ready to rumble!
	sacIndex = 0;
	memset(commsRecord.params.Status_Self_Test, 0, sizeof(commsRecord.params.Status_Self_Test));

	/// check the external flash is working
	if(false == selfTest_ExtFlash())
	{
		if(!Device_HasPMIC())
		{
			AddStatusArrayCode(SELFTEST_IS25LP128_ERROR);
		}
		else
		{
			AddStatusArrayCode(SELFTEST_IS25LP512_ERROR);
		}

		selfTestGood = false;
	}

	nSelfStartTick = xTaskGetTickCount();

	/* Read up-time from DS1374 */
	/* Checks DS1374 XTAL OK, I2C-2 and DS1374 */

    if(!Device_HasPMIC())
    {
    	logSTStarted("DS1374 Counter");

		if(false == DS137n_ReadCounter((uint32_t*)&ds137Start))
		{
			LOG_DBG( LOG_LEVEL_APP, "SELFTEST ERROR - DS1374\n");
			selfTestGood = false;
			AddStatusArrayCode(SELFTEST_DS1374_ERROR);
			ds137Start = 0x00;
			ds137ok = false;
		}
		LOG_DBG( LOG_LEVEL_APP, "Seconds since COLDBOOT = %d\n", ds137Start);
    }

	/* Read time from RTC */
	/* Checks RTC XTAL is ok */
    logSTStarted("RTC");
	if( false == RtcGetDatetimeInSecs(&rtcStart) )
	{
		// No explicit handling if error occurrs.
	}
	volatile TickType_t osTickAtStart = xTaskGetTickCount( );

	/* kick off the GNSS task - need to check that this has completed */
    gnssStarted = SelfTest_GnssStart(&selfTestGood, selfTestFlags);

    /* Check the humidity sensor, if fitted */
    SelfTest_Hdc1080(&selfTestGood);

    /* If we have a PMIC, wait for it's results which also updates 'Is_Move_Detect_Com'
     * ST_NO_PMIC flag allows command line running of MK24 tests in isolation
     * */
    if(Device_HasPMIC() && !(selfTestFlags & ST_NO_PMIC))
	{
		int pmic_wait_seconds = 0;
	#define PMIC_MAX_WAIT_SELFTEST_S (30)
		while(!PMIC_GetSelftestUpdatedFlag())
		{
			vTaskDelay(1000/portTICK_PERIOD_MS);
			if(++pmic_wait_seconds > PMIC_MAX_WAIT_SELFTEST_S)
			{
				LOG_EVENT(0, LOG_LEVEL_APP, ERRLOGMAJOR, "(%s) Failed to receive PMIC SelfTest results", __func__);
				break;
			}
		}

		if(pmic_wait_seconds <= PMIC_MAX_WAIT_SELFTEST_S)
		{
			PMIC_processPMICSelfTestResult();

			if(!PMIC_GetSelftestSuccessFlag())
			{
				selfTestGood = false;
			}

			LOG_DBG(LOG_LEVEL_APP, "MK24 - Got PMIC SelfTest results (%s)\n", PMIC_GetSelftestSuccessFlag()?"Passed":"Failed");
		}
	}

	/* ********** Keep next to MEMS self-test for movement detection **********
	 * ************************************************************************
	 * TODO: The following requirement needs to be updated based on the
	 * inputs from Julian.
	 * 4.11.7	Processor internal DAC will produce a 500 Hz sine wave which will be switched through the vibration sensor into the analogue front end
	 * 4.11.8	Processor will sample in envelope 3, wheel flat and raw front end configuration in turn
	 * 4.11.9	Envelope 3, sample rate  5120  Hz, 4096 samples; verify peak to peak amplitude of detected sine wave is between 995 mV and 1005 mV
	 * 4.11.10	Wheel flat , sample rate  1280  Hz, 4096 samples; verify peak to peak amplitude of detected sine wave is between 995 mV and 1005 mV
	 * 4.11.11	Raw, sample rate 25600  Hz, 32768 samples; verify amplitude of detected sine wave is between 995 mV and 1005 mV
	 * 4.11.12	Report raw time waveform to allow calibration in manufacture test.  Waveform will be bulk transferred over test UART
	 */
	//printf("\n----------- DAC self test ---------------\n");
	if(commsRecord.params.Is_Move_Detect_Com == 0x00)
	{
		if(false == PerformDACSelfTest())
		{
			printf("\n *** DAC Self Test Failed ***\n");
			selfTestGood = false;
			// error code added in PerformDACSelfTest
		}
	}
	//printf("\n========= END OF DAC SELF TEST ============\n");

	vTaskDelay(1100);

	LOG_DBG(LOG_LEVEL_APP, "MK24 - Checking RTC delta and Year\n");

	/* Read current time from the RTC and check for a delta */
	if( false == RtcGetDatetimeInSecs(&rtcStop) )
	{
		// No explicit handling if error occurrs.
	}
	volatile TickType_t osTickAtEnd = xTaskGetTickCount( );

	if(rtcStart == rtcStop)
	{
		LOG_DBG( LOG_LEVEL_APP, "SELFTEST ERROR - RTC\n");
		selfTestGood = false;
		AddStatusArrayCode(SELFTEST_RTC_ERROR);
		LOG_DBG( LOG_LEVEL_APP,
				 "RTC selftest error rtc1 rtc2 osticks1 osticks2  %u %u %u %u\n",
				 rtcStart, rtcStop, osTickAtStart, osTickAtEnd);

		LOG_EVENT(eLOG_NORMAL, LOG_NUM_APP, ERRLOGDEBUG,
				  "RTC selftest error rtc1 rtc2 osticks1 osticks2  %u %u %u %u ",
				  rtcStart, rtcStop, osTickAtStart, osTickAtEnd);

	}

	/* Check GNSS fix achieved */
	SelfTest_GnssComplete(gnssStarted);

	// only make second reading if first ok
	if(ds137ok && !Device_HasPMIC())
	{
		LOG_DBG( LOG_LEVEL_APP, "MK24 - Checking counter delta\n");

		/* Read current time from DS1374 and check for a delta */
		retval = DS137n_ReadCounter((uint32_t*)&ds137Stop);	//
		if((false == retval) || (ds137Start == ds137Stop))
		{
			LOG_DBG( LOG_LEVEL_APP, "SELFTEST ERROR - DS1374\n");
			selfTestGood = false;
			AddStatusArrayCode(SELFTEST_DS1374_ERROR);
		}
	}

	LOG_DBG(LOG_LEVEL_APP, "MK24 - Checking PCB temperature alarms\n");

	bool bGetTempFlagsOk = false;
	uint16_t flags;
	if(Vbat_GetFlags(&flags))
	{
		bGetTempFlagsOk =  true;

		if (flags & VBATRF_FLAG_LOW_TEMPERATURE_LIMIT_TRIG)
		{
			LOG_DBG(LOG_LEVEL_APP,"Low Limit alarm logged.\n");
			AddStatusArrayCode(SELFTEST_LOW_TEMP_LIMIT_EXCEEDED);
			selfTestGood = false;
		}
		if (flags & VBATRF_FLAG_HIGH_TEMPERATURE_LIMIT_TRIG)
		{
			LOG_DBG(LOG_LEVEL_APP,"High Limit alarm logged.\n");
			AddStatusArrayCode(SELFTEST_HIGH_TEMP_LIMIT_EXCEEDED);
			selfTestGood = false;
		}
	}
	else
	{
		LOG_EVENT(0, LOG_LEVEL_APP, ERRLOGMAJOR, "Failed to get Temp alarm flags", __func__);
	}

	//4.11.24	Store self-test logging summary.
		//          This will consist of all measurements made together with status codes specified in the "LVS0217 Uploaded Parameters" document
		//          :- click here for Projectlink document

		//4.11.25	The above referenced "LVS0217 Uploaded Parameters" document defines a number of two digit error codes.
		//          If more than one error code is generated the codes should be concatenated.
		//          This field will be defined as a text string to allow reporting of multiple codes in this way.

	// the following is used by test to ascertain result of the node selftest
	LOG_DBG( LOG_LEVEL_APP,
			"\n\nSelf test routine completed\n\n"
			"%s\n\n\n"
			"%s\n"
			"%s\n"
			"%s\n\n",
			star60,
			star60,
			(true == selfTestGood) ? "NODE_SELF_TEST_PASSED" : "NODE_SELF_TEST_FAILED",
			star60);

	commsRecord.params.Is_Self_Test_Successful = selfTestGood;
	measureRecord.timestamp = ConfigSvcData_GetIDEFTime();
	commsRecord.timestamp = measureRecord.timestamp;

	// capture dc level just before modem activity
	retval = EnergyMonitor_ReadDCLevel_v(&gReport.dcLevel.dcLevelVolts[DCLEVEL_BEFORE_MODEM]);

	if((selfTestFlags & ST_LOG_TEMP_ALARM) && bGetTempFlagsOk)
	{
		LOG_DBG(LOG_LEVEL_APP, "MK24 - Checking CM temperature alarms\n");

		if (flags & VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG)
		{
			LOG_DBG(LOG_LEVEL_APP,"Amber alarm logged.\n");
			AddStatusArrayCode(SELFTEST_AMBER_TEMP_THRESHOLD_EXCEEDED);
		}
		if (flags & VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG)
		{
			LOG_DBG(LOG_LEVEL_APP,"Red alarm logged.\n");
			AddStatusArrayCode(SELFTEST_RED_TEMP_THRESHOLD_EXCEEDED);
		}
	}

	LOG_DBG(LOG_LEVEL_APP, "MK24 - Checking POFS flag\n");

	// report status of Is_Power_On_Failsafe_Enabled to the back end via the selftest status array
	if(gNvmCfg.dev.measureConf.Is_Power_On_Failsafe_Enabled == true)
	{
		AddStatusArrayCode(SELFTEST_POWER_ON_FAILSAFE_ENABLED);
	}
	else
	{
		AddStatusArrayCode(SELFTEST_POWER_ON_FAILSAFE_DISABLED);
	}

	// as this is a mag switch wakeup or rtc wakeup - there are no measurements
	// save the comms and measure records to is25 - some data is needed for the next wakeup cycle
	storeCommsData();
	gNvmData.dat.is25.noOfCommsDatasetsToUpload = 1;

	nSelfStopTick = xTaskGetTickCount();
	LOG_DBG(LOG_LEVEL_APP,"\r\n---SELF TEST TOTAL TIME: %d[msec]----\r\n", (nSelfStopTick - nSelfStartTick));

	if(Device_HasPMIC() && !(selfTestFlags & ST_NO_PMIC))
	{
		PMIC_SendMk24STresults(sacIndex, selfTestGood);
	}

	// Check RTC Alarm functionality, if requested
	if(ST_PERFORM_RTC_ALARM_TEST & selfTestFlags)
	{
		logSTStarted("RTC Alarm");
		selfTestGood &= PerformRTCAlarmTest();
	}
	return selfTestGood;
}	// end of bool selfTest()

/*
 * AddStatusArrayCode
 *
 * @desc update the status array used during self test
 *
 * @params code to post to status array
 *
 */
void AddStatusArrayCode(uint8_t code)
{
	if(sacIndex < MAXSTATUSSELFTESTLENGTH)
	{
		__disable_irq();
		commsRecord.params.Status_Self_Test[sacIndex++] = code;
		__enable_irq();
	}
	else
	{
		LOG_EVENT(0, LOG_LEVEL_APP, ERRLOGMAJOR, "Failed to add error code in %s", __func__);
	}
}


/*================================================================================*
 |                                 CLI FUNCTIONS                                  |
 *================================================================================*/

/*
 * cliDacSTStart
 *
 * @desc	Self contained function to perform DAC self test. Setting the
 * 			necessary control and select lines are taken care by this function.
 * 			It outputs the ADC samples to the test UART which can then be used
 * 			to analyse the samples.
 *
 * @param	The usual CLI parameters.
 *
 * @returns
 */
bool cliDacSTStart(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
    if(argc == 1 && argi[0])
    {
    	printf("DAC Self Test started, ADC samples will be available at Test UART\n");
    	bDacSamplesToTestUART = true;
    }
    else
    {
    	printf("DAC Self Test started\n");
    	bDacSamplesToTestUART = false;
    }
	PerformDACSelfTest();
	bDacSamplesToTestUART = false;
	return true;
}

bool cliSelfTest( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	uint16_t flags = 0;

	/* Any flags set */
	if(args >= 1)
		flags = argi[0];

	// ensure that the is25 is ready for use
	if((flags & ST_NO_EXTF_INIT) == 0)
	{
		xTaskApp_makeIs25ReadyForUse();
	}

	// run the xTaskApp self test routine
	bool result = selfTest(flags + ST_NO_PMIC);

	printf("Self-test results: ");
    for(int i = 0; i < MAXSTATUSSELFTESTLENGTH; i++)
    {
    	printf("%02d ", commsRecord.params.Status_Self_Test[i]);
    }

	if(result == true)
	{
		printf("Self test passed\n");
		return true;
	}
	printf("Self test FAILED\n");

	printf("\n");
	return true;
}


#ifdef __cplusplus
}
#endif