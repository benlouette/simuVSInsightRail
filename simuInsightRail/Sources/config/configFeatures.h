#ifdef __cplusplus
extern "C" {
#endif

/*
 * configFeatures.h
 *
 *  Created on: Dec 9, 2015
 *      Author: George de Fockert
 */

#ifndef SOURCES_CONFIG_CONFIGFEATURES_H_
#define SOURCES_CONFIG_CONFIGFEATURES_H_

/*
 * here the defines for the insight platform modules to include in the project
 *
 * comment out what is not needed
 */

#define CONFIG_PLATFORM_CLI

#define CONFIG_PLATFORM_EVENT_LOG

#define CONFIG_PLATFORM_MODEM

#define CONFIG_PLATFORM_OTA

#define CONFIG_PLATFORM_MEASURE

#define PLATFORM_IS25
#define CONFIG_PLATFORM_GNSS

// only enable use for MQTT testing
#define CONFIG_PLATFORM_COMM

//#define CONFIG_PLATFORM_ETHERNET

#define CONFIG_PLATFORM_SVCDATA

#define CONFIG_PLATFORM_SVCFIRMWARE

// creates extra data structures/idef messages to test the whole mqtt/idef communication path
#define CONFIG_PLATFORM_IDEFSVCTESTDATA

#define CONFIG_PLATFORM_NFC

#endif /* SOURCES_CONFIG_CONFIGFEATURES_H_ */


#ifdef __cplusplus
}
#endif