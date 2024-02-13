#ifdef __cplusplus
extern "C" {
#endif

/*
 * MQTTNetwork.h
 *
 *  Created on: 27 dec. 2015
 *      Author: Daniel van der Velde
 */

#ifndef SOURCES_COMM_MQTT_PLATFORM_MQTTCLIENTC_MQTTNETWORK_H_
#define SOURCES_COMM_MQTT_PLATFORM_MQTTCLIENTC_MQTTNETWORK_H_

/*
 * Types
 */
enum {
    MQTTNETWORK_ERR_NOCONN = 1
};

typedef struct MQTT_Network MQTT_Network;

struct MQTT_Network
{
    void *conn_p;     // Points to network specific connection data (e.g. socket, netconn, etc.)
    int (*mqttread) (MQTT_Network*, unsigned char*, int, int);
    int (*mqttwrite) (MQTT_Network*, unsigned char*, int, int);
    void (*disconnect) (MQTT_Network*);
    void (*errorhandler) (MQTT_Network*, int);
    void *lastdata_p; // Holds last buffer
    int lastoffset;   // Offset, bytes already read previously
};



#endif /* SOURCES_COMM_MQTT_PLATFORM_MQTTCLIENTC_MQTTNETWORK_H_ */


#ifdef __cplusplus
}
#endif