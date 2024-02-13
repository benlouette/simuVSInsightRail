#ifdef __cplusplus
extern "C" {
#endif

/*
 * xTaskCommInt.h
 *
 *  Created on: 5 jan. 2016
 *      Author: Daniel
 */

#ifndef SOURCES_COMM_MQTT_PLATFORM_TASKCOMMINT_H_
#define SOURCES_COMM_MQTT_PLATFORM_TASKCOMMINT_H_

/*
 * Includes
 */
#include "MQTTNetwork.h"

/*
 * Types
 */

/*
 * Data
 */

/*
 * Functions
 */

MQTT_Network *CommMQTT_GetNetwork(void);

void CommMQTT_ErrorHandler(MQTT_Network *n, int err);

uint16_t CommMQTT_GetNextMsgId(void);



#endif /* SOURCES_COMM_MQTT_PLATFORM_TASKCOMMINT_H_ */


#ifdef __cplusplus
}
#endif