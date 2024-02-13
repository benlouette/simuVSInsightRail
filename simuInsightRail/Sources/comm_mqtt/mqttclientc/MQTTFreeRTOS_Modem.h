#ifdef __cplusplus
extern "C" {
#endif

/*
 * MQTTFreeRTOS_Modem.h
 *
 *  Created on: 27 dec. 2015
 *      Author: Daniel van der Velde
 */

#ifndef SOURCES_COMM_MQTT_PLATFORM_MQTTCLIENTC_MQTTFREERTOS_MODEM_H_
#define SOURCES_COMM_MQTT_PLATFORM_MQTTCLIENTC_MQTTFREERTOS_MODEM_H_

/*
 * Includes
 */
#include "configComm.h"

#if (CONFIG_COMM_LOWER_STACK_SELECT == CONFIG_COMM_LOWER_STACK_SELECT_MODEM)

#include "MQTTNetwork.h"
#include "MQTTFreeRTOS.h"

/*
 * Types
 */

/*
 * Functions
 */

int MQTTFreeRTOS_Modem_Read( MQTT_Network*, unsigned char*, int, int );
int MQTTFreeRTOS_Modem_Write( MQTT_Network*, unsigned char*, int, int );

void MQTTFreeRTOS_Modem_NetworkInit( MQTT_Network* );
int MQTTFreeRTOS_Modem_NetworkConnect( MQTT_Network*, char*, int );
//int MQTTFreeRTOS_Modem_NetworkConnectTLS( MQTT_Network*, char*, int, ... ); // TODO MQTT TLS
void MQTTFreeRTOS_Modem_Disconnect( MQTT_Network* );

#endif // not configured

#endif /* SOURCES_COMM_MQTT_PLATFORM_MQTTCLIENTC_MQTTFREERTOS_MODEM_H_ */


#ifdef __cplusplus
}
#endif