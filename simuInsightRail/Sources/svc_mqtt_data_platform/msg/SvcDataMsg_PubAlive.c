#ifdef __cplusplus
extern "C" {
#endif

/*
 * SvcDataMsg_PubAlive.c
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
#include "SvcDataMsg_PubAlive.h"

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

/*
 * Functions
 */


#if 0 // Encode is done in SvcData.c
/**
 * SvcDataMsg_PublishEncodeAlive
 *
 * @brief Encode the contents of a publish-alive (sub)message
 * @param stream_p output stream
 * @param msg_p    pointer to message structure that is pre-filled by the caller
 * @return true on success, false otherwise
 */
bool SvcDataMsg_PublishEncodeAlive(pb_ostream_t *stream_p, SKF_SvcDataMsg *msg_p)
{
    pb_field_t fields;
    pb_ostream_t substream;
    bool status = false;

    //

    return false;
}
#endif

/**
 * SvcDataMsg_PublishDecodeAlive
 *
 * @brief Decode the contents of a publish-alive (sub)message
 * @param stream_p input stream
 * @param msg_p    pointer to message structure that will be filled
 * @return true on success, false otherwise
 */
bool SvcDataMsg_PublishDecodeAlive(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p)
{
    const pb_field_t *fields_p;
    pb_istream_t substream;
    bool status = false;

    // Decode publish submessage

    LOG_DBG( LOG_LEVEL_COMM, "SvcDataMsg_PublishDecodeAlive: ");
    LOG_DBG( LOG_LEVEL_COMM, "Stream bytes left: %d\n", stream_p->bytes_left);

    //SvcDataMsg_PrintIStreamBytes(stream_p);

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

    // the stream is now just after the header part
    // Based on information in the header, we know that it must be a publish/pubalive message, but is it really so ?
    // So find the field descriptor for the publish message, it better be there otherwise we quit
    if(!getSubStreamForTargetFields(stream_p,SKF_SvcDataMsg_fields,SKF_Publish_fields,SKF_SvcDataMsg_publish_tag,&fields_p, &substream))
    	return false;

    //    message Publish{
    //        oneof _publications{
    //             AlivePub           alive = 1;
    //             DataPub            data = 2;
    //             MetadataPub        metadata = 3;
    //             SchedulePub        schedules = 4;
    //        }
    //    }

#if 0
    LOG_DBG( LOG_LEVEL_COMM, "Stream bytes left: %d\n", stream_p->bytes_left);
    LOG_DBG( LOG_LEVEL_COMM, "-varint: ");
    SvcDataMsg_PrintIStreamBytes(stream_p);
#endif

    // Decode alive tag and will return SKF_AlivePub_fields pointer when found
    fields_p = DecodeSubmessageTag(&substream, fields_p, SKF_Publish_alive_tag);
    if (fields_p == SKF_AlivePub_fields) {

        pb_istream_t subsubstream;

        //    // Alive notification. Used when no data is available and the server needs to be notified.
        //    message AlivePub {
        //        required uint64             time_stamp = 1;
        //    }
        // Decode subsubstream, split of the AlivePub part
        if (!pb_make_string_substream(&substream, &subsubstream))
        {
            pb_close_string_substream(stream_p, &substream);
            return false;
        }

        status = pb_decode(&subsubstream, fields_p, &msg_p->_messages.publish._publications.alive);

        // All fields are decoded, time to close the substreams
        pb_close_string_substream(&substream, &subsubstream);
        pb_close_string_substream(stream_p, &substream);

        return status;

    } else {
    	// it is not a publish/pubAlibe message according to protobuf
        pb_close_string_substream(stream_p, &substream);
        return false;
    }

    return true;
}




#ifdef __cplusplus
}
#endif