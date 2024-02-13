#ifdef __cplusplus
extern "C" {
#endif

/*
 * configSvcData.h
 *
 *  Created on: 28 mrt. 2016
 *      Author: Daniel van der Velde
 */

#ifndef SOURCES_CONFIG_CONFIGSVCDATA_H_
#define SOURCES_CONFIG_CONFIGSVCDATA_H_

/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>

/*
 * Functions
 */

/**
 * The following functions must be implemented in the (project-specific) functions defined
 * below.
 */

/**
 * ConfigSvcData_GetIDEFTime
 *
 * @return IDEF datetime-stamp in 100ns counts since UNIX epoch in UTC.
 */
uint64_t ConfigSvcData_GetIDEFTime(void);

void ConfigSvcData_PrintIDEFTime(uint64_t idefTime);

/**
 * ConfigSvcData_GetSourceId
 * @return Pointer to fixed 16 bytes holding the sourceId
 */
uint8_t *ConfigSvcData_GetSourceId(void);


/**
 * ConfigSvcData_GetTopicPrefix
 * @return Pointer to fixed 16 bytes holding the sourceId
 */
uint8_t *ConfigSvcData_GetTopicPrefix(void);


#endif /* SOURCES_CONFIG_CONFIGSVCDATA_H_ */


#ifdef __cplusplus
}
#endif