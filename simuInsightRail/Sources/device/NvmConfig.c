#ifdef __cplusplus
extern "C" {
#endif

/*
 * nvmConfig.c
 *
 *  Created on: Feb 4, 2016
 *      Author: George de Fockert
 *      to retrieve and store the application config which should be retained over a power cycle.
 */


/* Uncomment to use the development server */
//#define MQTT_DEVELOPMENT
//#define PRE_REL_SERVER_EN
/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "printgdf.h"
#include "flash.h"
#include "NvmConfig.h"
#include "NvmData.h"

#include "configSvcData.h"

#include "fsl_interrupt_manager.h"
#include "CRC.h"
#include "linker.h"
#include "log.h"

//#define PRE_REL_SERVER_EN	// un-comment for pre-release server
//#define MQTT_DEVELOPMENT	// un-comment for development server

tNvmCfg gNvmCfg;// for configuration data

// Ram copy of the energy data.
energyData_t gNvmEnergyData =
{
	.crc32 = 0,				// CRC
	.dataUpdated = false,	// Energy data updated flag
	{0.0f, 0.0f, 100.0f}	// Energy data
};

typedef enum {
	TESTMODE_NONE = 0x00,		// no test mode selected, normal operation expected
	TESTMODE_PCB,				// pcb test
	TESTMODE_NODE,				// node test
} testMode_t;

static bool g_bMovedToMultiDayUpload = false;

static bool CheckMovedToMultiDayUpload();

/*!
 * NvmConfigCheckCRC
 *
 * @brief	tests the crc of the Nvm config
 *
 * @param   pSrcBuf - pointer to the source buffer
 * @param	size_Bytes - size of the source buffer in bytes
 * @param 	srcCrc - Source CRC.
 *
 * @return true when successful, false otherwise
 */

static bool NvmConfigCheckCRC(void *pSrcBuf, uint32_t size_Bytes, uint32_t srcCrc)
{
    if (pSrcBuf != NULL)
    {
    	return (srcCrc == crc32_hardware((void *)pSrcBuf, size_Bytes));
    }

    return false;
}



/*!
 * NvmConfigRead
 *
 * @brief      reads the Nvm config
 *
 * @param      pointer to the destination buffer
 *
 * @returns true when successful, false otherwise
 */

bool NvmConfigRead( tNvmCfg * cfg)
{
    if (cfg != NULL)
    {
        memcpy((uint8_t *)cfg, (uint8_t*) __app_cfg,  sizeof(*cfg)) ;

        // check CRC
        return NvmConfigCheckCRC(&cfg->dev, sizeof(cfg->dev), cfg->crc32);
    }

    return false;
}

/*!
 * NvmConfigWrite
 *
 * @brief      writes the Nvm config
 *
 * @param      pointer to the source buffer
 *
 * @returns true when successful, false otherwise
 */
bool NvmConfigWrite(tNvmCfg * cfg)
{
    bool stat = false;

    if (cfg != NULL)
    {
        // calculate crc
        cfg->dev.general.updateCounter++;
        cfg->crc32 = crc32_hardware((void *)&cfg->dev, sizeof(cfg->dev));
        stat = DrvFlashEraseProgramSector((uint32_t*) __app_cfg, (uint32_t*) cfg, sizeof(*cfg));
    }

    return stat;
}


/*!
 * NvmConfigUpdateIfChanged
 *
 * @brief      compares given configuration to stored in flash version and updates the flash if there's a difference
 *
 * @param      display log message if set
 */
void NvmConfigUpdateIfChanged(bool logit)
{
    tNvmCfg * FlashCfg = (tNvmCfg * ) __app_cfg;

    // reset the logging to the nv stored value.
    gNvmCfg.dev.general.dbg_logging = FlashCfg->dev.general.dbg_logging;
    if(0 != memcmp( &gNvmCfg.dev, &FlashCfg->dev , sizeof(FlashCfg->dev)))
    {
    	g_bMovedToMultiDayUpload = CheckMovedToMultiDayUpload();

    	if(logit)
    	{
            LOG_DBG( LOG_LEVEL_CLI,  "Configuration has changed, writing it to flash\n");
    	}
    	if(NvmConfigWrite(&gNvmCfg) && Device_HasPMIC())
    	{
    		PMIC_ScheduleConfigUpdate();
    	}
    }
}

/*!
 * NvmConfig_HasMovedToMultiDayUpload
 *
 * @brief Returns true if config update has moved to a multiday upload
 *
 * @returns      True if config update has moved to a multiday upload
 */
bool NvmConfig_HasMovedToMultiDayUpload()
{
	return g_bMovedToMultiDayUpload;
}

/*!
 * CheckMovedToMultiDayUpload
 *
 * @brief      Checks if the new config changes the upload time from a single to multiday
 *
 * @returns      True if upload has moved to a multiday upload
 */
static bool CheckMovedToMultiDayUpload()
{
	tNvmCfg * FlashCfg = (tNvmCfg * ) __app_cfg;

	bool bCurrentlyDailyUpload = (FlashCfg->dev.commConf.Upload_repeat == 86400);
	bool bNewMultiDayUpload = (gNvmCfg.dev.commConf.Upload_repeat > 86400);

	return(bCurrentlyDailyUpload && bNewMultiDayUpload);
}

// TODO: I am in a hurry, so for the moment here, but it would be better to have each module responsible for its own defaults

static const tCfgGeneral generalDefaults = {

		.dbg_logging = 0,// no logging
		.updateCounter = 0,
		.timestamp = 0,
		.deviceSerialNumber = "000000000",
};

static const tCfgSelfTest selftest = {

		.maxSelfTestTempDelta = 20,		// °C
		.adcSettleMin = 5000000,
		.adcSettleMax = 6200000,
		.adcSettleTime = 30,
};

static void printGeneralConfig(tCfgGeneral * general)
{
    printf("\nBEGIN GENERAL:\n");
    printf("Debug logging = 0x%x\n", general->dbg_logging);
    printf("Update counter = 0x%x\n", general->updateCounter);
    printf("Timestamp      = ");
    ConfigSvcData_PrintIDEFTime(general->timestamp);
    printf("\n");
    printf("Device serial number = %s\n", general->deviceSerialNumber);
    printf("END GENERAL\n\n");

}

static const tCfgSchedule scheduleDefaults = {

		.mems_samplesPerSeconds = 10,	//
		.mems_sampleDuration = 10,		// [seconds]

		.gnss_maxSecondsToAquireValidData = 240,
		// [seconds] changed from 60 to 120 because of Ontime '15276 FW: GNSS fix timeout is too short'
		// changed from 120 to 240 JIRA SIRP-1147
};



static const tCfgModem modemDefaults = {
#if 1
        // JAY: This is causing problems while Config dw and OTA.
        .baudrate = 115200*8,
#else
		.baudrate = 115200*2,
#endif
		.imei = "\0",
		.iccid = "\0",
		.provider = 0,
		.providerProfile[0].apn = "EM", /* emnify */
		.providerProfile[0].simpin = "\0",
		.providerProfile[1].apn = "internet",/* kpn */
		.providerProfile[1].simpin = "0000",
		.service = 0,
		.serviceProfile[0].url = "127.0.0.1",
		.serviceProfile[0].portnr = 5055,
		.serviceProfile[0].secure = false,
		.serviceProfile[1].url = "127.0.0.1",
		.serviceProfile[1].portnr = 5055,
        .serviceProfile[0].secure = false,
		.minimumCsqValue = 1,
		.maxTimeToConnectMs = 180000, // this can take official 385 seconds, seen it taking almost 2 minutes, so compromise: 3 minutes
		.maxTimeToTerminateMs = 30000,
		.radioAccessTechnology = 2 // '0' gives 2G only, '1' dual mode with 3G peferred, '2' only 3G
};

static void printModemConfig(tCfgModem * modem)
{
	int i;
	printf("BEGIN MODEM:\n");
	printf("baudrate = %u\n", modem->baudrate);
	printf("IMEI: %s\nICCID: %s\n",modem->imei,modem->iccid);

	printf("provider profile %d\n",modem->provider);
	for (i=0; i<MODEM_PROVIDERPROFILES; i++)
	{
		printf("providerProfile[%d]  apn=%s, simpin=%s\n",
			i,
			modem->providerProfile[i].apn,
			modem->providerProfile[i].simpin);
	}
	printf("service profile %d\n",modem->service);
	for (i=0; i<MODEM_SERVICEPROFILES; i++)
	{

        printf("serviceProfile[%d]  url:port=%s:%d %ssecure\n",
            i,
            modem->serviceProfile[i].url,
            modem->serviceProfile[i].portnr,
            modem->serviceProfile[i].secure ? "\0" : "UN");
	}
	printf("Minimum csq value to connect : %d\n",modem->minimumCsqValue);
	printf("Max time to build connection : %u ms\n",modem->maxTimeToConnectMs);
	printf("Max time to terminate/powerdown connection : %u ms\n", modem->maxTimeToTerminateMs);
	printf("END MODEM\n\n");
}

static const tCfgMqtt mqttDefaults = {
        .deviceId = "\0",
#ifdef MQTT_DEVELOPMENT
        .subTopicPrefix = "SKF/Device/",
        .pubTopicPrefix = "SKF/Device/Server",
        .logTopicPrefix = "SKF/Log/",
        .epoTopicPrefix = "SKF/Ephemeris/",
        .service = 0,
#else
        .subTopicPrefix = "SKF/Device/",
        .pubTopicPrefix = "SKF/Device/Server",
        .logTopicPrefix = "SKF/Log/",
        .epoTopicPrefix = "SKF/Ephemeris/",
        .service = 0,
#endif
#if defined(PRE_REL_SERVER_EN) || defined(MQTT_DEVELOPMENT)
		.serviceProfile[0].url = "34.246.19.87", //Only for development
#else
		.serviceProfile[0].url = "mqtt.skf-connectivity.com",
#endif
        .serviceProfile[0].portnr = 1883,
        .serviceProfile[0].secure = false,
        .serviceProfile[1].url = "127.0.0.1",
        .serviceProfile[1].portnr = 1883,
        .serviceProfile[1].secure = false,
};

static void printMqttConfig(tCfgMqtt * mqtt)
{
    int i;
    printf("BEGIN MQTT:\n");
    printf("DeviceId: %s\n", mqtt->deviceId);
    printf("subTopicPrefix: %s\n", mqtt->subTopicPrefix);
    printf("pubTopicPrefix: %s\n", mqtt->pubTopicPrefix);
    printf("logTopicPrefix: %s\n", mqtt->logTopicPrefix);
    printf("epoTopicPrefix: %s\n", mqtt->epoTopicPrefix);


    printf("service profile %d\n",mqtt->service);
    for (i=0; i<MQTT_SERVICEPROFILES; i++)
    {

        printf("serviceProfile[%d]  url:port=%s:%d (%ssecure)\n",
            i,
            mqtt->serviceProfile[i].url,
            mqtt->serviceProfile[i].portnr,
            mqtt->serviceProfile[i].secure ? "\0" : "UN");
    }
    printf("END MQTT\n\n");
}



static const tCfgCommunication communicationDefaults = {
#ifdef PRE_REL_SERVER_EN
		 .Url_Server_1 = "mqqt.skf-cloud.com:1883",
         .Url_Server_2 = "mqqt.skf-cloud.com:1883",
         .Url_Server_3 = "mqqt.skf-cloud.com:1883",
         .Url_Server_4 = "mqqt.skf-cloud.com:1883",
#elif defined (MQTT_DEVELOPMENT)
		 .Url_Server_1 = "34.246.19.87:1883",
		 .Url_Server_2 = "34.246.19.87:1883",
		 .Url_Server_3 = "34.246.19.87:1883",
		 .Url_Server_4 = "34.246.19.87:1883",
#else
		 .Url_Server_1 = "10.0.102.11:1883",
		 .Url_Server_2 = "10.0.102.11:1883",
		 .Url_Server_3 = "10.0.102.11:1883",
		 .Url_Server_4 = "mqtt.skf-connectivity.com:1883",
#endif
         .Upload_Time = 15,		// 15 / 100 = 00:15hrs
         .Is_Verbose_Logging_Enabled = false,
		 .Retry_Interval_Upload = 5400,		// seconds
         .Max_Upload_Retries = 3,
         .Upload_repeat = 24*60*60,		// 86400 seconds
         .Number_Of_Upload = 1,
         .Confirm_Upload = 0,
};

static void printCommunicationConfig(tCfgCommunication * comm)
{
    printf("BEGIN COMMUNICATION:\n");
    printf("Url_Server_1 = %s\n", comm->Url_Server_1);
    printf("Url_Server_2 = %s\n", comm->Url_Server_2);
    printf("Url_Server_3 = %s\n", comm->Url_Server_3);
    printf("Url_Server_4 = %s\n", comm->Url_Server_4);
    printf("Upload_Time = %u\n", comm->Upload_Time);
    printf("Is_Verbose_Logging_Enabled = %d\n",comm->Is_Verbose_Logging_Enabled);
    printf("Retry_Interval_Upload = %u\n",comm->Retry_Interval_Upload);
    printf("Max_Upload_Retries = %u\n",comm->Max_Upload_Retries);
    printf("Upload_repeat = %u\n",comm->Upload_repeat);
    printf("Number_Of_Upload = %u\n",comm->Number_Of_Upload);
    printf("Confirm_Upload = %d\n",comm->Confirm_Upload);

    printf("END COMMUNICATION\n\n");
}




static const tCfgMeasurement measurementDefaults = {
        .Schedule_ID = "Z",
        .Is_Raw_Acceleration_Enabled = false,
        .Temperature_Alarm_Upper_Limit = 200.0,		// High limits set to avoid comms triggering.
        .Temperature_Alarm_Lower_Limit = 200.0,		// High limits set to avoid comms triggering.
        .Min_Hz = 600/60,
        .Max_Hz = 1500/60,
        .Allowed_Rotation_Change = 2,
        .Sample_Rate_Bearing = 2560,
#ifdef SOUTH_AMERICAN_BASED_ON_MVP
		.Samples_Bearing = 4096,
#else
		.Samples_Bearing = 4096,
#endif

        .Scaling_Bearing = 1.0,
        .Sample_Rate_Raw = 25600,
#ifdef SOUTH_AMERICAN_BASED_ON_MVP
		.Samples_Raw = 16384,
#else
        .Samples_Raw = 32768,
#endif
        .Scaling_Raw = 1.0,
        .Sample_Rate_Wheel_Flat = 512,
        .Samples_Wheel_Flat = 2048,
        .Scaling_Wheel_Flat = 1.0,
        .Time_Start_Meas = 600,			// 600 / 100 = 06:00
        .Retry_Interval_Meas = 5400,	// seconds (90 mins)
        .Max_Daily_Meas_Retries = 2,
        .Required_Daily_Meas = 0,		// stop measurement cycles until full schedule received
        .Acceleration_Threshold_Movement_Detect = 0.25,
        .Acceleration_Avg_Time_Movement_Detect = 5,
        .Is_Power_On_Failsafe_Enabled = true,
        .Is_Moving_Gating_Enabled = false,
        .Is_Upload_Offset_Enabled = true,
};

static void printMeasurementConfig(tCfgMeasurement * meas)
{
    printf("BEGIN MEASUREMENT:\n");
    printf("Schedule_ID = %s\n",meas->Schedule_ID);
    printf("Is_Raw_Acceleration_Enabled = %d\n",meas->Is_Raw_Acceleration_Enabled);
    printf("Temperature_Alarm_Upper_Limit = %f\n",meas->Temperature_Alarm_Upper_Limit);
    printf("Temperature_Alarm_Lower_Limit = %f\n",meas->Temperature_Alarm_Lower_Limit);
    printf("Min_Hz = %u\n",meas->Min_Hz);
    printf("Max_Hz = %u\n",meas->Max_Hz);
    printf("Allowed_Rotation_Change = %f\n",meas->Allowed_Rotation_Change);
    printf("Sample_Rate_Bearing = %u\n",meas->Sample_Rate_Bearing);
    printf("Samples_Bearing = %u\n",meas->Samples_Bearing);
    printf("Scaling_Bearing = %f\n",meas->Scaling_Bearing);
    printf("Sample_Rate_Raw = %u\n",meas->Sample_Rate_Raw);
    printf("Samples_Raw = %u\n",meas->Samples_Raw);
    printf("Scaling_Raw = %f\n",meas->Scaling_Raw);
    printf("Sample_Rate_Wheel_Flat = %u\n",meas->Sample_Rate_Wheel_Flat);
    printf("Samples_Wheel_Flat = %u\n",meas->Samples_Wheel_Flat);
    printf("Scaling_Wheel_Flat = %f\n",meas->Scaling_Wheel_Flat);
    printf("Time_Start_Meas = %u\n",meas->Time_Start_Meas);
    printf("Retry_Interval_Meas = %u\n",meas->Retry_Interval_Meas);
    printf("Max_Daily_Meas_Retries = %u\n",meas->Max_Daily_Meas_Retries);
    printf("Required_Daily_Meas = %u\n",meas->Required_Daily_Meas);
    printf("Acceleration_Threshold_Movement_Detect = %f\n",meas->Acceleration_Threshold_Movement_Detect);
    printf("Acceleration_Avg_Time_Movement_Detect = %d\n",meas->Acceleration_Avg_Time_Movement_Detect);
    printf("Is_Power_On_Failsafe_Enabled = %d\n",meas->Is_Power_On_Failsafe_Enabled);
    printf("Is_Moving_Gating_Enabled = %d\n",meas->Is_Moving_Gating_Enabled);
    printf("Is_Upload_Offset_Enabled = %d\n",meas->Is_Upload_Offset_Enabled);
    printf("END MEASUREMENT\n\n");
}


static const tCfgAssetId assetIdDefaults = {
        .Train_Operator = "Undefined",
        .Train_Fleet = "Undefined",
        .Train_ID = "Undefined",
        .Vehicle_ID = "Undefined",
        .Vehicle_Nickname = "Undefined",
        .Bogie_Serial_Number = "Undefined",
        .Wheelset_Number = "Undefined",
        .Wheelset_Serial_Number = "Undefined",
        .Is_Wheelset_Driven = false,
        .Wheel_Serial_Number = "Undefined",
        .Wheel_Side = "?",
        .Wheel_Diameter = 0.9,
        .Axlebox_Serial_Number = "Undefined",
        .Bearing_Brand_Model_Number = "Undefined",
        .Bearing_Serial_Number = "Undefined",
        .Sensor_Location_Angle = 0.0,
        .Sensor_Orientation_Angle = 0.0,
        .Train_Name = "Undefined",
        .Bogie_Number_In_Wagon = "Undefined",
};

static void printAssetIdConfig(tCfgAssetId * asset)
{
    printf("BEGIN ASSETID:\n");
    printf("Train_Operator = %s\n",asset->Train_Operator );
    printf("Train_Fleet = %s\n",asset->Train_Fleet );
    printf("Train_ID = %s\n",asset->Train_ID );
    printf("Vehicle_ID = %s\n",asset->Vehicle_ID );
    printf("Vehicle_Nickname = %s\n",asset->Vehicle_Nickname );
    printf("Bogie_Serial_Number = %s\n",asset->Bogie_Serial_Number );
    printf("Wheelset_Number = %s\n",asset->Wheelset_Number );
    printf("Wheelset_Serial_Number = %s\n",asset->Wheelset_Serial_Number );
    printf("Is_Wheelset_Driven = %d\n",asset->Is_Wheelset_Driven );
    printf("Wheel_Serial_Number = %s\n",asset->Wheel_Serial_Number );
    printf("Wheel_Side = %s\n",asset->Wheel_Side );
    printf("Wheel_Diameter = %f\n",asset->Wheel_Diameter );
    printf("Axlebox_Serial_Number = %s\n",asset->Axlebox_Serial_Number );
    printf("Bearing_Brand_Model_Number = %s\n",asset->Bearing_Brand_Model_Number );
    printf("Bearing_Serial_Number = %s\n",asset->Bearing_Serial_Number );
    printf("Sensor_Location_Angle = %f\n",asset->Sensor_Location_Angle );
    printf("Sensor_Orientation_Angle = %f\n",asset->Sensor_Orientation_Angle );
    printf("Train_Name = %s\n",asset->Train_Name );
    printf("Bogie_Number_In_Wagon = %s\n",asset->Bogie_Number_In_Wagon );

    printf("END ASSETID\n\n");
}


/*!
 * NvmConfigDefaults
 *
 * @brief      initialises and writes the Nvm config
 *
 * @param      pointer to the source buffer
 *
 * @returns
 */

void NvmConfigDefaults(tNvmCfg * cfg)
{
	memset( (uint8_t *) cfg, 0xff, sizeof(*cfg));// all to default flash empty state

	memcpy( & cfg->dev.general, &generalDefaults, sizeof(generalDefaults));

	memcpy( & cfg->dev.modem, &modemDefaults, sizeof(modemDefaults));

	memcpy( & cfg->dev.schedule, &scheduleDefaults, sizeof(scheduleDefaults));

	memcpy( & cfg->dev.mqtt, &mqttDefaults, sizeof(mqttDefaults));

    memcpy( & cfg->dev.commConf, &communicationDefaults, sizeof(communicationDefaults));

    memcpy( & cfg->dev.measureConf, &measurementDefaults, sizeof(measurementDefaults));

    memcpy( & cfg->dev.assetConf, &assetIdDefaults, sizeof(assetIdDefaults));

    memcpy( & cfg->dev.selftest, &selftest, sizeof(selftest));
}

/*!
 * NvmConfigOneShadowWriteEnergyData
 *
 * @brief   Writes the energy data to Nvm app1 config shadow area.
 *
 * @param	pEnergyData - pointer to the source buffer
 *
 * @return 	true when successful, false otherwise
 */
bool NvmConfigOneShadowWriteEnergyData( energyData_t *pEnergyData)
{
    bool stat = true;

    if (pEnergyData	!= NULL)
    {
        // calculate crc
    	pEnergyData->crc32 = crc32_hardware((void *)&pEnergyData->data, sizeof(pEnergyData->data));
        __disable_irq();// we may run in the same flash bank as the one we are erasing/programming, then interrupts may not execute code inside this bank !
        stat = DrvFlashEraseProgramSector((uint32_t*) __app_cfgsh, (uint32_t*) pEnergyData, sizeof(*pEnergyData));
        __enable_irq();
    }
    else
    {
        stat = false;
    }

    return stat;
}


/*!
 * NvmConfigOneShadowReadEnergyData
 *
 * @brief	reads the energy data from Nvm app 1 config shadow area
 *
 * @param	pEnergyData - pointer to the destination buffer
 *
 * @return 	true when successful, false otherwise
 */

bool NvmConfigOneShadowReadEnergyData( energyData_t *pEnergyData)
{
    bool stat = false;
    if (pEnergyData != NULL)
    {
        memcpy((uint8_t *) pEnergyData, (uint8_t*) __app_cfg_shadow,  sizeof(*pEnergyData)) ;
        stat = NvmConfigCheckCRC(&pEnergyData->data, sizeof(pEnergyData->data), pEnergyData->crc32);
		if(!stat)
		{
			pEnergyData->dataUpdated = false;
		}
    }
    return stat;
}

void NvmPrintRanges()
{
	printf("memory address (size) :\n");
	printf("app_text  = 0x%08x\n", __app_text);
	printf("app_nvdata= 0x%08x ( 0x%08x) used = 0x%08x\n", __app_nvdata, __app_nvdata_size, sizeof(gNvmData));
	printf("app_cfg   = 0x%08x ( 0x%08x) used = 0x%08x\n", __app_cfg, __app_cfg_size, sizeof(gNvmCfg));
	printf("dev_calib = 0x%08x ( 0x%08x)\n", __device_calib, __device_calib_size);
	printf("event_log = 0x%08x ( 0x%08x)\n", __event_log, __eventlog_size);
	printf("bootcfg   = 0x%08x ( 0x%08x)\n", __bootcfg, __bootcfg_size);
}


void NvmPrintConfig()
{
	tNvmCfg *cfg= (tNvmCfg * ) __app_cfg;
	NvmPrintRanges();
	printf("\nConfiguration dump:\n\n");
	printf("CRC check of config in flash %s\n", NvmConfigCheckCRC(&cfg->dev, sizeof(cfg->dev), cfg->crc32) ? "OK" : "FAILED");
	printf("CRC check of config in RAM  %s\n", NvmConfigCheckCRC(&gNvmCfg.dev, sizeof(gNvmCfg.dev), gNvmCfg.crc32) ? "OK" : "FAILED");
	printGeneralConfig(&gNvmCfg.dev.general);
	printModemConfig(&gNvmCfg.dev.modem);
	printMqttConfig(&gNvmCfg.dev.mqtt);
	printCommunicationConfig(&gNvmCfg.dev.commConf);
	printMeasurementConfig(&gNvmCfg.dev.measureConf);
	printAssetIdConfig(&gNvmCfg.dev.assetConf);
}


#ifdef __cplusplus
}
#endif