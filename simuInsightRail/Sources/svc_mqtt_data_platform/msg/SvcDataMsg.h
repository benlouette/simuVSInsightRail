#ifdef __cplusplus
extern "C" {
#endif

/*
 * SvcDataMsg.h
 *
 *  Created on: 14 mrt. 2016
 *      Author: Daniel van der Velde
 */

#ifndef SOURCES_SVC_MQTT_DATA_PLATFORM_SVCDATAMSG_H_
#define SOURCES_SVC_MQTT_DATA_PLATFORM_SVCDATAMSG_H_

/*
 * Includes
 */

#include <stdint.h>
#include <stdbool.h>

#include "pb.h"
#include "svccommon.pb.h"    // Generated from svccommon.proto
#include "svcdata.pb.h"      // Generated from svcdata.proto

/*
 * Macros
 */

/*
 * Types
 */

/*
 * Data
 */

/*
 * Functions
 */
bool SvcDataMsg_HdrDecodeSourceId(pb_istream_t *stream_p, const pb_field_t *field, void **arg);
bool SvcDataMsg_HdrEncodeSourceId(pb_ostream_t *stream_p, const pb_field_t *field, void * const *arg);

bool SvcDataMsg_HdrDecodeDeviceType(pb_istream_t *stream_p, const pb_field_t *field, void **arg);

void SvcDataMsg_PrintIStreamBytes(pb_istream_t *stream_p);
void SvcDataMsg_PrintOStreamBytes(pb_ostream_t *stream_p);
bool SvcDataMsg_EncodeSubmessageTag(pb_ostream_t *stream_p, const pb_field_t *subMsgFields_p, const pb_field_t *parentMsgFields_p);
const pb_field_t *DecodeSubmessageTag(pb_istream_t *stream_p, const pb_field_t *parentFields_p, uint32_t tagTarget);

bool getSubStreamForTargetFields(pb_istream_t *stream_p, const pb_field_t *parentFields_p,
	const pb_field_t *targetFields_p, uint32_t expectedTag, const pb_field_t **fields_p,
	pb_istream_t *substream_p );

bool SvcDataMsg_DecodeHeader(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p);

bool SvcDataMsg_EncodeString(pb_ostream_t *stream_p, const pb_field_t *field, void * const *arg);

bool SvcDataMsg_DecodeParameterValueGroup(pb_istream_t *stream_p, const pb_field_t *field_p, void **arg);
bool SvcDataMsg_EncodeParameterValueGroup(pb_ostream_t *stream_p, const pb_field_t *field, void * const *arg);


#if 0
//*********** Encoding *************
bool SvcDataMsg_EncodeHeader(pb_ostream_t *stream_p, SKF_SvcDataMsg *msg_p);

bool SvcDataMsg_RequestEncodeTransactionBegin(pb_ostream_t *stream_p, SKF_SvcDataMsg *msg_p);
bool SvcDataMsg_RequestEncodeTransactionCommit(pb_ostream_t *stream_p, SKF_SvcDataMsg *msg_p);
bool SvcDataMsg_RequestEncodeTransactionRollback(pb_ostream_t *stream_p, SKF_SvcDataMsg *msg_p);
bool SvcDataMsg_RequestEncodeStoreData(pb_ostream_t *stream_p, SKF_SvcDataMsg *msg_p);
bool SvcDataMsg_RequestEncodeGetData(pb_ostream_t *stream_p, SKF_SvcDataMsg *msg_p);
bool SvcDataMsg_RequestEncodeStoreMetadata(pb_ostream_t *stream_p, SKF_SvcDataMsg *msg_p);
bool SvcDataMsg_RequestEncodeStoreSchedules(pb_ostream_t *stream_p, SKF_SvcDataMsg *msg_p);

bool SvcDataMsg_ReplyEncodeResult(pb_ostream_t *stream_p, int32_t result_code_p, char *info_p);
bool SvcDataMsg_ReplyEncodeTransactionBegin(pb_ostream_t *stream_p, SKF_SvcDataMsg *msg_p);
bool SvcDataMsg_ReplyEncodeTransactionCommit(pb_ostream_t *stream_p, SKF_SvcDataMsg *msg_p);
bool SvcDataMsg_ReplyEncodeTransactionRollback(pb_ostream_t *stream_p, SKF_SvcDataMsg *msg_p);
bool SvcDataMsg_ReplyEncodeStoreData(pb_ostream_t *stream_p, SKF_SvcDataMsg *msg_p);
bool SvcDataMsg_ReplyEncodeGetData(pb_ostream_t *stream_p, SKF_SvcDataMsg *msg_p);
bool SvcDataMsg_ReplyEncodeStoreMetadata(pb_ostream_t *stream_p, SKF_SvcDataMsg *msg_p);
bool SvcDataMsg_ReplyEncodeStoreSchedules(pb_ostream_t *stream_p, SKF_SvcDataMsg *msg_p);

//bool SvcDataMsg_PublishEncodeAlive(pb_ostream_t *stream_p, SKF_SvcDataMsg *msg_p);
//bool SvcDataMsg_PublishEncodeData(pb_ostream_t *stream_p, SKF_SvcDataMsg *msg_p);
bool SvcDataMsg_PublishEncodeMetadata(pb_ostream_t *stream_p, SKF_SvcDataMsg *msg_p);
bool SvcDataMsg_PublishEncodeSchedule(pb_ostream_t *stream_p, SKF_SvcDataMsg *msg_p);

//*********** Decoding *************
bool SvcDataMsg_DecodeHeader(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p);

//bool SvcDataMsg_DecodeRequest(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p);
//bool SvcDataMsg_DecodeReply(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p);
//bool SvcDataMsg_DecodePublish(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p);

bool SvcDataMsg_RequestDecodeTransactionBegin(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p);
bool SvcDataMsg_RequestDecodeTransactionCommit(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p);
bool SvcDataMsg_RequestDecodeTransactionRollback(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p);
bool SvcDataMsg_RequestDecodeStoreData(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p);
bool SvcDataMsg_RequestDecodeGetData(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p);
bool SvcDataMsg_RequestDecodeStoreMetadata(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p);
bool SvcDataMsg_RequestDecodeStoreSchedules(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p);

bool SvcDataMsg_ReplyDecodeResult(pb_istream_t *stream_p, int32_t *result_code_p, char *info_p);
bool SvcDataMsg_ReplyDecodeTransactionBegin(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p);
bool SvcDataMsg_ReplyDecodeTransactionCommit(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p);
bool SvcDataMsg_ReplyDecodeTransactionRollback(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p);
bool SvcDataMsg_ReplyDecodeStoreData(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p);
bool SvcDataMsg_ReplyDecodeGetData(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p);
bool SvcDataMsg_ReplyDecodeStoreMetadata(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p);
bool SvcDataMsg_ReplyDecodeStoreSchedules(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p);

//bool SvcDataMsg_PublishDecodeAlive(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p);
//bool SvcDataMsg_PublishDecodeData(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p);
bool SvcDataMsg_PublishDecodeMetadata(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p);
bool SvcDataMsg_PublishDecodeSchedule(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p);
#endif

#endif /* SOURCES_SVC_MQTT_DATA_PLATFORM_SVCDATAMSG_H_ */


#ifdef __cplusplus
}
#endif