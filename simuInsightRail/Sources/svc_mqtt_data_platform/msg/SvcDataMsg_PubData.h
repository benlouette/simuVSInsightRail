#ifdef __cplusplus
extern "C" {
#endif

/*
 * SvcDataMsg_PubData.h
 *
 *  Created on: 28 mrt. 2016
 *      Author: Daniel van der Velde
 */

#ifndef SOURCES_SVC_MQTT_DATA_PLATFORM_MSG_SVCDATAMSG_PUBDATA_H_
#define SOURCES_SVC_MQTT_DATA_PLATFORM_MSG_SVCDATAMSG_PUBDATA_H_

/*
 * Includes
 */
#include "pb.h"
#include "svccommon.pb.h"    // Generated from svccommon.proto
#include "svcdata.pb.h"      // Generated from svcdata.proto
#include "configIDEF.h"


// device limit on the number of incomming parameters in one getDataRequest
#define MAX_GETDATAPARAMS (50)

/*
 * Functions
 */
//bool SvcDataMsg_EncodeParameterValue(pb_ostream_t *stream_p, const pb_field_t *field, void * const *arg);
//bool SvcDataMsg_DecodeParameterValue(pb_istream_t *stream_p, const pb_field_t *field, void **arg);


bool SvcDataMsg_PublishEncodeData(pb_ostream_t *stream_p, SKF_SvcDataMsg *msg_p);
bool SvcDataMsg_PublishDecodeData(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p);

bool SvcDataMsg_RequestDecodeStoreData(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p);
bool SvcDataMsg_ReplyDecodeStoreData(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p, int32_t *result_code_p);


bool SvcDataMsg_RequestDecodeGetData(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p, SvcDataParamValueGroup_t * paramGroupList_p );
bool SvcDataMsg_ReplyDecodeGetData(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p);

#endif /* SOURCES_SVC_MQTT_DATA_PLATFORM_MSG_SVCDATAMSG_PUBDATA_H_ */


#ifdef __cplusplus
}
#endif