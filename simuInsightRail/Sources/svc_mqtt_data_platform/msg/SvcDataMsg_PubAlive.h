#ifdef __cplusplus
extern "C" {
#endif

/*
 * SvcDataMsg_PubAlive.h
 *
 *  Created on: 28 mrt. 2016
 *      Author: Daniel van der Velde
 */

#ifndef SOURCES_SVC_MQTT_DATA_PLATFORM_MSG_SVCDATAMSG_PUBALIVE_H_
#define SOURCES_SVC_MQTT_DATA_PLATFORM_MSG_SVCDATAMSG_PUBALIVE_H_

/*
 * Includes
 */
#include "pb.h"
#include "svccommon.pb.h"    // Generated from svccommon.proto
#include "svcdata.pb.h"      // Generated from svcdata.proto

/*
 * Functions
 */
bool SvcDataMsg_PublishEncodeAlive(pb_ostream_t *stream_p, SKF_SvcDataMsg *msg_p);
bool SvcDataMsg_PublishDecodeAlive(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p);


#endif /* SOURCES_SVC_MQTT_DATA_PLATFORM_MSG_SVCDATAMSG_PUBALIVE_H_ */


#ifdef __cplusplus
}
#endif