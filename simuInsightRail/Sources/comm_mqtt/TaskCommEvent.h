#ifdef __cplusplus
extern "C" {
#endif

/*
 * TaskCommEvents.h
 *
 *  Created on: 23 dec. 2015
 *      Author: Daniel van der Velde
 */

#ifndef SOURCES_COMM_TASKCOMMEVENT_H_
#define SOURCES_COMM_TASKCOMMEVENT_H_

/*
 * Includes
 */
#include <FreeRTOS.h>
#include <queue.h>
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
    CommEvt_Undefined = 0,
    CommEvt_Connect = 100,
    CommEvt_Disconnect = 101,
    CommEvt_Publish = 102,
    CommEvt_SvcEvent = 200
} tCommEventDescriptor;

// void *payload_p, int payloadlen, uint8_t * topic,
typedef struct {
    char * topic;
    void * payload;
    uint32_t payloadlen;
} tPublishReq;


// placeholder for when we have parameters for a request
typedef  union {
        tPublishReq PublishReq ;
} tCommReqData;




typedef struct {
    tCommEventDescriptor Descriptor;
    QueueHandle_t replyQueue;

    tCommReqData ReqData;

} tCommEvent;



/*
 * structure for the reply data (currently only the rsult status)
 */

#if 0
// placeholder for when we have more response result than only the status
typedef {
    union {
        struct resp1param resp1 ;
        struct resp2param resp2 ;
        struct resp3param resp3 ;
    } param
} tCommRespData;

#endif

typedef struct  {
    bool rc_ok;
#if 0
    tCommRespData RespData; // no functions with parameters yet
#endif
} tCommResp;

// data local to each task, to be allocated once after boot at the start of each task
// the response of the comm task will put the result of the request here.
typedef struct  {
    QueueHandle_t    EventQueue_CommResp;
    tCommResp    CommResp;
} tCommHandle;

/*
 * Functions
 */



#endif /* SOURCES_COMM_TASKCOMMEVENT_H_ */


#ifdef __cplusplus
}
#endif