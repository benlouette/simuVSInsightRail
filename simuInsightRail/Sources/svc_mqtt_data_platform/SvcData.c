#ifdef __cplusplus
extern "C" {
#endif

/*

 * SvcData.c
 *
 *  Created on: 23 dec. 2015
 *      Author: Daniel van der Velde
 */

/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>

#include "MQTTClient.h"
#include "MQTTPacket.h"       // For MQTTPacket_equals: MQTTString comparison

// lwip stack defines these two, but in a different way than another include file, and we do not use this define, so to avoid problems, undefine these
#undef BIG_ENDIAN
#undef LITTLE_ENDIAN

#include "TaskCommInt.h"

#include "configMQTT.h"
#include "configSvcData.h"
#include "configIDEF.h"

#include "DataStore.h"
#include "configData.h"

#include "pb.h"
#include "pb_encode.h"
#include "pb_decode.h"
#include "svccommon.pb.h"
#include "svcdata.pb.h"

#include "ISvcData.h"
#include "SvcData.h"
#include "SvcDataMsg.h"
#include "SvcDataMsg_PubAlive.h"
#include "SvcDataMsg_PubData.h"

#include "SvcMqttFirmware.h"

#include "Log.h"

#include "SvcDataCLI.h"
#include "CS1.h"
#include "Resources.h"

// cpu.h or lower defines these two, but in a different way than another include file, and we do not use this define, so to avoid problems, undefine these
#undef BIG_ENDIAN
#undef LITTLE_ENDIAN
/*
 * Macros
 */

// big TODO: undefine PROTOBUFTEST  when committing !!!
//#define PROTOBUFTEST

/*
 * Types
 */
typedef struct {
    MQTTClient       *mqttClient_p;
    SemaphoreHandle_t apiMutex;           // ISvcData interface

    // Operational state
    ISvcDataState_t   svcState;           // SvcData state (disabled/started)

    // SVCDATA MSG - Outgoing Requests, so expecting a reply
    bool              reqStatePending;    // TODO use for request/reply state
    uint32_t          reqStateFunctionId; // TODO use for request/reply state
    uint32_t          reqStateMessageId; // message ID of last request function, used in checking for the correct reply
//    bool              reqStateBlocking;   // TODO use for blocking and non-blocking interface calls
    uint32_t          dataMsgTxLastId;      // Message ID counter for

    // SVCDATA MSG - Incoming Requests, so peer is expecting a reply from us
    // Of course, replies are initiated immediately, so variables below might not be needed.
//    bool              rspStatePending;    // TODO use for request/reply state
//    bool              rspStateBlocking;   // TODO use for blocking and non-blocking interface calls
//    uint32_t          rspStateFunctionId; // TODO use for request/reply state
//    uint32_t          dataMsgRxMessageId;   // Received message ID to be used for the reply.
    uint8_t         dataMsgRxSourceId[MAXSOURCEIDSIZE+1]; // no further use yet, holds latest received sourceId
    uint64_t        timestamp;// most recent timestamp of received parameter(s)

    // Default MQTT message settings
    enum QoS          mqttQosDefault;
    uint8_t           mqttRetainDefault;
    uint8_t           mqttDupDefault;

    // for the block transfer, this is easier that passing it to all kind of protobuf callback functions
    bool        blockTransfer; // true when doing multy message block transfer of big array
    uint32_t    maxElementsPerBuffer;   // when doing a large block transfer of an array, here is the number of elements to pass
    uint32_t    ElementOffset;
    uint32_t    nrElements;
} SvcData_State_t;

/*
 * Data
 */

static SvcData_State_t State;

// TX buffer (RX buffer is shared and provided by COMM component)
// TODO : check that the size should be 'mqtt overhead' smaller than the mqtt buffer
// mqtt overhead for a publish is roughly topiclenght+payloadlength+4
// TxBuf would be the payload, so CONFIG_MQTT_SEND_BUFFER_SIZE-4-MAXTOPICSIZE
#ifdef PROTOBUF_GPB2_1
static uint8_t TxBuf[CONFIG_MQTT_SEND_BUFFER_SIZE - MQTT_MAX_TOPIC_LEN - 4] = {SVCDATA_MQTT_SERDES_ID_GPB2_1, 0,};
#else
static uint8_t TxBuf[CONFIG_MQTT_SEND_BUFFER_SIZE - MQTT_MAX_TOPIC_LEN - 4] = {SVCDATA_MQTT_SERDES_ID_GPB2, 0,};
#endif

// Constructed MQTT TOPIC buffers
static char MQTTTopic_SvcData[MQTT_MAX_TOPIC_LEN+1] = {0,}; // NULL terminated string to pass to MQTT(Un)Subscribe()

// SvcData message structures
// These structures hold the definition of any message defined by the SvcData.proto file. This is kept
// static to save stack usage (quite a large structure).
// Separate Tx and Rx are required, since they can be accessed in parallel by different tasks
static SKF_SvcDataMsg MsgTx = SKF_SvcDataMsg_init_default;
static SKF_SvcDataMsg MsgRx = SKF_SvcDataMsg_init_default;
const  SKF_SvcDataMsg SvcDataMsg_Init = SKF_SvcDataMsg_init_default;



/*
 * Functions
 */


/*
 * callback administration
 */


#define SVCDATAMAXCALLBACKS   (2)


// will move to the status struct ?
static tSvcDataCallbackFuncPtr SvcDataCallbackFuncTable[SvcData_cb_max][SVCDATAMAXCALLBACKS];



// initialise local data with defined values (in this case everything null pointer)
static void SvcDataInitCallBack()
{
    int i,j;

   for (i=0; i<SvcData_cb_max; i++) {
        for (j=0; j<SVCDATAMAXCALLBACKS; j++) {
            SvcDataCallbackFuncTable[i][j] = NULL;
        }
    }
}

// remove the callback : safe to be called from another task
void SvcDataRemoveCallback(tSvcDataCallbackIndex cbIdx, tSvcDataCallbackFuncPtr cbFunc)
{
    uint8_t i;
    if (cbIdx < SvcData_cb_max) {

        for (i=0; (i<SVCDATAMAXCALLBACKS) ; i++) {

            CS1_CriticalVariable();

            CS1_EnterCritical();

            // running in another task context, and a 32 bit pointer assign is not atomic on this processor, so be on the safe side
            if (SvcDataCallbackFuncTable[cbIdx][i] == cbFunc) {
                SvcDataCallbackFuncTable[cbIdx][i] = NULL;
            }

            CS1_ExitCritical();
        }
    }
}
// set the callback : safe to be called from another task
void SvcDataSetCallback(tSvcDataCallbackIndex cbIdx, tSvcDataCallbackFuncPtr cbFunc)
{
    uint8_t i;

    if (cbIdx < SvcData_cb_max) {

        SvcDataRemoveCallback(cbIdx,  cbFunc);// just to prevent multiple entries of this address

        for (i=0; (i < SVCDATAMAXCALLBACKS) ; i++) {

            if (SvcDataCallbackFuncTable[cbIdx][i] == NULL) {
                CS1_CriticalVariable();

                CS1_EnterCritical();
                // running in another task context, and a 32 bit pointer assign is not atomic on this processor, so be on the safe side
                SvcDataCallbackFuncTable[cbIdx][i] = cbFunc;

                CS1_ExitCritical();
                break;// exit the for loop
            }
        }
    }
}


// call possible registered callback functions when we got something what others would like to know
static uint8_t handleCallback (tSvcDataCallbackIndex cbIdx, void * buf)
{
    uint8_t rc = 0;
    if (cbIdx < SvcData_cb_max) {
        uint8_t i;
        for (i=0; i < SVCDATAMAXCALLBACKS ; i++) {

            if (SvcDataCallbackFuncTable[cbIdx][i] != NULL) {
                uint8_t rc2;

                rc2 = SvcDataCallbackFuncTable[cbIdx][i]( buf ); // call whoever is interested.

                if (rc2 != 0) rc = rc2; // last callback with error status wins
            }
        }
    }
    return rc;
}


/*
 * end callback administration
 */



/*
 * debug/template callbacks
 */

static uint32_t handlePubAlive_cb(void * buf)
{
    LOG_DBG( LOG_LEVEL_COMM, "svcData():handlePubAlive_cb(%08x) called\n", buf);
    return 0;
}
static uint32_t handlePubData_cb(void * buf)
{
    LOG_DBG( LOG_LEVEL_COMM, "svcData():handlePubData_cb(%08x) called\n", buf);
    return 0;
}
static uint32_t handleStoreDataRequest_cb(void * buf)
{
    LOG_DBG( LOG_LEVEL_COMM, "svcData():handleStoreDataRequest_cb(%08x) called\n", buf);
    return 0;
}
static uint32_t handleStoreDataReply_cb(void * buf)
{
    tIdefStoreDataRep * data = (tIdefStoreDataRep * ) buf;

    LOG_DBG( LOG_LEVEL_COMM, "svcData():handleStoreDataReply_cb(%08x) called, resultcode = %d timestamp %llu\n", buf, data->result_code, data->timestamp);

    return 0;
}
static uint32_t handleGetDataRequest_cb(void * buf)
{
    LOG_DBG( LOG_LEVEL_COMM, "svcData():handleGetDataRequest_cb(%08x) called\n", buf);
#ifdef DEBUG
    if (dbg_logging & LOG_LEVEL_COMM) {
        tIdefGetDataReq * data = (tIdefGetDataReq * ) buf;
        int i;

        printf("Message id = %d\n",data->message_id);
        printf("Number of requested parameters = %d\n", data->paramGroupList_p->numberOfParamValues);
        for (i=0; i < data->paramGroupList_p->numberOfParamValues; i++) {
            printf("[%3d] = 0x%08x ( %s )\n", i, data->paramGroupList_p->ParamValue_p[i], IDEF_paramid_to_str(data->paramGroupList_p->ParamValue_p[i]));
        }
    }
#endif

    return 0;
}
static uint32_t handleGetDataReply_cb(void * buf)
{
    tIdefGetDataRep * data = (tIdefGetDataRep * ) buf;

    LOG_DBG( LOG_LEVEL_COMM, "svcData():handleGetDataReply_cb(%08x) called, timestamp = %llu\n", buf, data->timestamp);

    return 0;
}

/*
 * end debug/template callbacks
 */


void SvcData_SetTimestamp(uint64_t timestamp)
{
    if (timestamp==0) {
        State.timestamp = 0;// for resetting
    } else {
        if (timestamp>State.timestamp) {
            State.timestamp = timestamp; // only set if its newer
        }
    }
}


/**
 * SvcData_PublishMQTTMessage
 *
 * @brief Publish a message over MQTT
 * @param payload_p
 * @param payloadlen
 */
int SvcData_PublishMQTTMessage( void *payload_p, int payloadlen )
{
    int rc = FAILURE;
    MQTTMessage mqttMsg;

    mqttMsg.dup        = State.mqttDupDefault;
    mqttMsg.id         = CommMQTT_GetNextMsgId();
    mqttMsg.payload    = payload_p;
    mqttMsg.payloadlen = payloadlen;
    mqttMsg.qos        = State.mqttQosDefault;
    mqttMsg.retained   = State.mqttRetainDefault;

//    SEGGER_SYSVIEW_OnUserStart(0x2);

    // TODO Publish to base only, but for debug it's good to decode what we've sent to the broker
#if 1
    LOG_DBG( LOG_LEVEL_COMM, "PubTopic: %s\n", mqttGetPubTopic() );
    rc = MQTTPublish(State.mqttClient_p, (const char *) mqttGetPubTopic(), &mqttMsg );
#else
    // use for debug only
    rc = MQTTPublish(State.mqttClient_p, MQTTTopic_SvcData, &mqttMsg );
#endif

//    SEGGER_SYSVIEW_OnUserStop(0x2);

    if ( rc != SUCCESS ) {
        return -1;
    }

    return 0;
}

/**
 * SvcData_MessageHandler
 *
 * @brief Main Message handler for SvcDataMsg, decodes protobuf information into IDEF svcDataMsg
 * @param payload_p points to the start of the byte stream that contains the protobuf stream
 * @param payloadlen length of the byte stream
 */
static void SvcData_MessageHandler( void *payload_p, size_t payloadlen )
{
    uint8_t *ptr = (uint8_t*)payload_p;
    // Correct the length of the stream for the length of the actual protobuf stream
    // which does not include the serialization identifier
    size_t len = payloadlen;

#ifndef PROTOBUF_GPB2_1
    len -= 1;

    // First byte contains serialization identifier
	if (*ptr != SVCDATA_MQTT_SERDES_ID_GPB2) {
		// Not GPB2 format, ignore content
		return;
	}

	ptr++;

#endif




    // Create protobuf stream
    pb_istream_t istream = pb_istream_from_buffer(ptr, len);

    // SvcDataMsg_PrintIStreamBytes(&istream);
#if 0

    if (!pb_decode_tag(&istream, &wire_type, &tag, &eof)){
        LOG_DBG( LOG_LEVEL_COMM, "ERROR decode_tag: %s\n", PB_GET_ERROR(&istream) );
    } else {
        uint64_t tagvalue = 0;

        LOG_DBG( LOG_LEVEL_COMM, "OK decode_tag: tag %u, wt %u\n", tag, wire_type );

        // Now get the tag value (should be value for 'SKF_Header')
        if (!pb_decode_varint(&istream, &tagvalue)) {
            LOG_DBG( LOG_LEVEL_COMM, "ERROR decode_varint: %s\n", PB_GET_ERROR(&istream) );
        } else {
            LOG_DBG( LOG_LEVEL_COMM, "OK decode_varint: %u\n", tagvalue );
        }

    }
#endif

    // Prepare RX message header
    MsgRx = SvcDataMsg_Init;

// from the svcdata.proto file
//    message SvcDataMsg {
//        required Header     hdr  = 1;
//
//        oneof _messages {
//          Reply             reply = 2;
//          Publish           publish = 3;
//          Request           request = 4;
//        }
//    }

    // The source_id is part of the Header, the callback is set here
    // because the State structure, which also contains the source_id,
    // is declared static in this file and contains a semaphore
    memset(&State.dataMsgRxSourceId,0,sizeof(State.dataMsgRxSourceId));// zero the sourceID so it sure ends with a zero byte !

    MsgRx.hdr.source_id.funcs.decode = &SvcDataMsg_HdrDecodeSourceId;
    MsgRx.hdr.source_id.arg = &State.dataMsgRxSourceId;

    if (!SvcDataMsg_DecodeHeader(&istream, &MsgRx)) {
        LOG_DBG( LOG_LEVEL_COMM, "ERROR decode hdr: %s\n", PB_GET_ERROR(&istream) );
    } else {
        LOG_DBG( LOG_LEVEL_COMM, "OK decode hdr\n" );

        LOG_DBG( LOG_LEVEL_COMM, "MsgRx.hdr.function_id = %u\n", MsgRx.hdr.function_id);
        LOG_DBG( LOG_LEVEL_COMM, "MsgRx.hdr.version     = %u\n", MsgRx.hdr.version);
        LOG_DBG( LOG_LEVEL_COMM, "MsgRx.hdr.type        = %u\n", MsgRx.hdr.type);
        LOG_DBG( LOG_LEVEL_COMM, "MsgRx.hdr.message_id  = %u\n", MsgRx.hdr.message_id);
        LOG_DBG( LOG_LEVEL_COMM, "MsgRx.hdr.source_id   = %s\n", State.dataMsgRxSourceId );

        // OK use it
        // Check header and forward to function handlers
        switch (MsgRx.hdr.function_id) {

        case SKF_FunctionId_PUBALIVE:
            // not normally done towards the device !
            // Ignore
            LOG_DBG( LOG_LEVEL_COMM, ">>>> Incoming pubAlive<<<\n");
            if (!SvcDataMsg_PublishDecodeAlive(&istream, &MsgRx)) {
                LOG_DBG( LOG_LEVEL_COMM, "Error decode ALIVE\n" );
            } else {
                LOG_DBG( LOG_LEVEL_COMM, "Alive decoded OK\n" );
                LOG_DBG( LOG_LEVEL_COMM, "MsgRx alive timestamp: %llu\n", MsgRx._messages.publish._publications.alive.time_stamp );
                // TODO use callback to notify subscribed tasks something got in
                handleCallback(SvcData_cb_pubAlive, NULL);
            }
            break;

        case SKF_FunctionId_PUBDATA:
            LOG_DBG( LOG_LEVEL_COMM, ">>>> Incoming pubData<<<\n");
            // not normally done towards the device !

            // Handle PUBDATA receive (set internal parameters to new values)
            if (MsgRx.hdr.type == SKF_MsgType_PUBLISH) {
                // Handle
                if (!SvcDataMsg_PublishDecodeData(&istream, &MsgRx)) {
                    LOG_DBG( LOG_LEVEL_COMM, "Error decode PUBDATA\n" );
                } else {
                    LOG_DBG( LOG_LEVEL_COMM, "PUBDATA decoded OK\n" );
                    // TODO signal application that something has arrived
                    // TODO use callback to notify subscribed tasks
                    handleCallback(SvcData_cb_pubData, NULL);// the variable update is already done, just notify that this happened
                 }

            } else {
                // TODO Error, functionId says PUBDATA, so it must be a publish message (notification)
            }
            break;

        case SKF_FunctionId_STOREDATA:
            LOG_DBG( LOG_LEVEL_COMM, ">>>> Incoming storeData<<<\n");
            // Handle response
            if (MsgRx.hdr.type == SKF_MsgType_REPLY) {
                LOG_DBG( LOG_LEVEL_COMM, ">>>> Reply %d %d (pending=%d waiting for Id %d)\n",MsgRx.hdr.function_id, MsgRx.hdr.message_id,State.reqStatePending,State.reqStateMessageId);
                // TODO Handle pending response (of outgoing request)
#if 0
                // only for testing !!
                if (1) {
#else
                if (State.reqStatePending && (State.reqStateFunctionId == MsgRx.hdr.function_id) && (State.reqStateMessageId == MsgRx.hdr.message_id)) {
#endif
                    tIdefStoreDataRep  IdefStoreDataRep;
                    State.reqStatePending = false;
                    // TODO Support blocking and non-blocking API calls
                    if ( !SvcDataMsg_ReplyDecodeStoreData(&istream, &MsgRx, &IdefStoreDataRep.result_code) )  {
                        LOG_DBG( LOG_LEVEL_COMM, "Error decode REPLYDATA\n" );
                    } else {
                        LOG_DBG( LOG_LEVEL_COMM, "REPLYDATA decoded OK\n" );
                        // TODO signal application that something has arrived
                        // TODO use callback to notify subscribed tasks that the storeData we send out earlier has been acknowledged
                        IdefStoreDataRep.timestamp = MsgRx._messages.reply.timestamp;
                        handleCallback(SvcData_cb_storeDataReply, &IdefStoreDataRep);
                    }
                }
            } else if (MsgRx.hdr.type == SKF_MsgType_REQUEST) {
                // TODO Handle incoming request
                // Handle
                if ( !SvcDataMsg_RequestDecodeStoreData(&istream, &MsgRx))  {
                    LOG_DBG( LOG_LEVEL_COMM, "Error decode STOREDATA\n" );
                } else {
                    tIdefStoreDataReq IdefStoreDataReq;
                    LOG_DBG( LOG_LEVEL_COMM, "STOREDATA decoded OK\n" );
                    // TODO signal application that something has arrived
                    // TODO use callback to notify subscribed tasks

                    // data to send to callback:
                    // MsgRx.hdr.message_id (needed for the reply

                    IdefStoreDataReq.message_id = MsgRx.hdr.message_id;

                    handleCallback(SvcData_cb_storeDataRequest, &IdefStoreDataReq);
                }

            } else {
                // TODO Error, Store data function is either Request or Reply (not Publish)
                LOG_DBG( LOG_LEVEL_COMM, "Error STOREDATA must be request or reply\n" );
            }
            break;
        case SKF_FunctionId_GETDATA:

            LOG_DBG( LOG_LEVEL_COMM, ">>>> Incoming getData<<<\n");
            // Handle response
            if (MsgRx.hdr.type == SKF_MsgType_REPLY) {
                LOG_DBG( LOG_LEVEL_COMM, ">>>> Reply %d (req=%d) %d (pending=%d waiting for Id %d)\n",MsgRx.hdr.function_id, State.reqStateFunctionId, MsgRx.hdr.message_id,State.reqStatePending,State.reqStateMessageId);
                // TODO Handle pending response (of outgoing request)
#if 0
                // only for testing !!
                if (1) {
#else
                if (State.reqStatePending && (State.reqStateFunctionId == MsgRx.hdr.function_id) && (State.reqStateMessageId == MsgRx.hdr.message_id) ) {
#endif
                    tIdefGetDataRep  IdefGetDataRep;
                    State.reqStatePending = false;
                    // TODO Support blocking and non-blocking API calls
                    if ( !SvcDataMsg_ReplyDecodeGetData(&istream, &MsgRx) )  {
                        LOG_DBG( LOG_LEVEL_COMM, "Error decode GETDATA\n" );
                    } else {
                        IdefGetDataRep.timestamp = MsgRx._messages.reply.timestamp;
                        LOG_DBG( LOG_LEVEL_COMM, "GETDATA decoded OK\n" );
                        // TODO signal application that something has arrived
                        // TODO use callback to notify subscribed tasks which data must be send
                        handleCallback(SvcData_cb_getDataReply, &IdefGetDataRep);// the node requested data from the server, and now the server acknowledged that the request is received
                    }
                }
            } else if (MsgRx.hdr.type == SKF_MsgType_REQUEST) {
                // Handle
                SvcDataParamValueGroup_t  paramGroupList;
                if ( !SvcDataMsg_RequestDecodeGetData(&istream, &MsgRx, &paramGroupList))  {
                    LOG_DBG( LOG_LEVEL_COMM, "Error decode GETDATA\n" );
                } else {
                    tIdefGetDataReq IdefGetDataReq;

                    LOG_DBG( LOG_LEVEL_COMM, "GETDATA decoded OK\n" );
                    // TODO signal application that something has arrived
                    // TODO use callback to notify subscribed tasks
                    // So, now we have a parameter list, and the sending of the 'reply',
                    // and the dataPublish of this list we leave to the application, so let them know what to do

                    // data to send to callback:
                    // MsgRx.hdr.message_id (needed for the reply
                    // paramGroupList ( contains the info of what param/values to send back)
                    IdefGetDataReq.message_id = MsgRx.hdr.message_id;
                    IdefGetDataReq.paramGroupList_p = &paramGroupList;

                    handleCallback(SvcData_cb_getDataRequest, &IdefGetDataReq);
#if 0
                    {
                        // this may fail (deadlock) because of the semaphore !
                        ISvcDataRc_t rc = ISVCDATARC_ERR_STATE;
                        int32_t result_code = IDEF_RESULT_OK;
                        rc = ISvcData_ReplyGetData( MsgRx.hdr.message_id, result_code,  "test reply getData info string" );

                        if (rc != ISVCDATARC_OK) {
                            LOG_DBG( LOG_LEVEL_CLI, "reply  getData: failed\n");
                        }
                    }
#endif
                }

            } else {
                // TODO Error, Get data function is either Request or Reply (not Publish)
                LOG_DBG( LOG_LEVEL_COMM, "Error GETDATA must be request or reply\n" );
            }

            break;
        default:
            // Ignore
            break;
        }

        // Show all messages on CLI during debug
        LOG_DBG( LOG_LEVEL_COMM, "SvcData: RX (fid:%d, type:%d)\n", MsgRx.hdr.function_id, MsgRx.hdr.type );

    }
}

/**
 * SvcData_MQTTMessageHandler
 *
 * @brief Message handler for the MQTT part of the IDEF protocol (wrapped in protobuf).
 * @param msgData_p
 */
static void SvcData_MQTTMessageHandler( MessageData *msgData_p )
{
#ifdef PROTOBUF_GPB2_1
	uint8_t *ptr = NULL;
	uint16_t msgType = 0;
	size_t len = 0;
#endif


	if ( msgData_p == NULL )
        return;
#if 0
	// for debug only, packet is not handled
	else {
	    printf("SvcData_MQTTMessageHandler() received packet, exit now\n");
	    return;
	}
#endif

    if ( (msgData_p->topicName == NULL) || ( msgData_p->message == NULL ) )
        return;

    // Check topic name
    if ( !MQTTPacket_equals( msgData_p->topicName, MQTTTopic_SvcData) ) {
        return;
    }

    /* Handle incoming message
     *
     * msgData_p->message->dup;         // This is an original (0) or a duplicate (1) of same id
     * msgData_p->message->id;          // Packet identifier
     * msgData_p->message->payload;     // Payload pointer
     * msgData_p->message->payloadlen;  // Size of payload in bytes
     * msgData_p->message->qos;         // QoS: 0,1,2 are valid. Otherwise close connection
     * msgData_p->message->retained;    // Retained message (has been waiting at broker, e.g. server state info)
     *
     */

    if ( (msgData_p->message->qos != QOS0)
         && (msgData_p->message->qos != QOS1)
         && (msgData_p->message->qos != QOS2) ) {
        // TODO Close connection
        return;
    }

#ifdef PROTOBUF_GPB2_1
    // Payload length should be more than 3 bytes, serialization and message type
    // so check for payload length <= 2
    if ( (msgData_p->message->payload == NULL) || (msgData_p->message->payloadlen <= 2) )
        return;
#else
    if ( (msgData_p->message->payload == NULL) || (msgData_p->message->payloadlen == 0) )
        return;
#endif

#ifdef PROTOBUF_GPB2_1
    // Set pointer to the beginning of the payload
    ptr = msgData_p->message->payload;
    len = msgData_p->message->payloadlen;

    //Check the serialization
    // First byte contains serialization identifier
    if (*ptr != SVCDATA_MQTT_SERDES_ID_GPB2_1) {
    	// Not GPB2 format, ignore content
    	return;
    }

    // Move past the preamble byte
    ptr++;

    // Byte two and three contain message type
    msgType = (ptr[0]<<8) + ptr[1];

    // Move past the message type field
    ptr = &ptr[2];

    switch(msgType){
    	case MSG_TYPE_UNKNOWN:
    		// TODO Needs to be changed to NEW protocol structure
    		// Handle payload content INTERMEDIATE for protocol change
    		SvcData_MessageHandler(ptr,(len-3));

    		break;
    	case MSG_TYPE_REPLY:
    		break;
    	case MSG_TYPE_DATA_REQUEST:
    		break;
    	case MSG_TYPE_DATA_STORE_REPLY:
    		break;
    	case MSG_TYPE_DATA_STORE_REQUEST:
    		// Backend sends sometime a data store request, interpret it as
    		// MSG_TYPE_UNKNOWN
    		// Handle payload content INTERMEDIATE for protocol change
    		SvcData_MessageHandler(ptr,(len-3));
    		break;
    	case MSG_TYPE_BLOCK_DATA_MESSAGE:
    		break;
    	case MSG_TYPE_BLOCK_DATA_REPLY:
    		break;

#ifdef CONFIG_PLATFORM_OTA

    	case MSG_TYPE_FIRMWARE_UPDATE_NOTIFICATION:
    		SvcFirmware_MessageHandler_UpdateNotification(ptr, (len-3), eTRANSPORT_PIPE_MQTT);
    		break;
    	case MSG_TYPE_FIRMWARE_BLOCK_REQUEST:
    		// Not implemented since the device only sends requests and does
    		// not receive them
    		//SvcFirmware_MessageHandler_BlockRequest(ptr,(len-3));
    		break;
    	case MSG_TYPE_FIRMWARE_BLOCK_REPLY:
    		SvcFirmware_MessageHandler_BlockReply(ptr, (len-3), eTRANSPORT_PIPE_MQTT);
    		break;
#endif

    	default:
    		// Handle payload content
    		LOG_DBG( LOG_LEVEL_COMM, "NONE supported message type %u\n", msgType );
    		break;
    }

#else
    SvcData_MessageHandler( msgData_p->message->payload, msgData_p->message->payloadlen );
#endif


    // DEBUG
    LOG_DBG( LOG_LEVEL_COMM, "IDEFDevice: RX (id:%d)\n", msgData_p->message->id );
}

#if 0
/*
 * IDEFDevice_StartService
 */
bool IDEFDevice_StartService( MQTTClient *client_p )
{
    int rc = -1;

    // Initialize IDEF DEVICE Service
    // Subscribe to my topic
    rc = MQTTSubscribe( client_p, "/SKF/Device/0", QOS1, IDEFDevice_MQTTMessageHandler );
    if ( rc == 0 ) {
        LOG_DBG( LOG_LEVEL_COMM, "IDEFDevice: connected and subscribed to broker (%s)\n" , "<topic>");
    }

    return (rc == 0);
}
#endif

int SvcData_Init( MQTTClient *client_p, SvcDataConfig_t *config_p )
{
    State.mqttClient_p = client_p;
    State.svcState = ISVCDATA_STATE_DISABLED;
    State.apiMutex = xSemaphoreCreateMutex();

    State.mqttQosDefault = QOS0; // Default non-confirmed PUBLISH (at most once delivery)
    State.mqttDupDefault = 0;    // At QOS0, DUP must be 0
    State.mqttRetainDefault = 0; // Don't retain the message at the broker by default
    //State.mqttLastId = 0; // Access this value with the MQTTDATA_GETNEXTID macro

    State.dataMsgTxLastId = 0; // Access this value with the DATAMSG_GETNEXTID macro

#if 0
    // this must be redone when the configuration changes !
    // so do it just before we need it, and that is the moment when we subscribe !
    // so this is moved to ISvcData_Start()

    // Construct SvcData MQTT TOPIC (Check that it's not over 32 bytes including \0

    // subscribe to {company}/{service}/{Destination}, and destination is the deviceId
    // this string is needed more than once to check incoming messages, if it is for 'us' etc.
    strncpy(MQTTTopic_SvcData, (char *) mqttGetSubTopic(), sizeof(MQTTTopic_SvcData));
#endif

    SvcDataInitCallBack();

    // Init CLI commands
    SvcDataCLIInit();

#ifdef DEBUG
    // next callbacks are just for debugging/testing
    SvcDataSetCallback(SvcData_cb_pubAlive, handlePubAlive_cb);
    SvcDataSetCallback(SvcData_cb_pubData, handlePubData_cb);
    SvcDataSetCallback(SvcData_cb_storeDataRequest, handleStoreDataRequest_cb);
    SvcDataSetCallback(SvcData_cb_storeDataReply, handleStoreDataReply_cb);
    SvcDataSetCallback(SvcData_cb_getDataRequest, handleGetDataRequest_cb);
    SvcDataSetCallback(SvcData_cb_getDataReply, handleGetDataReply_cb);
#endif

    return ISVCDATARC_OK;
}

ISvcDataRc_t ISvcData_Start( void )
{
    int rc = -1;

    if ( State.mqttClient_p == NULL )
        return ISVCDATARC_ERR_PARAM;

    // Take API Mutex
    if (!xSemaphoreTake(State.apiMutex, 100))
        return ISVCDATARC_ERR_STATE;

    // TODO Check COMM state

    // create subscribe topic here (again) because configuration changes may have taken place.

    // Construct SvcData MQTT TOPIC (Check that it's not over 32 bytes including \0
    // subscribe to {company}/{service}/{Destination}, and destination is the deviceId
    // this string is needed more than once to check incoming messages, if it is for 'us' etc.
    strncpy(MQTTTopic_SvcData, (char *) mqttGetSubTopic(), sizeof(MQTTTopic_SvcData));

    rc = MQTTSubscribe( State.mqttClient_p, MQTTTopic_SvcData, QOS0, SvcData_MQTTMessageHandler );
    if ( rc == 0 ) {
        LOG_DBG( LOG_LEVEL_COMM, "IDEFDevice: subscribed to broker (%s)\n" , MQTTTopic_SvcData);

        State.svcState = ISVCDATA_STATE_STARTED;
    }

    // Release API Mutex
    xSemaphoreGive(State.apiMutex);

    return ISVCDATARC_OK;
}

ISvcDataRc_t ISvcData_Stop( void )
{
    if ( State.svcState == ISVCDATA_STATE_DISABLED )
    {
        return ISVCDATARC_OK;
    }

    // Take API Mutex
    if (!xSemaphoreTake(State.apiMutex, 100))
    {
        return ISVCDATARC_ERR_BUSY;
    }

    LOG_DBG( LOG_LEVEL_COMM, "Invoking MQTTUnsubscribe In %s()\n" , __func__);

    uint32_t saveTimeout = State.mqttClient_p->command_timeout_ms;
    // make it ten seconds worth, average time is 300ms, 180 seconds is just way too long
    State.mqttClient_p->command_timeout_ms = 10 * 1000;
    int rc = MQTTUnsubscribe( State.mqttClient_p, MQTTTopic_SvcData );
    State.mqttClient_p->command_timeout_ms = saveTimeout;

    if ( rc == 0 )
    {
        LOG_DBG( LOG_LEVEL_COMM, "IDEFDevice: unsubscribed from broker (%s)\n" , MQTTTopic_SvcData);
    }

    // Always set state to disabled
    State.svcState = ISVCDATA_STATE_DISABLED;

    // Release API Mutex
    xSemaphoreGive(State.apiMutex);

    return ISVCDATARC_OK;
}

/**
 * ISvcData_Publish_Alive
 *
 * @brief Send message: Alive
 * Typically sent only once to inform SKF Connectivity Data Service that
 * the device is present on the network.
 *
 * @return
 *   ISVCDATARC_OK       service stopped successfully
 *   ISVCDATARC_STATE    state error, e.g. service was not running
 */
ISvcDataRc_t ISvcData_Publish_Alive( void )
{
    int rc = -1;
    int result = ISVCDATARC_OK;

#ifdef PROTOBUFTEST
    // in testing mode, we do not use mqtt, so no need to check for that
#else
    if ( State.svcState == ISVCDATA_STATE_DISABLED )
        return ISVCDATARC_ERR_STATE;
#endif

    // Take API Mutex
    if (!xSemaphoreTake(State.apiMutex, 100))
        return ISVCDATARC_ERR_BUSY;

    // Construct ALIVE message and send

    // Construct message
    MsgTx = SvcDataMsg_Init;


#ifdef PROTOBUF_GPB2_1
        // Define serialization type (Preamble)
        TxBuf[0] = SVCDATA_MQTT_SERDES_ID_GPB2_1;

        TxBuf[1] = (MSG_TYPE_UNKNOWN & 0xff);
        TxBuf[2] = ((MSG_TYPE_UNKNOWN >> 8) & 0xff);

        // Skip Preamble + MsgType
        // For now use the the UNKNOWN Msg type as intermediate step for
        // NEW implementation of the flat IDEF protocol
        pb_ostream_t stream = pb_ostream_from_buffer(&TxBuf[3], sizeof(TxBuf)-3);
#else
        // Define serialization type (Preamble)
        TxBuf[0] = SVCDATA_MQTT_SERDES_ID_GPB2;

        // Skip Preamble
        pb_ostream_t stream = pb_ostream_from_buffer(&TxBuf[1], sizeof(TxBuf)-1);
#endif

    MsgTx.hdr.function_id = SKF_FunctionId_PUBALIVE;
    MsgTx.hdr.message_id = DATAMSG_GETNEXTID(State.dataMsgTxLastId);
    MsgTx.hdr.source_id.funcs.encode = &SvcDataMsg_HdrEncodeSourceId;
    MsgTx.hdr.source_id.arg = 0;
    MsgTx.hdr.type = SKF_MsgType_PUBLISH;
    MsgTx.hdr.version = SVC_DATA_PROTOCOL_VERSION;

    MsgTx.which__messages = SKF_SvcDataMsg_publish_tag;
    MsgTx._messages.publish.which__publications = SKF_Publish_alive_tag;
    MsgTx._messages.publish._publications.alive.time_stamp = ConfigSvcData_GetIDEFTime();
    LOG_DBG( LOG_LEVEL_COMM, "MsgTx alive timestamp: %llu\n", MsgTx._messages.publish._publications.alive.time_stamp );

    if (!pb_encode(&stream, SKF_SvcDataMsg_fields, &MsgTx)) {
        LOG_DBG( LOG_LEVEL_COMM, "pb_encode error! (fatal)\n" );
    }

    size_t len = stream.bytes_written;

    // get encoded size does a complete encode without writing, we just did a complete encode, so we know the size !
    //pb_get_encoded_size( &len, SKF_SvcDataMsg_fields, &MsgTx );
#ifdef PROTOBUF_GPB2_1
    len += 3; // Add length of preamble + msg_type
#else
    len += 1; // Add length of preamble
#endif

#ifdef PROTOBUFTEST
    // throw it in the decoder directly, do not send over mqtt
    // Handle payload content
    SvcData_MessageHandler( TxBuf, len );
#else
    rc = SvcData_PublishMQTTMessage( (void*)TxBuf, len );
    if (rc != 0) {
        result = ISVCDATARC_ERR_MQTT;
    }
#endif

    // Release API Mutex
    xSemaphoreGive(State.apiMutex);

    return result;
}


bool svcData_GetBlockInfo(uint32_t * offset_p, uint32_t * maxNrElements_p)
{
    if (offset_p) *offset_p = State.ElementOffset;
    if (maxNrElements_p) *maxNrElements_p = State.nrElements;

    return (State.blockTransfer);
}

/**
 * ISvcData_Publish_Data
 *
 * @brief Send message: Data
 * This function publishes the data in the list passed to this function. The data-list
 * is typically pre-configured and contains a list with parameter ID's that must be
 * sent over the wire.
 *
 * In case of sending a big array, the list must only contain one parameterId, the number of elements may be overruled.
 *
 * @param dataListId ID of the pre-configured IDEF data list (containing 1 or more parameterGroups of
 *                   of 1 or more parameterValues)
 * @param nrElementsOverrule    overrules the number of array elements transferred, set to zero for all elements
 * @param msgType        if reply, the messageId (next parameter) is taken, otherwise a self autogenerated one
 * @param msgType       message_id to use if the publish is a reply
 * @return
 *   ISVCDATARC_OK       service stopped successfully
 *   ISVCDATARC_STATE    state error, e.g. service was not running
 */
ISvcDataRc_t ISvcData_Publish_Data(  SvcDataData_t * dataListId, uint32_t nrElementsOverrule, SKF_MsgType msgType, int32_t message_id )
{
    int rc = -1;
    int result = ISVCDATARC_OK;
    uint32_t block_count = 1;

    uint32_t block_index = 0;

#ifdef DEBUG
    uint32_t startTimeMs, stopTimeMs;

    startTimeMs = xTaskGetTickCount() * portTICK_PERIOD_MS  ;
//    SEGGER_SYSVIEW_OnUserStart(0x1);
#endif

#ifdef PROTOBUFTEST
    // in testing mode, we do not use mqtt, so no need to check for that
#else
    if ( State.svcState == ISVCDATA_STATE_DISABLED )
        return ISVCDATARC_ERR_STATE;
#endif

//    // Take API Mutex
//    if (!xSemaphoreTake(State.apiMutex, 100))
//        return ISVCDATARC_ERR_BUSY;

    State.blockTransfer = false;
    State.ElementOffset = 0;
    State.maxElementsPerBuffer = 0;

    // determine if the parameter list contains a set of simple parameters (should fit in one message)
    // or if it contains one big array, which must be split over various messages

    if (dataListId->numberOfParamGroups == 1) {
        if (dataListId->ParamValueGroup_p->numberOfParamValues == 1) {
            uint32_t storeId;

            // Take API Mutex
            if (!xSemaphoreTake(State.apiMutex, 100))
                return ISVCDATARC_ERR_BUSY;

            if ( DATASTORE_RESERVED != (storeId = SvcData_IdefIdToDataStoreId( dataListId->ParamValueGroup_p->ParamValue_p[0] ))) {
                const DataDef_t * dataDef_p;
                dataDef_p = getDataDefElementById( storeId);
                if (dataDef_p != NULL) {
                    uint32_t maxElementsPerBuffer, bufferSpace;

                    // approximate room we have each buffer :

                    bufferSpace = (sizeof(TxBuf) - sizeof(MsgTx.hdr)) *0.9 ; // TODO: replace wild guess about the overhead with something more precise
                    maxElementsPerBuffer = bufferSpace / (DATADEFTYPE2SIZE(dataDef_p->type));
                    if (nrElementsOverrule == 0) {
                        nrElementsOverrule = dataDef_p->length;
                    }
                    if (nrElementsOverrule > dataDef_p->length) {
                        nrElementsOverrule = dataDef_p->length; // boundary check
                    }
                    block_count = nrElementsOverrule/maxElementsPerBuffer +1;
                    State.blockTransfer = true;//block_count>1;

                    State.maxElementsPerBuffer = maxElementsPerBuffer;

                    //printf("maxElementsPerBuffer = %d, bufferSpace = %d\n",maxElementsPerBuffer, bufferSpace);// TODO : remove

                } else {
                    result = ISVCDATARC_ERR_PARAM;
                }
            } else {
                // PANIC !
                result = ISVCDATARC_ERR_PARAM;
            }
            // Release API Mutex
            xSemaphoreGive(State.apiMutex);
        }
    }




    for (block_index=0; (block_index < block_count) && (result == ISVCDATARC_OK); block_index++) {

        LOG_DBG( LOG_LEVEL_COMM, "block_count/index = %d/%d\n",block_count,block_index);// TODO : remove

        if (block_index > 0) {
            // TODO Need to be resolved
        	// With out the delay the multiple blocks transfer will end up in sending a block multiple time
        	// missing another block. (the case when receiving a block at the same time as sending)
            // HARD to debug since adding breakpoints will also solve the problem (because of different timing)
        	// IMPORTANT For debugging this is okay but finally needs to be resolved.
        	vTaskDelay(1); // give the receiving task some breathing room, when the semaphore is not claimed
        }

        // Take API Mutex
        if (!xSemaphoreTake(State.apiMutex, 100))
            return ISVCDATARC_ERR_BUSY;

        State.ElementOffset = block_index * State.maxElementsPerBuffer;
        State.nrElements = (nrElementsOverrule - State.ElementOffset) > State.maxElementsPerBuffer ? State.maxElementsPerBuffer : (nrElementsOverrule - State.ElementOffset);

        // Construct PUBLISH-DATA message and send

        // Construct message
        MsgTx = SvcDataMsg_Init;

#ifdef PROTOBUF_GPB2_1
        // Define serialization type (Preamble)
        TxBuf[0] = SVCDATA_MQTT_SERDES_ID_GPB2_1;

        TxBuf[1] = (MSG_TYPE_UNKNOWN & 0xff);
        TxBuf[2] = ((MSG_TYPE_UNKNOWN >> 8) & 0xff);

        // Skip Preamble + MsgType
        // For now use the the UNKNOWN Msg type as intermediate step for
        // NEW implementation of the flat IDEF protocol
        pb_ostream_t stream = pb_ostream_from_buffer(&TxBuf[3], sizeof(TxBuf)-3);
#else
        // Define serialization type (Preamble)
        TxBuf[0] = SVCDATA_MQTT_SERDES_ID_GPB2;

        // Skip Preamble
        pb_ostream_t stream = pb_ostream_from_buffer(&TxBuf[1], sizeof(TxBuf)-1);
#endif

        MsgTx.hdr.function_id = SKF_FunctionId_PUBDATA;
        MsgTx.hdr.message_id = msgType == SKF_MsgType_REPLY ? message_id : DATAMSG_GETNEXTID(State.dataMsgTxLastId);
        MsgTx.hdr.source_id.funcs.encode = &SvcDataMsg_HdrEncodeSourceId;
        MsgTx.hdr.source_id.arg = 0; // TODO Pass source ID to encode function
        MsgTx.hdr.type = msgType;
        MsgTx.hdr.version = SVC_DATA_PROTOCOL_VERSION;

        MsgTx.which__messages = SKF_SvcDataMsg_publish_tag;
        MsgTx._messages.publish.which__publications = SKF_Publish_data_tag;

        // TODO : what to do with messages which have blocks ?
        MsgTx._messages.publish._publications.data.block_count = block_count;
        MsgTx._messages.publish._publications.data.block_index = block_index;
        MsgTx._messages.publish._publications.data.has_block_count = block_count>1/* State.blockTransfer */;
        MsgTx._messages.publish._publications.data.has_block_index = block_count>1/* State.blockTransfer */;

        MsgTx._messages.publish._publications.data.data.param_value_groups.funcs.encode = &SvcDataMsg_EncodeParameterValueGroup;
        MsgTx._messages.publish._publications.data.data.param_value_groups.arg = (void*)dataListId;


        // LOG_DBG( LOG_LEVEL_COMM, "MsgTx dataListId: %u\n", dataListId );

        if (!pb_encode(&stream, SKF_SvcDataMsg_fields, &MsgTx)) {
            LOG_DBG( LOG_LEVEL_COMM, "pb_encode error! (fatal) pbencode error: %s\n", stream.errmsg ? stream.errmsg : "unknown" );
            result = ISVCDATARC_ERR_FATAL; // programming error ?
        }
        LOG_DBG( LOG_LEVEL_COMM, "ISvcData_Publish_Data(): encode buffer %d used of  %d available\n",stream.bytes_written, stream.max_size);
        //SvcDataMsg_PrintOStreamBytes(&stream);

        if (result == ISVCDATARC_OK) {
            size_t len = stream.bytes_written;

#ifdef PROTOBUF_GPB2_1
            len += 3; // Add length of preamble + msg_type
#else
            len += 1; // Add length of preamble
#endif

            //printf("encoded size = %d\n",len);
#ifdef PROTOBUFTEST
            // throw it in the decoder directly, do not send over mqtt
            // Handle payload content
            SvcData_MessageHandler( TxBuf, len );
#else
            rc = SvcData_PublishMQTTMessage( (void*)TxBuf, len );
            if (rc != 0) {
                result = ISVCDATARC_ERR_MQTT;
            }
#endif
        }
        // Release API Mutex
        xSemaphoreGive(State.apiMutex);

    }
#ifdef DEBUG

//    SEGGER_SYSVIEW_OnUserStop(0x1);
    stopTimeMs = xTaskGetTickCount() * portTICK_PERIOD_MS  ;
    LOG_DBG( LOG_LEVEL_COMM," Publish_Data elapsed time %d ms\n",stopTimeMs - startTimeMs);
#endif
    return result;
}

/**
 * ISvcData_RequestStoreData
 *
 * @brief Send message: request store Data
 * This function sends the store data request, which sould be acknowledged by a reply from the receiver.
 * publishes the data in the list passed to this function. The data-list
 * is typically pre-configured and contains a list with parameter ID's that must be
 * sent over the wire.
 *
 * @param dataListId ID of the pre-configured IDEF data list (containing 1 or more parameterGroups of
 *                   of 1 or more parameterValues)
 * @param messageId_p   the message id of the request, next received reply should have this message Id
 *
 * @return
 *   ISVCDATARC_OK       service stopped successfully
 *   ISVCDATARC_STATE    state error, e.g. service was not running
 */
ISvcDataRc_t ISvcData_RequestStoreData(  SvcDataData_t * dataListId, uint32_t * messageId_p  )
{
    int rc = -1;
    int result = ISVCDATARC_OK;


#ifdef PROTOBUFTEST
    // in testing mode, we do not use mqtt, so no need to check for that
#else
    if ( State.svcState == ISVCDATA_STATE_DISABLED )
        return ISVCDATARC_ERR_STATE;
#endif

//    // Take API Mutex
//    if (!xSemaphoreTake(State.apiMutex, 100))
//        return ISVCDATARC_ERR_BUSY;

    State.blockTransfer = false;
    State.ElementOffset = 0;
    State.maxElementsPerBuffer = 0;

    // determine if the parameter list contains a set of simple parameters (should fit in one message)
    // or if it contains one big array, which must be split over various messages



        // Take API Mutex
        if (!xSemaphoreTake(State.apiMutex, 100))
            return ISVCDATARC_ERR_BUSY;

        // Construct request storedata message and send

        // Construct message
        MsgTx = SvcDataMsg_Init;

#ifdef PROTOBUF_GPB2_1
        // Define serialization type (Preamble)
        TxBuf[0] = SVCDATA_MQTT_SERDES_ID_GPB2_1;

        TxBuf[1] = (MSG_TYPE_UNKNOWN & 0xff);
        TxBuf[2] = ((MSG_TYPE_UNKNOWN >> 8) & 0xff);

        // Skip Preamble + MsgType
        // For now use the the UNKNOWN Msg type as intermediate step for
        // NEW implementation of the flat IDEF protocol
        pb_ostream_t stream = pb_ostream_from_buffer(&TxBuf[3], sizeof(TxBuf)-3);
#else
        // Define serialization type (Preamble)
        TxBuf[0] = SVCDATA_MQTT_SERDES_ID_GPB2;

        // Skip Preamble
        pb_ostream_t stream = pb_ostream_from_buffer(&TxBuf[1], sizeof(TxBuf)-1);
#endif

        MsgTx.hdr.function_id = SKF_FunctionId_STOREDATA;
        State.reqStateFunctionId = MsgTx.hdr.function_id;

        MsgTx.hdr.message_id = DATAMSG_GETNEXTID(State.dataMsgTxLastId);
        State.reqStateMessageId = MsgTx.hdr.message_id;
        if (messageId_p) *messageId_p = MsgTx.hdr.message_id;// we need this to know that we got the right reply

        MsgTx.hdr.source_id.funcs.encode = &SvcDataMsg_HdrEncodeSourceId;
        MsgTx.hdr.source_id.arg = 0; // TODO Pass source ID to encode function
        MsgTx.hdr.type = SKF_MsgType_REQUEST;
        MsgTx.hdr.version = SVC_DATA_PROTOCOL_VERSION;

        MsgTx.which__messages = SKF_SvcDataMsg_request_tag;
        MsgTx._messages.request.which__requests = SKF_Request_store_data_tag;

        MsgTx._messages.request._requests.store_data.has_trans_info = false; // TODO : find out what we want with transaction Id in the device
        MsgTx._messages.request._requests.store_data.trans_info.trans_id = 0;
        MsgTx._messages.request._requests.store_data.trans_info.trans_seq = 0;

        MsgTx._messages.request._requests.store_data.data.param_value_groups.funcs.encode = &SvcDataMsg_EncodeParameterValueGroup;
        MsgTx._messages.request._requests.store_data.data.param_value_groups.arg = (void*)dataListId;


        // LOG_DBG( LOG_LEVEL_COMM, "MsgTx dataListId: %u\n", dataListId );

        if (!pb_encode(&stream, SKF_SvcDataMsg_fields, &MsgTx)) {
            LOG_DBG( LOG_LEVEL_COMM, "pb_encode error! (fatal) pbencode error: %s\n", stream.errmsg ? stream.errmsg : "unknown" );
            result = ISVCDATARC_ERR_FATAL; // programming error ?
        }
        LOG_DBG( LOG_LEVEL_COMM, "ISvcData_RequestStoreData(): encode buffer %d used of  %d available\n",stream.bytes_written, stream.max_size);
        //SvcDataMsg_PrintOStreamBytes(&stream);

        if (result == ISVCDATARC_OK) {

            size_t len = stream.bytes_written;


#ifdef PROTOBUF_GPB2_1
            len += 3; // Add length of preamble + msg_type
#else
    		len += 1; // Add length of preamble
#endif


#ifdef PROTOBUFTEST
            // throw it in the decoder directly, do not send over mqtt
            // Handle payload content
            SvcData_MessageHandler( TxBuf, len );
#else
            rc = SvcData_PublishMQTTMessage( (void*)TxBuf, len );
            if (rc != 0) {
                result = ISVCDATARC_ERR_MQTT;
            } else {
                // we did send a request, so we must get a reply for that
                State.reqStatePending = true;
            }
#endif

        }
        // Release API Mutex
        xSemaphoreGive(State.apiMutex);


    return result;

}



// TODO move function to other file
bool SvcDataMsg_EncodeCstring(pb_ostream_t *stream_p, const pb_field_t *field, void * const *arg)
{
    // Get Device SourceId
    char * str_p = (char *)*arg;;

#if 0
    printf("BEGIN: SvcDataMsg_EncodeCstring:");
    SvcDataMsg_PrintOStreamBytes(stream_p);// TODO remove debugprint
#endif
    if (!pb_encode_tag_for_field(stream_p, field)) {
        return false;
    }

    if (!pb_encode_string(stream_p, (uint8_t *) str_p, strlen(str_p))) {
        return false;
    }
#if 0
    printf("END: SvcDataMsg_EncodeCstring:");
    SvcDataMsg_PrintOStreamBytes(stream_p);// TODO remove debugprint
#endif
    return true;
}



/**
 * ISvcData_ReplyStoreData
 *
 * @brief Send message: reply store Data
 * This function sends the store data reply, which should be called after receiving and handling th storedata request.
 *
 * @param message_id    id of the corresponding request message
 * @param result_code   ok or error code
 * @param info          optional info string describing the error when not OK
 *
 * @return
 *   ISVCDATARC_OK       service stopped successfully
 *   ISVCDATARC_STATE    state error, e.g. service was not running
 */
ISvcDataRc_t ISvcData_ReplyStoreData( uint32_t message_id, int32_t result_code, char * info )
{
    int rc = -1;
    int result = ISVCDATARC_OK;


#ifdef PROTOBUFTEST
    // in testing mode, we do not use mqtt, so no need to check for that
#else
    if ( State.svcState == ISVCDATA_STATE_DISABLED )
        return ISVCDATARC_ERR_STATE;
#endif



    State.blockTransfer = false;
    State.ElementOffset = 0;
    State.maxElementsPerBuffer = 0;




        // Take API Mutex
        if (!xSemaphoreTake(State.apiMutex, 100))
            return ISVCDATARC_ERR_BUSY;

        // Construct request storedata message and send

        // Construct message
        MsgTx = SvcDataMsg_Init;

#ifdef PROTOBUF_GPB2_1
        // Define serialization type (Preamble)
        TxBuf[0] = SVCDATA_MQTT_SERDES_ID_GPB2_1;

        TxBuf[1] = (MSG_TYPE_UNKNOWN & 0xff);
        TxBuf[2] = ((MSG_TYPE_UNKNOWN >> 8) & 0xff);

        // Skip Preamble + MsgType
        // For now use the the UNKNOWN Msg type as intermediate step for
        // NEW implementation of the flat IDEF protocol
        pb_ostream_t stream = pb_ostream_from_buffer(&TxBuf[3], sizeof(TxBuf)-3);
#else
        // Define serialization type (Preamble)
        TxBuf[0] = SVCDATA_MQTT_SERDES_ID_GPB2;

        // Skip Preamble
        pb_ostream_t stream = pb_ostream_from_buffer(&TxBuf[1], sizeof(TxBuf)-1);
#endif

        MsgTx.hdr.function_id = SKF_FunctionId_STOREDATA;
        MsgTx.hdr.message_id = message_id; // should be the ID from last request

        //State.dataMsgTxLastId = MsgTx.hdr.message_id;// we need this to know that we got the right reply

        MsgTx.hdr.source_id.funcs.encode = &SvcDataMsg_HdrEncodeSourceId;
        MsgTx.hdr.source_id.arg = 0; // TODO Pass source ID to encode function
        MsgTx.hdr.type = SKF_MsgType_REPLY;
        MsgTx.hdr.version = SVC_DATA_PROTOCOL_VERSION;

        MsgTx.which__messages = SKF_SvcDataMsg_reply_tag;
        MsgTx._messages.reply.which__replies = SKF_Reply_store_data_tag;


        MsgTx._messages.reply.result_code = result_code;

        MsgTx._messages.reply.info.arg = NULL;
        MsgTx._messages.reply.info.funcs.encode = NULL;

        if (info) {
            MsgTx._messages.reply.info.arg = info;
            MsgTx._messages.reply.info.funcs.encode = &SvcDataMsg_EncodeCstring;
        }

        MsgTx._messages.reply._replies.store_data.dummy_field = 0;


        LOG_DBG( LOG_LEVEL_COMM, "MsgTx replyStoreData message Id : %u\n", message_id);
        LOG_DBG( LOG_LEVEL_COMM, "MsgTx replyStoreData result_code: %d\n", result_code);
        if (info) {
            LOG_DBG( LOG_LEVEL_COMM,"MsgTx replyStoreData info : %s\n", info);
        }

        if (!pb_encode(&stream, SKF_SvcDataMsg_fields, &MsgTx)) {
            LOG_DBG( LOG_LEVEL_COMM, "pb_encode error! (fatal) pbencode error: %s\n", stream.errmsg ? stream.errmsg : "unknown" );
            result = ISVCDATARC_ERR_FATAL; // programming error ?
        }
        //SvcDataMsg_PrintOStreamBytes(&stream);

        if (result == ISVCDATARC_OK) {
            size_t len = stream.bytes_written;

#ifdef PROTOBUF_GPB2_1
            len += 3; // Add length of preamble + msg_type
#else
            len += 1; // Add length of preamble
#endif


#ifdef PROTOBUFTEST
            // throw it in the decoder directly, do not send over mqtt
            // Handle payload content
            SvcData_MessageHandler( TxBuf, len );
#else
            rc = SvcData_PublishMQTTMessage( (void*)TxBuf, len );
            if (rc != 0) {
                result = ISVCDATARC_ERR_MQTT;
            }
#endif

        }
        // Release API Mutex
        xSemaphoreGive(State.apiMutex);


    return result;

}


bool SvcDataMsg_EncodeParameters(pb_ostream_t *stream_p, const pb_field_t *field, void * const *arg)
{

    const IDEF_paramid_t * params_p =  ((SvcDataParamValueGroup_t * )  *arg)->ParamValue_p;
    uint32_t idx;

    for (idx=0; idx< ((SvcDataParamValueGroup_t * ) *arg)->numberOfParamValues; idx++) {

       if (!pb_encode_tag_for_field(stream_p, field)) {
            return false;
        }
printf("encode paramid %08x\n",params_p[idx]);
       // writeout the idef parameter id from the request list
       if (!pb_encode_varint(stream_p, params_p[idx])) {
           return false;
       }
    }
    return true;
}


bool SvcDataMsg_EncodeQuery(pb_ostream_t *stream_p, const pb_field_t *field, void * const *arg)
{

    // we do not use the start/endtime stuff, so we go directly to the encode parameter stuff
    SKF_GetDataReq_Query query = SKF_GetDataReq_Query_init_default;

    if (!pb_encode_tag_for_field(stream_p, field)) {
         return false;
     }

    query.has_end_time = false;
    query.has_start_time = false;
    query.parameters.arg = *arg;
    query.parameters.funcs.encode = &SvcDataMsg_EncodeParameters;


    if (!pb_encode_submessage(stream_p, SKF_GetDataReq_Query_fields, &query)) {
        return false;
    }

     return true;
}


/**
 * ISvcData_RequestGetData
 *
 * @brief Send message: request store Data
 * This function sends the store data request, which sould be acknowledged by a reply from the receiver.
 * publishes the data in the list passed to this function. The data-list
 * is typically pre-configured and contains a list with parameter ID's that must be
 * sent over the wire.
 *
 * @param dataListId ID of the pre-configured IDEF data list, timestamp not used
 * @param messageId_p   the message id of the request, next received reply should have this message Id
 *
 * @return
 *   ISVCDATARC_OK       service stopped successfully
 *   ISVCDATARC_STATE    state error, e.g. service was not running
 */
ISvcDataRc_t ISvcData_RequestGetData(  SvcDataParamValueGroup_t * dataListId, uint32_t * messageId_p  )
{
    int rc = -1;
    int result = ISVCDATARC_OK;


#ifdef PROTOBUFTEST
    // in testing mode, we do not use mqtt, so no need to check for that
#else
    if ( State.svcState == ISVCDATA_STATE_DISABLED )
        return ISVCDATARC_ERR_STATE;
#endif

//    // Take API Mutex
//    if (!xSemaphoreTake(State.apiMutex, 100))
//        return ISVCDATARC_ERR_BUSY;

    State.blockTransfer = false;
    State.ElementOffset = 0;
    State.maxElementsPerBuffer = 0;



        // Take API Mutex
        if (!xSemaphoreTake(State.apiMutex, 100))
            return ISVCDATARC_ERR_BUSY;

        // Construct request getdata message and send

        // Construct message
        MsgTx = SvcDataMsg_Init;

#ifdef PROTOBUF_GPB2_1
        // Define serialization type (Preamble)
        TxBuf[0] = SVCDATA_MQTT_SERDES_ID_GPB2_1;

        TxBuf[1] = (MSG_TYPE_UNKNOWN & 0xff);
        TxBuf[2] = ((MSG_TYPE_UNKNOWN >> 8) & 0xff);

        // Skip Preamble + MsgType
        // For now use the the UNKNOWN Msg type as intermediate step for
        // NEW implementation of the flat IDEF protocol
        pb_ostream_t stream = pb_ostream_from_buffer(&TxBuf[3], sizeof(TxBuf)-3);
#else
        // Define serialization type (Preamble)
        TxBuf[0] = SVCDATA_MQTT_SERDES_ID_GPB2;

        // Skip Preamble
        pb_ostream_t stream = pb_ostream_from_buffer(&TxBuf[1], sizeof(TxBuf)-1);
#endif

        MsgTx.hdr.function_id = SKF_FunctionId_GETDATA;
        State.reqStateFunctionId = MsgTx.hdr.function_id;
        MsgTx.hdr.message_id = DATAMSG_GETNEXTID(State.dataMsgTxLastId);
        State.reqStateMessageId = MsgTx.hdr.message_id;

        if (messageId_p) *messageId_p = MsgTx.hdr.message_id;// we need this to know that we got the right reply

        MsgTx.hdr.source_id.funcs.encode = &SvcDataMsg_HdrEncodeSourceId;
        MsgTx.hdr.source_id.arg = 0; // TODO Pass source ID to encode function
        MsgTx.hdr.type = SKF_MsgType_REQUEST;
        MsgTx.hdr.version = SVC_DATA_PROTOCOL_VERSION;

        MsgTx.which__messages = SKF_SvcDataMsg_request_tag;
        MsgTx._messages.request.which__requests = SKF_Request_get_data_tag;


        MsgTx._messages.request._requests.get_data.query.arg = (void *) dataListId;
        MsgTx._messages.request._requests.get_data.query.funcs.encode = &SvcDataMsg_EncodeQuery;

        // LOG_DBG( LOG_LEVEL_COMM, "MsgTx dataListId: %u\n", dataListId );

        if (!pb_encode(&stream, SKF_SvcDataMsg_fields, &MsgTx)) {
            LOG_DBG( LOG_LEVEL_COMM, "pb_encode error! (fatal) pbencode error: %s\n", stream.errmsg ? stream.errmsg : "unknown" );
            result = ISVCDATARC_ERR_FATAL; // programming error ?
        }
        //SvcDataMsg_PrintOStreamBytes(&stream);

        if (result == ISVCDATARC_OK) {
            size_t len = stream.bytes_written;


#ifdef PROTOBUF_GPB2_1
            len += 3; // Add length of preamble + msg_type
#else
            len += 1; // Add length of preamble
#endif


#ifdef PROTOBUFTEST
            // throw it in the decoder directly, do not send over mqtt
            // Handle payload content
            SvcData_MessageHandler( TxBuf, len );
#else
            rc = SvcData_PublishMQTTMessage( (void*)TxBuf, len );
            if (rc != 0) {
                result = ISVCDATARC_ERR_MQTT;
            } else {
                // we did send a request, so we must get a reply for that
                State.reqStatePending = true;
            }
#endif

        }
        // Release API Mutex
        xSemaphoreGive(State.apiMutex);


    return result;

}

/**
 * ISvcData_ReplyGetData
 *
 * @brief Send message: reply get Data
 * This function sends the get data reply, which should be called after receiving and handling the getdata request.
 *
 * @param message_id    id of the corresponding request message
 * @param result_code   ok or error code
 * @param info          optional info string describing the error when not OK
 *
 * @return
 *   ISVCDATARC_OK       service stopped successfully
 *   ISVCDATARC_STATE    state error, e.g. service was not running
 */
ISvcDataRc_t ISvcData_ReplyGetData( uint32_t message_id, int32_t result_code, char * info )
{
    int rc = -1;
    int result = ISVCDATARC_OK;


#ifdef PROTOBUFTEST
    // in testing mode, we do not use mqtt, so no need to check for that
#else
    if ( State.svcState == ISVCDATA_STATE_DISABLED )
        return ISVCDATARC_ERR_STATE;
#endif

//    // Take API Mutex
//    if (!xSemaphoreTake(State.apiMutex, 100))
//        return ISVCDATARC_ERR_BUSY;

    State.blockTransfer = false;
    State.ElementOffset = 0;
    State.maxElementsPerBuffer = 0;

    // determine if the parameter list contains a set of simple parameters (should fit in one message)
    // or if it contains one big array, which must be split over various messages



        // Take API Mutex
        if (!xSemaphoreTake(State.apiMutex, 100))
            return ISVCDATARC_ERR_BUSY;

        // Construct request storedata message and send

        // Construct message
        MsgTx = SvcDataMsg_Init;

#ifdef PROTOBUF_GPB2_1
        // Define serialization type (Preamble)
        TxBuf[0] = SVCDATA_MQTT_SERDES_ID_GPB2_1;

        TxBuf[1] = (MSG_TYPE_UNKNOWN & 0xff);
        TxBuf[2] = ((MSG_TYPE_UNKNOWN >> 8) & 0xff);

        // Skip Preamble + MsgType
        // For now use the the UNKNOWN Msg type as intermediate step for
        // NEW implementation of the flat IDEF protocol
        pb_ostream_t stream = pb_ostream_from_buffer(&TxBuf[3], sizeof(TxBuf)-3);
#else
        // Define serialization type (Preamble)
        TxBuf[0] = SVCDATA_MQTT_SERDES_ID_GPB2;

        // Skip Preamble
        pb_ostream_t stream = pb_ostream_from_buffer(&TxBuf[1], sizeof(TxBuf)-1);
#endif

        MsgTx.hdr.function_id = SKF_FunctionId_GETDATA;
        MsgTx.hdr.message_id = message_id; // should be the ID from last request

        //State.dataMsgTxLastId = MsgTx.hdr.message_id;// we need this to know that we got the right reply

        MsgTx.hdr.source_id.funcs.encode = &SvcDataMsg_HdrEncodeSourceId;
        MsgTx.hdr.source_id.arg = 0; // TODO Pass source ID to encode function
        MsgTx.hdr.type = SKF_MsgType_REPLY;
        MsgTx.hdr.version = SVC_DATA_PROTOCOL_VERSION;

        MsgTx.which__messages = SKF_SvcDataMsg_reply_tag;
        MsgTx._messages.reply.which__replies = SKF_Reply_get_data_tag;


        MsgTx._messages.reply.result_code = result_code;

        MsgTx._messages.reply.info.arg = NULL;
        MsgTx._messages.reply.info.funcs.encode = NULL;

        if (info) {
            MsgTx._messages.reply.info.arg = info;
            MsgTx._messages.reply.info.funcs.encode = &SvcDataMsg_EncodeCstring;
        }

        MsgTx._messages.reply._replies.get_data.dummy_field = 0;


        LOG_DBG( LOG_LEVEL_COMM, "MsgTx replyGetData message Id : %u\n", message_id);
        LOG_DBG( LOG_LEVEL_COMM, "MsgTx replyGetData result_code: %d\n", result_code);
        if (info) {
            LOG_DBG( LOG_LEVEL_COMM,"MsgTx replyGetData info : %s\n", info);
        }

        if (!pb_encode(&stream, SKF_SvcDataMsg_fields, &MsgTx)) {
            LOG_DBG( LOG_LEVEL_COMM, "pb_encode error! (fatal) pbencode error: %s\n", stream.errmsg ? stream.errmsg : "unknown" );
            result = ISVCDATARC_ERR_FATAL; // programming error ?
        }
        //SvcDataMsg_PrintOStreamBytes(&stream);

        if (result == ISVCDATARC_OK) {
            size_t len = stream.bytes_written;

#ifdef PROTOBUF_GPB2_1
            len += 3; // Add length of preamble + msg_type
#else
            len += 1; // Add length of preamble
#endif


#ifdef PROTOBUFTEST
            // throw it in the decoder directly, do not send over mqtt
            // Handle payload content
            SvcData_MessageHandler( TxBuf, len );
#else
            rc = SvcData_PublishMQTTMessage( (void*)TxBuf, len );
            if (rc != 0) {
                result = ISVCDATARC_ERR_MQTT;
            }
#endif

        }
        // Release API Mutex
        xSemaphoreGive(State.apiMutex);


    return result;

}

/*****************************************************************************
 * structures and data required by the ephemeris download
 */
#define TO_100ms	100
#define TO_10s		10000
#define TO_20s		20000
#define TO_30s		30000
#define TO_40s		40000

extern bool epoDebug;

#ifdef _MSC_VER
#define PACKED_STRUCT_START __pragma(pack(push, 1))
#define PACKED_STRUCT_END   __pragma(pack(pop))
#else
#define PACKED_STRUCT_START
#define PACKED_STRUCT_END   __attribute__((packed))
#endif
PACKED_STRUCT_START
typedef struct  _epoData {
	int tag_block;
	int offset;
	uint8_t data[EPO_BUFSIZE];
} t_epoData;
PACKED_STRUCT_END

SemaphoreHandle_t epoSemaphore = NULL;
static char MQTTTopic_EPOData[MQTT_MAX_TOPIC_LEN+1] = {0,};

/*
 * epo_MQTTMessageHandler
 *
 * @desc	callback to process data from the SKF/EPhemeris/IMEIxxxxxxxxxxxxxxx topic
 *
 * @param 	pointer to MessageData
 *
 * @returns void
 */
static void epo_MQTTMessageHandler(MessageData *msgData_p)
{
	t_epoData *epo = (t_epoData*)msgData_p->message->payload;
	if(epoDebug)
	{
		LOG_DBG(LOG_LEVEL_GNSS,  "EPO block %d\r\n", epo->tag_block &  0xFFFF);
	}
	memcpy((void*)(((int)g_pSampleBuffer) + epo->offset), &epo->data[0], EPO_BUFSIZE);
	if(epo->tag_block == 0x55AAFFFF)
	{
		// last block so let the update code know
		xSemaphoreGive(epoSemaphore);
	}
    return;
}

/*
 * EPO_Start
 *
 * @desc	subscribe to the topic SKF/EPhemeris/IMEIxxxxxxxxxxxxxxx
 *
 * @returns ISVCDATARC_OK on success otherwise an error code
 */
int EPO_Start(void)
{
    if(State.mqttClient_p == NULL)
    {
        return ISVCDATARC_ERR_PARAM;
    }

    // Take API Mutex
    if(!xSemaphoreTake(State.apiMutex, TO_100ms))
    {
        return ISVCDATARC_ERR_STATE;
    }

    if(NULL == epoSemaphore)
    {
    	epoSemaphore = xSemaphoreCreateBinary();
    }
    else
    {
    	// already exists so just in case, clean it out
		xSemaphoreTake(epoSemaphore, 0);
    }

    // TODO Check COMM state

    // create subscribe topic here (again) because configuration changes may have taken place.

    // Construct SvcData MQTT TOPIC (Check that it's not over 41 bytes including \0
    // subscribe to {company}/{service}/{Destination}, and destination is the deviceId
    // this string is needed more than once to check incoming messages, if it is for 'us' etc.
    strncpy(MQTTTopic_EPOData, (char *) mqttGetEpoTopic(), sizeof(MQTTTopic_EPOData));
    int rc = MQTTSubscribe(State.mqttClient_p, MQTTTopic_EPOData, QOS0, epo_MQTTMessageHandler);
    // Release API Mutex
    xSemaphoreGive(State.apiMutex);
    if(rc == 0)
    {
        LOG_DBG(LOG_LEVEL_GNSS, "EPO: subscribed to broker (%s)\n", MQTTTopic_EPOData);
        State.svcState = ISVCDATA_STATE_STARTED;
        return ISVCDATARC_OK;
    }

    return rc;
}

/*
 * EPO_Wait
 *
 * @desc	wait on the download completing, signalled on the semaphore
 *
 * @returns pdTRUE on success otherwise pdFALSE
 */
int EPO_Wait(void)
{
	return xSemaphoreTake(epoSemaphore, TO_20s);
}

/*
 * EPO_Stop
 *
 * @desc	unsubscribe to the topic SKF/EPhemeris/IMEIxxxxxxxxxxxxxxx
 *
 * @returns ISVCDATARC_OK on success otherwise an error code
 */
int EPO_Stop(void)
{
    if ( State.svcState == ISVCDATA_STATE_DISABLED )
    {
        return ISVCDATARC_OK;
    }

    // Take API Mutex
    if (!xSemaphoreTake(State.apiMutex, TO_100ms))
    {
        return ISVCDATARC_ERR_BUSY;
    }

    uint32_t saveTimeout = State.mqttClient_p->command_timeout_ms;
    // make it ten seconds worth, average time is 300ms, 180 seconds is just way too long
    State.mqttClient_p->command_timeout_ms = 10 * 1000;
    int rc = MQTTUnsubscribe( State.mqttClient_p, MQTTTopic_EPOData );
    State.mqttClient_p->command_timeout_ms = saveTimeout;

    // Release API Mutex
    xSemaphoreGive(State.apiMutex);

    // did we un-subscribe ok?
    if (rc == 0)
    {
        LOG_DBG(LOG_LEVEL_GNSS, "EPO: unsubscribed from broker (%s)\n" , MQTTTopic_EPOData);
        return ISVCDATARC_OK;
    }

    return ISVCDATARC_ERR_MQTT;
}



#ifdef __cplusplus
}
#endif