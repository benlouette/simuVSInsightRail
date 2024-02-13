#ifdef __cplusplus
extern "C" {
#endif

/*
 * configData.h
 *
 *  Created on: Feb 8, 2016
 *      Author: George de Fockert
 *
 *      Defines the data for the specific project, and are unique for the product
 *      except the general list needed to identify the device (Device_Id)
 */

#ifndef SOURCES_DATASTORE_CONFIGDATA_H_
#define SOURCES_DATASTORE_CONFIGDATA_H_

#include <stdint.h>
#include "DataDef.h"
#include "configFeatures.h"

// if no floating point support is required, disabling next define saves up a lot of floating point code
#define DATASTORE_ENABLE_FLOAT


// TODO: example maximum sample sizes, should be defined elsewhere !
// The values I made up without knowing what the real limit should be, so adapt !
#define MAX_ENV3_SAMPLES        (1024*4)
#define MAX_FLAT_SAMPLES        (1024*4)
#define MAX_RAW_SAMPLES         (1024*32)

#define MAXCARDINALDIRECTIONLENGTH (1)
#define MAXSTATUSSELFTESTLENGTH (20)

// Define the minimum value of upload repeat, in secs.
// for daily upload it is (60*60*24 = 86400), For every 2nd day upload
// the value would be = 86400*2 = 172800.
#define MIN_UPLOAD_REPEAT_SECS	(86400)

// TODO: Also this struct should better be defined elsewhere !
#define MAX_GNSS_SATELITES (20)
typedef struct {
    uint64_t timestamp;

    struct {
        uint8_t Energy_Remaining;
        bool Is_Move_Detect_Meas;
        float Temperature_Pcb;
        float Temperature_External;
        float GNSS_Lat_1;
        uint8_t GNSS_NS_1[MAXCARDINALDIRECTIONLENGTH+1];
        float GNSS_Long_1;
        uint8_t GNSS_EW_1[MAXCARDINALDIRECTIONLENGTH+1];
        float GNSS_Speed_To_Rotation_1;
        uint16_t GNSS_Course_1;
        uint32_t GNSS_Time_To_Fix_1;
        bool Is_Speed_Within_Env;
        float GNSS_Speed_To_Rotation_2;
        float Rotation_Diff;
        float GNSS_Time_Diff;
        //bool Is_Good_Meas;
        bool Is_Good_Speed_Diff;

        uint8_t GNSS_Sat_Id[MAX_GNSS_SATELITES];
        uint8_t GNSS_Sat_Snr[MAX_GNSS_SATELITES];
#if 0
        // measurement settings used for the waveform data, will become active after TG5 because it needs needs new IDEF parameters
        // (quick fix for TG5, but Roland has to do also some work for that and don't want this now !)
        uint32_t Sample_Rate_Bearing;
        uint32_t Samples_Bearing;
        float  Scaling_Bearing;
        uint32_t Sample_Rate_Raw;
        uint32_t Samples_Raw;
        float  Scaling_Raw;
        uint32_t Sample_Rate_Wheel_Flat;
        uint32_t Samples_Wheel_Flat;
        float  Scaling_Wheel_Flat;
#endif
    } params;
} measure_record_t;


typedef struct {
    uint64_t timestamp;

    struct {
        uint8_t Wakeup_Reason;
        float EnergyRemaining_percent;
        uint8_t Com_Signal_Quality;
        bool Is_Move_Detect_Com;
        float Acceleration_Y;
        float Acceleration_X;
        float Acceleration_Z;
        bool Is_Self_Test_Successful;
        uint8_t Status_Self_Test[MAXSTATUSSELFTESTLENGTH];
        float Humidity;
        uint32_t Up_Time;
        float GNSS_Lat_Com;
        uint8_t GNSS_NS_Com[MAXCARDINALDIRECTIONLENGTH+1];
        float GNSS_Long_Com;
        uint8_t GNSS_EW_Com[MAXCARDINALDIRECTIONLENGTH+1];
        float GNSS_Speed_To_Rotation_Com;
        uint64_t Time_Last_Wake;
        uint32_t Time_Elap_Registration;
        uint32_t Time_Elap_TCP_Connect;
        uint32_t Time_Elap_TCP_Disconnect;
        uint32_t Time_Elap_Modem_On;
        float V_0;
        float V_1;
        float V_2;
        float E_GNSS_Cycle;
        float E_Modem_Cycle;
        float E_Previous_Wake_Cycle;
    } params;
} comms_record_t;

// dataRecord structure used by external flash is25
// this is not a nice way of doing this, but inherited from the way Rex implemented it for TG5 for storing both in external flash
typedef struct
{
    comms_record_t commsRecord;
    measure_record_t measureRecord;
}dataRecord_t;


extern measure_record_t measureRecord;
extern comms_record_t commsRecord;

extern uint64_t timestamp_env3;
extern uint64_t timestamp_wheelflat;
extern uint64_t timestamp_raw;

extern uint32_t extflash_env3_adress;// contains byte address where the envelope 3 waveform in external flash starts
extern uint32_t extflash_wheelflat_adress; // etc.
extern uint32_t extflash_raw_adress;


extern uint64_t currentIdefTime;

extern uint32_t HardwareVersionForConfig ;// read from hardware io lines
extern uint32_t Firmware_Update ;// TODO: must come from bootloader flash record
// RAM (Modifiable) copy of the Firmware version
extern uint32_t Firmware_Version;

/*

 */


typedef enum {
    DATASTORE_RESERVED = 0,// to avoid problems in the CLI when using a string as ID

    //    DR:     device Status/Info Record (most of them used in the signon message)
        DR_Device_Type,
        DR_Hardware_Version,
        DR_Firmware_Version,

        DR_Device_Description,
        DR_Device_Serial_Number,
        DR_currentIdefTime,
        DR_configIdefTime, // last tim eth econfiguration was updated
	// etc.
	MODEM_IMEI,
	MODEM_ICCID,
	MODEM_PROVIDERPROFILENR,
	MODEM_SIMPIN0,
	MODEM_APN0,
	MODEM_SIMPIN1,
	MODEM_APN1,
	MODEM_SERVICEPROFILENR,
	MODEM_URL0,
	MODEM_PORTNR0,
	MODEM_URL1,
	MODEM_PORTNR1,

	//SCHEDULE_SLEEP_TIME,
	SCHEDULE_DS1374_SLEEP_TIME_MINS,
	MODEM_MINCSQ,

	SCHEDULE_TEST_MODE,
	SCHEDULE_GNSS_RMC_NOR,
	SCHEDULE_GNSS_RMC_INT,

	NODE_MAX_ON_TIME_MINUTES,	// node max on time in minutes

	SCHEDULE_DDPS,				// dummy data packet size
	SCHEDULE_DDNOP,				// dummy data number of packets sent each data upload

	SCHEDULE_ACC_NOS,			// acceleration sensor sample length
	SCHEDULE_ACC_SR,			// acceleration sensor sample rate [mSeconds]
	SCHEDULE_COLLECT_DATA_IF_MOVING,		// only collect data if moving

	MODEM_MAXCONNECTTIME,
	MODEM_MAXTERMINATETIME,
	MODEM_RADIOACCESSTECH,

	// flag to control collection of temperature data
	SCHEDULE_COLLECT_TEMP_DATA,

	// flag to control collection of movement data data
	SCHEDULE_COLLECT_MOVEMENT_DATA,

	// flag to control collection of acceleration data
	SCHEDULE_COLLECT_ACCEL_DATA,

	// flag to control collection of GNSS data
	SCHEDULE_COLLECT_GNSS_DATA,

	// movement threshold limit +-[g]
	SCHEDULE_MOVEMENT_THRES_LIMIT,

	// sync rtc wakeup to o'clock
	SCHEDULE_SYNC_RTC_OCLOCK,

	// only operate during daylight hours
	SCHEDULE_DAYTIME_COLLECTION,
	SCHEDULE_DAYTIME_STARTHR,
	SCHEDULE_DAYTIME_ENDHR,

	SCHEDULE_SUPERCAP_THRES,
	SCHEDULE_CHECK_SUPERCAP_VOLTS,

	SCHEDULE_MEMS_FSD,
	SCHEDULE_MEMS_THRESHOLD,
	SCHEDULE_MEMS_DURATION,
	SCHEDULE_MEMS_SAMPLESPERSECOND,
	SCHEDULE_MEMS_SAMPLEDURATION,

	SCHEDULE_GNSS_MAX_TIME_TO_VALID_DATA,

	GENERAL_SELF_TEST_TEMP_DELTA,

	GNSS_IGNORE_STARTUP_SYSMSG, //Flag to Ignore the GNSS STARTUP STS MSG Check.

    MODEM_BAUDRATE,
    MODEM_SECURE0,
    MODEM_SECURE1,

    MQTT_DEVICEID,
    MQTT_SUBTOPICPREFIX,
    MQTT_PUBTOPICPREFIX,
    MQTT_LOGTOPICPREFIX,
    MQTT_EPOTOPICPREFIX,
    MQTT_SERVICEPROFILENR,
    MQTT_URL0,
    MQTT_PORTNR0,
    MQTT_SECURE0,
    MQTT_URL1,
    MQTT_PORTNR1,
    MQTT_SECURE1,

#ifdef CONFIG_PLATFORM_IDEFSVCTESTDATA
    SVCDATATEST_TIMESTAMPA,
    SVCDATATEST_TIMESTAMPB,
    SVCDATATEST_TIMESTAMPC,
    SVCDATATEST_BOOL,
    SVCDATATEST_BYTE,
    SVCDATATEST_SBYTE,
    SVCDATATEST_INT16,
    SVCDATATEST_UINT16,
    SVCDATATEST_INT32,
    SVCDATATEST_UINT32,
    SVCDATATEST_INT64,
    SVCDATATEST_UINT64,
    SVCDATATEST_SINGLE,
    SVCDATATEST_DOUBLE,
    SVCDATATEST_STRING,
    SVCDATATEST_DATETIME,
    SVCDATATEST_UINT32A,
    SVCDATATEST_UINT32ABIG,
#endif

    // MR: short for Measurement Record (see parameter table
    MR_Timestamp,
    MR_Energy_Remaining,
    MR_Is_Move_Detect_Meas,
    MR_Temperature_Pcb,
    MR_Temperature_External,
    MR_GNSS_Lat_1,
    MR_GNSS_NS_1,
    MR_GNSS_Long_1,
    MR_GNSS_EW_1,
    MR_GNSS_Speed_To_Rotation_1,
    MR_GNSS_Course_1,
    MR_GNSS_Time_To_Fix_1,
    MR_Is_Speed_Within_Env,
    MR_GNSS_Speed_To_Rotation_2,
    MR_Rotation_Diff,
    MR_GNSS_Time_Diff,
    MR_GNSS_Sat_Id,
    MR_GNSS_Sat_Snr,
    MR_Is_Good_Speed_Diff,

    MR_Timestamp_Env3,
    MR_Acceleration_Env3,
    MR_Timestamp__Wheel_Flat_Detect,
    MR_Acceleration_Wheel_Flat_Detect,
    MR_Timestamp_Raw,
    MR_Acceleration_Raw,

//  CR: Communications Record

    CR_Com_timestamp,
    CR_IMEI,
    CR_ICCID,
    CR_Wakeup_Reason,

    CR_Energy_Remaining,
    CR_Com_Signal_Quality,
    CR_Is_Last_Com_Successful,
    CR_Is_Move_Detect_Com,
    CR_Acceleration_X,
    CR_Acceleration_Y,
    CR_Acceleration_Z,
    CR_Is_Self_Test_Successful,
    CR_Status_Self_Test,
    CR_Humidity,
    CR_Up_Time,
    CR_GNSS_Lat_Com,
    CR_GNSS_NS_Com,
    CR_GNSS_Long_Com,
    CR_GNSS_EW_Com,
    CR_GNSS_Speed_To_Rotation_Com,
    CR_Time_Last_Wake,
    CR_Time_Elap_Registration,
    CR_Time_Elap_TCP_Connect,
    CR_Time_Elap_TCP_Disconnect,
    CR_Time_Elap_Modem_On,
    CR_V_0,
    CR_V_1,
    CR_V_2,
    CR_E_GNSS_Cycle,
    CR_E_Modem_Cycle,
    CR_E_Previous_Wake_Cycle,

// SR: Schedule record

    SR_Schedule_ID,
    SR_Is_Raw_Acceleration_Enabled,
    SR_Url_Server_1,
    SR_Url_Server_2,
    SR_Url_Server_3,
    SR_Url_Server_4,
    SR_Temperature_Alarm_Upper_Limit,
    SR_Temperature_Alarm_Lower_Limit,
    SR_Min_Hz,
    SR_Max_Hz,
    SR_Allowed_Rotation_Change,
    SR_Sample_Rate_Bearing,
    SR_Samples_Bearing,
    SR_Sample_Rate_Raw,
    SR_Samples_Raw,
    SR_Sample_Rate_Wheel_Flat,
    SR_Samples_Wheel_Flat,
    SR_Time_Start_Meas,
    SR_Retry_Interval_Meas,
    SR_Max_Daily_Meas_Retries,
    SR_Required_Daily_Meas,
    SR_Upload_Time,
    SR_Is_Verbose_Logging_Enabled,
    SR_Retry_Interval_Upload,
    SR_Max_Upload_Retries,
    SR_Upload_repeat,
    SR_Number_Of_Upload,
    SR_Acceleration_Threshold_Movement_Detect,
    SR_Acceleration_Avg_Time_Movement_Detect,
    SR_Is_Power_On_Failsafe_Enabled,
    SR_Is_Moving_Gating_Enabled,
    SR_Is_Upload_Offset_Enabled,
    SR_confirm_upload,
    SR_Scaling_Bearing,
    SR_Scaling_Raw,
    SR_Scaling_Wheel_Flat,


// AR: asset ID configuration record

    AR_Train_Operator,
    AR_Train_Fleet,
    AR_Train_ID,
    AR_Vehicle_ID,
    AR_Vehicle_Nickname,
    AR_Bogie_Serial_Number,
    AR_Wheelset_Number,
    AR_Wheelset_Serial_Number,
    AR_Is_Wheelset_Driven,
    AR_Wheel_Serial_Number,
    AR_Wheel_Side,
    AR_Wheel_Diameter,
    AR_Axlebox_Serial_Number,
    AR_Bearing_Brand_Model_Number,
    AR_Bearing_Serial_Number,
    AR_Sensor_Location_Angle,
    AR_Sensor_Orientation_Angle,
    AR_Train_Name,
    AR_Bogie_Number_In_Wagon,


	ADC_SETTLE_MIN,
	ADC_SETTLE_MAX,
	ADC_SETTLE_TIME,
} ProjectObjects_t;

//
typedef enum {
	PROGRAM_FLASH,
	CONFIG_FLASH,// MCU internal storage of configuration data
	CONFIG_RAM,// ram copy of config
	INT_RAM, // internal (on chip) processor RAM
	EXT_FLASH
} memId_t;

#define MEMID2STRING(dd)  \
		  (dd == PROGRAM_FLASH) ? "PROGRAM_FLASH" : \
		  (dd == CONFIG_FLASH)  ? "CONFIG_FLASH " : \
          (dd == CONFIG_RAM)    ? "CONFIG_RAM   " : \
		  (dd == INT_RAM)       ? "INT_RAM      " : \
		  (dd == EXT_FLASH)     ? "EXT_FLASH    " : \
				  "Unknown"

extern const DataDef_t sDataDef[] ;
/*
 * Functions
 */
uint32_t getNrDataDefElements();
bool copyBytesRamToStore(const DataDef_t * dst_info, uint8_t byteOffset, uint8_t *ram_p, uint32_t count, bool su);
bool copyBytesStoreToRam(uint8_t *ram_p, const DataDef_t * src_info, uint32_t byteOffset,  uint32_t count);


#endif /* SOURCES_DATASTORE_CONFIGDATA_H_ */


#ifdef __cplusplus
}
#endif