#ifdef __cplusplus
extern "C" {
#endif

/*
 * xTaskAppEvent.h
 *
 *  Created on: 30 nov. 2015
 *      Author: D. van der Velde
 *
 *  Event definitions for Application Task: Passenger Rail Sensor Node
 *
 */

#ifndef SOURCES_APP_XTASKAPPEVENT_H_
#define SOURCES_APP_XTASKAPPEVENT_H_

/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>

#include "freeRTOS.h"
#include "queue.h"

#include "configIDEF.h"
#include "ISvcData.h"
/*
 * Macros
 */

/*
 * Types
 */

/*
 * Event descriptors
 */
typedef enum
{
    AppEvt_idef_publish = 0,
    AppEvt_idef_PubData,
    AppEvt_idef_storeDataRequest,
    AppEvt_idef_storeDataReply,
    AppEvt_idef_getDataRequest,
    AppEvt_idef_getDataReply,



	AppEvt_idef_firmwareUpdateNotification,
	AppEvt_idef_firmwareBlockReply
} tAppEventDescriptor;

#if 0
defined in ISvcData.h
typedef struct {
    int32_t  message_id;// message ID to use in the reply
    SvcDataParamValueGroup_t * paramGroupList_p;// pointer to the received parameter list information
}    tIdefGetDataReq;
#endif
// placeholder for when we have parameters for a request
typedef  union {
    tIdefGetDataReq IdefGetDataReq;
    tIdefStoreDataReq IdefStoreDataReq;
    int32_t reply_result_code;
} tAppData;

/*
 * to keep the size of the structure small (it is copied to the queue by freeRTOS)
 * the actual request is linked using a pointer
 *
 */
typedef struct {
    tAppEventDescriptor Descriptor;   // Event Descriptor
    tAppData    data;
} tAppEvent;


#endif /* SOURCES_APP_XTASKAPPEVENT_H_ */


#ifdef __cplusplus
}
#endif