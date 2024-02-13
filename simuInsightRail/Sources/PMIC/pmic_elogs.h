#ifdef __cplusplus
extern "C" {
#endif

/*
 * pmic_elogs.h
 *
 *  Created on: 17 Jan 2020
 *      Author: KC2663
 */

#ifndef SOURCES_PMIC_PMIC_ELOGS_H_
#define SOURCES_PMIC_PMIC_ELOGS_H_

typedef enum LogEventType
{
	eLOG_ERROR_NONE,								   	//Node error detected
	eLOG_ERROR_TEMPERATURE_READ_FAILED,                	//Failed to read temperature
	eLOG_INFO_SELFTEST_NFC_EVENT,                      	//Selftest NFC Event has been requested
	eLOG_INFO_DECOMMISSION_NFC_EVENT,				   	//Decommission NFC Event has been requested
	eLOG_WARN_ENTERING_SHIP_MODE,				       	//Node entering shipping mode
	eLOG_WARN_ENTERING_CMSD_MODE,			           	//Node entering commissioned mode
	eLOG_INFO_BOOT,                                    	//Node booting up after first power on or reset
	eLOG_RTC_WAKEUP_POSTPONED,						   	//RTC wakeup detected but processing wakeup postponed until highier energy capacity
	eLOG_HARD_FAULT_DETECTED,						   	//Hard fault detected
	eLOG_TEMPERATURE_DATA_CRC_FAILURE,					//CRC failure on temperature data read back from ext flash
	eLOG_COUNTER_SETUP_FAIL_DETECTED,					//Fail to setup / disable external counter detected.
	eLOG_SHUTDOWN_MK24_MSG_NOT_RCVD,					//Fail to Shutdown MK24, Shutdown Msg not Rcvd.
	eLOG_FAILED_NFC_WRITE,								//Failure while writing NDEF
	eLOG_PMIC_RTC_RECOVERY_EVENT,						//PMIC RTC recovery kicked in.
	eLOG_PMIC_COMMS_ERROR,								//PMIC & MK24 communication error
	eLOG_TEMPERATURE_DATA_ERASE_FAILURE,				//Failed to erase flash during temp data storage
	eLOG_ERROR_TEMPERATURE_READ_FROM_FLASH_FAILED,		//Error while reading from external flash
	eLOG_ERROR_TEMPERATURE_WRITE_TO_FLASH_FAILED,		//Error while writing to external flash
	eLOG_ERROR_TEMPERATURE_LOG_SEND_FAILED,				//Error while sending temperature Logs
	eLOG_ERROR_ADC_PROBLEM,								//Error during ADC operation
	eLOG_FAILED_EN_RECORD_SETUP,						//Failure while setting up energy record
	eLOG_INFO_EN_RECORD_RETRIEVED,						//Restored energy record from backup
	eLOG_INFO_EN_RECORD_NO_BACKUP,						//No valid backup found, saved initialised record

	eLOG_MAX //Don't add error enum values passed this point
} LOG_EVENT_TYPE;

typedef struct LogEventCodeMap
{
	LOG_EVENT_TYPE eLogEventCode;
	char* pLogEventString;
} LOG_EVENT_CODE_MAP;

static const LOG_EVENT_CODE_MAP g_logEventMap[] =
{
		{eLOG_ERROR_NONE, "No Error"},
		{eLOG_ERROR_TEMPERATURE_READ_FAILED, "Temperature Read Failed"},
		{eLOG_INFO_SELFTEST_NFC_EVENT, "Selftest NFC Event detected"},
		{eLOG_INFO_DECOMMISSION_NFC_EVENT, "Decommission NFC Event detected"},
		{eLOG_WARN_ENTERING_SHIP_MODE, "Node entered ship mode"},
		{eLOG_WARN_ENTERING_CMSD_MODE, "Node entered commissioned mode"},
		{eLOG_INFO_BOOT, "PMIC Boot detected"},
		{eLOG_RTC_WAKEUP_POSTPONED, "RTC wake-up postponed until higher capacity available"},
		{eLOG_HARD_FAULT_DETECTED, "Hard fault detected"},
		{eLOG_TEMPERATURE_DATA_CRC_FAILURE,"CRC failure on temperature logs read"},
		{eLOG_COUNTER_SETUP_FAIL_DETECTED, "External Counter Failure"},
		{eLOG_SHUTDOWN_MK24_MSG_NOT_RCVD, "MK24 Shutdown Msg not Rcvd"},
		{eLOG_FAILED_NFC_WRITE,	"Failure while writing NDEF"},
		{eLOG_PMIC_RTC_RECOVERY_EVENT, "PMIC RTC recovery kicked in"},
		{eLOG_PMIC_COMMS_ERROR, "PMIC to MK24 communication error"},
		{eLOG_TEMPERATURE_DATA_ERASE_FAILURE, "Failed to erase flash during temperature data storage"},
		{eLOG_ERROR_TEMPERATURE_READ_FROM_FLASH_FAILED, "Error while reading from external flash"},
		{eLOG_ERROR_TEMPERATURE_WRITE_TO_FLASH_FAILED, "Failed to write temperature logs to flash"},
		{eLOG_ERROR_TEMPERATURE_LOG_SEND_FAILED, "Failed to send temperature logs"},
		{eLOG_ERROR_ADC_PROBLEM, "Error in ADC"},
		{eLOG_FAILED_EN_RECORD_SETUP, "Failed to reset energy record"},
		{eLOG_INFO_EN_RECORD_RETRIEVED,	"Energy read from flash backup"},
		{eLOG_INFO_EN_RECORD_NO_BACKUP,	"Failed to read Energy from flash backup: saved init record"}
};

char* PmicElog_getLogMessage(LOG_EVENT_TYPE eLogEventCode)
{
	return g_logEventMap[eLogEventCode].pLogEventString;
}

#endif /* SOURCES_PMIC_PMIC_ELOGS_H_ */


#ifdef __cplusplus
}
#endif