#ifdef __cplusplus
extern "C" {
#endif

/*
 * configMQTT.c
 *
 *  Created on: Apr 13, 2016
 *      Author: George de Fockert
 */
#include <string.h>
#include <stdio.h>

#include "NvmConfig.h"

#include "configMQTT.h"
#include "fsl_misc_utilities.h"



void mqttGetServerConfig(uint8_t ** url, uint16_t * port, bool * secure)
{
    uint8_t service;

    service = gNvmCfg.dev.mqtt.service % MQTT_SERVICEPROFILES;
    if (url)    *url    = (uint8_t *) gNvmCfg.dev.mqtt.serviceProfile[service].url;
    if (port)   *port   = gNvmCfg.dev.mqtt.serviceProfile[service].portnr;
    if (secure) *secure = gNvmCfg.dev.mqtt.serviceProfile[service].secure;
}


uint8_t * mqttGetDeviceId()
{
    if (strlen( (char *) gNvmCfg.dev.mqtt.deviceId) > 0) return  gNvmCfg.dev.mqtt.deviceId;
    else return NULL;
}


// TODO: not happy with this static storage.
// gs=global static.
// This array is NOT always NULL terminated.
static char gs_topic[MQTT_TOPIC_PREFIX_LEN + MQTT_DEVICEID_LEN + 1];

// Note:
// some MQTT clients/brokers can handle a '/' at the end of a topic, some not, so :
// we must avoid a '/' at the end of a topic !
//

// construct topic out of prefix
// publish to the publish topic (without device id appended)
char * mqttGetPubTopic()
{
    uint32_t idx;
    // strncpy may end the string with NULL but not always.
    strncpy(gs_topic, (char *) gNvmCfg.dev.mqtt.pubTopicPrefix, MIN(sizeof(gNvmCfg.dev.mqtt.pubTopicPrefix), sizeof(gs_topic)));

    // Make sure the array ends with NULL since we can't use strlen() below if it doesn't.
    gs_topic[sizeof(gs_topic) - 1] = '\0';

    //strncat(topic, (char *) gNvmCfg.dev.mqtt.deviceId, sizeof(topic) - strlen(topic));
    idx = strlen(gs_topic);
    if (gs_topic[idx-1]== '/') {
        gs_topic[idx-1] = '\0'; // see note above about topic ending with '/'
    }

    return gs_topic;
}

// Construct publish topic from the given subtopic
char *mqttConstructTopicFromSubTopic(const char *pstrSubTopic)
{
    // strncpy may end the string with NULL but not always.
	strncpy(gs_topic, (char *) gNvmCfg.dev.mqtt.pubTopicPrefix, MIN(sizeof(gNvmCfg.dev.mqtt.pubTopicPrefix), sizeof(gs_topic)));

    // Make sure the array ends with NULL since we can't use strlen() below if it doesn't.
    gs_topic[sizeof(gs_topic) - 1] = '\0';

	// Copy what fits of "src" into "dst". strncat() writes n+1 bytes to the array incl. NULL.
	strncat(gs_topic, pstrSubTopic, sizeof(gs_topic) - strlen(gs_topic) - 1);
	return gs_topic;
}

// construct topic out of prefix and deviceId
// listen to <pubtopicprefix>/<deviceID>
char * mqttGetTopic(char *topic, int size_topic)
{
    // strncpy may end the string with NULL but not always.
    strncpy(gs_topic, topic, MIN(sizeof(gs_topic), size_topic));

    // Make sure the array ends with NULL since we can't use strlen() below if it doesn't.
    gs_topic[sizeof(gs_topic) - 1] = '\0';

    uint32_t idx = strlen(gs_topic);
    if ((gs_topic[idx-1] != '/') && (idx < sizeof(gs_topic)-1))	{
        gs_topic[idx] = '/'; // make sure the topic has a '/' between them.
    }

    if (gNvmCfg.dev.mqtt.deviceId[0] == '\0') {
        if  (strlen((char *) gNvmCfg.dev.modem.imei)>0) {
            // now construct the device id to be used by mqtt (and later idef)

            // Copy IMEI incl. NULL so that we can use strlen() below.
            // man snprintf: The functions snprintf() and vsnprintf() write at most size bytes (including the terminating null byte ('\0')) to str.
            snprintf((char *)gNvmCfg.dev.mqtt.deviceId, sizeof(gNvmCfg.dev.mqtt.deviceId), "IMEI");

            // Copy what fits of "src" into "dst". strncat() writes n+1 bytes to the array incl. NULL.
            strncat((char *)gNvmCfg.dev.mqtt.deviceId, (char *) gNvmCfg.dev.modem.imei, MIN(sizeof(gNvmCfg.dev.modem.imei), sizeof(gNvmCfg.dev.mqtt.deviceId) - strlen((char*)gNvmCfg.dev.mqtt.deviceId) - 1));
            // cli not yet initialised !! LOG_DBG(LOG_LEVEL_COMM,"mqttGetSubTopic: deviceId empty, constructing out of IMEI number : %s\n",(char *)gNvmCfg.dev.mqtt.deviceId);
        } else {
            // cli not yet initialised !! LOG_DBG(LOG_LEVEL_COMM,"mqttGetSubTopic: IMEI is empty, cannot construct subscribe topic !\n");
        }
    }
    if (gNvmCfg.dev.mqtt.deviceId[0] == '/') {
        // Copy what fits of "src" into "dst". strncat() writes n+1 bytes to the array incl. NULL.
        strncat(gs_topic, (char *) &gNvmCfg.dev.mqtt.deviceId[1], MIN(sizeof(gNvmCfg.dev.mqtt.deviceId) - 1, sizeof(gs_topic) - strlen(gs_topic) - 1));
    } else {
        // Copy what fits of "src" into "dst". strncat() writes n+1 bytes to the array incl. NULL.
        strncat(gs_topic, (char *) gNvmCfg.dev.mqtt.deviceId, MIN(sizeof(gNvmCfg.dev.mqtt.deviceId), sizeof(gs_topic) - strlen(gs_topic) - 1));
    }

    idx = strlen(gs_topic);
    if (idx>0) {
        if ( gs_topic[idx-1]== '/') {
            gs_topic[idx-1] = '\0'; // see note above about topic ending with '/'
        }
    }

    return gs_topic;
}

// construct topic out of prefix and deviceId
// subscribe to <subTopicPrefix>/<deviceID>
char * mqttGetSubTopic()
{
	return mqttGetTopic((char*)gNvmCfg.dev.mqtt.subTopicPrefix, sizeof(gNvmCfg.dev.mqtt.pubTopicPrefix));
}

// construct topic out of prefix and deviceId
// log to <logtopicprefix>/<deviceID>
char * mqttGetLogTopic()
{
    return mqttGetTopic((char*)gNvmCfg.dev.mqtt.logTopicPrefix, sizeof(gNvmCfg.dev.mqtt.logTopicPrefix));
}

// construct topic out of prefix and deviceId
// subscribe to <epoTopicPrefix>/<deviceID>
char * mqttGetEpoTopic()
{
    return mqttGetTopic((char*)gNvmCfg.dev.mqtt.epoTopicPrefix, sizeof(gNvmCfg.dev.mqtt.epoTopicPrefix));
}

// construct topic out of prefix
// publish to the Ephemeris topic (without device id appended)
char * mqttGetPubEpoTopic()
{
	uint8_t epoTopic[MQTT_TOPIC_PREFIX_LEN] = "MTK";

    // strncpy may end the string with NULL but not always.
    strncpy(gs_topic, (char *)gNvmCfg.dev.mqtt.epoTopicPrefix, MIN(sizeof(gs_topic), sizeof(gNvmCfg.dev.mqtt.epoTopicPrefix)));

    // Make sure the array ends with NULL since we can't use strlen() below if it doesn't.
    gs_topic[sizeof(gs_topic) - 1] = '\0';

    // Copy what fits of "src" into "dst". strncat() writes n+1 bytes to the array incl. NULL.
    strncat(gs_topic, (char *)epoTopic, MIN(sizeof(epoTopic), sizeof(gs_topic) - strlen(gs_topic) - 1));

    uint32_t length = strlen(gs_topic);
    if (gs_topic[length-1]== '/') {
        gs_topic[length-1] = '\0'; // see note above about topic ending with '/'
    }

    return gs_topic;
}




#ifdef __cplusplus
}
#endif