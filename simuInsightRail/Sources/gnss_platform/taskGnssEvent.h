#ifdef __cplusplus
extern "C" {
#endif

/*
 * taskGnssEvent.h
 *
 *  Created on: Jan 31, 2017
 *      Author: ka3112
 */

#ifndef SOURCES_APP_TASKGNSSEVENT_H_
#define SOURCES_APP_TASKGNSSEVENT_H_


#include "freeRTOS.h"
#include "queue.h"




/*
 * Event descriptors
 */
typedef enum
{
    Gnss_Evt_Undefined = 0,
    Gnss_Evt_GnssDataReceived,   // interrupt routine has colected one complete GNSS message
} t_GnssEventDescriptor;

/*
 * to keep the size of the structure small (it is copied to the queue by freeRTOS)
 * the actual request is linked using a pointer
 *
 */
typedef struct {
    t_GnssEventDescriptor Descriptor;   // Event Descriptor
    struct gnss_buf_str *processing;	// pointer to buffer to process
} t_GnssEvent;


#endif /* SOURCES_APP_TASKGNSSEVENT_H_ */


#ifdef __cplusplus
}
#endif