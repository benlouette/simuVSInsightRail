#ifdef __cplusplus
extern "C" {
#endif

/*
 * SvcData.h
 *
 *  Created on: 23 dec. 2015
 *      Author: Daniel van der Velde
 */

#ifndef SOURCES_SVC_MQTT_DATA_H_
#define SOURCES_SVC_MQTT_DATA_H_

/*
 * Includes
 */
#include "MQTTClient.h"
#include "configIDEF.h"

/*
 * Macros
 */
// Ephemeris MACROS
#define EPO_BUFSIZE	2048 // Do not change this value. Hard coded in back end to minimize com. timeout.
#define EPO_DAYS	14

// DATA Message ID generation
#define DATAMSG_GETNEXTID(m_lastidvar)       (++m_lastidvar)

// version for the IDEF protocol version, used in the message header
#define SVC_DATA_PROTOCOL_VERSION  (0x0200)

#define MAXSOURCEIDSIZE (32)

// MQTT Data Service Serialization Identifiers:
#define SVCDATA_MQTT_SERDES_ID_IBIN           (0x00)
#define SVCDATA_MQTT_SERDES_ID_GPB2           (0x01)
#define SVCDATA_MQTT_SERDES_ID_GPB2_1         (0x02)

const typedef enum {
	MSG_TYPE_UNKNOWN = 0,
	MSG_TYPE_REPLY = 1,
	MSG_TYPE_DATA_REQUEST = 2,
	MSG_TYPE_DATA_STORE_REPLY = 3,
	MSG_TYPE_DATA_STORE_REQUEST = 4,
	MSG_TYPE_BLOCK_DATA_MESSAGE = 5,
	MSG_TYPE_BLOCK_DATA_REPLY = 6,
	MSG_TYPE_FIRMWARE_UPDATE_NOTIFICATION = 7,
	MSG_TYPE_FIRMWARE_BLOCK_REQUEST = 8,
	MSG_TYPE_FIRMWARE_BLOCK_REPLY = 9
} IDEF_MSG_TYPE;

/*
 * Types
 */
#if 1
typedef struct {
    int todo;
} SvcDataConfig_t;
#endif



/*
 * Data
 */

/*
 * Functions
 */

int SvcData_Init( MQTTClient *client_p, SvcDataConfig_t *config_p );
int SvcData_PublishMQTTMessage( void *payload_p, int payloadlen );

bool svcData_GetBlockInfo(uint32_t * offset_p, uint32_t * maxNrElements_p);

void SvcData_SetTimestamp(uint64_t timestamp);

// function prototypes for Ephemeris download
int EPO_Start( void );
int EPO_Wait( void );
int EPO_Stop( void );

#endif /* SOURCES_SVC_MQTT_DATA_H_ */



#ifdef __cplusplus
}
#endif