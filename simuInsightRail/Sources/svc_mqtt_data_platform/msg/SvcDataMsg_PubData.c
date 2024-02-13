#ifdef __cplusplus
extern "C" {
#endif

/*
 * SvcDataMsg_PubData.c
 *
 *  Created on: 28 mrt. 2016
 *      Author: Daniel van der Velde
 */

/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>

#include "pb.h"
#include "pb_encode.h"
#include "pb_decode.h"

#include "svccommon.pb.h"    // Generated from svccommon.proto
#include "svcdata.pb.h"      // Generated from svcdata.proto

#include "SvcDataMsg.h"
#include "SvcDataMsg_PubData.h"

#include "SvcData.h"
#include "configSvcData.h"

#include "DataStore.h"
#include "configData.h"
#include "Log.h"

/*
 * Macros
 */

/*
 * Types
 */

/*
 * Data
 */
// for the getdata function, this array stores the parameter ID's to return to the server
static IDEF_paramid_t IdefParamList[MAX_GETDATAPARAMS];

/*
 * Functions
 */




#if 0 // Done in SvcData
/**
 * SvcDataMsg_PublishEncodeData
 *
 * @brief Encode the contents of a publish-data (sub)message
 * @param stream_p output stream
 * @param msg_p    pointer to message structure that is pre-filled by the caller
 * @return true on success, false otherwise
 */
bool SvcDataMsg_PublishEncodeData(pb_ostream_t *stream_p, SKF_SvcDataMsg *msg_p)
{
    pb_field_t fields;
    pb_ostream_t substream;
    bool status = false;

    // Encode data message by iterating the predefined PUBLISH-DATA parameter list for the
    // selected group.


    return false;
}
#endif




/**
 * SvcDataMsg_PublishDecodeData
 *
 * @brief Decode the contents of a publish-data (sub)message
 * @param stream_p input stream
 * @param msg_p    pointer to message structure that will be filled
 * @return true on success, false otherwise
 */
bool SvcDataMsg_PublishDecodeData(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p)
{
    const pb_field_t *fields_p;
    pb_istream_t substream;
    bool status = false;

//    // This message is used to exchange data between device and server.
//    // The Header is used to describe the kind of message.
//    message SvcDataMsg {
//        required Header     hdr  = 1;
//
//        oneof _messages {
//          Reply             reply = 2;
//          Publish           publish = 3;
//          Request           request = 4;
//        }
//    }
//
// we came here because of the header 'told' us that it is a publish data request.
// the input stream has been 'eaten' to just after the header, now we must see if it is indeed
// a publish data message


    // Decode publish submessage
//    LOG_DBG( LOG_LEVEL_COMM, "\nPublish1: Stream bytes left: %d\n", stream_p->bytes_left);
//    SvcDataMsg_PrintIStreamBytes(stream_p);

    if(!getSubStreamForTargetFields(stream_p,SKF_SvcDataMsg_fields,SKF_Publish_fields,SKF_SvcDataMsg_publish_tag,&fields_p, &substream))
    	return false;

//
//    message Publish{
//        oneof _publications{
//             AlivePub           alive = 1;
//             DataPub            data = 2;
//             MetadataPub        metadata = 3;
//             SchedulePub        schedules = 4;
//        }
//    }
//
// one level deeper, now we have to see that it is the DataPub we have in the message
//

//    LOG_DBG( LOG_LEVEL_COMM, "\nPublish3: Stream bytes left: %d\n", stream_p->bytes_left);
//     SvcDataMsg_PrintIStreamBytes(stream_p);
//
//    LOG_DBG( LOG_LEVEL_COMM, "\nPublish3: SUBStream bytes left: %d\n", substream.bytes_left);
//     SvcDataMsg_PrintIStreamBytes(&substream);

    // from the header we know it must be a pubData message, the protobuf tag should come to the same conclusion
    // Decode pubdata tag and will return SKF_DataPub_fields pointer when found
    fields_p = DecodeSubmessageTag(&substream, fields_p, SKF_Publish_data_tag);

    if (fields_p == SKF_DataPub_fields) {
        pb_istream_t subsubstream;


        // from the svcdata.proto file
        //    message DataPub {
        //       required Data               data = 1;
        //        optional sint32             block_index = 2;
        //        optional sint32             block_count = 3;
        //    }
        //
        // the generated structure
        //typedef struct _SKF_DataPub {
        //       SKF_Data data;
        //        bool has_block_index;
        //        int32_t block_index;
        //        bool has_block_count;
        //        int32_t block_count;
        //    } SKF_DataPub;
        //
        //

        // Decode subsubstream
        if (!pb_make_string_substream(&substream, &subsubstream))
        {
            pb_close_string_substream(stream_p, &substream);
            return false;
        }

        // the SKF_data struct contains something variable, so needs a callback
        msg_p->_messages.publish._publications.data.data.param_value_groups.arg = NULL;
        msg_p->_messages.publish._publications.data.data.param_value_groups.funcs.decode = &SvcDataMsg_DecodeParameterValueGroup;
        status = pb_decode(&subsubstream, fields_p, &msg_p->_messages.publish._publications.data);


        if (msg_p->_messages.publish._publications.data.has_block_count) {
            LOG_DBG( LOG_LEVEL_COMM, "data.block_count = %d\n", msg_p->_messages.publish._publications.data.block_count);
        }  else {
            LOG_DBG( LOG_LEVEL_COMM, "data.block_count : NO\n");
        }

        if (msg_p->_messages.publish._publications.data.has_block_index) {
            LOG_DBG( LOG_LEVEL_COMM, "data.block_index = %d\n", msg_p->_messages.publish._publications.data.block_index);
        } else {
            LOG_DBG( LOG_LEVEL_COMM, "data.block_index : NO\n");
        }


        pb_close_string_substream(&substream, &subsubstream);
        pb_close_string_substream(stream_p, &substream);

        if (status == false) {
            if (stream_p->errmsg) LOG_DBG( LOG_LEVEL_COMM, "pb errmsg = %s\n",stream_p->errmsg);
        }

        return status;

    } else {
        pb_close_string_substream(stream_p, &substream);
        return false;
    }

    return true;
}






/**
 * SvcDataMsg_RequestDecodeStoreData
 *
 * @brief Decode the contents of a Request StoreData-data (sub)message
 * @param stream_p input stream
 * @param msg_p    pointer to message structure that will be filled
 * @return true on success, false otherwise
 */
bool SvcDataMsg_RequestDecodeStoreData(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p)
{
    const pb_field_t *fields_p;
    pb_istream_t substream;
    bool status = false;

//    // This message is used to exchange data between device and server.
//    // The Header is used to describe the kind of message.
//    message SvcDataMsg {
//        required Header     hdr  = 1;
//
//        oneof _messages {
//          Reply             reply = 2;
//          Publish           publish = 3;
//          Request           request = 4;
//        }
//    }
//
// we came here because of the header 'told' us that it is a Request StoreData.
// the input stream has been 'eaten' to just after the header, now we must see if it is indeed
// a Request store data message


    // Decode publish submessage
    LOG_DBG( LOG_LEVEL_COMM, "\nrequest1: Stream bytes left: %d\n", stream_p->bytes_left);
    //SvcDataMsg_PrintIStreamBytes(stream_p);

    if(!getSubStreamForTargetFields(stream_p,SKF_SvcDataMsg_fields,SKF_Request_fields,SKF_SvcDataMsg_request_tag,&fields_p, &substream))
       	return false;

//    message Request{
//    // Functions
//        oneof _requests{
//            TransactionBeginReq     trans_begin = 1;
//            TransactionCommitReq    trans_commit = 2;
//            TransactionRollbackReq  trans_rollback = 3;
//
//            StoreDataReq            store_data = 4;
//            GetDataReq              get_data = 5;
//            StoreMetadataReq        store_metadata = 6;
//            StoreSchedulesReq       store_schedules = 7;
//         }
//    }


// one level deeper, now we have to see that it is the StoreDataReq we have in the message
//

//    LOG_DBG( LOG_LEVEL_COMM, "\nRequest3: Stream bytes left: %d\n", stream_p->bytes_left);
//     SvcDataMsg_PrintIStreamBytes(stream_p);
//
//    LOG_DBG( LOG_LEVEL_COMM, "\nRequest3: SUBStream bytes left: %d\n", substream.bytes_left);
//     SvcDataMsg_PrintIStreamBytes(&substream);

    // from the header we know it must be a StoreDataReq message, the protobuf tag should come to the same conclusion
    // Decode pubdata tag and will return SKF_DataPub_fields pointer when found
    fields_p = DecodeSubmessageTag(&substream, fields_p,  SKF_Request_store_data_tag);

    if (fields_p == SKF_StoreDataReq_fields) {
        pb_istream_t subsubstream;


        // from the svcdata.proto file
        //        message StoreDataReq {
        //            optional TransactionInfo    trans_info = 1;
        //            required Data               data = 2;
        //        }

        //
        // the generated structure
        //        typedef struct _SKF_StoreDataReq {
        //            bool has_trans_info;
        //            SKF_TransactionInfo trans_info;
        //            SKF_Data data;
        //        } SKF_StoreDataReq;


        // Decode subsubstream
        if (!pb_make_string_substream(&substream, &subsubstream))
        {
            pb_close_string_substream(stream_p, &substream);
            return false;
        }

        // the SKF_data struct contains something variable, so needs a callback
        msg_p->_messages.request._requests.store_data.data.param_value_groups.arg = NULL;
        msg_p->_messages.request._requests.store_data.data.param_value_groups.funcs.decode = &SvcDataMsg_DecodeParameterValueGroup;
        status = pb_decode(&subsubstream, fields_p, &msg_p->_messages.request._requests.store_data);


        if (msg_p->_messages.request._requests.store_data.has_trans_info) {
            LOG_DBG( LOG_LEVEL_COMM, "trans_info.trans_id  = %d\n", msg_p->_messages.request._requests.store_data.trans_info.trans_id);
            LOG_DBG( LOG_LEVEL_COMM, "trans_info.trans_seq = %d\n", msg_p->_messages.request._requests.store_data.trans_info.trans_seq);
        }  else {
            LOG_DBG( LOG_LEVEL_COMM, "trans_info.trans_id : NO\n");
        }


        pb_close_string_substream(&substream, &subsubstream);
        pb_close_string_substream(stream_p, &substream);

        if (status == false) {
            if (stream_p->errmsg) LOG_DBG( LOG_LEVEL_COMM, "pb errmsg = %s\n",stream_p->errmsg);
        }

        return status;

    } else {
        pb_close_string_substream(stream_p, &substream);
        return false;
    }

    return true;
}




typedef struct {
    char * str;
    uint32_t maxlen;
} cstring_t;

bool SvcDataMsg_replyDecodeInfo(pb_istream_t *stream_p, const pb_field_t *field, void **arg)
{


#if 0
    bool status;

    while (stream_p->bytes_left) {
        uint8_t tmp;

        status = pb_read(stream_p, &tmp, 1);

        printf(" %02x", tmp);
    }
    printf("\n");
#endif
#if 1

    cstring_t * cs_p = *arg;
    uint32_t copylen = stream_p->bytes_left;

    if (copylen >= cs_p->maxlen-1) {
        LOG_DBG( LOG_LEVEL_COMM, "ERROR SvcDataMsg_replyDecodeInfo: string too long : %d >= %d, truncated !\n", copylen, cs_p->maxlen-1 );
        copylen = cs_p->maxlen-1;

    }

    cs_p->str[copylen] = '\0';// c string terminator

    // The TAG and Length field have already been decoded by pb_decode
    // (that's how we got here in the callback...)


    // Read the raw byte stream into buffer pointed to by *arg
    if (pb_read(stream_p, (uint8_t *) cs_p->str, copylen)) {

        return true;
    } else {
        LOG_DBG( LOG_LEVEL_COMM, "ERROR SvcDataMsg_replyDecodeInfo: %s\n", PB_GET_ERROR(stream_p) );
    }

#endif
    return false;
}


/**
 * SvcDataMsg_ReplyDecodeStoreData
 *
 * @brief Decode the contents of a Reply StoreData-data (sub)message
 * @param stream_p input stream
 * @param msg_p    pointer to message structure that will be filled
 * @param result_code_p pointer to give back the resultcode of the reply
 * @return true on success, false otherwise
 */
bool SvcDataMsg_ReplyDecodeStoreData(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p, int32_t *result_code_p)
{
    const pb_field_t *fields_p;
    pb_istream_t substream;
    bool status = false;
    char infobuf[MAX_REPLY_INFO_STRING_SIZE+1];
    cstring_t cs;
    cs.maxlen = sizeof(infobuf);
    cs.str = infobuf;

    memset(infobuf,0, sizeof(infobuf));

//    // This message is used to exchange data between device and server.
//    // The Header is used to describe the kind of message.
//    message SvcDataMsg {
//        required Header     hdr  = 1;
//
//        oneof _messages {
//          Reply             reply = 2;
//          Publish           publish = 3;
//          Request           request = 4;
//        }
//    }
//
// we came here because of the header 'told' us that it is a Reply StoreData.
// the input stream has been 'eaten' to just after the header, now we must see if it is indeed
// a Request store data message


    // Decode publish submessage
    LOG_DBG( LOG_LEVEL_COMM, "\nrequest1: Stream bytes left: %d\n", stream_p->bytes_left);
    //SvcDataMsg_PrintIStreamBytes(stream_p);

    if(!getSubStreamForTargetFields(stream_p,SKF_SvcDataMsg_fields,SKF_Reply_fields,SKF_SvcDataMsg_reply_tag,&fields_p, &substream))
           	return false;

//    message Reply {
//    // Header
//        required int32          result_code = 1;
//        optional string         info = 2;
//
//    // Functions
//        oneof _replies{
//            TransactionBeginRep     trans_begin = 3;
//            TransactionCommitRep    trans_commit = 4;
//            TransactionRollbackRep  trans_rollback = 5;
//
//            StoreDataRep            store_data = 6;
//            GetDataRep              get_data = 7;
//            StoreMetadataRep        store_metadata = 8;
//            StoreSchedulesRep       store_schedules = 9;
//        }
//    }

// one level deeper, now we have to see that it is the StoreDataRep we have in the message
//

//    LOG_DBG( LOG_LEVEL_COMM, "\nRequest3: Stream bytes left: %d\n", stream_p->bytes_left);
//     SvcDataMsg_PrintIStreamBytes(stream_p);
//
//    LOG_DBG( LOG_LEVEL_COMM, "\nRequest3: SUBStream bytes left: %d\n", substream.bytes_left);
//     SvcDataMsg_PrintIStreamBytes(&substream);

    msg_p->_messages.reply.info.arg = &cs;
    msg_p->_messages.reply.info.funcs.decode =  &SvcDataMsg_replyDecodeInfo;

    status = pb_decode(&substream, fields_p, &msg_p->_messages.reply);

    if (status==true) {
        if (msg_p->_messages.reply.has_timestamp) {
            LOG_DBG( LOG_LEVEL_COMM, "\nReplyDecodeStoreData: reply timestamp %llu\n", msg_p->_messages.reply.timestamp);
        } else {
            msg_p->_messages.reply.timestamp = 0;
            LOG_DBG( LOG_LEVEL_COMM, "\nReplyDecodeStoreData: no timestamp\n");
        }
        if (result_code_p) *result_code_p = msg_p->_messages.reply.result_code;
    } else {
        if (stream_p->errmsg) LOG_DBG( LOG_LEVEL_COMM, "pb errmsg = %s\n",stream_p->errmsg);
    }
    LOG_DBG( LOG_LEVEL_COMM, "resultcode = %d\n",  msg_p->_messages.reply.result_code);
    if (strlen(cs.str)) {
        LOG_DBG( LOG_LEVEL_COMM, "info = %s\n",  cs.str);
    }

    LOG_DBG( LOG_LEVEL_COMM, "\nReply: Stream bytes left: %d\n", stream_p->bytes_left);
     //SvcDataMsg_PrintIStreamBytes(stream_p);


    LOG_DBG( LOG_LEVEL_COMM, "\nReply: SUBStream bytes left: %d\n", substream.bytes_left);
     //SvcDataMsg_PrintIStreamBytes(&substream);

    pb_close_string_substream(stream_p, &substream);

    return status;
}



/**
 * SvcDataMsg_ReplyDecodeGetData
 *
 * @brief Decode the contents of a Reply StoreData-data (sub)message
 * @param stream_p input stream
 * @param msg_p    pointer to message structure that will be filled
 * @return true on success, false otherwise
 */
bool SvcDataMsg_ReplyDecodeGetData(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p)
{
    const pb_field_t *fields_p;
    pb_istream_t substream;
    bool status = false;
    char infobuf[30];
    cstring_t cs;
    cs.maxlen = sizeof(infobuf);
    cs.str = infobuf;

    memset(infobuf,0, sizeof(infobuf));

//    // This message is used to exchange data between device and server.
//    // The Header is used to describe the kind of message.
//    message SvcDataMsg {
//        required Header     hdr  = 1;
//
//        oneof _messages {
//          Reply             reply = 2;
//          Publish           publish = 3;
//          Request           request = 4;
//        }
//    }
//
// we came here because of the header 'told' us that it is a Reply StoreData.
// the input stream has been 'eaten' to just after the header, now we must see if it is indeed
// a Request store data message


    // Decode publish submessage
    LOG_DBG( LOG_LEVEL_COMM, "\nreply1: Stream bytes left: %d\n", stream_p->bytes_left);
    //SvcDataMsg_PrintIStreamBytes(stream_p);

    if(!getSubStreamForTargetFields(stream_p,SKF_SvcDataMsg_fields,SKF_Reply_fields,SKF_SvcDataMsg_reply_tag,&fields_p, &substream))
            return false;

//    message Reply {
//    // Header
//        required int32          result_code = 1;
//        optional string         info = 2;
//
//    // Functions
//        oneof _replies{
//            TransactionBeginRep     trans_begin = 3;
//            TransactionCommitRep    trans_commit = 4;
//            TransactionRollbackRep  trans_rollback = 5;
//
//            StoreDataRep            store_data = 6;
//            GetDataRep              get_data = 7;
//            StoreMetadataRep        store_metadata = 8;
//            StoreSchedulesRep       store_schedules = 9;
//        }
//    }

// one level deeper, now we have to see that it is the StoreDataRep we have in the message
//

//    LOG_DBG( LOG_LEVEL_COMM, "\nRequest3: Stream bytes left: %d\n", stream_p->bytes_left);
//     SvcDataMsg_PrintIStreamBytes(stream_p);
//
//    LOG_DBG( LOG_LEVEL_COMM, "\nRequest3: SUBStream bytes left: %d\n", substream.bytes_left);
//     SvcDataMsg_PrintIStreamBytes(&substream);

    msg_p->_messages.reply.info.arg = &cs;
    msg_p->_messages.reply.info.funcs.decode =  &SvcDataMsg_replyDecodeInfo;

    status = pb_decode(&substream, fields_p, &msg_p->_messages.reply);

    if (msg_p->_messages.reply.has_timestamp) {
        LOG_DBG( LOG_LEVEL_COMM, "\nReplyDecodeGetData: reply timestamp %llu\n", msg_p->_messages.reply.timestamp);
    } else {
        msg_p->_messages.reply.timestamp = 0;
        LOG_DBG( LOG_LEVEL_COMM, "\nReplyDecodeGetData: no timestamp\n");
    }

    LOG_DBG( LOG_LEVEL_COMM, "resultcode = %d\n",  msg_p->_messages.reply.result_code);
    if (strlen(cs.str)) {
        LOG_DBG( LOG_LEVEL_COMM, "info = %s\n",  cs.str);
    }

        LOG_DBG( LOG_LEVEL_COMM, "\nReply: Stream bytes left: %d\n", stream_p->bytes_left);
         //SvcDataMsg_PrintIStreamBytes(stream_p);


        LOG_DBG( LOG_LEVEL_COMM, "\nReply: SUBStream bytes left: %d\n", substream.bytes_left);
         //SvcDataMsg_PrintIStreamBytes(&substream);
#if 0
    fields2_p = DecodeSubmessageTag(&substream, fields_p, SKF_Reply_store_data_tag);

    if (fields2_p == SKF_StoreDataRep_fields) {
        pb_istream_t subsubstream;


        // from the svcdata.proto file
        //        message StoreDataRep {
        //        }

        //
        // the generated structure
        //        typedef struct _SKF_StoreDataReq {
        //            bool has_trans_info;
        //            SKF_TransactionInfo trans_info;
        //            SKF_Data data;
        //        } SKF_StoreDataReq;


        // Decode subsubstream
        if (!pb_make_string_substream(&substream, &subsubstream))
        {
            pb_close_string_substream(stream_p, &substream);
            return false;
        }

        status = pb_decode(&subsubstream, fields2_p, &msg_p->_messages.reply._replies.store_data.dummy_field);


#if 0
        if (msg_p->_messages.reply.) {
            LOG_DBG( LOG_LEVEL_COMM, "trans_info.trans_id = %d\n", msg_p->_messages.request._requests.store_data.trans_info.trans_id);
            LOG_DBG( LOG_LEVEL_COMM, "trans_info.trans_id = %d\n", msg_p->_messages.request._requests.store_data.trans_info.trans_id);
        }  else {
            LOG_DBG( LOG_LEVEL_COMM, "trans_info.trans_id : NO\n");
        }
#endif

        pb_close_string_substream(&substream, &subsubstream);

        pb_close_string_substream(stream_p, &substream);

        if (status == false) {
            if (stream_p->errmsg) LOG_DBG( LOG_LEVEL_COMM, "pb errmsg = %s\n",stream_p->errmsg);
        }

        return status;

    } else {
        pb_close_string_substream(stream_p, &substream);
        return false;
    }
#else
    pb_close_string_substream(stream_p, &substream);
#endif

    return status;
}


bool SvcDataMsg_DecodeParameters(pb_istream_t *stream_p, const pb_field_t *field, void **arg)
{
    bool rc_ok = true;
    uint64_t value;
    SvcDataParamValueGroup_t * paramGroupList_p = (SvcDataParamValueGroup_t *) *arg;

    if (!pb_decode_varint(stream_p, &value))
    	return false;

    if (paramGroupList_p) {
        if (paramGroupList_p->numberOfParamValues < MAX_GETDATAPARAMS) {
            paramGroupList_p->ParamValue_p[paramGroupList_p->numberOfParamValues++] = value;
        } else {
            // parameterlist overflow
            LOG_DBG( LOG_LEVEL_COMM, "SvcDataMsg_DecodeParameters: parameter table overflow\n");
            rc_ok = false;
        }
    }
    //LOG_DBG( LOG_LEVEL_COMM, "another parameter ID : %08llx\n",value);

    return rc_ok;
}

bool SvcDataMsg_DecodeQuery(pb_istream_t *stream_p, const pb_field_t *field, void **arg)
{

    bool status = true;
    SKF_GetDataReq_Query query = SKF_GetDataReq_Query_init_default;

    // printf("We got at SvcDataMsg_replyDecodeQuery %08x %08x %08x\n", stream_p, field_p, arg);
    // printf("bytes left = %d\n",stream_p->bytes_left);
    //SvcDataMsg_PrintIStreamBytes(stream_p);

    //            message Query{
    //                repeated uint32         parameters = 1;
    //                optional uint64         start_time = 2;
    //                optional uint64         end_time = 3;
    //            }

    query.parameters.arg = *arg;
    query.parameters.funcs.decode = &SvcDataMsg_DecodeParameters;


    status = pb_decode(stream_p, SKF_GetDataReq_Query_fields, &query);
    LOG_DBG( LOG_LEVEL_COMM, "another query\n");
    return status;
}





/**
 * SvcDataMsg_RequestDecodeGetData
 *
 * @brief Decode the contents of a Request getData-data (sub)message
 * @param stream_p input stream
 * @param msg_p    pointer to message structure that will be filled
 * @param paramGroupList_p  structure describing the list of parameter to send to the server
 * @return true on success, false otherwise
 */
bool SvcDataMsg_RequestDecodeGetData(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p, SvcDataParamValueGroup_t * paramGroupList_p )
{
    const pb_field_t *fields_p;
    pb_istream_t substream;
    bool status = false;

//    // This message is used to exchange data between device and server.
//    // The Header is used to describe the kind of message.
//    message SvcDataMsg {
//        required Header     hdr  = 1;
//
//        oneof _messages {
//          Reply             reply = 2;
//          Publish           publish = 3;
//          Request           request = 4;
//        }
//    }
//
// we came here because of the header 'told' us that it is a Request StoreData.
// the input stream has been 'eaten' to just after the header, now we must see if it is indeed
// a Request store data message


    // Decode request submessage
    LOG_DBG( LOG_LEVEL_COMM, "\nrequest1: Stream bytes left: %d\n", stream_p->bytes_left);
    //SvcDataMsg_PrintIStreamBytes(stream_p);

    if(!getSubStreamForTargetFields(stream_p,SKF_SvcDataMsg_fields,SKF_Request_fields,SKF_SvcDataMsg_request_tag,&fields_p, &substream))
    	return false;

//    message Request{
//    // Functions
//        oneof _requests{
//            TransactionBeginReq     trans_begin = 1;
//            TransactionCommitReq    trans_commit = 2;
//            TransactionRollbackReq  trans_rollback = 3;
//
//            StoreDataReq            store_data = 4;
//            GetDataReq              get_data = 5;
//            StoreMetadataReq        store_metadata = 6;
//            StoreSchedulesReq       store_schedules = 7;
//         }
//    }


// one level deeper, now we have to see that it is the GetDataReq we have in the message
//

//    LOG_DBG( LOG_LEVEL_COMM, "\nRequest3: Stream bytes left: %d\n", stream_p->bytes_left);
//     SvcDataMsg_PrintIStreamBytes(stream_p);
//
//    LOG_DBG( LOG_LEVEL_COMM, "\nRequest3: SUBStream bytes left: %d\n", substream.bytes_left);
//     SvcDataMsg_PrintIStreamBytes(&substream);

    // from the header we know it must be a getDataReq message, the protobuf tag should come to the same conclusion
    // Decode the tag and will return SKF_DataPub_fields pointer when found
    fields_p = DecodeSubmessageTag(&substream, fields_p, SKF_Request_get_data_tag);

    if (fields_p == SKF_GetDataReq_fields) {
        pb_istream_t subsubstream;

        // from the svcdata.proto file
//        message GetDataReq {
//            message Query{
//                repeated uint32         parameters = 1;
//                optional uint64         start_time = 2;
//                optional uint64         end_time = 3;
//            }
//            repeated Query query = 1;
//        }

        // Decode subsubstream
        if (!pb_make_string_substream(&substream, &subsubstream)) {
            pb_close_string_substream(stream_p, &substream);
            return false;
        }

        if (paramGroupList_p) {
            paramGroupList_p->numberOfParamValues = 0;
            paramGroupList_p->ParamValue_p = IdefParamList;
        }

        // the SKF_data struct contains something variable, so needs a callback
        msg_p->_messages.request._requests.get_data.query.arg = paramGroupList_p;
        msg_p->_messages.request._requests.get_data.query.funcs.decode = &SvcDataMsg_DecodeQuery;

        status = pb_decode(&subsubstream, fields_p, &msg_p->_messages.request._requests.get_data);


        pb_close_string_substream(&substream, &subsubstream);
        pb_close_string_substream(stream_p, &substream);

        if (status == false) {
            if (stream_p->errmsg) LOG_DBG( LOG_LEVEL_COMM, "pb errmsg = %s\n", stream_p->errmsg);
        }

        return status;

    } else {
        pb_close_string_substream(stream_p, &substream);
        return false;
    }

    return true;
}



#ifdef __cplusplus
}
#endif