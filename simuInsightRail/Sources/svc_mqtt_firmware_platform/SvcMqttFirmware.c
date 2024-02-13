#ifdef __cplusplus
extern "C" {
#endif

/*
 * SvcMqttFirmware.c
 *
 *  Created on: 26 dec. 2015
 *      Author: Daniel van der Velde
 */

/*
 * Includes
 */

#define _CRT_SECURE_NO_WARNINGS
#include <stdint.h>
#include <stdbool.h>

#include "configFeatures.h"
#ifdef CONFIG_PLATFORM_OTA

#include "SvcMqttFirmware.h"
#include "ISvcFirmware.h"
// lwip stack defines these two, but in a different way than another include file, and we do not use this define, so to avoid problems, undefine these
#undef BIG_ENDIAN
#undef LITTLE_ENDIAN

#include "SvcData.h"
#include "SvcDataMsg.h"
#include "Datadef.h"
#include "svcfirmware.pb.h"
#include "binaryCLI.h"

#include "configMQTT.h"
#include "configData.h"


#include "pb.h"
#include "pb_encode.h"
#include "pb_decode.h"


#include "Log.h"

#include "SvcDataMsg.h"
#include "ISvcData.h"


#include "configBootloader.h"

// TODO put in another include file not nice to include a higher layer like this
#include "xTaskAppOta.h"


#include "DataStore.h"

#include "Device.h"

#include "NvmData.h"

#include "CS1.h"
#include "image.h"
#include "linker.h"

// cpu.h or lower defines these two, but in a different way than another include file, and we do not use this define, so to avoid problems, undefine these
#undef BIG_ENDIAN
#undef LITTLE_ENDIAN


typedef struct {
	MQTTClient       *mqttClient_p;				// MQTT Client
	SemaphoreHandle_t apiMutex;           		// ISvcFirmware interface

	// Operational state
	ISvcFirmwareState_t   svcState;           	// SvcData state (disabled/started)

    uint8_t           dataMsgRxSourceId[MAXSOURCEIDSIZE+1]; 	// no further use yet, holds latest received sourceId
    uint8_t			  dataMsgRxDeviceType[MAXSOURCEIDSIZE+1]; 	// no further use yet, holds latest received devicetype update notification

    uint32_t          dataMsgTxLastId;      	// Message ID counter

    // Default MQTT message settings
    enum QoS          mqttQosDefault;
    uint8_t           mqttRetainDefault;
    uint8_t           mqttDupDefault;

} SvcFirmware_State_t;


// what stays in the c file
#define SVCFIRMWAREMAXCALLBACKS   (2)

/*
 * Data
 */
// TODO refers to gBootcfg but there is not yet a semaphore protecting this struct
extern BootCfg_t gBootCfg;

uint8_t *pImageBuf = (uint8_t *) __sample_buffer;
//uint32_t imageBufSize = (uint32_t) __sample_buffer_size;


static uint8_t TxBuf[CONFIG_MQTT_SEND_BUFFER_SIZE - MQTT_MAX_TOPIC_LEN - 4] = {SVCDATA_MQTT_SERDES_ID_GPB2_1, 0,};

// Constructed MQTT TOPIC buffers
static char MQTTTopic_SvcFirmware[MQTT_MAX_TOPIC_LEN+1] = {0,}; // NULL terminated string to pass to MQTT(Un)Subscribe()

static SvcFirmware_State_t StateFw;
static SKF_SvcFirmwareBlockRequest MsgTxBlockReq = SKF_SvcFirmwareBlockRequest_init_default;
static SKF_SvcFirmwareUpdateNotification MsgRxUpdateNot = SKF_SvcFirmwareUpdateNotification_init_default;
static SKF_SvcFirmwareBlockReply MsgRxBlockRep = SKF_SvcFirmwareBlockReply_init_default;
static TransportPipe_t m_eMsgTransportPipe = eTRANSPORT_PIPE_MQTT;
static tBlockReplyInfo blockReplyInfo;
static uint32_t m_nMaxFwOtaBlockSize;

const  SKF_SvcFirmwareBlockRequest SvcFirmwareBlockReq_Init = SKF_SvcFirmwareBlockRequest_init_default;
const  SKF_SvcFirmwareBlockReply SvcFirmwareBlockRep_Init = SKF_SvcFirmwareBlockReply_init_default;


//static SKF_SvcFirmwareUpdateNotification MsgRx = SKF_SvcFirmwareUpdateNotification_init_default;
const  SKF_SvcFirmwareUpdateNotification SvcFirmwareUpdateNotificationMsg_Init = SKF_SvcFirmwareUpdateNotification_init_default;

/*
 * Functions
 */

// will move to the status struct ?
static tSvcFirmwareCallbackFuncPtr SvcFirmwareCallbackFuncTable[SvcFirmware_cb_max][SVCFIRMWAREMAXCALLBACKS];

//------------------------------------------------------------------------------
/// Get the current maximum block size for OTA
///
/// @return uint32_t The current maximum block size for OTA
//------------------------------------------------------------------------------
uint32_t SvcFirmware_GetMaxFwOtaBlockSize()
{
	return m_nMaxFwOtaBlockSize;
}

//------------------------------------------------------------------------------
/// Get the maximum block size for OTA over MQTT
///
/// @return uint32_t The maximum block size for OTA over MQTT
//------------------------------------------------------------------------------
uint32_t SvcFirmware_GetMqttMaxFwOtaBlockSize()
{
	// TODO Needs to be changed in something derived from the receiving buffer for MQTT
	return 1536;
}

//------------------------------------------------------------------------------
/// Set the current maximum block size for OTA
///
/// @param  nMaxFwOtaBlockSize (uint32_t) -- The maximum block size for OTA
///
/// @return void
//------------------------------------------------------------------------------
void SvcFirmware_SetMaxFwOtaBlockSize(uint32_t nMaxFwOtaBlockSize)
{
	m_nMaxFwOtaBlockSize = nMaxFwOtaBlockSize;
}

//------------------------------------------------------------------------------
/// Get the current transport pipe type
///
/// @return TransportPipe_t The current transport pipe type
//------------------------------------------------------------------------------
TransportPipe_t SvcFirmware_GetCurrentTransportPipe()
{
	return m_eMsgTransportPipe;
}

/**
 * SvcFirmwareRemoveCallback
 *
 * @brief remove the callback : safe to be called from another task
 * @param cbIdx
 * @param cbFunc
 */
void SvcFirmwareRemoveCallback(tSvcFirmwareCallbackIndex cbIdx, tSvcFirmwareCallbackFuncPtr cbFunc)
{
    uint8_t i;
    if (cbIdx < SvcFirmware_cb_max) {

        for (i=0; (i<SVCFIRMWAREMAXCALLBACKS) ; i++) {

            CS1_CriticalVariable();

            CS1_EnterCritical();

            // running in another task context, and a 32 bit pointer assign is not atomic on this processor, so be on the safe side
            if (SvcFirmwareCallbackFuncTable[cbIdx][i] == cbFunc) {
                SvcFirmwareCallbackFuncTable[cbIdx][i] = NULL;
            }

            CS1_ExitCritical();
        }
    }
}

/**
 * SvcFirmwareSetCallback
 *
 * @brief set the callback : safe to be called from another task
 * @param cbIdx
 * @param cbFunc
 */
void SvcFirmwareSetCallback(tSvcFirmwareCallbackIndex cbIdx, tSvcFirmwareCallbackFuncPtr cbFunc)
{
    uint8_t i;

    if (cbIdx < SvcFirmware_cb_max)
    {

        SvcFirmwareRemoveCallback(cbIdx,  cbFunc);// just to prevent multiple entries of this address

        for (i=0; (i < SVCFIRMWAREMAXCALLBACKS) ; i++)
        {

            if (SvcFirmwareCallbackFuncTable[cbIdx][i] == NULL)
            {
                CS1_CriticalVariable();

                CS1_EnterCritical();
                // running in another task context, and a 32 bit pointer assign is not atomic on this processor, so be on the safe side
                SvcFirmwareCallbackFuncTable[cbIdx][i] = cbFunc;

                CS1_ExitCritical();
                break;// exit the for loop
            }
        }
    }
}

/**
 * handleCallback
 *
 * @brief call possible registered callback functions when we got a remote request
 * @param cbIdx
 * @param buf
 */
static uint8_t handleCallback (tSvcFirmwareCallbackIndex cbIdx, void * buf)
{
    uint8_t rc = 0;
    if (cbIdx < SvcFirmware_cb_max)
    {
        uint8_t i;
        for (i=0; i < SVCFIRMWAREMAXCALLBACKS ; i++)
        {

            if (SvcFirmwareCallbackFuncTable[cbIdx][i] != NULL)
            {
                // call whoever is interested.
                uint8_t rc2 = SvcFirmwareCallbackFuncTable[cbIdx][i]( buf );

                if (rc2 != 0)
                {
                	rc = rc2; // last callback with error status wins
                }
            }
        }
    }
    return rc;
}


/**
 * SvcFirmwareMsg_DecodeImage
 *
 * @brief Decode the contents of a Firmware (sub)message
 * @param stream_p input stream
 * @param field_p  pointer to field descriptor struct
 * @param **arg
 * @return true on success, false otherwise
 */
bool SvcFirmwareMsg_DecodeImageIdef(pb_istream_t *stream_p, const pb_field_t *field_p, void **arg)
{
	bool status = true;

	uint32_t *pSize = (uint32_t *) *arg;


	//Check if the size of the image in the message does not exceed SvcFirmware_GetMaxFwOtaBlockSize()
	if(stream_p->bytes_left > SvcFirmware_GetMaxFwOtaBlockSize())
	{
		LOG_DBG( LOG_LEVEL_COMM, "ERROR size of image:%d > SvcFirmware_GetMaxFwOtaBlockSize():%d\n", stream_p->bytes_left, SvcFirmware_GetMaxFwOtaBlockSize());
		return false;
	}

	*pSize = stream_p->bytes_left;

	// make use of the internal nanopb way a buffer is handled,
	// stream_p->state points to where we are in the input buffer
	// stream_p->bytes_left is the length in bytes of our data.
	// record the length of this
	memcpy(pImageBuf, stream_p->state, stream_p->bytes_left);

	// Update the status of the stream
	stream_p->state = (uint8_t*)stream_p->state +  stream_p->bytes_left;
	stream_p->bytes_left = 0;

	return status;
}

bool SvcFirmwareMsg_DecodeImageRaw(const void* pBuffer, uint32_t nBufferLen)
{
	//Check if the size of the image in the message does not exceed SvcFirmware_GetMaxFwOtaBlockSize()
	if(nBufferLen > SvcFirmware_GetMaxFwOtaBlockSize())
	{
		LOG_DBG(LOG_LEVEL_COMM, "ERROR %s, size of image:%d > SvcFirmware_GetMaxFwOtaBlockSize():%d\n",
				__func__,
				nBufferLen,
				SvcFirmware_GetMaxFwOtaBlockSize());
		return false;
	}

	memcpy(pImageBuf, pBuffer, nBufferLen);

	return true;
}

void DoHandle_UpdateNotification()
{
    // Show all messages on CLI during debug
#ifdef DEBUG
	if (dbg_logging & LOG_LEVEL_COMM)
	{
		printf("\n\n>>> SvcFirmware: RX SvcFirmwareUpdateNotification <<<\n\n");

		printf("MsgRxUpdateNot.version = %u\n", MsgRxUpdateNot.version);
		printf("MsgRxUpdateNot.source_id   = %s\n", StateFw.dataMsgRxSourceId );
		printf("MsgRxUpdateNot.device_type = %s\n", StateFw.dataMsgRxDeviceType);
		printf("MsgRxUpdateNot.message_id  = %u\n", MsgRxUpdateNot.message_id);
		printf("MsgRxUpdateNot.hardware_version  = %u\n", MsgRxUpdateNot.hardware_version);
		printf("MsgRxUpdateNot.firmware_version  = %u\n", MsgRxUpdateNot.firmware_version);
		printf("MsgRxUpdateNot.image_size  = %u\n", MsgRxUpdateNot.image_size);
	}
#endif

	// If the FW version image size exceeds the allocated space for storing the
	// image then log an event.
	if(MsgRxUpdateNot.image_size > configBootloader_GetAcceptableImageSize_Bytes())
	{
		LOG_EVENT(0, LOG_NUM_OTA, ERRLOGMAJOR,
				  "ImageSize > AvlbSpace.RxImageSize: %d, ExtFlash Max IMAGE size: %d",
				  MsgRxUpdateNot.image_size, configBootloader_GetAcceptableImageSize_Bytes());
	}
	// Check if update notification is already registered
	// Rework this for non-volatile structure that holds OTA information and interact with 'new' boot_config
	else if(OtaProcess_GetNewFwVer() != MsgRxUpdateNot.firmware_version)
	{
		OtaProcess_SetBlocksWrtittenInFlash(0);
		OtaProcess_SetRetryCount(MAX_NUM_OF_OTA_RETRIES);
		OtaProcess_SetImageSize(MsgRxUpdateNot.image_size);
		OtaProcess_SetNewFwVer(MsgRxUpdateNot.firmware_version);
		if(OtaProcess_WriteToFlash()==false)
		{
			LOG_EVENT(0, LOG_NUM_OTA, ERRLOGMAJOR, "OTA process mgmnt data write failed.");
		}
		handleCallback(SvcFirmware_cb_updateNotification, NULL);
	}
	else
	{
		LOG_DBG( LOG_LEVEL_COMM,"Firmware update notification was already registered\n");
	}
}

/**
 * SvcFirmware_MessageHandler_UpdateNotification
 *
 * @brief Main Message handler for SvcFirmare Update Notification, decodes protobuf information into IDEF SvcFirmwareUpdateNotification
 * @param payload_p points to the start of the byte stream that contains the protobuf stream
 * @param payloadlen length of the byte stream
 */
void SvcFirmware_MessageHandler_UpdateNotification(void *payload_p, size_t payloadlen, TransportPipe_t eTransportPipe)
{
	m_eMsgTransportPipe = eTransportPipe;
	pImageBuf = (uint8_t *)__sample_buffer; //We always use the sample buffer for storage

	// Prepare RX message header
	MsgRxUpdateNot = SvcFirmwareUpdateNotificationMsg_Init;

	memset(&StateFw.dataMsgRxSourceId,0,sizeof(StateFw.dataMsgRxSourceId));// zero the sourceID so it sure ends with a zero byte !

	MsgRxUpdateNot.source_id.funcs.decode = &SvcDataMsg_HdrDecodeSourceId;
	MsgRxUpdateNot.source_id.arg = &StateFw.dataMsgRxSourceId;

	MsgRxUpdateNot.device_type.funcs.decode = &SvcDataMsg_HdrDecodeDeviceType;
	MsgRxUpdateNot.device_type.arg = &StateFw.dataMsgRxDeviceType;

	switch(m_eMsgTransportPipe)
	{
	case eTRANSPORT_PIPE_CLI:
		if(payloadlen == sizeof(MsgRxUpdateNot))
		{
			SvcFirmware_SetMaxFwOtaBlockSize(binaryCLI_GetMaxPacketSize());
			memcpy(&MsgRxUpdateNot, payload_p, sizeof(MsgRxUpdateNot));
			DoHandle_UpdateNotification();
		}
		else
		{
			LOG_DBG(LOG_LEVEL_COMM, "ERROR eTRANSPORT_PIPE_CLI but payloadlen:%d != SKF_SvcFirmwareUpdateNotification:%d\n", payloadlen, sizeof(SKF_SvcFirmwareUpdateNotification));
		}
		break;

	case eTRANSPORT_PIPE_MQTT:
		{
			pb_istream_t istream = pb_istream_from_buffer(payload_p, payloadlen);

			if (pb_decode(&istream, SKF_SvcFirmwareUpdateNotification_fields, &MsgRxUpdateNot))
			{
				// Only handle message if device type are equal
				const DataDef_t* dataDef_deviceType_p = getDataDefElementById(DR_Device_Type);

				if(strcmp((char*)dataDef_deviceType_p->address, (char*)StateFw.dataMsgRxDeviceType) == 0)
				{
					SvcFirmware_SetMaxFwOtaBlockSize(SvcFirmware_GetMqttMaxFwOtaBlockSize());
					DoHandle_UpdateNotification();
				}
				else
				{
					LOG_DBG(LOG_LEVEL_COMM,"ERROR Invalid firmware update notification\n");
					//TODO notify backend of failure
				}
			}
			else
			{
				LOG_DBG(LOG_LEVEL_COMM, "ERROR decode SvcFirmwareUpdateNotification: %s\n", PB_GET_ERROR(&istream));
			}
		}
		break;

	default:
		LOG_DBG(LOG_LEVEL_COMM, "ERROR %s, Unsupported eTransportPipe: %d\n", __func__, m_eMsgTransportPipe);
		break;
	}
}

/**
 * SvcFirmware_MessageHandler_BlockReply
 *
 * @brief Main Message handler for SvcFirmare Block Reply, decodes protobuf information into IDEF SvcFirmwareBlockReply
 * @param payload_p points to the start of the byte stream that contains the protobuf stream
 * @param payloadlen length of the byte stream
 */
void SvcFirmware_MessageHandler_BlockReply(void *payload_p, size_t payloadlen, TransportPipe_t eTransportPipe)
{
	if(m_eMsgTransportPipe == eTransportPipe)
	{
		// Prepare RX message header
		MsgRxBlockRep = SvcFirmwareBlockRep_Init;

		// The source_id is part of the Header, the callback is set here
		// because the State structure, which also contains the source_id,
		// is declared static in this file and contains a semaphore
		memset(&StateFw.dataMsgRxSourceId,0,sizeof(StateFw.dataMsgRxSourceId));// zero the sourceID so it sure ends with a zero byte !

		MsgRxBlockRep.source_id.funcs.decode = &SvcDataMsg_HdrDecodeSourceId;
		MsgRxBlockRep.source_id.arg = &StateFw.dataMsgRxSourceId;

		MsgRxBlockRep.device_type.funcs.decode = &SvcDataMsg_HdrDecodeDeviceType;
		MsgRxBlockRep.device_type.arg = &StateFw.dataMsgRxDeviceType;

		// Need to store the Image data somewhere, reusing the sampling buffer
		MsgRxBlockRep.image.funcs.decode = &SvcFirmwareMsg_DecodeImageIdef;
		uint32_t tmpSizeOfImage = 0;
		MsgRxBlockRep.image.arg = (void*) &tmpSizeOfImage;

		bool bIsSuccess = false;
		switch(m_eMsgTransportPipe)
		{
		case eTRANSPORT_PIPE_MQTT:
			{
				pb_istream_t istream = pb_istream_from_buffer(payload_p, payloadlen);
				if (pb_decode(&istream, SKF_SvcFirmwareBlockReply_fields, &MsgRxBlockRep))
				{
					bIsSuccess = true;
				}
				else
				{
					LOG_DBG( LOG_LEVEL_COMM, "ERROR decode SvcFirmwareBlockReply: %s\n", PB_GET_ERROR(&istream) );
				}
			}
			break;
		case eTRANSPORT_PIPE_CLI:
			memcpy(&MsgRxBlockRep, payload_p, sizeof(SKF_SvcFirmwareBlockReply));
			if(SvcFirmwareMsg_DecodeImageRaw((uint8_t*)payload_p + sizeof(SKF_SvcFirmwareBlockReply), payloadlen - sizeof(SKF_SvcFirmwareBlockReply)))
			{
				tmpSizeOfImage = payloadlen - sizeof(SKF_SvcFirmwareBlockReply);
				bIsSuccess = true;
			}
			break;
		default:
			LOG_DBG(LOG_LEVEL_COMM, "ERROR %s, Unsupported eTransportPipe: %d\n", __func__, m_eMsgTransportPipe);
			break;
		}

		if(bIsSuccess)
		{
			// Show all messages on CLI during debug
			#ifdef DEBUG
				if (dbg_logging & LOG_LEVEL_COMM)
				{
					printf("SvcFirmware: RX SvcFirmwareBlockReply\n");

					printf("MsgRxBlockRep.version = %u\n", MsgRxBlockRep.version);
					printf("MsgRxBlockRep.source_id   = %s\n", StateFw.dataMsgRxSourceId );
					printf("MsgRxBlockRep.message_id  = %u\n", MsgRxBlockRep.message_id);
					printf("MsgRxBlockRep.device_type = %s\n", StateFw.dataMsgRxDeviceType);
					printf("MsgRxBlockRep.hardware_version  = %u\n", MsgRxBlockRep.hardware_version);
					printf("MsgRxBlockRep.firmware_version  = %u\n", MsgRxBlockRep.firmware_version);
					printf("MsgRxBlockRep.byte_index  = %u\n", MsgRxBlockRep.byte_index);
					printf("MsgRxBlockRep.image_size  = %u\n", MsgRxBlockRep.image_size);
				}
			#endif
			// TODO fix for not able to send zero defect ontime 14344
			blockReplyInfo.byteIndex = MsgRxBlockRep.byte_index -1;
			blockReplyInfo.fwVersion = MsgRxBlockRep.firmware_version;
			blockReplyInfo.hwVersion = MsgRxBlockRep.hardware_version;
			blockReplyInfo.size = tmpSizeOfImage;
			#ifdef DEBUG
				if (dbg_logging & LOG_LEVEL_COMM)
				{
					printf("blockReplyInfo.byteIndex  = %u\n", blockReplyInfo.byteIndex);
					printf("blockReplyInfo.size  = %u\n",blockReplyInfo.size);
				}
			#endif
			handleCallback(SvcFirmware_cb_blockReply,(void *) &blockReplyInfo);
		}
	}
	else
	{
		LOG_DBG(LOG_LEVEL_COMM, "ERROR Block reply from unexpected pipe:%d, expected pipe:%ds\n", eTransportPipe, m_eMsgTransportPipe);
	}
}



int SvcFirmware_Init( MQTTClient *client_p, ISvcFirmwareConfig_t *config_p )
{
    StateFw.mqttClient_p = client_p;
    StateFw.svcState = ISVCFIRMWARE_STATE_STARTED;
    StateFw.apiMutex = xSemaphoreCreateMutex();

    StateFw.mqttQosDefault = QOS0; // Default non-confirmed PUBLISH (at most once delivery)
    StateFw.mqttDupDefault = 0;    // At QOS0, DUP must be 0
    StateFw.mqttRetainDefault = 0; // Don't retain the message at the broker by default

    StateFw.dataMsgTxLastId = 0; // Access this value with the DATAMSG_GETNEXTID macro

    // Construct SvcData MQTT TOPIC (Check that it's not over 32 bytes including \0

    // subscribe to {company}/{service}/{Destination}, and destination is the deviceId
    // this string is needed more than once to check incoming messages, if it is for 'us' etc.
    strncpy(MQTTTopic_SvcFirmware, (char *) mqttGetSubTopic(), sizeof(MQTTTopic_SvcFirmware));

    return ISVCFIRMWARERC_OK;
}

ISvcFirmwareRc_t ISvcFirmware_Start( void )
{
//    int rc = -1;
	ISvcDataRc_t rcSvcData;

	// SvcFirmware is not subscribing to a separate topic but instead uses
	// SvcData topic and MQTT client therefore it will start SvcData service
	rcSvcData = ISvcData_Start();
	if(rcSvcData == ISVCDATARC_OK)
		return ISVCFIRMWARERC_OK;
	else if(rcSvcData == ISVCDATARC_ERR_PARAM)
		return ISVCFIRMWARERC_ERR_PARAM;
	else
		return ISVCFIRMWARERC_ERR_STATE;

}

ISvcFirmwareRc_t ISvcFirmware_Stop( void )
{
//    int rc = -1;

	ISvcDataRc_t rcSvcData;

	// SvcFirmware is not subscribing to a separate topic but instead uses
	// SvcData topic and MQTT client therefore it will stop SvcData service
	rcSvcData = ISvcData_Stop();
	if(rcSvcData == ISVCDATARC_OK)
		return ISVCFIRMWARERC_OK;
	else if(rcSvcData == ISVCDATARC_ERR_BUSY)
		return ISVCFIRMWARERC_ERR_BUSY;
	else
		return ISVCFIRMWARERC_ERR_STATE;

    return ISVCFIRMWARERC_OK;
}


/**
 * ISvcFirmware_Block_Request
 *
 * @brief Send message: Block Request
 * This function sends a block request
 *
 *
 * @param byteIndex
 * @param blockSize
 * @return
 *   ISVCFIRMWARERC_OK
 *   ISVCFIRMWARERC_ERR_STATE
 */
ISvcFirmwareRc_t ISvcFirmware_Block_Request( uint32_t byteIndex, uint32_t blockSize, uint32_t firmwareVersion)
{
#ifdef PROTOBUFTEST
    // in testing mode, we do not use mqtt, so no need to check for that
#else
    if ( StateFw.svcState == ISVCFIRMWARE_STATE_DISABLED )
        return ISVCFIRMWARERC_ERR_STATE;
#endif
	// Take API Mutex
	if (xSemaphoreTake(StateFw.apiMutex, 100) == pdFALSE)
	{
		return ISVCFIRMWARERC_ERR_BUSY;
	}

	// Construct message
	MsgTxBlockReq = SvcFirmwareBlockReq_Init;

	MsgTxBlockReq.version = SVC_DATA_PROTOCOL_VERSION;

	const DataDef_t* dataDef_sourceId_p = getDataDefElementById(MQTT_DEVICEID);

	MsgTxBlockReq.source_id.funcs.encode = &SvcDataMsg_EncodeString;
	MsgTxBlockReq.source_id.arg = (void *) dataDef_sourceId_p;

	MsgTxBlockReq.message_id = DATAMSG_GETNEXTID(StateFw.dataMsgTxLastId);

	// retrieve data type from datastore
	const DataDef_t* dataDef_deviceType_p = getDataDefElementById(DR_Device_Type);

	// Use data store element
	MsgTxBlockReq.device_type.funcs.encode = &SvcDataMsg_EncodeString;
	MsgTxBlockReq.device_type.arg = (void *) dataDef_deviceType_p;

	MsgTxBlockReq.hardware_version = Device_GetHardwareVersion();
	MsgTxBlockReq.firmware_version = firmwareVersion;
	MsgTxBlockReq.byte_index = byteIndex;
	MsgTxBlockReq.block_size = blockSize;

	size_t nBytesToWrite = 0;
	int nResultCode = ISVCFIRMWARERC_ERR_FATAL;
	switch(m_eMsgTransportPipe)
	{
	case eTRANSPORT_PIPE_MQTT:
		{
			TxBuf[0] = SVCDATA_MQTT_SERDES_ID_GPB2_1;
			TxBuf[1] = ((MSG_TYPE_FIRMWARE_BLOCK_REQUEST >> 8) & 0xff);
			TxBuf[2] = (MSG_TYPE_FIRMWARE_BLOCK_REQUEST & 0xff);
			pb_ostream_t stream = pb_ostream_from_buffer(&TxBuf[3], sizeof(TxBuf)-3); // Skip preamble + msgType
			if (pb_encode(&stream, SKF_SvcFirmwareBlockRequest_fields, &MsgTxBlockReq))
			{
				nBytesToWrite = stream.bytes_written;
				nBytesToWrite += 3; // Add length of preamble + msg_type
				nResultCode = ISVCFIRMWARERC_OK;
			}
			else
			{
				LOG_DBG( LOG_LEVEL_COMM, "pb_encode error! (fatal) pbencode error: %s\n", stream.errmsg ? stream.errmsg : "unknown" );
			}
		}
		break;
	case eTRANSPORT_PIPE_CLI:
		nBytesToWrite = sizeof(MsgTxBlockReq);
		nResultCode = ISVCFIRMWARERC_OK;
		break;
	default:
		LOG_DBG(LOG_LEVEL_COMM, "ERROR %s, Unsupported eTransportPipe: %d\n", __func__, m_eMsgTransportPipe);
		break;
	}

	if ((nResultCode == ISVCFIRMWARERC_OK) && (nBytesToWrite > 0))
	{
#ifdef PROTOBUFTEST
		// throw it in the decoder directly, do not send over mqtt
		// Handle payload content
		//SvcData_MessageHandler( TxBuf, nBytesToWrite );
#else
		switch(m_eMsgTransportPipe)
		{
		case eTRANSPORT_PIPE_MQTT:
			if(SvcData_PublishMQTTMessage((void*)TxBuf, nBytesToWrite) != 0)
			{
				nResultCode = ISVCFIRMWARERC_ERR_MQTT;
			}
			break;
		case eTRANSPORT_PIPE_CLI:
			if(!binaryCLI_sendPacket(E_BinaryCli_PacketRequest, &MsgTxBlockReq, nBytesToWrite))
			{
				nResultCode = ISVCFIRMWARERC_ERR_MQTT;
			}
			break;
		default:
			LOG_DBG(LOG_LEVEL_COMM, "ERROR %s, Unsupported eTransportPipe: %d\n", __func__, m_eMsgTransportPipe);
			break;
		}
#endif
	}

	// Release API Mutex
	xSemaphoreGive(StateFw.apiMutex);
    return nResultCode;
}


#endif


#ifdef __cplusplus
}
#endif