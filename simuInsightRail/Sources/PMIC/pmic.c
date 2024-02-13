#ifdef __cplusplus
extern "C" {
#endif

/*
 * pmic.c
 *
 *  Created on: 13 Feb 2019
 *      Author: RZ8556
 */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "fsl_misc_utilities.h"
#include "fsl_uart_driver.h"
#include "fsl_rtc_hal.h"
#include "fsl_rtc_driver.h"
#include "fsl_os_abstraction_free_rtos.h"

#include "xTaskApp.h"
#include "xTaskDevice.h"
#include "Vbat.h"

#include "printgdf.h"
#include "PinDefs.h"
#include "PinConfig.h"
#include "i2c.h"
#include "extFlash.h"
#include "selfTest.h"
#include "Measurement.h"
#include "convert_junit.h"
#include "PMIC_UART.h"
#include "pmic.h"
#include "NvmData.h"
#include "NvmConfig.h"
#include "temperature.h"
#include "schedule.h"
#include "json.h"
#include "nfc.h"
#include "configBootloader.h"
#include "xTaskAppOta.h"
#include "xTaskAppCommsTest.h"
#include "image.h"
#include "boardSpecificCLI.h"
#include "Eventlog.h"
#include "configSvcData.h"
#include "pmic_elogs.h"
#include "SSD_FTFx.h"// to access the macro PGM_SIZE_BYTE
#include "extflash.h"
#include "rtc.h"
#include "json.h"
#include "CLIcmd.h"
#include "rtc.h"
#include "utils.h"

// 1 day will typically be two messages
#define PAYLOAD_RED_AMBER_TEMP_ALARM_NUM_BYTES	(2)		// 1 Byte for Red and 1 for Amber.
#define SHUTDOWN_MSG_PAYLOAD_LENGTH_BYTES		(3)		// 1 Byte for Engineering Mode status, 2nd for clearing the CM Alarms, 3rd for next RTC wakeup task
#define MK24_EXIT_ENGG_MODE_PAYLOAD_BYTE		('0')	// Value send to PMIC to indicate exit engineering mode.
#define FLASH_RETRIES							(3)

#define TLI_VOLTAGE_HIGH						(3.7f)

/*
 * Return codes for the temperature ACK. (eMK24_TEMP_RECEIVED_ACK_ID)
 */
typedef enum {
    MK24_TEMP_RECEIVED_ACK_RETURN_CODE__NO_ACK_RECEIVED = -1,
    MK24_TEMP_RECEIVED_ACK_RETURN_CODE__OK = 0,
    MK24_TEMP_RECEIVED_ACK_RETURN_CODE__NO_SPACE_LEFT = 1,
    MK24_TEMP_RECEIVED_ACK_RETURN_CODE__WRITE_FAILED = 2,
    MK24_TEMP_RECEIVED_ACK_RETURN_CODE__FAILED_TO_GET_TEMP_REC_CNT = 3,
} TemperatureReceivedAckReturnCode_t;

// Represents the last comms status. True indicates the last comms was successful.
extern bool lastCommsOk;
extern uint8_t nodeTaskThisCycle;
extern report_t gReport;
extern tNvmData gNvmData;

#define METADATA_SIZE (0x7F0)
// we allocate 256 more than strictly necessary in case metadata overruns
static char meta_data_buffer[METADATA_SIZE + 256];
static volatile bool m_bIsMetaDataValid = false;
static uint32_t m_nMetaDataRecvIdx = 0;
static const char* k_pstrEmptyMetaData = "{}";

static SelfTestData m_PMICselfTestData;
static bool m_bSelfTestUpdated = false;

static Mk24EnergyResult_t m_PMICenergyData = {.voltage_mV = 0, .lowestVoltage_mV = 0, .highestCurrent_uA = 0, .energy_Total_mJ = 0, .energy_this_run_nWh = 0};
static bool m_bEnergyUpdated = false;

static bool m_bPmicStatusMsgRx = false;	// PMIC has communicated the status to MK24. It's assumed that only 1 msg is needed so this variable is never reset to false. If assumption changes, need to modify code to reflect that.
static PMIC_Status_t m_stcPmicStatusMsg = {.wakeReason = 0};
static QueueHandle_t PMICMetadataQueue = NULL;
static QueueHandle_t PMICCapacityRemDataQueue = NULL;
static QueueHandle_t PMICDumpQueue = NULL;

static void handleWakeReason(uint8_t wakeReason);
static void PMIC_SendMetadataReqMsg();
static void SendEventLogAck(uint8_t nRemainingLogEntryCount);
static void handleTemperatureLog(TemperatureLog_t* pTemperatureLog);
static void handleEventLog(PMIC_ErrorLog* stcPmicErrorLog);
static void handleUpdateRTC(const uint32_t*);
static void SendTempLogsAck(TemperatureReceivedAckReturnCode_t return_code);
static float PMIC_ConvertToTemperature(int, int);

static bool pmic_CLI(uint32_t argc, uint8_t * argv[], uint32_t * argi);
static bool pmicProg_CLI(uint32_t args, uint8_t * argv[], uint32_t * argi);
static bool PmicBackup_CLI(uint32_t args, uint8_t * argv[], uint32_t * argi);
static bool pmicProgHelp(uint32_t argc, uint8_t * argv[], uint32_t * argi);
static bool pmicBackupHelp(uint32_t argc, uint8_t * argv[], uint32_t * argi);
void handlePmicStoreEnergyUse( uint32_t *buf );

// Map the PMIC wake reason to MK24 wake reason.
static WAKEUP_CAUSES_t GetMK24WakeReason(ePMIC_WAKE_REASONS ePmicWake)
{
	WAKEUP_CAUSES_t eMK24WakeReason = WAKEUP_CAUSE_UNKNOWN;
	switch(ePmicWake)
	{
		case PMIC_WAKE_RTC:
			eMK24WakeReason = WAKEUP_CAUSE_RTC;
		break;

		case PMIC_WAKE_TEMP_ALARM:
			eMK24WakeReason = WAKEUP_CAUSE_THERMOSTAT;
		break;

		case PMIC_WAKE_FULL_SELFTEST:
			eMK24WakeReason = WAKEUP_CAUSE_FULL_SELFTEST;
		break;

		case PMIC_WAKE_DAILY_SELFTEST_REDUCED:
			eMK24WakeReason = WAKEUP_CAUSE_SELFTEST_REDUCED;
		break;

		default:
			LOG_EVENT(0, LOG_LEVEL_PMIC, ERRLOGFATAL,"Unmapped PMIC Wake event: %d\n", ePmicWake);
		break;
	}

	return eMK24WakeReason;
}

/**
 * @param cmd_id		The command ID to use in the packet.
 * @param out_buf		A buffer to store the packet in.
 * @param out_buf_len	The size of out_buf in bytes.
 * @param in_buf		Payload data.
 * @param in_buf_len	Number of bytes of payload data.
 * @return				The number of bytes stored in out_buf.
 */
size_t PMIC_formatCommand(int cmd_id, uint8_t *out_buf, size_t out_buf_len, uint8_t *in_buf, size_t in_buf_len)
{
	const size_t packet_size = in_buf_len + MK24_PMIC_PROTOCOL_OVERHEAD + MK24_PMIC_MESSAGE_OVERHEAD;

	if(out_buf_len < packet_size)
	{
		LOG_EVENT(0, LOG_LEVEL_PMIC, ERRLOGFATAL,
				"PMIC_formatCommand: Buffer to small. cmd_id=%d, out_buf_len=%d: packet_size=%d\n",
				cmd_id, out_buf_len, packet_size);
	}

	uint8_t crc = 0;

	out_buf[0] = SOH;
	out_buf[1] = packet_size;
	out_buf[2] = STX;

	// this is the payload
	out_buf[3] = MK24_PMIC_PROTOCOL_VERSION;
	out_buf[4] = cmd_id;
	// copy the payload buffer it is there
	if(NULL != in_buf)
	{
		memcpy(&out_buf[(MK24_PMIC_PROTOCOL_OVERHEAD-3) + MK24_PMIC_MESSAGE_OVERHEAD], in_buf, in_buf_len);
	}

	// mark end of payload
	out_buf[(MK24_PMIC_PROTOCOL_OVERHEAD-3) + MK24_PMIC_MESSAGE_OVERHEAD + in_buf_len] = ETX;
	for(int i = 0; i < in_buf_len + (MK24_PMIC_PROTOCOL_OVERHEAD-2) + MK24_PMIC_MESSAGE_OVERHEAD; i++)
	{
	  crc ^= out_buf[i];
	}
	out_buf[in_buf_len + (MK24_PMIC_PROTOCOL_OVERHEAD-2) + MK24_PMIC_MESSAGE_OVERHEAD] = crc;
	out_buf[in_buf_len + (MK24_PMIC_PROTOCOL_OVERHEAD-1) + MK24_PMIC_MESSAGE_OVERHEAD] = EOT;

	return packet_size;
}

/*
 * PMIC_SendCommand
 *
 * @desc    Send bytes to the PMIC comms UART
 *          Blocks until complete, although UART_DRV_SendData() seems to queue
 *          data, so if the queue isn't full then will return quickly.
 *
 * @param   pBytes - pointer to the bytes to send
 * 			NumBytes - how many to send
 *
 * @returns UART status (we expect kStatus_UART_Success)
 */
uart_status_t PMIC_SendCommand(uint8_t *pBytes, uint8_t NumBytes)
{
    uart_status_t UARTStatus = kStatus_UART_Fail;
    uint32_t bytesRemaining;
    uint32_t UARTInstance = PMIC_UART_getUartInstance();

    LOG_DBG( LOG_LEVEL_PMIC,"MK24->PMIC, Send Cmd: %d, Payload:%d,%d\n", pBytes[4], pBytes[5], pBytes[6]);
    for(int i=0; i<NumBytes; i++)
    {
    	UARTStatus = UART_DRV_SendData(UARTInstance, pBytes++, 1);

    	bytesRemaining = NumBytes;
    	while(bytesRemaining > 0)
    	{
    		UART_DRV_GetTransmitStatus(UARTInstance, &bytesRemaining);
    	}

    	OSA_TimeDelay(2);
    }

    return UARTStatus;
}

#include "task.h"
#include <xTaskDefs.h>

QueueHandle_t PMICQueue = NULL;
QueueHandle_t PMIC_NDEFQueue = NULL;

/*
 * PMIC_GetNDEF_QueueHandle
 *
 * @desc    Returns the PMIC NDEF Queue handle
 *
 * @param
 *
 * @return - Pmic NDEF queue handle
 */
QueueHandle_t PMIC_GetNDEF_QueueHandle()
{
	return PMIC_NDEFQueue;
}

/*
 * PMIC_GetMetadataResult
 *
 * @desc    Get pointer to metadata
 * *
 * @return	pointer to the metadata
 */
char* PMIC_GetMetadataResult()
{
	return meta_data_buffer;
}


/*
 * PMIC_GetSelftestUpdatedFlag
 *
 * @desc    Get flag indicating PMIC selftest updated
 * *
 * @return	true if selftest has been updated
 */
bool PMIC_GetSelftestUpdatedFlag()
{
	return m_bSelfTestUpdated;
}


/*
 * PMIC_GetEnergyUpdatedFlag
 *
 * @desc    Get flag indicating energy data updated
 * *
 * @return	true if energy data has been updated
 */
bool PMIC_GetEnergyUpdatedFlag()
{
	return m_bEnergyUpdated;
}


/*
 * PMIC_GetEnergyData
 *
 * @desc    Access the energy data
 * *
 * @return	Return a pointer to the energy data
 */
Mk24EnergyResult_t* PMIC_GetEnergyData()
{
	return &m_PMICenergyData;
}


/*
 * PMIC_GetSelftestSuccessFlag
 *
 * @desc    Get flag indicating PMIC selftest success or failure
 * *
 * @return	true if selftest success
 */
bool PMIC_GetSelftestSuccessFlag()
{
	return m_PMICselfTestData.bSelfTestPassed;
}


/*
 * PMIC_GetPMICrtc
 *
 * @desc    Get PMIC RTC seconds counter and status
 * *
 * @return	structure of counter and status
 */
PMIC_RTCstatus_t PMIC_GetPMICrtc()
{
	return (PMIC_RTCstatus_t){.rtcSeconds = m_stcPmicStatusMsg.rtcSeconds,
							  .rtcStatus = m_stcPmicStatusMsg.rtcStatus};
}

/**
 * Get GNSS status from PMIC (PMIC controlled GNSS)
 *
 * @return PMIC_Status_t*   Pointer to data once received from PMIC, NULL otherwise
 */
PMIC_Status_t* PMIC_GetPMICStatus(void)
{
	if (m_bPmicStatusMsgRx)
		return &m_stcPmicStatusMsg;
	else
		return NULL;
}

static bool m_updateConfig = false;

/**
 * PMIC_ScheduleConfigUpdate
 *
 * @desc    Schedule a configuration change message to be sent to PMIC
 * *
 * @return	none
 */
void PMIC_ScheduleConfigUpdate()
{
	m_updateConfig = true;
}


/*
 * PMIC_ConfigUpdateScheduled
 *
 * @desc    Check if a configuration change message due to be sent to PMIC
 * *
 * @return	true if message pending
 */
bool PMIC_ConfigUpdateScheduled()
{
	if(m_updateConfig)
	{
		m_updateConfig = false;
		return true;
	}
	return false;
}

void xTaskPMIC(void *pvParameters)
{
	PMICQueue_t queue;

	// create a queue
	PMICQueue = xQueueCreate(MAX_NBR_BUFFERS, sizeof(PMICQueue_t));
	PMICMetadataQueue = xQueueCreate(1, sizeof(char *));
	PMIC_NDEFQueue = xQueueCreate(1, sizeof(bool));
	PMICDumpQueue = xQueueCreate(1, sizeof(bool));
	PMICCapacityRemDataQueue = xQueueCreate(1, sizeof(uint16_t));

	m_bIsMetaDataValid = false;
	m_nMetaDataRecvIdx = 0;

	// Make IS25 ready for use.
	xTaskApp_makeIs25ReadyForUse(); // NOT the best place for this - if gets reformatted we miss messages from PMIC

	while(1)
	{
		if(pdTRUE == xQueueReceive(PMICQueue, &queue, portMAX_DELAY))
		{
			// do something
			switch(queue.type)
			{
			case PMIC_TEST_RESULT_ID:
				{
					static bool processing_ut = false;

					queue.buf[queue.size] = 0;

					if(!processing_ut)
					{
						if(strncmp((char*)queue.buf, "UT:START", 8) == 0)
						{
							processing_ut = true;
							convert_JUnit_Start((char*)queue.buf);
						}
						else
						{
							printf((char*)queue.buf);
						}
					}
					else
					{
						if(strncmp((char*)queue.buf, "UT:STOP", 7) == 0)
						{
							processing_ut = false;
							convert_JUnit_Stop();
						}
						else
						{
							convert_JUnit_Item((char*)queue.buf);
						}
					}
				}
				break;

			case PMIC_TEMPERATURE_LOG_ID:
				{
					handleTemperatureLog((TemperatureLog_t*)queue.buf);
				}
				break;

			case PMIC_STORE_ENERGY_USE_ID:
				{
					handlePmicStoreEnergyUse((uint32_t*)queue.buf);
				}
				break;

			case PMIC_STATUS_ID:
				{
					memcpy((uint8_t*)&m_stcPmicStatusMsg, queue.buf, sizeof(PMIC_Status_t));
					m_bPmicStatusMsgRx = true;

					gReport.tmp431.localTemp = PMIC_ConvertToTemperature(m_stcPmicStatusMsg.temperatureData.local.high, m_stcPmicStatusMsg.temperatureData.local.low);
					gReport.tmp431.remoteTemp = PMIC_ConvertToTemperature(m_stcPmicStatusMsg.temperatureData.remote.high, m_stcPmicStatusMsg.temperatureData.remote.low);

					// Convert gnss utc time to rtc_datetime_t format //
					rtc_datetime_t gnss_datetime = {};
					ConvertGnssUtc2Rtc(m_stcPmicStatusMsg.gnssStatus.utc_time, m_stcPmicStatusMsg.gnssStatus.utc_date, &gnss_datetime);
					LOG_DBG(LOG_LEVEL_PMIC,
						"PMIC Status\n"
						" Version = %d\n"
						" Wake Reason = %d\n"
						" Motion Detected = %d\n"
						" Temperatures: PCB = %.2f, CM = %.2f degC\n"
						" Voltage = %4.1f V\n"
						" PMIC Mode = %d (1=Cmsd,2=Ship)\n",
						m_stcPmicStatusMsg.version,
						m_stcPmicStatusMsg.wakeReason,
						m_stcPmicStatusMsg.motionDetected,
						gReport.tmp431.localTemp,
						gReport.tmp431.remoteTemp,
						((float)m_stcPmicStatusMsg.voltage) / 1000,
						m_stcPmicStatusMsg.PMICMode
						);

					LOG_DBG(LOG_LEVEL_PMIC,
						"PMIC Status, GNSS:\n"
						"  Is GNSS on = %d\n"
						"  Is GNSS fix good = %d\n"
						"  GNSS Time To First Accurate Fix = %d ms\n"
						"  HDOP = %f\n"
						"  utc time = %f\n"
						"  utc date = %d\n"
						"  utc datetime %s\n"
						"  GNSS_Lat_1 = %f\n"
						"  GNSS_NS_1 = %s\n"
						"  GNSS_Long_1 = %f\n"
						"  GNSS_EW_1 = %s\n"
						"  Speed to Rotation 1 = %f\n"
						"  GNSS Course 1 = %d\n",
						m_stcPmicStatusMsg.gnssStatus.is_gnss_mod_on,
						m_stcPmicStatusMsg.gnssStatus.is_gnss_fix_good,
						m_stcPmicStatusMsg.gnssStatus.time_to_first_accurate_fix_ms,
						m_stcPmicStatusMsg.gnssStatus.hdop,
						m_stcPmicStatusMsg.gnssStatus.utc_time,
						m_stcPmicStatusMsg.gnssStatus.utc_date,
						RtcDatetimeToString(gnss_datetime),
						m_stcPmicStatusMsg.gnssStatus.GNSS_Lat_1,
						m_stcPmicStatusMsg.gnssStatus.GNSS_NS_1,
						m_stcPmicStatusMsg.gnssStatus.GNSS_Long_1,
						m_stcPmicStatusMsg.gnssStatus.GNSS_EW_1,
						m_stcPmicStatusMsg.gnssStatus.GNSS_Speed_To_Rotation_1,
						m_stcPmicStatusMsg.gnssStatus.GNSS_Course_1
					)

					LOG_DBG(LOG_LEVEL_PMIC, "  GPS num sat: = %d\n", m_stcPmicStatusMsg.gnssStatus.gpsSat.numsat);
					LOG_DBG(LOG_LEVEL_PMIC, "  GPS Sat Id/SNR  =");
					for (int i=0; i<m_stcPmicStatusMsg.gnssStatus.gpsSat.numsat && i < MAX_GNSS_SATELITES; i++)
					{
						LOG_DBG(LOG_LEVEL_PMIC, " %d/%d",
								m_stcPmicStatusMsg.gnssStatus.gpsSat.id[i],
								m_stcPmicStatusMsg.gnssStatus.gpsSat.snr[i]);
					}
					LOG_DBG(LOG_LEVEL_PMIC, "\n");

					LOG_DBG(LOG_LEVEL_PMIC, "  Glonass num sat: = %d\n", m_stcPmicStatusMsg.gnssStatus.glonassSat.numsat);
					LOG_DBG(LOG_LEVEL_PMIC, "  Glonass Sat Id/SNR  =");
					for (int i=0; i<m_stcPmicStatusMsg.gnssStatus.glonassSat.numsat && i < MAX_GNSS_SATELITES; i++)
					{
						LOG_DBG(LOG_LEVEL_PMIC, " %d/%d",
								m_stcPmicStatusMsg.gnssStatus.glonassSat.id[i],
								m_stcPmicStatusMsg.gnssStatus.glonassSat.snr[i] );
					}

					LOG_DBG(LOG_LEVEL_PMIC, "\n\n");

					if(Device_IsHarvester())
					{
						LOG_DBG(LOG_LEVEL_PMIC,
							" Capacity = %4.1f mAh\n"
							" Duty Cycle = %d %%\n",
							(float)m_stcPmicStatusMsg.capacity,
							m_stcPmicStatusMsg.harvesterDutyCycle
							);
					}

					LOG_DBG(LOG_LEVEL_PMIC,
							" RTC: %s, Status:0x%02X (if not 0x%02X, PMIC time may be invalid)\n"
							" Energy Used: %.2f J\n",
							RtcUTCToString(m_stcPmicStatusMsg.rtcSeconds),
							m_stcPmicStatusMsg.rtcStatus,
							PMIC_RTC_IS_GOOD,
							((float)m_stcPmicStatusMsg.energyUsed_mJ) / 1000);

					handleWakeReason(m_stcPmicStatusMsg.wakeReason);
				}
				break;

			case PMIC_TEMPERATURE_ALARM_ID:
				{
					PMIC_TemperatureAlarm_t stcPmicTempAlarmMsg;
					memcpy((uint8_t*)&stcPmicTempAlarmMsg, queue.buf, sizeof(PMIC_TemperatureAlarm_t));

					if(PMIC_checkTemperatureAlarm(stcPmicTempAlarmMsg))
					{
						xTaskApp_startApplicationTask(WAKEUP_CAUSE_THERMOSTAT);
					}
					else
					{
						LOG_EVENT(0, LOG_LEVEL_PMIC, ERRLOGFATAL, "%s() unknown alarm code [%d]",
								__func__,
								(stcPmicTempAlarmMsg.cm_alarm > stcPmicTempAlarmMsg.pcb_alarm)? stcPmicTempAlarmMsg.cm_alarm: stcPmicTempAlarmMsg.pcb_alarm);
						xTaskDeviceShutdown(false);
						vTaskDelete(NULL);
					}
				}
				break;

			case PMIC_METADATA_ID:
				{
					// receiving array is sized to accomodate all possible metadata, plus a 'max' size of 256
					memcpy((meta_data_buffer + m_nMetaDataRecvIdx), (uint8_t*)queue.buf, queue.size);

					m_nMetaDataRecvIdx += queue.size;
					if(m_nMetaDataRecvIdx >= (uint32_t)__app_version_size)
					{
						m_bIsMetaDataValid = true;
						m_nMetaDataRecvIdx = 0;

						if(PMICMetadataQueue != NULL)
						{
							if(pdFALSE == xQueueSend(PMICMetadataQueue, &meta_data_buffer, 500/portTICK_PERIOD_MS))
							{
								LOG_DBG(LOG_LEVEL_PMIC,"%s: xQueueSend failed\n", __func__);
							}
						}
					}
				}
				break;

			case PMIC_SELFTEST_RESULT_ID:
				{
					memcpy(&m_PMICselfTestData, (uint8_t*)queue.buf, queue.size);
					m_bSelfTestUpdated = true;
				}
				break;

			case PMIC_NDEF_DATA_ID:
				if(NULL != PMIC_NDEFQueue)
				{
					NDEF* pNDEF = NFC_GetNDEF();
					if(NULL != pNDEF)
					{
						bool ok = true;
						memcpy(pNDEF->contents, (uint8_t*)queue.buf, queue.size);
						xQueueSend(PMIC_NDEFQueue, &ok, 0);
					}
				}
				break;

			case PMIC_ENERGY_INFO_ID:
				memcpy(&m_PMICenergyData, (uint8_t*)queue.buf, queue.size);
				m_bEnergyUpdated = true;
				break;

			case PMIC_REQUEST_MK24_INFO:
				LOG_DBG(LOG_LEVEL_PMIC, "MK24 Already Powered, sending power up status \n");
				PMIC_SendMK24PowerUpStatus();
				break;

			case PMIC_CAPACITY_REM_INFO_ID:
				if(NULL != PMICCapacityRemDataQueue)
				{
					uint16_t m_CapacityRem_mAh = *(uint16_t*)&queue.buf[0];
					xQueueSend(PMICCapacityRemDataQueue, &m_CapacityRem_mAh, 0);
				}
				break;

			case PMIC_EVENT_LOG:
				{
					handleEventLog((PMIC_ErrorLog*)queue.buf);
				}
				break;

			case PMIC_UPDATE_RTC_ID:
				handleUpdateRTC((uint32_t*)queue.buf);
				break;

			case PMIC_DUMP_ID:
				{
					// format is: [address (4 bytes)] [128 bytes]
					// we are complete if we receive 0xFFFF as address
					uint32_t address = (uint32_t)&(__sample_buffer[0]);
					bool complete = false;

					address += *(uint32_t*)&queue.buf[0];
	#if 0
					LOG_DBG(LOG_LEVEL_PMIC,"Address: 0x%08X ", address);
	#endif
					static uint32_t nFlashSize = 0;

					if(nFlashSize == 0)
					{
						if(JSON_value != jsonFetch(PMIC_GetMetadataResult(), "flashSize", &nFlashSize, true))
						{
							nFlashSize = 0x20000;
						}
					}

					if(PMICDumpQueue != NULL && (address >= (uint32_t)&(__sample_buffer[0]) + (nFlashSize - 1)))
					{
						nFlashSize = 0;
						complete = true;
						if(pdFALSE == xQueueSend(PMICDumpQueue, &complete, 500/portTICK_PERIOD_MS))
						{
							LOG_DBG(LOG_LEVEL_PMIC,"%s: xQueueSend failed\n", __func__);
						}
					}
					else
					{
						memcpy((uint8_t*)(address), (uint8_t*)(queue.buf + sizeof(uint32_t)), 128);
	#if 0
						LOG_DBG(LOG_LEVEL_PMIC,"Buffer received -> 0x%02X 0x%02X 0x%02X 0x%02X\n",
								queue.buf[0], queue.buf[1], queue.buf[2], queue.buf[3]);
	#endif
					}
				}
				break;

			default:
				break;
			}
		}
	}
}

TaskHandle_t _TaskHandle_PMIC = NULL;// not static because of easy print of task info in the CLI

bool PMIC_StartPMICtask()
{
	if (pdPASS != xTaskCreate(
            xTaskPMIC,				/* pointer to the task */
            (char const*)"PMIC",	/* task name for kernel awareness debugging */
			STACKSIZE_XTASK_PMIC, /* task stack size */
            NULL,					/* optional task startup argument */
			PRIORITY_XTASK_PMIC,	/* initial priority */
            &_TaskHandle_PMIC		/* optional task handle to create */
        ))
    {
        return false; /* error! probably out of memory */
    }
    return true;
}

void PMIC_UpdateParamsInCommsRecord(void)
{
	if(m_bPmicStatusMsgRx)
	{
		if(Device_IsHarvester())
		{
			commsRecord.params.Acceleration_Y = (float)m_stcPmicStatusMsg.version;
			commsRecord.params.Acceleration_X = (float)m_stcPmicStatusMsg.capacity;
		}

		commsRecord.params.Is_Move_Detect_Com = m_stcPmicStatusMsg.motionDetected;
		commsRecord.params.Wakeup_Reason = GetMK24WakeReason(m_stcPmicStatusMsg.wakeReason);
		commsRecord.params.EnergyRemaining_percent = 0;
	}
}


/*
 * PMIC_IsTLIVoltageGoodToStartOTA
 *
 * @desc    If PMIC wake reason is selftest:activated (PMIC_WAKE_FULL_SELFTEST),
 * 		    or a temperature alarm (PMIC_WAKE_TEMP_ALARM), don't start OTA if
 * 		    the TLI < 3.7V
 *
 * @return	pointer to the metadata
 */
bool PMIC_IsTLIVoltageGoodToStartOTA()
{
	if(((PMIC_WAKE_TEMP_ALARM == m_stcPmicStatusMsg.wakeReason) ||
	   (PMIC_WAKE_FULL_SELFTEST == m_stcPmicStatusMsg.wakeReason)) &&
	   (commsRecord.params.V_0 < TLI_VOLTAGE_HIGH))
	{
		return false;
	}
	return true;
}

static float PMIC_ConvertToTemperature(int nHighTemp, int nlowTemp)
{
	return (nHighTemp - 64) + 0.0625*(nlowTemp >> 4);
}

/*
 * PMIC_getMetadata
 *
 * @desc    Get the metadata from the PMIC
 *
 * @return	pointer to the metadata
 */
const char* PMIC_getMetadata(void)
{
	if(m_bIsMetaDataValid)
	{
		return meta_data_buffer;
	}

	PMIC_SendMetadataReqMsg();

	// Wait until the Metadata is received.
	if(!PMIC_IsMetadataRcvd())
	{
		LOG_EVENT(0, LOG_LEVEL_PMIC, ERRLOGFATAL,  "Metadata from PMIC not Rcvd");
		return k_pstrEmptyMetaData;
	}

	if(m_bIsMetaDataValid)
	{
		return meta_data_buffer;
	}

	return k_pstrEmptyMetaData;
}


/*!
 * PMIC_SendDumpReqMsg
 *
 * @brief      Construct and send memory dump request message to the PMIC.
 *
 * @param      none
 */
void PMIC_SendDumpReqMsg()
{
	// allocate a buffer
	uint8_t cmd[MK24_PMIC_PROTOCOL_OVERHEAD + MK24_PMIC_MESSAGE_OVERHEAD];

	// construct 'PMIC Dump Request' command
	int len = PMIC_formatCommand(MK24_REQUEST_DUMP_ID, cmd, sizeof(cmd), NULL, 0);
	PMIC_SendCommand(cmd, len);
}


/*!
 * PMIC_SendMetadataReqMsg
 *
 * @brief      Construct and send metadata request message to the PMIC.
 *
 * @param      none
 */
void PMIC_SendMetadataReqMsg()
{
	m_bIsMetaDataValid = false;
	m_nMetaDataRecvIdx = 0;

	// allocate a buffer
	uint8_t cmd[MK24_PMIC_PROTOCOL_OVERHEAD + MK24_PMIC_MESSAGE_OVERHEAD];

	// construct 'PMIC Metadata Request' command
	int len = PMIC_formatCommand(MK24_REQUEST_PMIC_METADATA_ID, cmd, sizeof(cmd), NULL, 0);
	PMIC_SendCommand(cmd, len);
}


/*!
 * PMIC_SendSelfTestReqMsg
 *
 * @brief      Construct and send self test request message to the PMIC.
 *
 * @param      none
 */
void PMIC_SendSelfTestReqMsg()
{
	// allocate a buffer
	uint8_t cmd[MK24_PMIC_PROTOCOL_OVERHEAD + MK24_PMIC_MESSAGE_OVERHEAD];

	// construct 'PMIC Self Test Request' command
	int len = PMIC_formatCommand(MK24_RUN_SELFTEST_ID, cmd, sizeof(cmd), NULL, 0);
	PMIC_SendCommand(cmd, len);
}


/*!
 * PMIC_SendEnergyReqMsg
 *
 * @brief      Construct and send energy request message to the PMIC.
 *
 * @param      none
 */
void PMIC_SendEnergyReqMsg()
{
	// allocate a buffer
	uint8_t cmd[MK24_PMIC_PROTOCOL_OVERHEAD + MK24_PMIC_MESSAGE_OVERHEAD];
	m_bEnergyUpdated = false;

	// construct 'PMIC Energy Request' command
	int len = PMIC_formatCommand(MK24_REQUEST_ENERGY_ID, cmd, sizeof(cmd), NULL, 0);
	PMIC_SendCommand(cmd, len);
}


/*!
 * PMIC_IsMetadataRcvd
 *
 * @brief   Waits for either the PMIC metadata event to be triggered, or until
 * 			the wait event timeout occurs.
 *
 * @return	true - if the metadata is received within the timeout period,
 * 			false otherwise
 */
bool PMIC_IsMetadataRcvd()
{
	char* pszTempBuf;
	if(pdTRUE == xQueueReceive(PMICMetadataQueue, &pszTempBuf, 2000 / portTICK_PERIOD_MS))
	{
		xQueueReset(PMICMetadataQueue);
		return true;
	}
	else
	{
		m_nMetaDataRecvIdx = 0;
		m_bIsMetaDataValid = false;
		return false;
	}
}


/*!
 * PMIC_IsDumpRcvd
 *
 * @brief   Waits for either the PMIC dump complete event to be triggered, or until
 * 			the wait event timeout occurs.
 *
 * @return	true - if the dump is received within the timeout period,
 * 			false otherwise
 */
bool PMIC_IsDumpRcvd()
{
	bool rc_ok;

	if(pdTRUE == xQueueReceive(PMICDumpQueue, &rc_ok, 60000/portTICK_PERIOD_MS))
	{
		xQueueReset(PMICDumpQueue);
		return true;
	}
	else
	{
		LOG_EVENT(0, LOG_LEVEL_PMIC, ERRLOGFATAL,  "Dump from PMIC not Rcvd");
		return false;
	}
}


/*!
 * PMIC_IsCapacityRemInfoRcvd
 *
 * @brief   Waits for either the PMICCapacityRemDataQueue event to be triggered,
 * 			 or until the wait event timeout occurs.
 *
 * @param   pSrcBuf - pointer to the pointer which needs the info about where
 * 			the capacity remaining is stored.
 *
 * @return	true - if the Capacity remaining info is received within the timeout period,
 * 			false otherwise
 */
bool PMIC_IsCapacityRemInfoRcvd(uint16_t *pSrcBuf)
{
	if((pSrcBuf != NULL) && (pdTRUE == xQueueReceive(PMICCapacityRemDataQueue, pSrcBuf, 1000/portTICK_PERIOD_MS)))
	{
		xQueueReset(PMICCapacityRemDataQueue);
		return true;
	}
	else
	{
		LOG_EVENT(0, LOG_LEVEL_PMIC, ERRLOGFATAL,  "Capacity Rem from PMIC not Rcvd");
		return false;
	}
}



/*!
 * handleUpdateRTC
 *
 * @desc Handle clock update message from the PMIC
 *
 * @param pNewTime - pointer to new time setting
 *
 * @return none
 */
static void handleUpdateRTC(const uint32_t* pNewTime)
{
	rtc_datetime_t datetime;

	RTC_HAL_ConvertSecsToDatetime(pNewTime, &datetime);

	// disable this here just in case - will be set up again at next schedule
	DisableRtcCounterAlarmInt(RTC_IDX);
	if (RtcSetTime(&datetime) == false)
	{
		LOG_EVENT(996, LOG_NUM_APP, ERRLOGMAJOR, "%s(): Set RTC time failed", __func__);
	}
}


/*!
 * handleWakeReason
 *
 * @desc Handle wake reason from the PMIC status message
 *
 * @param wakeReason
 *
 * @return none
 */
static void handleWakeReason(uint8_t wakeReason)
{
	// Read the last comms status.
	lastCommsOk = Vbat_IsFlagSet(VBATRF_FLAG_LAST_COMMS_OK);

	switch(wakeReason)
	{
		case PMIC_WAKE_RTC:
		{
			xTaskApp_startApplicationTask(WAKEUP_CAUSE_RTC);
		}
		break;

		case PMIC_WAKE_FULL_SELFTEST:
		{
			checkInitialSetup();
			xTaskApp_startApplicationTask(WAKEUP_CAUSE_FULL_SELFTEST);
		}
		break;

		case PMIC_WAKE_DAILY_SELFTEST_REDUCED:
		{
			// Only perform reduced self test, no Comms.
			xTaskApp_startApplicationTask(WAKEUP_CAUSE_SELFTEST_REDUCED);
		}
		break;

		case PMIC_WAKE_RTC_SYNC:
		{
			rtc_datetime_t rtc_datetime;
			(void)RtcGetTime(&rtc_datetime);

			uint32_t rtcStart = 0;
			uint32_t rtcStop = 0;
			RtcGetDatetimeInSecs(&rtcStart); // intentionally ignoring return value
			vTaskDelay(2500);
			RtcGetDatetimeInSecs(&rtcStop); // intentionally ignoring return value

			schedule_CalculateNextWakeup();

			if(((rtcStop - rtcStart == 2) || (rtcStop - rtcStart == 3)) && (rtc_datetime.year > 2019))
			{
				PMIC_sendMK24ParamUpdateMessage((params_t){	.group = PARAMS_RTC,
															.params.rtc = {.time_s = RTC_HAL_GetSecsReg(RTC), .alarm_s = RTC_HAL_GetAlarmReg(RTC)}});
			}
			else
			{
				// set RTC from PMIC RTC if we trust it, and reschedule
				if(m_stcPmicStatusMsg.rtcStatus & PMIC_RTC_IS_GOOD)
				{
					handleUpdateRTC((uint32_t*)&m_stcPmicStatusMsg.rtcSeconds);
					schedule_CalculateNextWakeup();
				}
			}
			xTaskDeviceShutdown(false);
		}
		break;

		case PMIC_WAKE_TEMP_ALARM:
		case PMIC_WAKE_ENG:
			//Don't start application
			break;

		case PMIC_WAKE_RESET_RTC:
		{
			DisableRtcCounterAlarmInt(RTC_IDX);
			xTaskDeviceShutdown(false);
			break;
		}
		default:
			break;
	}
}


/*!
 * handleEventLog
 *
 * @desc	Handle event log from the PMIC
 * @param 	pstcPmicErrorLog - pointer to error log structure
 *
 * @return
 */
static void handleEventLog(PMIC_ErrorLog* pstcPmicErrorLog)
{
	//Get number of logs stored in internal flash
	static uint8_t nRemainingLogEntryCount = 0;

	static uint16_t nSpaceLeft = 0;

	if(nRemainingLogEntryCount == 0)
	{
		tEventLog_inFlash * addrEventLog = NULL;
		uint16_t nUsedSpace = 0;

		EventLog_InitFlashData();

		while (EventLog_getLog(&addrEventLog))
		{
			nUsedSpace += MAXERRLOGFRAMELENGTH;
		}

		EventLog_InitFlashData();

		//Ensure to leave 10% of space left
		nSpaceLeft = (SYS_FLASH_ERRLOG_SIZE - nUsedSpace) * 0.9;
		nRemainingLogEntryCount = nSpaceLeft / MAXERRLOGFRAMELENGTH;
	}

	//If there is space remaining then store the log and send ack to PMIC
	if (nRemainingLogEntryCount > 0)
	{
		char temp[MAXLOGSTRINGLENGTH];

		strncpy(temp, (pstcPmicErrorLog->nEventCode < eLOG_MAX) ?
						PmicElog_getLogMessage(pstcPmicErrorLog->nEventCode) : "Unknown code", MAXLOGSTRINGLENGTH);

		snprintf(temp, MAXLOGSTRINGLENGTH, "%s, %s", temp, pstcPmicErrorLog->nLogMessage);
		temp[MAXLOGSTRINGLENGTH - 1] = 0;

		LOG_EVENT(PMIC_EVENTLOG_BAND + pstcPmicErrorLog->nEventCode,
				LOG_LEVEL_PMIC,
				pstcPmicErrorLog->severity,
				temp,
				pstcPmicErrorLog->nTimestamp_secs);

		uint8_t length = ERRORLOGFIXEDPART + strlen(temp) + 1;

		length += (PGM_SIZE_BYTE - (length % PGM_SIZE_BYTE));

		nSpaceLeft -=  length;

		nRemainingLogEntryCount = nSpaceLeft / MAXERRLOGFRAMELENGTH;
	}

	if(nRemainingLogEntryCount != 0)
	{
		vTaskDelay(100/portTICK_PERIOD_MS);
		SendEventLogAck(nRemainingLogEntryCount);
	}
}


/*!
 * processPMICSelfTestResult
 *
 * @desc - Process self test data structure received from PMIC
 * 		   Called from PMIC Task
 */
void PMIC_processPMICSelfTestResult()
{
	// Note: configuration for acceleration values PMIC side is configured to use
	//       sensitivity of +/-2g in high resolution mode
	float nMemsDivisorToG = 16380.0; // 1/16380 = 0.0610 mg/LSB for +/-2g sensitivity

	commsRecord.params.Is_Move_Detect_Com = m_PMICselfTestData.isMovementDetected;
	commsRecord.params.V_0 = ((float)m_PMICselfTestData.nTLIVoltage_mV)/1000;
	measureRecord.params.Temperature_Pcb = PMIC_ConvertToTemperature(m_PMICselfTestData.temperatureData.local.high, m_PMICselfTestData.temperatureData.local.low);
	measureRecord.params.Temperature_External = PMIC_ConvertToTemperature(m_PMICselfTestData.temperatureData.remote.high, m_PMICselfTestData.temperatureData.remote.low);

	LOG_DBG(LOG_LEVEL_PMIC, "Temperature_Pcb %f degC\n"
			"Temperature_External %f degC\n",
			measureRecord.params.Temperature_Pcb,
			measureRecord.params.Temperature_External);

	if(!m_PMICselfTestData.isMovementDetected)
	{
		commsRecord.params.Acceleration_X = ((float)m_PMICselfTestData.memsAxes.x) / nMemsDivisorToG;
		commsRecord.params.Acceleration_Y = ((float)m_PMICselfTestData.memsAxes.y) / nMemsDivisorToG;
		commsRecord.params.Acceleration_Z = ((float)m_PMICselfTestData.memsAxes.z) / nMemsDivisorToG;

		LOG_DBG(LOG_LEVEL_PMIC, "Acceleration_X = %9f, Acceleration_Y = %9f, Acceleration_Z = %9f\n",
				commsRecord.params.Acceleration_X,
				commsRecord.params.Acceleration_Y,
				commsRecord.params.Acceleration_Z);
	}
	else
	{
		commsRecord.params.Acceleration_X = 0;
		commsRecord.params.Acceleration_Y = 0;
		commsRecord.params.Acceleration_Z = 0;
	}

	for(int i = 0; i < m_PMICselfTestData.bSelfTestCodeCount; i++)
	{
		AddStatusArrayCode(m_PMICselfTestData.selfTestCodes[i]);
	}

	m_bSelfTestUpdated = false;
}


static void handleTemperatureLog(TemperatureLog_t* pTemperatureLog)
{
	static bool s_log_full = false;
	int nReadingsInThisMessage = MAX_TEMPERATURE_READINGS_PER_MESSAGE;
	uint8_t nNoOfReadings = pTemperatureLog->reading_count;
	uint8_t nSequenceNumber = pTemperatureLog->sequence_number;

	// need to keep these for more messages
	static uint32_t timestampSeconds;
	static uint16_t readingInterval_s;
	static uint8_t totalReadingCount;
	static uint16_t recordSpaceLeft = 0;

	// As first (or only) message then we have total number
	if(nSequenceNumber == 0)
	{
		totalReadingCount = pTemperatureLog->reading_count;
		readingInterval_s = pTemperatureLog->timer_interval_seconds;
		timestampSeconds = pTemperatureLog->nTimestamp;

		uint16_t noRecords = 0;
		if(!Temperature_RecordCount(&noRecords))
		{
			LOG_EVENT(0, LOG_LEVEL_PMIC, ERRLOGFATAL, "%s(): Failed to get temperature record count", __func__);
			SendTempLogsAck(MK24_TEMP_RECEIVED_ACK_RETURN_CODE__FAILED_TO_GET_TEMP_REC_CNT);
			return;
		}
		recordSpaceLeft = TEMPERATURE_MAX_NO_RECORDS - noRecords;

		LOG_DBG( LOG_LEVEL_PMIC,"Temperature readings: %d, interval: %d secs, Start Timestamp: %s\r\n",
				nNoOfReadings,
				readingInterval_s,
				RtcUTCToString(timestampSeconds));
	}

	if(nNoOfReadings < MAX_TEMPERATURE_READINGS_PER_MESSAGE)
	{
		nReadingsInThisMessage = nNoOfReadings;
	}

	bool bTempWriteSuccess = true;

	/*
	 * Save all the readings - we assume that there is enough storage
	 * but do check and error if there isn't!
	 */
	for(int i = 0; i < nReadingsInThisMessage; i++)
	{
		// convert the TMP431 sensor format - incoming as little-endian
		float temperature = PMIC_ConvertToTemperature((pTemperatureLog->temperatures[i] >> 8),
				(pTemperatureLog->temperatures[i] & 0x00FF));

		if(recordSpaceLeft >= totalReadingCount)
		{
			s_log_full = false;
			if(!Temperature_WriteRecord(temperature, timestampSeconds))
			{
				// log an error!
				LOG_EVENT(0, LOG_LEVEL_PMIC, ERRLOGFATAL, "%s(): Ext Flash error writing temperature record", __func__);
				SendTempLogsAck(MK24_TEMP_RECEIVED_ACK_RETURN_CODE__WRITE_FAILED);
				bTempWriteSuccess = false;
				break;
			}
		}
		else
		{
			if(s_log_full == false)
			{
				LOG_EVENT(0, LOG_LEVEL_PMIC, ERRLOGINFO, "%s(): Error, no space left to store a days worth of temperature logs.\n", __func__);
				s_log_full = true;
			}

			SendTempLogsAck(MK24_TEMP_RECEIVED_ACK_RETURN_CODE__NO_SPACE_LEFT);
			bTempWriteSuccess = false;
			break;
		}
		timestampSeconds += readingInterval_s;
	}

	if(bTempWriteSuccess)
	{
		LOG_DBG(LOG_LEVEL_PMIC,"%s(): %d Temperature logs stored.\n", __func__, nReadingsInThisMessage);
		vTaskDelay(100/portTICK_PERIOD_MS);
		SendTempLogsAck(MK24_TEMP_RECEIVED_ACK_RETURN_CODE__OK);
	}
}

bool PMIC_checkTemperatureAlarm(PMIC_TemperatureAlarm_t stcPmicTempAlarmMsg)
{
	bool bValidAlarm = true;

	LOG_DBG( LOG_LEVEL_PMIC, ">>>>>>>>> TEMPERATURE ALARM WAKEUP <<<<<<<<<\n\n");
	/*
	 * Map PMIC alarm codes to VBAT/self-test codes
	 * Text in the comment is the corresponding PMIC enumerated type
	 */
	const tSelfTestStatusCodes alarm_mapping[] =
	{
		SELFTEST_LOW_TEMP_LIMIT_EXCEEDED,			// CM_TEMPERATURE_LOW
		0,											// CM_TEMPERATURE_OK
		VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG,	// CM_TEMPERATURE_AMBER
		VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG,		// CM_TEMPERATURE_RED
		SELFTEST_HIGH_TEMP_LIMIT_EXCEEDED,			// CM_TEMPERATURE_HIGH
		SELFTEST_LOW_TEMP_LIMIT_EXCEEDED,			// PCB_TEMPERATURE_LOW
		0,											// PCB_TEMPERATURE_OK
		SELFTEST_HIGH_TEMP_LIMIT_EXCEEDED			// PCB_TEMPERATURE_HIGH
	};

	LOG_DBG( LOG_LEVEL_PMIC,
			"Previous temperature alarm status, Red:%d, Amber:%d\n",
			Vbat_IsFlagSet(VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG),
			Vbat_IsFlagSet(VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG));

	/*
	 * PCB_TEMPERATURE_HIGH is defined in E_CM_temperature_alarm_state_t and E_PCB_temperature_alarm_state_t
	 * See PMIC.h
	 */
	if((stcPmicTempAlarmMsg.cm_alarm <= PCB_TEMPERATURE_HIGH) && (stcPmicTempAlarmMsg.pcb_alarm <= PCB_TEMPERATURE_HIGH))
	{
		uint8_t alarm = alarm_mapping[(stcPmicTempAlarmMsg.cm_alarm != CM_TEMPERATURE_OK)? stcPmicTempAlarmMsg.cm_alarm: stcPmicTempAlarmMsg.pcb_alarm];

		/*
		* We have the temperature readings in the message
		* convert the TMP431 sensor format - incoming as little-endian
		*/

		gReport.tmp431.remoteTemp = PMIC_ConvertToTemperature(stcPmicTempAlarmMsg.stcTemperatureData.remote.high,
															  stcPmicTempAlarmMsg.stcTemperatureData.remote.low);
		gReport.tmp431.localTemp = PMIC_ConvertToTemperature(stcPmicTempAlarmMsg.stcTemperatureData.local.high,
				  	  	  	  	  	  	  	  	  	  	  	 stcPmicTempAlarmMsg.stcTemperatureData.local.low);
		char* alarmType = "";

		switch(alarm)
		{
			case VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG:
			case VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG:
				alarmType = (alarm == VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG) ? "AMBER" : "RED";
				Vbat_SetFlag(alarm);
				break;

			case SELFTEST_HIGH_TEMP_LIMIT_EXCEEDED:
				alarmType = "HIGH TEMP LIMIT EXCEEDED";
				Vbat_SetFlag(VBATRF_FLAG_HIGH_TEMPERATURE_LIMIT_TRIG);
				break;

			case SELFTEST_LOW_TEMP_LIMIT_EXCEEDED:
				alarmType = "LOW TEMP LIMIT EXCEEDED";
				Vbat_SetFlag(VBATRF_FLAG_LOW_TEMPERATURE_LIMIT_TRIG);
				break;

			// NOTE: early return
			default:
				LOG_EVENT(0, LOG_LEVEL_PMIC, ERRLOGFATAL, "%s() unexpected alarm code: %d", __func__, alarm);
				return false;
		}

		LOG_EVENT(0,
				  LOG_LEVEL_PMIC,
				  ERRLOGWARN,
				  "%s Alarm triggered, PCB Temp: %d degC, CM Temp: %d degC",
				  alarmType,
				  (int)gReport.tmp431.localTemp,
				  (int)gReport.tmp431.remoteTemp);
	}
	else
	{
		bValidAlarm = false;
	}

	return bValidAlarm;
}


/*!
 * PMIC_SendMK24PowerUpStatus
 *
 * @brief      Construct and send a status message to the PMIC.
 *
 * @param      none
 * @return 	   none
 */
void PMIC_SendMK24PowerUpStatus()
{
	int is_engineering_mode = Device_GetEngineeringMode();

	// allocate a buffer big enough for systemInfo_t structure plus eng. mode jumper status
	uint8_t cmd[MK24_PMIC_PROTOCOL_OVERHEAD + MK24_PMIC_MESSAGE_OVERHEAD + sizeof(systemInfo_t) + 1];

	cmd[MK24_PMIC_PAYLOAD] = (is_engineering_mode? 'E' : '0');
	systemInfo_t *pSysInfo = (systemInfo_t*)(&cmd[MK24_PMIC_PAYLOAD + 1]);

	pSysInfo->bootloaderVersion = getFirmwareVersionMeta(__loader_version);
	pSysInfo->applicationVersion = getFirmwareVersionMeta(__app_version);
	pSysInfo->hardwareVersion = (uint8_t)Device_GetHardwareVersion();
	strncpy(pSysInfo->imei, (char*)gNvmCfg.dev.modem.imei, IMEI_LENGTH);
	strncpy(pSysInfo->manufactureDate, NFC_GetManufacturingDate(), MANUFACTURE_DATE_LENGTH);
	strncpy(pSysInfo->model, NFC_GetDevicePartNumber(), MODEL_LENGTH);

	// construct 'MK24 Power up status' message
	int len = PMIC_formatCommand(MK24_POWERUP_STATUS_ID, cmd, sizeof(cmd), NULL, sizeof(systemInfo_t) + 1);

	PMIC_SendCommand(cmd, len);
}


/*!
 * PMIC_SendMk24STresults
 *
 * @brief      Construct and send an MK24 ST results message to the PMIC.
 *
 * @param      nSelfTestCodeCount - Number of failures
 *             bSelfTestPassed - pass in whether this is a fail or not
 * @return     none
 */
void PMIC_SendMk24STresults(uint32_t nSelfTestCodeCount, bool bSelfTestPassed)
{
	Mk24SelfTestResults stcSTResults;

	uint8_t cmd[MK24_PMIC_PROTOCOL_OVERHEAD + MK24_PMIC_MESSAGE_OVERHEAD + sizeof(Mk24SelfTestResults)];

	if(nSelfTestCodeCount > MAXSTATUSSELFTESTLENGTH)
	{
		nSelfTestCodeCount = MAXSTATUSSELFTESTLENGTH;
	}

	stcSTResults.bSelfTestPassed = bSelfTestPassed;
	stcSTResults.nSelfTestCodeCount = nSelfTestCodeCount;

	for(int i = 0; i < stcSTResults.nSelfTestCodeCount; i++)
	{
		stcSTResults.selfTestCodes[i] = commsRecord.params.Status_Self_Test[i];
	}

	int len = PMIC_formatCommand(MK24_SELFTEST_RESULTS_ID, cmd, sizeof(cmd), (uint8_t*)&stcSTResults, sizeof(Mk24SelfTestResults));
	PMIC_SendCommand(cmd, len);
}



void PMIC_Reboot()
{
	LOG_DBG(LOG_LEVEL_APP, "PMIC Rebooting\n");
	pinConfigDigitalOut(PMIC_RESET, kPortMuxAsGpio, 1, false);
	vTaskDelay(100/portTICK_PERIOD_MS);
	pinConfigDigitalOut(PMIC_RESET, kPortPinDisabled, 0, false);
}


/*!
 * PMIC_Powerdown
 *
 * @desc    Ask the PMIC to shutdown.
 *
 * @return  none
 */
void PMIC_Powerdown()
{
#define MAX_SHUTDOWN_ATTEMPTS (3)

	vTaskDelay(100/portTICK_PERIOD_MS);
	LOG_DBG(LOG_LEVEL_APP, "Send MK24 shutdown status to PMIC\n");

	// allocate a buffer
	uint8_t cmd[MK24_PMIC_PROTOCOL_OVERHEAD + MK24_PMIC_MESSAGE_OVERHEAD + SHUTDOWN_MSG_PAYLOAD_LENGTH_BYTES];

	uint8_t payload[SHUTDOWN_MSG_PAYLOAD_LENGTH_BYTES] = {MK24_EXIT_ENGG_MODE_PAYLOAD_BYTE, 0, 0};

	// Based on CM Alarm clearing logic update the payload[1]
	uint8_t state;
	Vbat_GetAlarmState(&state);

	// NodeTask that will be run on next cycle (i.e. measurement or upload)
	uint8_t nodeTaskRunNextCycle;
	Vbat_GetNodeWakeupTask(&nodeTaskRunNextCycle);

	if(state == TEMPERATURE_ALARM_RETRY)
	{
		payload[1] = 2;
	}
	else if(state == TEMPERATURE_ALARM_RE_ARM)
	{
		payload[1] = MK24_RESET_CM_TMP_ALARM_PAYLOAD_BYTE;
		Vbat_SetAlarmState(TEMPERATURE_NO_ALARM);
	}

	payload[2] = nodeTaskRunNextCycle;

	// construct 'MK24 Powerdown Status' command
	int len = PMIC_formatCommand(MK24_POWERDOWN_STATUS_ID, cmd, sizeof(cmd), &payload[0], SHUTDOWN_MSG_PAYLOAD_LENGTH_BYTES);
	int attempts = 0;

	// switch off Reset-on-LVD detect in case we don't die gracefully..
	PMC_WR_LVDSC1_LVDRE(PMC, 0);

	// should never complete the delay..
	for(;;)
	{
		PMIC_SendCommand(cmd, len);
		vTaskDelay(2000/portTICK_PERIOD_MS);

		// oops we did not shut down
		if(++attempts >= MAX_SHUTDOWN_ATTEMPTS)
		{
			LOG_EVENT(0, LOG_NUM_PMIC, ERRLOGWARN, "Exceeded MK24 shutdown attempts");
                        vTaskDelay(1000/portTICK_PERIOD_MS);
			PMIC_Reboot();
			vTaskDelay(5000/portTICK_PERIOD_MS);
			attempts = 0;
		}
	}
}

/*!
 * PMIC_sendMK24ParamUpdateMessage
 *
 * @desc    Send Param Update Message
 *
 * @param   params_t - parameter structure to be sent
 *
 */
void PMIC_sendMK24ParamUpdateMessage(params_t stcParams)
{
	// allocate a buffer
	uint8_t cmd[MK24_PMIC_PROTOCOL_OVERHEAD + MK24_PMIC_MESSAGE_OVERHEAD + sizeof(params_t)];

	int len = PMIC_formatCommand(MK24_UPDATE_PARAM_ID, cmd, sizeof(cmd), (uint8_t*)&stcParams, sizeof(params_t));

	PMIC_SendCommand(cmd, len);
}


/*
 * PMIC_ProgImage
 *
 * @desc    Programs the PMIC image from the image stored in the external flash.
 *
 * @param   is_test: set to true to program from sample buffer, false to program from OTA location
 * @param   size:	used if 'test mode'
 *
 * @return	true if the PMIC image is programmed successfully, false otherwise
 */
bool PMIC_ProgImage(bool is_test, int size)
{
	uint8_t attempts = 1;
	bool flash_read_fail = false, pmic_prog_fail = false, success = false;

	LOG_DBG(LOG_LEVEL_PMIC, "Programming the PMIC Image now...\n");
	while(attempts <= FLASH_RETRIES)
	{
		if(!is_test)
		{
			size = OtaProcess_GetImageSize();
			if(!IS25_ReadBytes(gBootCfg.cfg.ImageInfoFromLoader.OTAstartAddrForApp, (uint8_t*)__sample_buffer, OtaProcess_GetImageSize()))
			{
				flash_read_fail = true;
				break;
			}
		}
		extern int PMICprogram(const uint8_t *image, uint32_t size);
		int rc = PMICprogram((uint8_t*)__sample_buffer, size);

		if(0 == rc)
		{
			success = true;
			break;
		}

		if(rc > 0)
		{
			pmic_prog_fail = true;
		}
		attempts++;
	}

	if(flash_read_fail)
	{
		LOG_EVENT(0, LOG_NUM_APP, ERRLOGMAJOR, "External flash failed read operation");
	}

	if(pmic_prog_fail > 0)
	{
		LOG_EVENT(0, LOG_LEVEL_PMIC, ERRLOGMAJOR, "PMIC Update Failed, returned code: %d", pmic_prog_fail);
	}

	if(attempts > FLASH_RETRIES)
	{
		LOG_EVENT(0, LOG_LEVEL_PMIC, ERRLOGMAJOR, "PMIC Update Failed!");
	}
	else
	{
		LOG_EVENT(0, LOG_LEVEL_PMIC, ERRLOGMAJOR, "PMIC Update Succeeded at attempt %d", attempts);
	}

	// If we programmed the PMIC, check the metadata
	if(success)
	{
		success = PMIC_IsMetadataRcvd();
		if(success)
		{
			const int meta_offset = (uint32_t)__app_version - (uint32_t)__app_origin;
			if(!is_test)
			{
				if(!IS25_ReadBytes(gBootCfg.cfg.ImageInfoFromLoader.OTAstartAddrForApp, (uint8_t*)__sample_buffer, meta_offset + (uint32_t)__app_version_size))
				{
					LOG_EVENT(0, LOG_LEVEL_PMIC, ERRLOGMAJOR, "External flash failed read while checking PMIC metadata");
					success = false;
				}
			}
			if(success)
			{
				char* pNewImageMetadata = (char*)__sample_buffer + meta_offset;
				if(strncmp(meta_data_buffer, pNewImageMetadata, (uint32_t)__app_version_size) != 0)
				{
					LOG_EVENT(0, LOG_LEVEL_PMIC, ERRLOGMAJOR, "PMIC metadata failed verification");
					m_bIsMetaDataValid = false;
					success = false;
				}
				else
				{
					m_bIsMetaDataValid = true;
				}
			}
		}
		else
		{
			LOG_EVENT(0, LOG_LEVEL_PMIC, ERRLOGFATAL,  "Metadata from PMIC not Rcvd");
		}

		if(success)
		{
			LOG_DBG(LOG_LEVEL_PMIC, "PMIC metadata matched OK\n");
		}
	}

	return success;
}


/*!
 * PMIC_SendCapacityRemReqMsg
 *
 * @brief      Construct and send Capacity remaining request message to the PMIC.
 *
 * @param      none
 */
void PMIC_SendCapacityRemReqMsg()
{
	// allocate a buffer
	uint8_t cmd[MK24_PMIC_PROTOCOL_OVERHEAD + MK24_PMIC_MESSAGE_OVERHEAD];

	// construct 'PMIC Capacity Request' command
	int len = PMIC_formatCommand(MK24_REQUEST_CAPACITY_REM_INFO_ID, cmd, sizeof(cmd), NULL, 0);
	PMIC_SendCommand(cmd, len);
}

/*!
 * SendEventLogAck
 *
 * @brief      Construct and send Event log ack message to the PMIC.
 *
 * @param      nRemainingLogEntryCount - No of remaining log entries that can be saved
 */
static void SendEventLogAck(uint8_t nRemainingLogEntryCount)
{
	// allocate a buffer
	uint8_t cmd[MK24_PMIC_PROTOCOL_OVERHEAD + MK24_PMIC_MESSAGE_OVERHEAD + sizeof(nRemainingLogEntryCount)];

	// construct 'Send event log ack'
	int len = PMIC_formatCommand(MK24_LOG_RECEIVED_ACK_ID, cmd, sizeof(cmd), &nRemainingLogEntryCount, sizeof(nRemainingLogEntryCount));
	PMIC_SendCommand(cmd, len);
}

/*!
 * SendEventLogAck
 *
 * @brief      Construct and send Temperature log ack message to the PMIC.
 *
 */
static void SendTempLogsAck(TemperatureReceivedAckReturnCode_t return_code)
{
	// allocate a buffer
	uint8_t cmd[MK24_PMIC_PROTOCOL_OVERHEAD + MK24_PMIC_MESSAGE_OVERHEAD + 1];
	uint8_t code = return_code;

	// construct 'Send event log ack'
	int len = PMIC_formatCommand(MK24_TEMP_RECEIVED_ACK_ID, cmd, sizeof(cmd), &code, sizeof(code));
	PMIC_SendCommand(cmd, len);
}

/*!
 * PMIC_sendStoreEnergyUse
 *
 * @desc    Send store energy use message
 *
 */
uint8_t pmicStoreEnergyUse_successful = 0;
uint32_t PMIC_sendStoreEnergyUse( void )
{
	// allocate a buffer
	uint8_t cmd[MK24_PMIC_PROTOCOL_OVERHEAD + MK24_PMIC_MESSAGE_OVERHEAD ];

	// construct
	int len = PMIC_formatCommand(MK24_STORE_ENERGY_USE_ID, cmd, sizeof(cmd), 0, 0 );
	pmicStoreEnergyUse_successful = 0;
	PMIC_SendCommand(cmd, len);

	// wait a moment to let response arrive
	vTaskDelay(1000);
	if( pmicStoreEnergyUse_successful )
		return 1;
	else
		return 0;
}

/*!
 * handlePmicSendStoreEnergyUse
 *
 * @desc    handler for message received from PMIC
 *
 */
void handlePmicStoreEnergyUse( uint32_t *buf )
{
	if( *buf == PMIC_STORE_ENERGY_USE_SUCCESSFUL )
	{
		pmicStoreEnergyUse_successful = 1;
	}
	else if( *buf == PMIC_STORE_ENERGY_USE_ERR )
	{
		pmicStoreEnergyUse_successful = 0;
	}
	else
	{
		pmicStoreEnergyUse_successful = 0;
	}
}


/*
 * PMIC CLI
 */

static const struct cliCmd pmicSpecificCommands[] =
{
	{"PMIC", 		"\t\t\t\tSend a PMIC specific command 'pmic ?' for help", pmic_CLI, 0},
	{"PMICProg",	"\t\t\tPMIC flashing test commands",	pmicProg_CLI, pmicProgHelp},
	{"PMICBackup",  "\t\t\tPMIC backup test commands", PmicBackup_CLI, pmicBackupHelp}
};

/*
 * Call this if device has a PMIC
 */
void PMIC_InitCLI()
{
	(void)cliRegisterCommands(pmicSpecificCommands,
			sizeof(pmicSpecificCommands)/sizeof(*pmicSpecificCommands));
}

/*
 * CLI command to pass commands through to PMIC
 */
static bool pmic_CLI(uint32_t argc, uint8_t* argv[], uint32_t * argi)
{
	char tx_buf[100];
	uint8_t tx_formatted_buf[120];

	if(strcmp("reboot", (char*)argv[0]) == 0)
	{
		pinConfigDigitalOut(PMIC_RESET, kPortMuxAsGpio, 1, false);
		OSA_TimeDelay(100);
		GPIO_DRV_WritePinOutput(PMIC_RESET, 0);
		return true;
	}

	tx_buf[0] = 0;

	for(int i=0; i<argc; i++)
	{
		strcat(tx_buf, (char*)argv[i]);
	}

	int length;
	strcat(tx_buf, "\r\n");
	length = PMIC_formatCommand(MK24_PMIC_TEST_COMMAND_ID, tx_formatted_buf, sizeof(tx_formatted_buf), (uint8_t*)tx_buf, strlen(tx_buf));
	if(length)
	{
		(void)PMIC_SendCommand(tx_formatted_buf, length);
	}

	return true;
}

static bool pmicProgHelp(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	printf(	"pmicProg - program from downloaded image\n"
			"pmicProg b - program from backup image\n"
			"pmicProg t - program from test image\n"
			"pmicProg l n - program from test image [n] times\n"
			"pmicProg c - program from corrupted test image\n");
	return true;
}


static bool pmicProg_CLI(uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	if(args == 0)
	{
		image_UpdatePmicAppFromExtFlashAddr(gBootCfg.cfg.ImageInfoFromLoader.OTAstartAddrForApp);
		return true;
	}

	if(args == 1)
	{
		if(strcmp("b", (const char*)argv[0]) == 0)
		{
			image_UpdatePmicAppFromExtFlashAddr(EXTFLASH_BACKUP_PMIC_START_ADDR);

			return true;
		}

		// get the PMIC image!
		LOG_DBG(LOG_LEVEL_PMIC, "Getting PMIC image\n");
		PMIC_SendDumpReqMsg();
		if(!PMIC_IsDumpRcvd())
		{
			printf("\n\nFailed to get PMIC image!\n");
			return true;
		}

		if(strcmp("c", (const char*)argv[0]) == 0)
		{
			__sample_buffer[0] = 0xFF;

		}

		// program from the sample buffer
		PMIC_ProgImage(true, 0xFFFF);
		return true;
	}

	if(args == 2)
	{
		int i;
		for(i=0; i<argi[1]; i++)
		{
			if(PMIC_ProgImage(true, 0xFFFF) != 0)
			{
				printf("\n\nPMIC flash programming error at %d\n", i);
				return true;
			}
			vTaskDelay(2000);
		}

		printf("\n\nPMIC flash programmed %d times without error\n", i);
	}


	return true;
}

static bool PmicBackup_CLI(uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	if (args == 0)
	{
		(void)image_FirstTimePmicBackup();
	}

	if(args == 1)
	{
		if(strcmp("e", (const char*)argv[0]) == 0)
		{
			IS25_PerformSectorErase(EXTFLASH_BACKUP_PMIC_START_ADDR, BACKUP_PMIC_MAX_SIZE_BYTES);
		}
	}

	return true;
}

static bool pmicBackupHelp(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	printf(	"pmicBackup - Get Image from PMIC and backup\n"
			"pmicBackup e - Erase PMIC backup area\n");

	return true;
}


#ifdef __cplusplus
}
#endif