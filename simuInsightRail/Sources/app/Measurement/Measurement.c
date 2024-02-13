#ifdef __cplusplus
extern "C" {
#endif

/*
 * Measurement.c
 *
 *  Created on: 8 Sep 2017
 *      Author: TK2319
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "printgdf.h"

#include "resources.h"
#include "log.h"
#include "configLog.h"

#include "configData.h"
#include "configSvcData.h"
#include "NvmConfig.h"
#include "NvmData.h"
#include "rtc.h"

#include "taskGnss.h"
#include "gnssMT3333.h"
#include "xTaskAppGNSS.h"
#include "xTaskApp.h"
#include "ExtFlash.h"
#include "PassRailMeasure.h"
#include "EnergyMonitor.h"
#include "Device.h"
#include "pmic.h"
#include "CUnit/util.h"
#include "Measurement.h"

#define GNSS_SPEED_TIMEOUT_MAX	300

enum {
	eErase = 1100,
	eWait,
	eWrite,
};
extern tExtFlashHandle extFlashHandle;
extern report_t gReport;

struct gnssWaveMeasureSpeed
{
    bool validMeasurement;
    float speedBefore_Hz;
    float speedAfter_Hz;
    uint32_t start_ms;
    uint32_t stop_ms;
} ;

struct gnssWaveMeasureSpeedRange
{
    float lowFreq_Hz;
    float highFreq_Hz;
    float allowedVariationPercent;
};

// --------------------- static function prototypes ------------------------- //
// -------------------------------------------------------------------------- //
static void logDClevel(void);
static bool dataType_to_sampleParams(uint32_t, tMeasId *, uint32_t *, uint32_t *, float *);
static bool readGnssSpeed(tGnssCollectedData *, bool, bool);
static bool waveMeasure(struct gnssWaveMeasureSpeed*, struct gnssWaveMeasureSpeedRange*, uint32_t, uint32_t, bool, bool);
static void updateGnssMeasurementRecord_NoFix(const tGnssCollectedData* const pGnssCollectedData);
static void updateGnssMeasurementRecordFromPMIC_NoFix(const PmicGnssStatus_t* const pGnssData);
static bool updateGnssMeasurementRecord_1(tGnssCollectedData *, struct gnssWaveMeasureSpeedRange *, float);
static void updateGnssMeasurementRecord_2(struct gnssWaveMeasureSpeed *);
static bool checkMovementCondition(void);
static bool handleGnssMeasurement(bool, bool*);
static bool saveMeasureRecordToFlash(void);

// ---------------------------- static functions ---------------------------- //
// -------------------------------------------------------------------------- //
/**
 * @brief    Extract the parameters for the requested data type
 *
 * @param   dataType - waveform type RAW/ENV/WFLAT
 * @param   measId_p - pointer to a tMeasId structure
 * @param   numSamples_p - pointer to the number of samples  (uint32)
 * @param   sampleRate_p - pointer to the sample rate (uint32)
 * @param   conversionFactor_p - pointer to the conversion factor (float)
 *
 * @return - true if successful, otherwise false
 */
static bool dataType_to_sampleParams(uint32_t dataType, tMeasId * measId_p, uint32_t *numSamples_p, uint32_t *sampleRate_p, float * conversionFactor_p )
{
    bool rc_ok = true;

    // try to get all nitty gritty stuff in this function to convert to more generic values
    // this sample rate encoded in the measurementId is not nice.

    switch (dataType) {
    case IS25_RAW_SAMPLED_DATA:
        *measId_p = MEASID_RAWACCEL_25600SPS;
        *sampleRate_p = gNvmCfg.dev.measureConf.Sample_Rate_Raw;
        *numSamples_p = gNvmCfg.dev.measureConf.Samples_Raw;
        *conversionFactor_p = 1.0;
        rc_ok = (gNvmCfg.dev.measureConf.Sample_Rate_Raw == 25600); // unfortunately only one option here
        break;

    case IS25_VIBRATION_DATA:
        *sampleRate_p = gNvmCfg.dev.measureConf.Sample_Rate_Bearing;
        *numSamples_p = gNvmCfg.dev.measureConf.Samples_Bearing;
        *conversionFactor_p = 1.0;
        switch (gNvmCfg.dev.measureConf.Sample_Rate_Bearing) {
        case 1280:
            *measId_p = MEASID_VIB_1280SPS;
            break;

        case 2560:
            *measId_p = MEASID_VIB_2560SPS;
            break;

        case 5120:
            *measId_p = MEASID_VIB_5120SPS;
            break;

        default:
            *measId_p = MEASID_VIB_1280SPS;
            *sampleRate_p = 1280;
            rc_ok = false;
            break;
        }
        break;

    case IS25_WHEEL_FLAT_DATA:
        *sampleRate_p = gNvmCfg.dev.measureConf.Sample_Rate_Wheel_Flat;
        *numSamples_p = gNvmCfg.dev.measureConf.Samples_Wheel_Flat;
        *conversionFactor_p = 1.0;
        switch (gNvmCfg.dev.measureConf.Sample_Rate_Wheel_Flat) {
        case 256:
            *measId_p = MEASID_WFLATS_256SPS;
            break;
        case 512:
            *measId_p = MEASID_WFLATS_512SPS;
            break;
        case 1280:
            *measId_p = MEASID_WFLATS_1280SPS;
            break;
        default:
            *measId_p = MEASID_WFLATS_256SPS;
            *sampleRate_p = 256;
            rc_ok = false;
            break;
        }
        break;

    default:
        rc_ok = false;
        break;
    }

    if (rc_ok == false) {
        LOG_DBG( LOG_LEVEL_APP, "dataType_to_sampleParams(): ERROR sampling configuration error for dataType = %d\n", dataType);
    }
    return rc_ok;
}


/**
 * @brief    Perform the required wave measurement
 *
 * @param   gnssData_p - pointer tGnssCollectedData structure
 * @param   before - flag to indicate if before/after measurement
 * @param   ignoreGnssFailures - ignore GNSS failures
 *
 * @return - true if successful, otherwise false
 */
static bool readGnssSpeed(tGnssCollectedData *gnssData_p, bool before, bool ignoreGnssFailures)
{
	uint32_t timeout = GNSS_maxSecondsToAquireValidData();

	// speed readings are taken a maximum of 6 times
	// we could change to 4 when raw is disabled but why complicate things
	// Maximum on time is 10 minutes so limit total time to 8 minutes
	if(ignoreGnssFailures)
	{
		if(timeout > GNSS_SPEED_TIMEOUT_MAX)
		{
			timeout = GNSS_SPEED_TIMEOUT_MAX;
		}
		timeout = (((8 * 60) - timeout) / 6);
	}

	bool gnss_ok = gnssCommand(GNSS_READ_RMC, timeout * 1000);
	if (gnss_ok)
	{
		gnss_ok = gnssRetrieveCollectedData(gnssData_p, 1000);// should not take longer than a second
	}

	// a case of 'no matter what'
	if ((false == gnss_ok) && ignoreGnssFailures)
	{
		LOG_DBG(LOG_LEVEL_APP, "%s(): GNSS (RMC %s) failure IGNORED\n", __func__, (before ? "before" : "after"));
		// fake data
		gnssData_p->currentFixTimeMs = before ? 0 : 1;// fake numbers !
		gnssData_p->speed_knots = 0;
		gnss_ok = true;
	}
	return gnss_ok;
}


/**
 * @brief    Perform the required wave measurement
 *
 * @param   gnssSpeed_p - pointer gnssWaveMeasureSpeed structure
 * @param   speedrange_p - pointer to gnssWaveMeasureSpeedRange structure
 * @param   dataType - waveform type RAW/ENV/WFLAT
 * @param   measureSetNr - measurement data set to use
 * @param   ignoreGnssFailures - ignore GNSS failures
 * @param   bGNSSisValid - GNSS is valid
 *
 * @return - true if successful, otherwise false
 */
static bool waveMeasure(struct gnssWaveMeasureSpeed* gnssSpeed_p, struct gnssWaveMeasureSpeedRange* speedrange_p, uint32_t dataType,
						uint32_t measureSetNr, bool ignoreGnssFailures, bool bGNSSisValid)
{
    bool rc_ok = true;
    bool extflash_ok = false;
    tGnssCollectedData gnssData;// just a warning, this is a large structure
    uint32_t numSamples;
    uint32_t sampleRate;
    tMeasId  measId;
    float conversionfactor;
    int errCode;
    const char sWaveforms[][6] = {
    		"raw",
			"env3",
			"wflat"
    };

    /*
     *      record speed_before_wave
     *      switch off gnss messages
     *      perform wave measurement
     *      switch on gnss mesages
     *      record speed after wave
     *      store wave
     */

    /*
     * TODO tidy up clumsy handling of return codes, some spec clarification may help!
     * TODO re-factor to move speed measurement to caller
     */

    gnssSpeed_p->validMeasurement = false;
    gnssSpeed_p->speedAfter_Hz = 0.0;
    gnssSpeed_p->speedBefore_Hz = 0.0;
    gnssSpeed_p->start_ms = 0;
	gnssSpeed_p->stop_ms = 0;

    // read speed before measure (if GNSS valid)
    if(bGNSSisValid)
	{
    	rc_ok = readGnssSpeed(&gnssData, true, ignoreGnssFailures);
    	if(rc_ok)
		{
    		gnssSpeed_p->start_ms = gnssData.currentFixTimeMs;
    		if(gnssIsRotnSpeedChangeTestEn())
    		{
    			// For testing purposes, the speed can be faked, these functions are glorified read only globals.
    			gnssData.speed_knots = gnssGetPreMeasurementSpeed_Knots();
    		}
    		gnssSpeed_p->speedBefore_Hz = xTaskApp_calculateWheelHz(gnssData.speed_knots, gNvmCfg.dev.assetConf.Wheel_Diameter);
		}
	}

    // it may be nice to disable incoming messages from the GNSS during measurement
	if(false == MT3333_reporting(false, 2000))
	{
		LOG_DBG(LOG_LEVEL_APP, "%s(): ERROR disabling GNSS messages\n", __func__);
	}

    LOG_DBG(LOG_LEVEL_APP, "%s(%s) capture waveform\n" , __func__, sWaveforms[dataType]);
    if (true == dataType_to_sampleParams(dataType, &measId, &numSamples, &sampleRate, &conversionfactor))
    {
        float fStartEnergy;
        bool energyReadOk = EnergyMonitor_GetEnergyConsumed_J(&fStartEnergy, NULL);
    	uint32_t startMeasureTicks = xTaskGetTickCount();

    	rc_ok = xTaskApp_doSampling(false, measId, numSamples, sampleRate);

    	uint32_t nDuration_msecs = DURATION_MSECS(startMeasureTicks);

    	if(energyReadOk)
    	{
			float fEndEnergy;
			if(EnergyMonitor_GetEnergyConsumed_J(&fEndEnergy, NULL))
			{
				LOG_DBG(LOG_LEVEL_APP,"WaveEnergyUsed:%.2f, TotalEnergy:%.2f, Duration:%d msecs\n",
					fEndEnergy - fStartEnergy, fEndEnergy, nDuration_msecs);
			}
    	}
    }
    else
    {
    	rc_ok = false;
    }

    // it may be nice to re enable incomming messages from the GNSS during measurement
	if(false == MT3333_reporting(true, 2000))
	{
		LOG_DBG( LOG_LEVEL_APP, "%s(): ERROR enabling GNSS messages\n", __func__);
	}

    // lets kick off the storing of the waveform while the gnss is busy, so it runs in parallel with the gnss speed retrieval.
    if (rc_ok)
    {
    	extflash_ok = true;
    	errCode = extFlash_write(&extFlashHandle, (uint8_t *) g_pSampleBuffer , numSamples * sizeof(int32_t) , dataType, measureSetNr, 0);
    	if(errCode < 0)
    	{
    		LOG_EVENT(eWrite, LOG_NUM_APP, ERRLOGFATAL, "extFlash write failed; error %s", extFlash_ErrorString(errCode));
    		extflash_ok = rc_ok = false;
    	}
    }

    // read speed after measure if GNSS is good
    if(bGNSSisValid && rc_ok)
    {
    	rc_ok = readGnssSpeed(&gnssData, false, ignoreGnssFailures);
		if(rc_ok)
		{
			gnssSpeed_p->stop_ms =  gnssData.currentFixTimeMs;
		}
    }

    // now wait for the store dataset flash we kicked off earlier is done, also wait if gnss failed after the kickoff !
    if(extflash_ok)
	{
    	errCode = extFlash_WaitReady(&extFlashHandle, EXTFLASH_MAXWAIT_MS);
		if(errCode < 0)
		{
			// if this fails, should we still do speeds?
			LOG_EVENT(eWait, LOG_NUM_APP, ERRLOGFATAL, "extFlash write timeout; error %s", extFlash_ErrorString(errCode));
			rc_ok = false;
		}
	}

    if(bGNSSisValid && rc_ok)
    {
        // division by zero leads to infinite value, lets take something big
        float speedVariationPercent = 999999;

        if(gnssIsRotnSpeedChangeTestEn())
		{
        	// For testing purposes, the speed can be faked, these functions are glorified read only globals.
        	gnssData.speed_knots = gnssGetPostMeasurementSpeed_Knots();
		}
        gnssSpeed_p->speedAfter_Hz = xTaskApp_calculateWheelHz(gnssData.speed_knots, gNvmCfg.dev.assetConf.Wheel_Diameter);

        float meanSpeed_Hz = (gnssSpeed_p->speedAfter_Hz + gnssSpeed_p->speedBefore_Hz)/2;
        if(meanSpeed_Hz != 0.0f)
        {
            speedVariationPercent = 100 * fabs(gnssSpeed_p->speedAfter_Hz - gnssSpeed_p->speedBefore_Hz)/ meanSpeed_Hz;
        }

        LOG_DBG(LOG_LEVEL_APP,"%s(%s) before= %f Hz, after= %f Hz,  variation = %f %%\n\n", __func__,
                sWaveforms[dataType],
                gnssSpeed_p->speedBefore_Hz,
                gnssSpeed_p->speedAfter_Hz,
                speedVariationPercent);

        if (    (speedVariationPercent  <= speedrange_p->allowedVariationPercent ) &&
                (meanSpeed_Hz           >= speedrange_p->lowFreq_Hz              ) &&
                (meanSpeed_Hz           <= speedrange_p->highFreq_Hz             )
            )
        {
            gnssSpeed_p->validMeasurement = true;
        }
        else
        {
			LOG_DBG(LOG_LEVEL_APP,"%s() wave measurement outside specified speed limits!\n", __func__);

			/*
			* gnssSpeed_p->validMeasurement will still be false - however caller does NOT check
			* this, so simpler (?) to force rc_ok to false
			*/
			if(ignoreGnssFailures)
			{
				// movement gating set to zero
				LOG_DBG(LOG_LEVEL_APP,"%s() speed failure ignored!\n", __func__);
				gnssSpeed_p->validMeasurement = true;
				rc_ok = true;
			}
			else
			{
				// what caused the invalid measurement
				measureRecord.params.Energy_Remaining = eSpeedBits;
				if (meanSpeed_Hz < speedrange_p->lowFreq_Hz)
				{
					measureRecord.params.Energy_Remaining |= eSpeedBSlow;
				}
				if (meanSpeed_Hz > speedrange_p->highFreq_Hz)
				{
					measureRecord.params.Energy_Remaining |= eSpeedBFast;
				}
				if (speedVariationPercent  > speedrange_p->allowedVariationPercent )
				{
					measureRecord.params.Energy_Remaining |= eSpeedBDiff;
				}
				measureRecord.params.GNSS_Speed_To_Rotation_1 = gnssSpeed_p->speedBefore_Hz;
				measureRecord.params.GNSS_Speed_To_Rotation_2 = gnssSpeed_p->speedAfter_Hz;
				measureRecord.params.GNSS_Time_Diff = (gnssSpeed_p->stop_ms - gnssSpeed_p->start_ms)/1000.0;// milliseconds to seconds
				measureRecord.params.Rotation_Diff = (gnssSpeed_p->speedAfter_Hz - gnssSpeed_p->speedBefore_Hz);
				measureRecord.params.Is_Good_Speed_Diff = false;
				gnssSpeed_p->validMeasurement = false;
				rc_ok = false;
			}
        }
    }

	if(rc_ok)
	{
		if(bGNSSisValid)
		{
			updateGnssMeasurementRecord_2(gnssSpeed_p);
		}
	}
	else
	{
		  LOG_DBG(LOG_LEVEL_APP, "%s(): %s wave measure failed\n", __func__, sWaveforms[dataType]);
	}

    return rc_ok;
}

/**
 * @brief Update measurement record with gnss data from PMIC status, when no fix is obtained
 *
 * @param[in] pGnssData   Pointer to gnss data
 */
static void updateGnssMeasurementRecordFromPMIC_NoFix(const PmicGnssStatus_t* const pGnssData)
{
	if (Device_PMICcontrolGPS())
	{
		measureRecord.params.Energy_Remaining = eNoFix;
		measureRecord.params.GNSS_Time_To_Fix_1 = pGnssData->time_to_first_accurate_fix_ms/1000; // report in seconds
		memcpy(measureRecord.params.GNSS_NS_1, pGnssData->GNSS_NS_1, sizeof(pGnssData->GNSS_NS_1)/sizeof(*pGnssData->GNSS_NS_1));
		memcpy(measureRecord.params.GNSS_EW_1, pGnssData->GNSS_EW_1, sizeof(pGnssData->GNSS_EW_1)/sizeof(*pGnssData->GNSS_EW_1));
		measureRecord.params.GNSS_Long_1 = pGnssData->GNSS_Long_1;
		measureRecord.params.GNSS_Lat_1 = pGnssData->GNSS_Lat_1;
		measureRecord.params.GNSS_Course_1 = pGnssData->GNSS_Course_1;

		uint8_t idx  = 0;
		for (int i=0; i< pGnssData->gpsSat.numsat && idx<MAX_GNSS_SATELITES; i++)
		{
			measureRecord.params.GNSS_Sat_Id[i] =  pGnssData->gpsSat.id[i];
			measureRecord.params.GNSS_Sat_Snr[i] =  pGnssData->gpsSat.snr[i];
			idx++;
		}

		for (int i=0; i< pGnssData->glonassSat.numsat && idx<MAX_GNSS_SATELITES; i++)
		{
			measureRecord.params.GNSS_Sat_Id[ pGnssData->gpsSat.numsat+i] = pGnssData->glonassSat.id[i];
			measureRecord.params.GNSS_Sat_Snr[ pGnssData->gpsSat.numsat+i] = pGnssData->glonassSat.snr[i];
			idx++;
		}
	}
}

/**
 * @brief Update measurement record with collected gnss data, when no fix is obtained
 *
 * @param[in] pGnssData   Pointer to gnss data
 */
static void updateGnssMeasurementRecord_NoFix(const tGnssCollectedData* const pGnssCollectedData)
{
	measureRecord.params.Energy_Remaining = eNoFix;
	measureRecord.params.GNSS_Time_To_Fix_1 = GNSS_maxSecondsToAquireValidData();
	measureRecord.params.GNSS_NS_1[0] = pGnssCollectedData->NS;
	measureRecord.params.GNSS_NS_1[1] = '\0';
	measureRecord.params.GNSS_EW_1[0] = pGnssCollectedData->EW;
	measureRecord.params.GNSS_EW_1[1] = '\0';
	measureRecord.params.GNSS_Long_1 =  pGnssCollectedData->longitude;
	measureRecord.params.GNSS_Lat_1 = pGnssCollectedData->latitude;
	measureRecord.params.GNSS_Course_1 = pGnssCollectedData->course_degrees;

	uint16_t satIdx=0;
	// copy over the GPS satelite info
	for (int i=0; i< pGnssCollectedData->gpsSat.numsat && satIdx<sizeof(measureRecord.params.GNSS_Sat_Id)/sizeof(*measureRecord.params.GNSS_Sat_Id); i++) {
		measureRecord.params.GNSS_Sat_Id[satIdx] = pGnssCollectedData->gpsSat.id[i];
		measureRecord.params.GNSS_Sat_Snr[satIdx] = pGnssCollectedData->gpsSat.snr[i];
		satIdx++;
	}
	// copy over the GLONASS satelite info
	for (int i=0; i< pGnssCollectedData->glonassSat.numsat && satIdx<sizeof(measureRecord.params.GNSS_Sat_Id)/sizeof(*measureRecord.params.GNSS_Sat_Id); i++) {
		measureRecord.params.GNSS_Sat_Id[satIdx] = pGnssCollectedData->glonassSat.id[i];
		measureRecord.params.GNSS_Sat_Snr[satIdx] = pGnssCollectedData->glonassSat.snr[i];
		satIdx++;
	}
	//LOG_DBG( LOG_LEVEL_APP, "GNSS: noOfSatellites %d\n", pGnssCollectedData->gpsSat.numsat + pGnssCollectedData->glonassSat.numsat);
}

/**
 * @brief  records gnss data to measurement record
 *
 * @param data_p - pointer to the gnss data structure
 * @param speedRange_p pointer to a structure holding the speed profile describing when the measurement is considered valid
 * @param Wheel_Diameter used to convert speed into rotations/sec
 *
 * @return  false when outside speed range
 */
static bool updateGnssMeasurementRecord_1(tGnssCollectedData *data_p, struct gnssWaveMeasureSpeedRange * speedRange_p, float Wheel_Diameter)
{
    // TODO - move into gnss routine and stop using gReport
    uint16_t satIdx=0;
    float speedHz;

	if (Device_PMICcontrolGPS())
	{
		const PMIC_Status_t* const pPMIC_Status = PMIC_GetPMICStatus();
		if (NULL != pPMIC_Status)
		{
			measureRecord.params.GNSS_Time_To_Fix_1 = (pPMIC_Status->gnssStatus.time_to_first_accurate_fix_ms + data_p->firstAccurateFixTimeMs - data_p->powerupTimeMs)/1000; // report in seconds
		}
	}
	else
	{
		measureRecord.params.GNSS_Time_To_Fix_1 = (data_p->firstAccurateFixTimeMs - data_p->powerupTimeMs)/1000;// report in seconds
	}

	measureRecord.params.GNSS_NS_1[0] = data_p->NS;
	measureRecord.params.GNSS_NS_1[1] = '\0';
	measureRecord.params.GNSS_Long_1 =  data_p->longitude;
	measureRecord.params.GNSS_Lat_1 = data_p->latitude;
	measureRecord.params.GNSS_EW_1[0] = data_p->EW;
	measureRecord.params.GNSS_EW_1[1] = '\0';
	measureRecord.params.GNSS_Course_1 = data_p->course_degrees;

	// copy over the GPS satelite info
	for (int i=0; i< data_p->gpsSat.numsat && satIdx<sizeof(measureRecord.params.GNSS_Sat_Id)/sizeof(*measureRecord.params.GNSS_Sat_Id); i++) {
		measureRecord.params.GNSS_Sat_Id[satIdx] = data_p->gpsSat.id[i];
		measureRecord.params.GNSS_Sat_Snr[satIdx] = data_p->gpsSat.snr[i];
		satIdx++;
	}
	// copy over the GLONASS satelite info
	for (int i=0; i< data_p->glonassSat.numsat && satIdx<sizeof(measureRecord.params.GNSS_Sat_Id)/sizeof(*measureRecord.params.GNSS_Sat_Id); i++) {
		measureRecord.params.GNSS_Sat_Id[satIdx] = data_p->glonassSat.id[i];
		measureRecord.params.GNSS_Sat_Snr[satIdx] = data_p->glonassSat.snr[i];
		satIdx++;
	}
	LOG_DBG( LOG_LEVEL_APP, "GNSS: noOfSatellites %d\n", data_p->gpsSat.numsat + data_p->glonassSat.numsat);

    // I do not like next part, need a cleaner solution...

    // Now calculate the Speed to Rotation in rpm using the Ground Speed and wheel dia.
    speedHz = xTaskApp_calculateWheelHz(data_p->speed_knots, Wheel_Diameter);
    LOG_DBG( LOG_LEVEL_APP, "\nGroundSpeed: %f[Knots] -> Rotation: %f[Hz]\n\n",
            data_p->speed_knots, speedHz);

	// gated slow/fast error
	if(speedHz < speedRange_p->lowFreq_Hz)
	{
		// speed is too slow
		measureRecord.params.Energy_Remaining = eSpeedSlow;
		measureRecord.params.GNSS_Speed_To_Rotation_1 = speedHz;
		measureRecord.params.Is_Speed_Within_Env = false;
	}
	else if (speedHz > speedRange_p->highFreq_Hz)
	{
		// speed is too fast
		measureRecord.params.Energy_Remaining = eSpeedFast;
		measureRecord.params.GNSS_Speed_To_Rotation_1 = speedHz;
		measureRecord.params.Is_Speed_Within_Env = false;
	}
    else
    {
    	// speed is within parameters
        measureRecord.params.Is_Speed_Within_Env = true;
    }

    return measureRecord.params.Is_Speed_Within_Env;
}


/**
 * @brief - records the measurement according algorithm described in the comment
 *         because the function compares against the current recorded values,
 *         these must be initialized properly before the first call to this function
 *
 * @param  gnssSpeed_p   pointer to a structure holding the measured speeds before
 *                       and after the wave measurement
 * @return  nothing
 */
static void updateGnssMeasurementRecord_2(struct gnssWaveMeasureSpeed * gnssSpeed_p)
{
    float speedDiff = fabs(gnssSpeed_p->speedAfter_Hz - gnssSpeed_p->speedBefore_Hz);

    // we have to reduce the two or three GNSS speed before/after measurements to one,
    // because we have only one set of parameters at the moment and doing a speed measurement around every wave is something which was required later
    // following parameters to report
    //  measureRecord.params.Is_Good_Speed_Diff : if one fails, all fails
    //  measureRecord.params.GNSS_Speed_To_Rotation_1 : speed before measurement
    //  measureRecord.params.GNSS_Speed_To_Rotation_2 : speed after measurement
    //  measureRecord.params.GNSS_Time_Diff : time between last and first measurement
    //  measureRecord.params.Rotation_Diff : speed difference between last and first measurement
    // algorithm :  the measurement where the largest speed variation is observed, is reported !

    measureRecord.params.Is_Good_Speed_Diff &= gnssSpeed_p->validMeasurement;// one false measurement will set final result to false
    /*
     * Needs to be >= in the unlikely event that speeds are identical
     */
    if (speedDiff >= measureRecord.params.Rotation_Diff)
    {
        measureRecord.params.GNSS_Speed_To_Rotation_1 = gnssSpeed_p->speedBefore_Hz;
        measureRecord.params.GNSS_Speed_To_Rotation_2 = gnssSpeed_p->speedAfter_Hz;
        measureRecord.params.GNSS_Time_Diff = (gnssSpeed_p->stop_ms - gnssSpeed_p->start_ms)/1000.0;// milliseconds to seconds
        measureRecord.params.Rotation_Diff = (gnssSpeed_p->speedAfter_Hz - gnssSpeed_p->speedBefore_Hz);
    }
}


/**
 * @brief Log debug Voltage at start of measurement cycle
 */
static void logDClevel(void)
{
	// read the dc level at start of measurements
	bool retval = EnergyMonitor_ReadDCLevel_v(&gReport.dcLevel.dcLevelVolts[DCLEVEL_AT_START]);
	if(retval == false)
	{
		LOG_DBG( LOG_LEVEL_APP, "MEASUREMENT ERROR - V_0 failed v = %f\n", gReport.dcLevel.dcLevelVolts[DCLEVEL_AT_START]);
		// TODO - these sort of instances need to be recorded
	}
	else
	{
		LOG_DBG( LOG_LEVEL_APP, "Startup dc level V_0 = %f\n", gReport.dcLevel.dcLevelVolts[DCLEVEL_AT_START]);
	}
}

/**
 * Handles the gnss fixes and records waveforms
 *
 * @param[in]  keepGnssOn   if true, the gnss will not be switched off before leaving the function
 * @param[out] speedValid   if the speed is valid or not
 */
static bool handleGnssMeasurement(bool keepGnssOn, bool* speedValid)
{
// GDF: start with refactor, get rid of weird switch/case  and do something with return status
    /*
     *  first kick off wipe of flash where we will store the measurement(s)
     *
     *  Then start up the gnss
     *  we will have to wait for a fix, to get an idea of the speed
     *  do we have to wait until the speed is accurate enough (HDOP value) ???
     *  When we have a fix,
     *      update position satellites etc,
     *       calculate the speed
     *  if we have speed gating enabled, and speed is not in range, abort measurements
     *  if raw waveform enabled enabled
     *      record speed_before_raw
     *      switch off gnss messages
     *      perform raw measurement
     *      switch on gnss mesages
     *      record speed after raw
     *      store raw
     *
     *  record speed before env3
     *  switch off gnss messages
     *  perform env3 measurement
     *  switch on gnss messages
     *  record speed after env3
     *  store env3

     *  record speed before wheelflat
     *  switch off gnss messages
     *  perform wheelflat measurement
     *  switch on gnss messages
     *  record speed after wheelflat
     *  store env3
     */

	bool rc_ok = true;
    bool power_on_ok = false;
    bool ignoreGnssFailures = !gNvmCfg.dev.measureConf.Is_Moving_Gating_Enabled;// this optional ignoring failures makes the code way too messy...
    struct gnssWaveMeasureSpeedRange speedRange = {
            .lowFreq_Hz =  gNvmCfg.dev.measureConf.Min_Hz,
            .highFreq_Hz = gNvmCfg.dev.measureConf.Max_Hz,
            .allowedVariationPercent = gNvmCfg.dev.measureConf.Allowed_Rotation_Change
    };

    // assume it's an invalid speed
    *speedValid = false;

    // kick of the erase of the flash area we will be using for storage later on, erase flash is time expensive, and this can be done in parallel with the GNSS fix
    int errCode = extFlash_erase(&extFlashHandle, gNvmData.dat.is25.is25CurrentDatasetNo, 0 /* EXTFLASH_MAXWAIT */);// no wait, we will wait just before doing the measurements
	if (errCode < 0)
	{
		LOG_EVENT(eErase, LOG_NUM_APP, ERRLOGFATAL, "extFlash erase dataset %d failed; error %s", gNvmData.dat.is25.is25CurrentDatasetNo, extFlash_ErrorString(errCode));
		rc_ok = false;
	}
	else
	{
       LOG_DBG(LOG_LEVEL_APP, "%s(): flash erase of dataset %d started,\nStart GNSS\n", __func__, gNvmData.dat.is25.is25CurrentDatasetNo);

	   uint32_t gnssStartTick = xTaskGetTickCount();
	   float fGnssStartEnergy;
	   bool gotStartEnergyOk = EnergyMonitor_GetEnergyConsumed_J(&fGnssStartEnergy, NULL);

	   // ------------------------- GNSS starting --------------------------- //
       bool gnss_ok = gnssCommand(GNSS_POWER_ON, 8000);
       if(gnss_ok)
	   {
    	   // powered on OK so let's get a fix
    	   power_on_ok = true;
    	   gnss_ok = gnssCommand(GNSS_READ_RMC_HDOP, 0);// nowait for result, we will do that later. used to be GNSS_READ_RMC
	   }

       // now wait for the erase dataset flash we kicked off earlier is done, this will very likely be faster than the GNSS fix
       // we must do this even if GNSS failed
       errCode = extFlash_WaitReady(&extFlashHandle, EXTFLASH_MAXWAIT_MS);
       if(errCode < 0)
       {
           LOG_EVENT(eWait, LOG_NUM_APP, ERRLOGFATAL, "extFlash erase timeout; error %s", extFlash_ErrorString(errCode));
           rc_ok = false;
       }

		tGnssCollectedData gnssData = {};// just a warning, this is a large structure
		bool gnssDataReady = 0;

		#define MIN_GNSS_CMD_TIMEOUT (2000) // GNSS msgs come every second, add some room so we don't miss them.
		#define MIN_GNSS_CMD_GSV_TIMEOUT (5000) // GSV messages are sent from GPS chip every 5 seconds
		// check whether the issued command either completes or times out
		if(gnss_ok && rc_ok)
		{
			uint32_t gnss_Timeout = GNSS_maxSecondsToAquireValidData() * 1000;
			
			if (Device_PMICcontrolGPS()){
				const PMIC_Status_t* const pPMIC_Status = PMIC_GetPMICStatus();
				if (NULL != pPMIC_Status && pPMIC_Status->gnssStatus.is_gnss_fix_good)
				{
					if (GNSS_maxSecondsToAquireValidData() * 1000 > pPMIC_Status->gnssStatus.time_to_first_accurate_fix_ms + MIN_GNSS_CMD_TIMEOUT)
					{
						gnss_Timeout = GNSS_maxSecondsToAquireValidData() * 1000 - pPMIC_Status->gnssStatus.time_to_first_accurate_fix_ms;
					}
					else
					{
						gnss_Timeout = MIN_GNSS_CMD_TIMEOUT;
					}
				}
			}
			
			LOG_DBG(LOG_LEVEL_APP, "GNSS: current timeout is %d ms\n", gnss_Timeout);
			
			///////////// Blocking, waiting for First Accurate fix /////////////
			gnss_ok = GNSS_WaitForCompletion(gnss_Timeout);
			
			if(false == gnss_ok)
			{
				measureRecord.params.GNSS_Time_To_Fix_1 = GNSS_maxSecondsToAquireValidData();
				LOG_DBG(LOG_LEVEL_APP, "GNSS: (RMC) HDOP= %5.2f, Timeout %s\n", gnssStatus.collectedData.HDOP, ignoreGnssFailures ? "IGNORED" : "");
			}
			
			// get GNSS collected data to decide what to do next
			gnssDataReady = gnssRetrieveCollectedData(&gnssData, 1000);//should not take longer than a second

			// Collect list of satellites, regardless of HDOP value.
			// GSV messages might come in between, check if we have luckily obtain list of satellites. If not, need to wait...
			if (gnssDataReady && gnssData.gpsSat.numsat == 0 && gnssData.glonassSat.numsat == 0)
			{	// Retrieve the satellite id/SNR. Since we have had a fix, this should take from 0-5 seconds.
				// TODO: Should we spend up to 5 seconds to retrieve this metrics???
				LOG_DBG(LOG_LEVEL_APP, "GNSS: (GSV) Getting list of satellites, timeout %d ms\n", MIN_GNSS_CMD_GSV_TIMEOUT);
				if(false == gnssCommand(GNSS_READ_GSV, MIN_GNSS_CMD_GSV_TIMEOUT))
				{
					// Only log, this failure affects metrics but should not affect measurement cycle execution.
					LOG_DBG(LOG_LEVEL_APP, "GNSS: (GSV) Timeout after %d ms\n", MIN_GNSS_CMD_GSV_TIMEOUT);
				}
			}
		}

		// Obtain latest GNSS collected data
		gnssDataReady = gnssRetrieveCollectedData(&gnssData, 1000);// should not take longer than a second

		if((false == gnss_ok) && !ignoreGnssFailures)
		{
			if (gnssDataReady)
			{
				updateGnssMeasurementRecord_NoFix(&gnssData);
			}
			rc_ok = false; // we don't want to do any more GNSS..
		}

       if(rc_ok || ignoreGnssFailures)
       {
           // time to process all the GNSS data.
           bool speed_within_range = false;

           // GNSS was OK, proceed
           if(gnss_ok)
           {
               if(gnssIsRotnSpeedChangeTestEn())
			   {
            	   gnssData.speed_knots = gnssGetPreMeasurementSpeed_Knots(); // For testing purposes, the speed can be faked, these functions are glorified read only globals.
			   }

               if(gnssDataReady)
               {
                    // only set the time if the RMC sentence is valid
                    // NOTE not logging an invalid sentence as they can occur
                    if(gnssData.valid)
                    {
                        SetRTCFromGnssData(&gnssData);
                        // Update the time stamp in case it changes quite dramatically
                        measureRecord.timestamp = ConfigSvcData_GetIDEFTime();
                    }

                   speed_within_range = updateGnssMeasurementRecord_1(
                          &gnssData,
                          &speedRange,
                          gNvmCfg.dev.assetConf.Wheel_Diameter);

                   if(speed_within_range || ignoreGnssFailures)
                   {
                	   *speedValid = true;
                   }
               }
           }
           else
           {
               // GNSS failed, so should we ignore that
               if(ignoreGnssFailures)
               {
                   speed_within_range = true;// fake result
                   *speedValid = true;
               }
           }

           // done all GNSS stuff, check now if we can go measure
           if (gNvmCfg.dev.measureConf.Is_Moving_Gating_Enabled && (false == speed_within_range))
           {
               LOG_DBG(LOG_LEVEL_APP, "%s(): speed gating active, and speed outside range\n", __func__);
           }
           else
           {
              // speed is either IN range (or faked) or gating is disabled
        	  // if rc_ok is false when we get here then we DON'T have GNSS.. and shouldn't attempt any speed readings
        	  gnss_ok = rc_ok;
              struct gnssWaveMeasureSpeed gnssSpeed;

              // reset this data, it will contain the data of one wave measurement, which is selected on the 'fly'
              measureRecord.params.Rotation_Diff = 0;
              measureRecord.params.Is_Good_Speed_Diff = true;// we have multiple measurements, if one is false, this becomes false
              measureRecord.params.GNSS_Speed_To_Rotation_1 = 0;
              measureRecord.params.GNSS_Speed_To_Rotation_2 = 0;
              measureRecord.params.GNSS_Time_Diff = 0;
              measureRecord.params.Rotation_Diff = 0;

              // waveMeasure passed extra parameter (gnss_ok) to NOT do speed stuff
              // TODO speed measure really should be factored out of waveMeasure
			  rc_ok = waveMeasure(&gnssSpeed, &speedRange, IS25_VIBRATION_DATA, gNvmData.dat.is25.is25CurrentDatasetNo, ignoreGnssFailures, gnss_ok);
			  if(rc_ok)
			  {
				  rc_ok = waveMeasure(&gnssSpeed, &speedRange, IS25_WHEEL_FLAT_DATA, gNvmData.dat.is25.is25CurrentDatasetNo, ignoreGnssFailures, gnss_ok);
			  }
			  else
			  {
				  LOG_DBG(LOG_LEVEL_APP, "%s(): 2nd wave measure abandoned\n", __func__);
			  }

              // maybe we could do the raw as last to have the measurements as close together as possible (the flash write takes much longer, because of the amount of data)
              // also advised by Julian, because of analog settling time
			  if(rc_ok)
			  {
				  if(gNvmCfg.dev.measureConf.Is_Raw_Acceleration_Enabled)
				  {
					  rc_ok = waveMeasure(&gnssSpeed, &speedRange, IS25_RAW_SAMPLED_DATA, gNvmData.dat.is25.is25CurrentDatasetNo, ignoreGnssFailures, gnss_ok);
				  }
				  else
				  {
					  LOG_DBG(LOG_LEVEL_APP, "%s(): raw acceleration measurement disabled, skipped\n", __func__);
				  }
			  }
			  else
			  {
				  LOG_DBG(LOG_LEVEL_APP, "%s(): raw acceleration measurement abandoned\n", __func__);
			  }
           }
       }	// if(rc_ok || ignoreGnssFailures)

       // shutdown gnss module, regardless of possible failures.
	   if (!keepGnssOn && power_on_ok)
	   {
		  // power off the gnss
		   gnssCommand(GNSS_POWER_OFF, 1000);
		   // Update the energy used in this cycle (support for legacy rev 3 HW.)
		   uint32_t gnssOnDuration_msecs = DURATION_MSECS(gnssStartTick);

		   if(gotStartEnergyOk)
		   {
			   float fGnssEndEnergy;
			   if(EnergyMonitor_GetEnergyConsumed_J(&fGnssEndEnergy, NULL))
			   {
				   LOG_DBG( LOG_LEVEL_APP,"\nGnss Energy used:%.2f, Total Used:%.2f, Duration:%d [msec]\n",
					   fGnssEndEnergy - fGnssStartEnergy, fGnssEndEnergy, gnssOnDuration_msecs);
			   }
		   }
	   }
   } // can't store data

   return rc_ok;
}

/**
 * @brief Check if sensor has detected movement, either via MK24 or PMIC
 *
 * @retval - true if movement has been detected, false otherwise
 */
static bool checkMovementCondition(void)
{
	if(!Device_HasPMIC())
	{
		LOG_DBG( LOG_LEVEL_APP, "Checking mems sensor for movement\n");
		xTaskApp_DetectMovement(&measureRecord.params.Is_Move_Detect_Meas);// TODO : handle measurement failure
	}
	else
	{   //PMIC available: Movement detection reported to MK24 from PMIC
		LOG_DBG( LOG_LEVEL_APP, "Checking PMIC status for movement\n");
		
		const PMIC_Status_t* const pPMIC_Status = PMIC_GetPMICStatus();
		measureRecord.params.Is_Move_Detect_Meas = (NULL != pPMIC_Status) ? pPMIC_Status->motionDetected : false;
		
		LOG_DBG( LOG_LEVEL_APP, "%s() : Movement %s detected\n", __func__, measureRecord.params.Is_Move_Detect_Meas ? "IS" : "NOT");
	}

	return measureRecord.params.Is_Move_Detect_Meas;
}


/**
 * @brief Save measurement record to flash
 *
 * @retval true if successful, false otherwise
 */
static bool saveMeasureRecordToFlash(void)
{
	LOG_DBG( LOG_LEVEL_APP, "Saving IS25_MEASURED_DATA to dataset %d\n", gNvmData.dat.is25.is25CurrentDatasetNo);
	int bytesWrite = extFlash_write(&extFlashHandle, (uint8_t *) &measureRecord, sizeof(measureRecord), IS25_MEASURED_DATA , gNvmData.dat.is25.is25CurrentDatasetNo, EXTFLASH_MAXWAIT_MS);
	if(bytesWrite != sizeof(measureRecord))
	{
		LOG_EVENT(eWait, LOG_NUM_APP, ERRLOGFATAL, "%s() extFlash_write failed; error %s",
				__func__, extFlash_ErrorString(bytesWrite));
		return false;
	}

	gNvmData.dat.schedule.noOfGoodMeasurements++;

		// as far as I understand this part, is25CurrentDatasetNo points to the first empty dataset to use for storage,
		// set 0 is special, so it wraps around to '1'
	if(++gNvmData.dat.is25.is25CurrentDatasetNo >= MAX_NUMBER_OF_DATASETS)
	{
		gNvmData.dat.is25.is25CurrentDatasetNo = FIRST_DATASET;
	}

	// measurement dataset complete
	// advance/loop around the index to the next empty dataset slot
	if(gNvmData.dat.is25.noOfMeasurementDatasetsToUpload < (MAX_NUMBER_OF_DATASETS - 1))
	{
		gNvmData.dat.is25.noOfMeasurementDatasetsToUpload++;
	}
	else
	{
		gNvmData.dat.is25.measurementDatasetStartIndex = gNvmData.dat.is25.is25CurrentDatasetNo + 1;
		if(gNvmData.dat.is25.measurementDatasetStartIndex >= MAX_NUMBER_OF_DATASETS)
		{
			gNvmData.dat.is25.measurementDatasetStartIndex = FIRST_DATASET;
		}
	}

	LOG_DBG( LOG_LEVEL_APP,
			"is25CurrentDatasetNo            = %d\n"
			"noOfMeasurementDatasetsToUpload = %d\n"
			"measurementDatasetStartIndex    = %d\n",
			gNvmData.dat.is25.is25CurrentDatasetNo,
			gNvmData.dat.is25.noOfMeasurementDatasetsToUpload,
			gNvmData.dat.is25.measurementDatasetStartIndex);

	return true;
}


// ---------------------------- Public functions ---------------------------- //
// -------------------------------------------------------------------------- //
/**
 * @brief Top level do measurement function
 *
 * Main function that do everything including check for pre-conditions,
 * do measurements (vibration, wheelflat, raw), store data to flash, etc.
 *
 * @return - true if successful, otherwise false
 */
bool Measurement_DoMeasurements(bool KeepGnssOn)
{
	bool retval = true;

	// clear the measurement record
	memset(&measureRecord, 0, sizeof(measureRecord));
	// record timestamp, temperatures at wakeup
	measureRecord.timestamp = ConfigSvcData_GetIDEFTime();
	measureRecord.params.Temperature_External = gReport.tmp431.remoteTemp;
	measureRecord.params.Temperature_Pcb = gReport.tmp431.localTemp;

	logDClevel();

	if (false == checkMovementCondition())
	{
		if(gNvmCfg.dev.measureConf.Is_Moving_Gating_Enabled)
		{	// abort measurements - not moving
			LOG_DBG( LOG_LEVEL_APP, "\n\nWARNING - no movement detected - aborting measurement\n\n");
			measureRecord.params.Energy_Remaining = eStopped;
			ExtFlash_storeGatedMeasData();

			return false;
		}

		LOG_DBG(LOG_LEVEL_APP, "Mems sensor did not detect movement: IGNORED\n");
	}

	// PBI #163840 - If GNSS fix is done in PMIC, so we can decide to stop measurement right here.
	if (Device_PMICcontrolGPS())
	{
		if (gNvmCfg.dev.measureConf.Is_Moving_Gating_Enabled)
		{
			const PMIC_Status_t* const pPMIC_Status = PMIC_GetPMICStatus();
			if (NULL == pPMIC_Status)
			{
				LOG_DBG(LOG_LEVEL_APP, "%s(): PMIC Status not available!\n", __func__);
				measureRecord.params.Energy_Remaining = eMissingPMICdata;
				ExtFlash_storeGatedMeasData();
				return false;
			}

			if (!pPMIC_Status->gnssStatus.is_gnss_fix_good)
			{
				updateGnssMeasurementRecordFromPMIC_NoFix(&pPMIC_Status->gnssStatus);
				ExtFlash_storeGatedMeasData();
				return false;
			}
			
			// continue GPS operation with MK24 (but with less timeout, in case the fix is lost)
		}
		else // !gNvmCfg.dev.measureConf.Is_Moving_Gating_Enabled
		{
			// Nothing to do here, just continue....
		}
	}

	// ------------------ GNSS handling, for all HW Revs -------------------- //
	// Now get the GNSS ON duration
	bool speedIsValid = false;
	retval = handleGnssMeasurement(KeepGnssOn, &speedIsValid);
	if(retval == false)
	{
		LOG_DBG(LOG_LEVEL_APP, "GNSS measurement failed\n");
		speedIsValid = false;
	}

	do
	{
		if(!speedIsValid)
		{
			// if invalid speed
			LOG_DBG(LOG_LEVEL_APP, "Comms record GNSS items cleared..\n");

			commsRecord.params.GNSS_Lat_Com = 0.0;
			commsRecord.params.GNSS_Long_Com = 0.0;
			commsRecord.params.GNSS_Speed_To_Rotation_Com = 0.0;

			retval = false;
			break;
		}

		if (false == saveMeasureRecordToFlash())
		{
			break;
		}
	} while(0);

	// do we have a gated measurement record to store?
	if(!retval && measureRecord.params.Energy_Remaining)
	{
		ExtFlash_storeGatedMeasData();
	}

	return retval;
}	// end of static bool DoMeasurements()


#ifdef __cplusplus
}
#endif