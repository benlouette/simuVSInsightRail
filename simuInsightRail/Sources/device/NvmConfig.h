#ifdef __cplusplus
extern "C" {
#endif

/*
 * NvmConfig.h
 *
 *  Created on: Feb 4, 2016
 *      Author: George de Fockert
 */

#ifndef SOURCES_DEVICE_NVMCONFIG_H_
#define SOURCES_DEVICE_NVMCONFIG_H_

#include <stdint.h>
#include <stdbool.h>


// quick implementation to get some permanent storage for release 2 of the passenger railway project

#include "linker.h"

#define MAXSCHEDULECONFIG	(128)
#define MAXDEVICESERIALNUMBERSIZE (30+1)

#ifdef _MSC_VER
#define PACKED_STRUCT_START __pragma(pack(push, 1))
#define PACKED_STRUCT_END   __pragma(pack(pop))
#else
#define PACKED_STRUCT_START 
#define PACKED_STRUCT_END   __attribute__((packed))
#endif

PACKED_STRUCT_START
typedef struct  generalStruct
{
	uint32_t dbg_logging; // initial logging settings for debug monitoring	(default should be  0)
	uint32_t updateCounter; // every time the configuration is updated after 'reset'
	uint64_t timestamp; // when the last update is done to the configuration
	//bool	keepOn; // startup in debug/test mode,  and not in the application powerup/measure/send/powerdown cycle
	char deviceSerialNumber[MAXDEVICESERIALNUMBERSIZE];
	bool gnssIgnoreSysMsg;	// Flag to Ignore the GNSS STARTUP STS MSG Check.
} tCfgGeneral;
PACKED_STRUCT_END

PACKED_STRUCT_START
typedef struct  selftestStruct
{
	uint8_t maxSelfTestTempDelta;		// max allowed temp difference between local and remote temp sensors to allow self test to pass
	uint32_t adcSettleMin;            // min for ADC settle limits
	uint32_t adcSettleMax;            // max for ADC settle limits
	uint8_t adcSettleTime;             // max ADC settle time in seconds
} tCfgSelfTest;
PACKED_STRUCT_END
PACKED_STRUCT_START
typedef struct  scheduleStruct
{
	// mems sensor settings
	uint8_t mems_samplesPerSeconds;
	uint8_t mems_sampleDuration;

	uint16_t gnss_maxSecondsToAquireValidData;		// max time in seconds given to the gnss to acquire valid data

	//DAYTIME_data_t daytime;
} tCfgSchedule;
PACKED_STRUCT_END

#define MODEM_IMEI_LEN 	(15+1)
#define MODEM_ICCID_LEN (20+1)

#define MODEM_SIMPIN_LEN (4+1)
#define MODEM_APN_LEN 	(15+1)
#define MODEM_URL_LEN 	(39+1)
#define MODEM_PROVIDERPROFILES (2)
#define MODEM_SERVICEPROFILES (2)

PACKED_STRUCT_START
typedef struct  serviceProfileStruct
{
    char url[MODEM_URL_LEN]; // zero terminated string (default at the moment : 'lvs0217.dyndns.org'
    uint16_t portnr; // for the moment 5055
    bool secure; // for a secure (TLS) connection
} tServiceProfile;
PACKED_STRUCT_END

PACKED_STRUCT_START
typedef	struct  modemConfigStruct
{
    uint32_t baudrate;
	uint8_t imei[MODEM_IMEI_LEN];
	uint8_t iccid[MODEM_ICCID_LEN];
	uint8_t provider; // 0..5
	uint8_t service; // 0..9
	struct  providerProfileStruct
	{
		char apn[MODEM_APN_LEN]; // zero terminated string with the access point name ('internet' for KPN)
		char simpin[MODEM_SIMPIN_LEN]; // zero terminated pin number '0000' for skf kpn sim
	} providerProfile[MODEM_PROVIDERPROFILES];// for release 2, only one (modem supports 6 profiles!)

	tServiceProfile serviceProfile[MODEM_SERVICEPROFILES];// for release 2, only one (modem supports 10)

	// and various maximum timeout values for modem commands will be added in the near future
    uint8_t minimumCsqValue; // the signal quality as reported by the T command AT+CSQ must be equal or above this value to build up the connection
    uint32_t maxTimeToConnectMs; // after this time, in milliseconds, is expired, powering up the modem/building the tcp connection process is aborted
    uint32_t maxTimeToTerminateMs; // after this time, in milliseconds, is expired after starting the connection/modem shutdown, the modem power is switched of anyway
    uint8_t radioAccessTechnology; // '0' gives 2G only, '1' dual mode with 3G peferred, '2' only 3G
} tCfgModem;
PACKED_STRUCT_END

// non volatile configuration items for MQTT
#define MQTT_SERVICEPROFILES (2)
#define MQTT_TOPIC_PREFIX_LEN (20)
#define MQTT_DEVICEID_LEN (MODEM_IMEI_LEN+4)

PACKED_STRUCT_START
typedef struct  MqttConfigStruct
{
    uint8_t deviceId[MQTT_DEVICEID_LEN];// device ID example = IMEI0123456789012345
    uint8_t subTopicPrefix[MQTT_TOPIC_PREFIX_LEN];// topic where the device subscribes to, for receiving messages (example SKF/Device/<unique device id>)
    uint8_t pubTopicPrefix[MQTT_TOPIC_PREFIX_LEN];// topic to where the device publishes its messages/requests  (example SKF/Server)
    uint8_t logTopicPrefix[MQTT_TOPIC_PREFIX_LEN];// topic to where the device publishes its messages/requests  (example SKF/Log)
    uint8_t epoTopicPrefix[MQTT_TOPIC_PREFIX_LEN];// topic to where the device publishes its messages/requests  (example SKF/Ephemeris/<unique device id>)
    uint8_t service; // 0..1
    tServiceProfile serviceProfile[MQTT_SERVICEPROFILES];// for release 2, only one (modem supports 10)
} tCfgMqtt;
PACKED_STRUCT_END



#define MAXURLSIZE (50+1)
PACKED_STRUCT_START
typedef struct 
{
     char Url_Server_1[MAXURLSIZE];
     char Url_Server_2[MAXURLSIZE];
     char Url_Server_3[MAXURLSIZE];
     char Url_Server_4[MAXURLSIZE];
     uint32_t Upload_Time;
     bool Is_Verbose_Logging_Enabled;
     uint32_t Retry_Interval_Upload;
     uint32_t Max_Upload_Retries;
     uint32_t Upload_repeat;
     uint32_t Number_Of_Upload;
     bool Confirm_Upload;
} tCfgCommunication;
PACKED_STRUCT_END

PACKED_STRUCT_START
typedef struct 
{
    char Schedule_ID[2];
    bool Is_Raw_Acceleration_Enabled;
    float Temperature_Alarm_Upper_Limit;
    float Temperature_Alarm_Lower_Limit;
    uint32_t Min_Hz;
    uint32_t Max_Hz;
    float  Allowed_Rotation_Change;
    uint32_t Sample_Rate_Bearing;
    uint32_t Samples_Bearing;
    float Scaling_Bearing;
    uint32_t Sample_Rate_Raw;
    uint32_t Samples_Raw;
    float Scaling_Raw;
    uint32_t Sample_Rate_Wheel_Flat;
    uint32_t Samples_Wheel_Flat;
    float Scaling_Wheel_Flat;
    uint32_t Time_Start_Meas;
    uint32_t Retry_Interval_Meas;
    uint32_t Max_Daily_Meas_Retries;
    uint32_t Required_Daily_Meas;
    float Acceleration_Threshold_Movement_Detect;
    uint32_t Acceleration_Avg_Time_Movement_Detect;
    bool Is_Power_On_Failsafe_Enabled;
    bool Is_Moving_Gating_Enabled;
    bool Is_Upload_Offset_Enabled;

} tCfgMeasurement;
PACKED_STRUCT_END


#define MAX_ASSETID_SIZE    (21+1)
#define MAX_WHEELSIDE_SIZE  (2+1)

PACKED_STRUCT_START
typedef struct  
{
	char Train_Operator[MAX_ASSETID_SIZE];
	char Train_Fleet[MAX_ASSETID_SIZE];
	char Train_ID[MAX_ASSETID_SIZE];
	char Vehicle_ID[MAX_ASSETID_SIZE];
	char Vehicle_Nickname[MAX_ASSETID_SIZE];
	char Bogie_Serial_Number[MAX_ASSETID_SIZE];
	char Wheelset_Number[MAX_ASSETID_SIZE];
	char Wheelset_Serial_Number[MAX_ASSETID_SIZE];
	bool Is_Wheelset_Driven;
	char Wheel_Serial_Number[MAX_ASSETID_SIZE];
	char Wheel_Side[MAX_WHEELSIDE_SIZE];   /* two chars, 'RH' (right hand) or 'LH' (left hand) */
	float Wheel_Diameter;
	char Axlebox_Serial_Number[MAX_ASSETID_SIZE];
	char Bearing_Brand_Model_Number[MAX_ASSETID_SIZE];
	char Bearing_Serial_Number[MAX_ASSETID_SIZE];
	float Sensor_Location_Angle;
	float Sensor_Orientation_Angle;
	char Train_Name[MAX_ASSETID_SIZE];
	char Bogie_Number_In_Wagon[MAX_ASSETID_SIZE];
} tCfgAssetId;
PACKED_STRUCT_END

PACKED_STRUCT_START
struct  nvmConfigStruct
{
    uint32_t crc32; // 32 bit crc whole data struct
    bool  spare;
    //bool  dirty; // when true, ram copy has changed, NV copy must be updated

    struct  devStruct
    {
    	// some config parameters which affects general device behavior
        tCfgGeneral general;

   		// some schedule config parameters which affects device scheduled behaviour
		tCfgSchedule schedule;

        // module specific configs
        tCfgModem  modem;

        // module specific configs
        tCfgMqtt  mqtt;

        tCfgCommunication commConf;

        tCfgMeasurement measureConf;

        tCfgAssetId assetConf;

        tCfgSelfTest selftest;

    } dev;
};
PACKED_STRUCT_END
PACKED_STRUCT_START
typedef struct  energyDataStruct
{
    uint32_t crc32; 						// 32 bit crc whole data struct
    bool dataUpdated;						// Flag to detect if the energy data is updated for writing to NVM.
    struct  energyStruct
	{
    	float energySumMeasurementCycles;	// Running total of energy consumed during measure cycle.
    	float energySumModemCycles;			// Running total of energy consumed during upload cycle.
    	float energyRemaining_percent;		// Remaining energy in percent.
    } data;
}energyData_t;
PACKED_STRUCT_END

typedef struct nvmConfigStruct tNvmCfg;

extern tNvmCfg gNvmCfg;
extern energyData_t gNvmEnergyData;

bool NvmConfigRead( tNvmCfg * cfg);
bool NvmConfigWrite( tNvmCfg * cfg);
void NvmConfigDefaults( tNvmCfg * cfg);
void NvmConfigUpdateIfChanged(bool logit);
bool NvmConfigIsUpdated();
bool NvmConfig_HasMovedToMultiDayUpload();

void NvmPrintRanges();
void NvmPrintConfig();

bool NvmConfigOneShadowWriteEnergyData( energyData_t *pEnergyData);
bool NvmConfigOneShadowReadEnergyData( energyData_t *pEnergyData);

#endif /* SOURCES_DEVICE_NVMCONFIG_H_ */


#ifdef __cplusplus
}
#endif