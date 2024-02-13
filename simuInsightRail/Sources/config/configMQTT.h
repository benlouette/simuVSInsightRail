#ifdef __cplusplus
extern "C" {
#endif

/*
 * configMQTT.h
 *
 *  Created on: 22 dec. 2015
 *      Author: Daniel van der Velde
 */

#ifndef SOURCES_CONFIG_CONFIGMQTT_H_
#define SOURCES_CONFIG_CONFIGMQTT_H_

#include "NvmConfig.h"

/* Buffer sizes */
#define CONFIG_MQTT_SEND_BUFFER_SIZE             (2048*2)	//(1024)
#define CONFIG_MQTT_RECV_BUFFER_SIZE             (2048*2)	//(1024)

#define MQTT_MAX_TOPIC_LEN  (MQTT_TOPIC_PREFIX_LEN + MQTT_DEVICEID_LEN)

#if 0
/* MQTT Topics, obsolete after the introduction of datastore/nvmconfig */
#define CONFIG_MQTT_SUBTOPIC_PREFIX_DEVICE       "SKF/Device/"
#define CONFIG_MQTT_SUBTOPIC_PREFIX_FIRMWARE     "SKF/Firmware/"
#define CONFIG_MQTT_SUBTOPIC_PREFIX_EPHEMERIS    "SKF/Ephemeris/"

#define CONFIG_MQTT_PUBTOPIC_PREFIX_DEVICE       CONFIG_MQTT_SUBTOPIC_PREFIX_DEVICE
#define CONFIG_MQTT_PUBTOPIC_PREFIX_FIRMWARE     CONFIG_MQTT_SUBTOPIC_PREFIX_FIRMWARE
#define CONFIG_MQTT_PUBTOPIC_PREFIX_EPHEMERIS    CONFIG_MQTT_SUBTOPIC_PREFIX_EPHEMERIS
#endif

const char kstrOTACompleteSubTopic[] = "/OTAComplete";
const char kstrImagesManifestUpdateSubTopic[] = "/ImagesManifestUpdate";

void mqttGetServerConfig(uint8_t **url, uint16_t *port, bool *secure);
uint8_t *mqttGetDeviceId();
char *mqttGetPubTopic();
char *mqttConstructTopicFromSubTopic(const char *pstrSubTopic);
char *mqttGetSubTopic();
char *mqttGetLogTopic();
char *mqttGetEpoTopic();
char *mqttGetPubEpoTopic();



#endif /* SOURCES_CONFIG_CONFIGMQTT_H_ */


#ifdef __cplusplus
}
#endif