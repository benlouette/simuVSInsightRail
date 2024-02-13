#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Copyright (c) 2014, 2015 IBM Corp.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 * The Eclipse Public License is available at
 *    http://www.eclipse.org/legal/epl-v10.html
 * and the Eclipse Distribution License is available at
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Allan Stockdill-Mander - initial API and implementation and/or initial documentation
 *******************************************************************************/
/*
 * MQTTFreeRTOS_lwIP.h
 *
 *
 */
#if !defined(MQTTFREERTOS_LWIP_H)
#define MQTTFREERTOS_LWIP_H

/*
 * Includes
 */
#include "configComm.h"

#if (CONFIG_COMM_LOWER_STACK_SELECT == CONFIG_COMM_LOWER_STACK_SELECT_NET)

#include "MQTTNetwork.h"
#include "MQTTFreeRTOS.h"

#include "lwip/api.h"

/*
 * Types
 */

/*
 * Functions
 */
int MQTTFreeRTOS_lwIP_read(MQTT_Network*, unsigned char*, int, int);
int MQTTFreeRTOS_lwIP_write(MQTT_Network*, unsigned char*, int, int);
void MQTTFreeRTOS_lwIP_disconnect(MQTT_Network*);

void MQTTFreeRTOS_lwIP_NetworkInit(MQTT_Network*);
int MQTTFreeRTOS_lwIP_NetworkConnect(MQTT_Network*, char*, int);
/*int MQTTFreeRTOS_lwIP_NetworkConnectTLS(MQTT_Network*, char*, int, SlSockSecureFiles_t*, unsigned char, unsigned int, char);*/

#endif // not configured

#endif /* MQTTFREERTOS_LWIP_H */


#ifdef __cplusplus
}
#endif