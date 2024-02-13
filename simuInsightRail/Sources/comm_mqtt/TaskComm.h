#ifdef __cplusplus
extern "C" {
#endif

/*
 * TaskComm.h
 *
 *  Created on: 23 dec. 2015
 *      Author: Daniel van der Velde
 */

#ifndef SOURCES_COMM_MQTT_PLATFORM_TASKCOMM_H_
#define SOURCES_COMM_MQTT_PLATFORM_TASKCOMM_H_

/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>

#include "TaskCommEvent.h"
/*
 * Macros
 */

#define COMM_ERR_OK                                (0)
#define COMM_ERR_PARAM                             (1)
#define COMM_ERR_STATE                             (2)


/*
 * Types
 */

typedef enum {
    // In disconnected state, there is no communication
    COMM_STATE_DISCONNECTED   = 0,
    // In connected state, communication is possible and MQTT ping requests (PINGREQ)
    // are sent periodically to the server (MQTT broker).
    COMM_STATE_CONNECTED      = 1
} Comm_state_t;

typedef void (*Comm_StateCallback_fn)(Comm_state_t);


/*
 * Data
 */

/*
 * Functions
 */

// Init and RTOS task functions
void TaskComm_Init( void );
void TaskComm( void *pvParameters );

// COMM API
int32_t TaskComm_RegisterHandlers( void ); // TODO Application callbacks

bool TaskComm_InitCommands(tCommHandle * handle);
int32_t TaskComm_WaitReady( tCommHandle * handle, uint32_t maxWaitMs);
int32_t TaskComm_Connect( tCommHandle * handle, uint32_t maxWaitMs );
int32_t TaskComm_Disconnect( tCommHandle * handle, uint32_t maxWaitMs );

int32_t TaskComm_Publish( tCommHandle * handle, void *payload_p, int payloadlen, uint8_t * topic,  uint32_t maxWaitMs );// TODO : debug function at the moment

int32_t TaskComm_SendData( void ); // TODO Generic data references
int32_t TaskComm_FirmwareUpdate( void );


#endif /* SOURCES_COMM_MQTT_PLATFORM_TASKCOMM_H_ */


#ifdef __cplusplus
}
#endif