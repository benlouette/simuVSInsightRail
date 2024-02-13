#ifdef __cplusplus
extern "C" {
#endif

/*
 * SvcMqttFirmware.h
 *
 *  Created on: 26 dec. 2015
 *      Author: Daniel van der Velde
 */

#ifndef SOURCES_SVC_MQTT_FIRMWARE_H_
#define SOURCES_SVC_MQTT_FIRMWARE_H_

/*
 * Includes
 */
#include "MQTTClient.h"
#include "ISvcFirmware.h"

#include "pb.h"


/*
 * Macros
 */

/*
 * Types
 */

typedef enum {
    ISVCFIRMWARERC_OK = 0,
    ISVCFIRMWARERC_ERR_BUSY,
    ISVCFIRMWARERC_ERR_PARAM,
    ISVCFIRMWARERC_ERR_STATE,
    ISVCFIRMWARERC_ERR_TIMEOUT,
    ISVCFIRMWARERC_ERR_MQTT,
    ISVCFIRMWARERC_ERR_FATAL
} ISvcFirmwareRc_t;

typedef struct {
    int todo;
} ISvcFirmwareConfig_t;

typedef enum
{
	eTRANSPORT_PIPE_MQTT,
	eTRANSPORT_PIPE_CLI
} TransportPipe_t;

/*
 * Data
 */

/*
 * Functions
 */

ISvcFirmwareRc_t ISvcFirmware_Start( void );
ISvcFirmwareRc_t ISvcFirmware_Stop( void );

uint32_t SvcFirmware_GetMaxFwOtaBlockSize();
uint32_t SvcFirmware_GetMqttMaxFwOtaBlockSize();
void SvcFirmware_SetMaxFwOtaBlockSize(uint32_t nMaxFwOtaBlockSize);
TransportPipe_t SvcFirmware_GetCurrentTransportPipe();

void SvcFirmware_MessageHandler_UpdateNotification( void *payload_p, size_t payloadlen, TransportPipe_t eTransportPipe);
void SvcFirmware_MessageHandler_BlockReply(void *payload_p, size_t payloadlen, TransportPipe_t eTransportPipe);

bool SvcFirmwareMsg_DecodeImageIdef(pb_istream_t *stream_p, const pb_field_t *field_p, void **arg);

int SvcFirmware_Init( MQTTClient *client_p, ISvcFirmwareConfig_t *config_p );


ISvcFirmwareRc_t ISvcFirmware_Block_Request( uint32_t byteIndex, uint32_t blockSize, uint32_t firmwareVersion);
void SvcFirmwareSetCallback(tSvcFirmwareCallbackIndex cbIdx, tSvcFirmwareCallbackFuncPtr cbFunc);
void SvcFirmwareRemoveCallback(tSvcFirmwareCallbackIndex cbIdx, tSvcFirmwareCallbackFuncPtr cbFunc);

#endif /* SOURCES_SVC_MQTT_FIRMWARE_H_ */


#ifdef __cplusplus
}
#endif