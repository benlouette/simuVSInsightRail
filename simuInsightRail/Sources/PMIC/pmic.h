#ifdef __cplusplus
extern "C" {
#endif

/*
 * pmic.h
 *
 *  Created on: 13 Feb 2019
 *      Author: RZ8556
 */

#ifndef SOURCES_PMIC_PMIC_H_
#define SOURCES_PMIC_PMIC_H_

#include <stdint.h>
#include <stdbool.h>

#include "fsl_uart_hal.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "fsl_rtc_hal.h"
#include "Eventlog.h"
#include "NvmConfig.h"
#include "configData.h"


#ifdef _MSC_VER
#define PACKED_STRUCT_START __pragma(pack(push, 1))
#define PACKED_STRUCT_END   __pragma(pack(pop))
#else
#define PACKED_STRUCT_START
#define PACKED_STRUCT_END   __attribute__((packed))
#endif

#define SOH (0x01)
#define STX (0x02)
#define ETX (0x03)
#define EOT (0x04)

#define FIRMWARE_VERSION			(0)
#define MK24_PMIC_PROTOCOL_VERSION	(0)

#define MK24_PMIC_PROTOCOL_OVERHEAD (6)
#define MK24_PMIC_MESSAGE_OVERHEAD	(2)
#define MK24_PMIC_PAYLOAD (MK24_PMIC_PROTOCOL_OVERHEAD+MK24_PMIC_MESSAGE_OVERHEAD-3)

#define MK24_PMIC_MESSAGE_SIZE      (256)
#define MK24_PMIC_MAX_MESSAGE_PAYLOAD (MK24_PMIC_MESSAGE_SIZE-(MK24_PMIC_PROTOCOL_OVERHEAD+MK24_PMIC_MESSAGE_OVERHEAD))

#define TLI_TOTAL_CAPACITY_MAH		(430) // 430mAh
#define MAX_TEMPERATURE_READINGS_PER_MESSAGE 	(100)

/*
 * Message identifiers from PMIC->MK24
 */
#define PMIC_STATUS_ID							(1)
#define PMIC_TEST_RESULT_ID						(2)
#define PMIC_TEMPERATURE_LOG_ID					(3)
#define	PMIC_TEMPERATURE_ALARM_ID				(4)
#define PMIC_METADATA_ID						(5)
#define PMIC_SELFTEST_RESULT_ID					(6)
#define PMIC_NDEF_DATA_ID						(7)
#define PMIC_REQUEST_MK24_INFO					(8)
#define PMIC_CAPACITY_REM_INFO_ID				(9)
#define PMIC_EVENT_LOG							(10)
#define PMIC_DUMMY_ID							(11)
#define	PMIC_DUMP_ID							(12)
#define PMIC_UPDATE_RTC_ID						(13)
#define PMIC_ENERGY_INFO_ID						(14)
#define PMIC_STORE_ENERGY_USE_ID				(15)

/*
 * Message identifiers from MK24->PMIC
 */
#define MK24_POWERUP_STATUS_ID					(101)
#define MK24_PMIC_TEST_COMMAND_ID				(102)
#define MK24_POWERDOWN_STATUS_ID				(103)
#define	MK24_UPDATE_PARAM_ID					(104)
#define	MK24_REQUEST_PMIC_METADATA_ID			(105)
#define	MK24_RUN_SELFTEST_ID					(106)
#define	MK24_NDEF_RECORDS_ID					(107)
#define	MK24_READ_NDEF_RECORDS_ID				(108)
#define	MK24_REQUEST_CAPACITY_REM_INFO_ID		(109)
#define MK24_LOG_RECEIVED_ACK_ID				(110)
#define MK24_TEMP_RECEIVED_ACK_ID				(111)
#define	MK24_REQUEST_DUMP_ID					(112)
#define MK24_SELFTEST_RESULTS_ID				(113)
#define MK24_REQUEST_ENERGY_ID					(114)
#define MK24_STORE_ENERGY_USE_ID				(115)

#define	PMIC_STORE_ENERGY_USE_ERR 0
#define	PMIC_STORE_ENERGY_USE_SUCCESSFUL 1

typedef enum
{
	PARAMS_TEMP_ALARMS,
	PARAMS_MEMS,
	PARAMS_RTC,
	PARAMS_SELFTEST,
    PARAMS_MOTION_DETECT,
	PARAMS_CONFIG  // temp_alarms + mems + selftest + motion_detect
} ePMIC_UPDATE_PARAMS;

PACKED_STRUCT_START
struct  sGnssSatInfo
{// struct layout is chosen because of the existing passenger rail reporting way.
    uint16_t numsat;
    uint8_t id[MAX_GNSS_SATELITES];
    uint8_t snr[MAX_GNSS_SATELITES];
};
PACKED_STRUCT_END
PACKED_STRUCT_START
typedef struct 
{
    // "new" control logic related
    uint8_t is_gnss_mod_on;
    uint8_t is_gnss_fix_good;
    float hdop;
    float utc_time;
    uint32_t utc_date;

    float GNSS_Lat_1;
    uint8_t GNSS_NS_1[MAXCARDINALDIRECTIONLENGTH+1];
    float GNSS_Long_1;
    uint8_t GNSS_EW_1[MAXCARDINALDIRECTIONLENGTH+1];
    float GNSS_Speed_To_Rotation_1;
    uint16_t GNSS_Course_1;
    uint32_t time_to_first_accurate_fix_ms;
//    bool Is_Speed_Within_Env;
//    float GNSS_Speed_To_Rotation_2;
//    float Rotation_Diff;
//    float GNSS_Time_Diff;
//    //bool Is_Good_Meas;
//    bool Is_Good_Speed_Diff;
    struct sGnssSatInfo gpsSat;
    struct sGnssSatInfo glonassSat;
} PmicGnssStatus_t;
PACKED_STRUCT_END
PACKED_STRUCT_START
typedef struct 
{
    uint8_t isGatingEnabled;
} motionDetect_t;
PACKED_STRUCT_END
PACKED_STRUCT_START
typedef struct 
{
	ePMIC_UPDATE_PARAMS group;

	union
    {
        struct
		{
			uint32_t time_s;
			uint32_t alarm_s;
		} rtc;
        struct
        {

				PACKED_STRUCT_START
            struct 
			{
				uint8_t lowerLimit;
				uint8_t upperLimit;
			} tempAlarms;
				PACKED_STRUCT_END
					PACKED_STRUCT_START
            struct 
			{
				uint32_t threshold_mg;
				uint32_t duration_s;
			} mems;
				PACKED_STRUCT_END
					PACKED_STRUCT_START
            struct 
			{
				uint8_t tempDelta;
			} selftest;
				PACKED_STRUCT_END

            motionDetect_t motionDetect;
        } config;
    } params;
} params_t;
PACKED_STRUCT_END
PACKED_STRUCT_START
typedef struct 
{
    uint32_t nTimestamp_secs;                         //UTC time in seconds of the event
    tEventLogSevlvl severity;                         //Severity of event.
    uint8_t nEventCode;                               //Event code mapped to string on mk24 side
    char nLogMessage[120];                         //Variable length optional string
} PMIC_ErrorLog;
PACKED_STRUCT_END

typedef enum
{
	PMIC_WAKE_NONE,
	PMIC_WAKE_RTC, 	// RTC wakeup measurement/upload cycle (inherently schedules next wakeup)
	PMIC_WAKE_FULL_SELFTEST,  // Selftest wakeup + upload + schedule next wakeup
	PMIC_WAKE_RTC_SYNC,	// Synchronise PMIC and MK24 RTCs + schedule next wakeup
	PMIC_WAKE_TEMP_ALARM, // Temperature alarm, alarm triggered seperately
	PMIC_WAKE_DAILY_SELFTEST_REDUCED, // Daily selftest, No GPS Fix
	PMIC_WAKE_ENG,		// Engineering wakeup, wait for user input
	PMIC_WAKE_RESET_RTC	//Wakeup to reset the MK24 RTC Wakeup line
} ePMIC_WAKE_REASONS;

#define MK24_RESET_CM_TMP_ALARM_PAYLOAD_BYTE (1)

typedef enum
{
	CM_TEMPERATURE_LOW,
	CM_TEMPERATURE_OK,
	CM_TEMPERATURE_AMBER,
	CM_TEMPERATURE_RED,
	CM_TEMPERATURE_HIGH
} E_CM_temperature_alarm_state_t;

typedef enum
{
	PCB_TEMPERATURE_LOW = CM_TEMPERATURE_HIGH + 1,
	PCB_TEMPERATURE_OK,
	PCB_TEMPERATURE_HIGH
} E_PCB_temperature_alarm_state_t;


typedef enum
{
	TEMPERATURE_NO_ALARM,
	TEMPERATURE_ALARM_RE_ARM,
	TEMPERATURE_ALARM_RETRY
} E_CM_temperature_alarm_send_state_t;


/*
 * This is packed so that protocol send code can handle it.
 * sequence_number is used where too many readings to send in one message
 */

PACKED_STRUCT_START
typedef struct 
{
	uint32_t nTimestamp;
	uint8_t sequence_number;
	uint8_t reading_count;
	uint16_t timer_interval_seconds;
	uint16_t temperatures[MAX_TEMPERATURE_READINGS_PER_MESSAGE];
} TemperatureLog_t;
PACKED_STRUCT_END
PACKED_STRUCT_START
typedef struct 
{
    uint8_t high;
    uint8_t low;
} TemperatureReading_t;
PACKED_STRUCT_END
PACKED_STRUCT_START
typedef struct 
{
	TemperatureReading_t remote;
	TemperatureReading_t local;
} TemperatureData_t;
PACKED_STRUCT_END
PACKED_STRUCT_START
/*
 * Structure to match message from PMIC
 */
typedef struct 
{
	uint8_t cm_alarm;
	uint8_t pcb_alarm;
	TemperatureData_t stcTemperatureData;
} PMIC_TemperatureAlarm_t;

/*
 * Structure to match message from PMIC
 */
PACKED_STRUCT_END
PACKED_STRUCT_START
typedef struct 
{
	uint16_t voltage_mV;
	uint16_t lowestVoltage_mV;
	uint32_t highestCurrent_uA;
	uint32_t energy_Total_mJ;
	uint64_t energy_this_run_nWh;
} Mk24EnergyResult_t;
PACKED_STRUCT_END



typedef enum {
  TLI_STATE_Unknown,	// TLI < 2.7V
  TLI_STATE_CCL,		// TLI >= 2.7V & TLI < 3.4V
  TLI_STATE_LPL,		// TLI >= 3.4V & TLI < 3.7V
  TLI_STATE_NOL,		// TLI >= 3.7V
} E_TLI_State;

typedef enum
{
	SENSOR_COMMISSIONED = 1,	// commissioned: sensor should enter timed mode after power on
	SENSOR_SHIP_MODE			// sensor should return to ship mode after power on
} ePMIC_SensorMode;

PACKED_STRUCT_END
PACKED_STRUCT_START
typedef struct 
{
    int16_t x;
    int16_t y;
    int16_t z;
} tLis3dAccel;
PACKED_STRUCT_END
PACKED_STRUCT_START
typedef struct 
{
    uint8_t				version;			// version # for firmware
    ePMIC_WAKE_REASONS	wakeReason;			// @ePMIC_WAKE_REASONS
    bool				motionDetected;		// true=moving
    E_TLI_State			PMICState;			// @E_TLI_State
    int16_t				dummyData;			// old Temperature data size, unused dummyData for now
    uint16_t			capacity;			// battery capacity
    uint16_t			voltage;			// battery voltage
    uint16_t			harvesterDutyCycle;	// duty cycle
    ePMIC_SensorMode	PMICMode;			// ship mode or commissioned
    TemperatureData_t	temperatureData;	// temperature
    uint32_t			rtcSeconds;			// PMIC RTC
    uint8_t				rtcStatus;			// If not equal to 0x07 RTC may be invalid
    uint32_t			energyUsed_mJ;		// Total energy used (mJ). Battery is maximum of 10^5 J = 10^8 mJ so fits in 2^32 (approx 4x10^9)
    PmicGnssStatus_t    gnssStatus;         // GNSS status, if started by PMIC
} PMIC_Status_t;

PACKED_STRUCT_END

typedef struct
{
	uint32_t			rtcSeconds;			// PMIC RTC
	uint8_t				rtcStatus;			// Status of PMIC RTC: 0x07 is good
} PMIC_RTCstatus_t;

#define RTC_CLOCK_STATUS_RUNNING 	(1)
#define RTC_CLOCK_STATUS_ACCURATE 	(2)
#define RTC_CLOCK_STATUS_YEAR_VALID (4)

#define PMIC_RTC_IS_GOOD (RTC_CLOCK_STATUS_RUNNING | RTC_CLOCK_STATUS_ACCURATE | RTC_CLOCK_STATUS_YEAR_VALID)

#define IMEI_LENGTH (MODEM_IMEI_LEN)
#define MODEL_LENGTH (7+1) // e.g. "CMWR 31"
#define MANUFACTURE_DATE_LENGTH (6+1) // e.g. "200728"
PACKED_STRUCT_END
PACKED_STRUCT_START
typedef struct 
{
	uint8_t hardwareVersion;
	char imei[IMEI_LENGTH];
	char model[MODEL_LENGTH];
	char manufactureDate[MANUFACTURE_DATE_LENGTH];
	uint32_t bootloaderVersion;
	uint32_t applicationVersion;
}systemInfo_t;

PACKED_STRUCT_END
PACKED_STRUCT_START
typedef struct 
{
    bool				bSelfTestPassed;							// Did Self test pass
    uint8_t				bSelfTestCodeCount;							// Number of self test codes added
    bool				isMovementDetected;							// Was movement detected
    tLis3dAccel			memsAxes;									// MEMS x,y,z data
    uint16_t    		nTLIVoltage_mV;								// TLI Voltage mV
    TemperatureData_t   temperatureData;							// Collected temperature data
    uint8_t				selfTestCodes[MAXSTATUSSELFTESTLENGTH];		// Array of self test codes
} SelfTestData;
PACKED_STRUCT_END
PACKED_STRUCT_START
typedef struct
{
	bool     	bSelfTestPassed;
	uint8_t 	nSelfTestCodeCount;
    uint8_t		selfTestCodes[MAXSTATUSSELFTESTLENGTH];	//Array of self test codes
} Mk24SelfTestResults;
PACKED_STRUCT_END

typedef bool (*tPMICMsgHandlerFuncPtr)(uint8_t *payload, uint16_t bytes);

void PMIC_InitCLI();
size_t PMIC_formatCommand(int cmd_id, uint8_t *out_buf, size_t out_buf_len, uint8_t *in_buf, size_t in_buf_len);
uart_status_t PMIC_SendCommand(uint8_t *pBytes, uint8_t NumBytes);
bool PMIC_StartPMICtask();
void PMIC_UpdateParamsInCommsRecord(void);
void PMIC_Powerdown();
void PMIC_sendMK24TempAlarmLimitsUpdateMessage();
void PMIC_sendMK24MetadataReqMessage();
const char* PMIC_getMetadata(void);
void PMIC_sendMK24SelfTestRunMessage();
void PMIC_SendMK24PowerUpStatus();
QueueHandle_t PMIC_GetSelfTestQueueHandle();
QueueHandle_t PMIC_GetNDEF_QueueHandle();
bool PMIC_IsMetadataRcvd();
char* PMIC_GetMetadataResult();
bool PMIC_GetEnergyUpdatedFlag();
void PMIC_SendEnergyReqMsg();
Mk24EnergyResult_t* PMIC_GetEnergyData();
bool PMIC_ProgImage(bool, int);
void PMIC_SendCapacityRemReqMsg();
bool PMIC_IsCapacityRemInfoRcvd(uint16_t *pSrcBuf);
void PMIC_WriteNdefFromCurrentMode();
void PMIC_sendMK24ParamUpdateMessage(params_t stcParams);
bool PMIC_checkTemperatureAlarm(PMIC_TemperatureAlarm_t stcPmicTempAlarmMsg);
void PMIC_SendDumpReqMsg();
bool PMIC_IsDumpRcvd();
void PMIC_SendMk24STresults(uint32_t nSelfTestCodeCount, bool bSelfTestPassed);
void PMIC_processPMICSelfTestResult();
bool PMIC_GetSelftestUpdatedFlag();
bool PMIC_GetSelftestSuccessFlag();
void PMIC_SendSelfTestReqMsg();         // used primarily for task run app 98
PMIC_RTCstatus_t PMIC_GetPMICrtc();
void PMIC_Reboot();
bool PMIC_IsTLIVoltageGoodToStartOTA();
void PMIC_ScheduleConfigUpdate();
bool PMIC_ConfigUpdateScheduled();
PMIC_Status_t* PMIC_GetPMICStatus(void);
uint32_t PMIC_sendStoreEnergyUse( void );

#endif /* SOURCES_PMIC_PMIC_H_ */


#ifdef __cplusplus
}
#endif