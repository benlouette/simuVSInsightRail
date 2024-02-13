#ifdef __cplusplus
extern "C" {
#endif

/*
 * SvcDataMsg.c
 *
 *  Created on: 14 mrt. 2016
 *      Author: Daniel van der Velde
 */


/*
 * Includes
 */

#include <stdint.h>
#include <stdbool.h>

#include "pb.h"
#include "pb_common.h"
#include "pb_encode.h"
#include "pb_decode.h"
#include "svccommon.pb.h"    // Generated from svccommon.proto
#include "svcdata.pb.h"      // Generated from svcdata.proto

#include "SvcData.h"
#include "SvcDataMsg.h"
#include "configSvcData.h"

#include "DataStore.h"
#include "configData.h"
#include "log.h"

/*
 * Macros
 */

/*
 * Types
 */

/*
 * Data
 */


// Pre-define SvcData definition structures
//const

/*
 * Functions
 */

bool SvcDataMsg_HdrDecodeSourceId(pb_istream_t *stream_p, const pb_field_t *field, void **arg)
{
    if (stream_p->bytes_left <= MAXSOURCEIDSIZE) {

        // The TAG and Length field have already been decoded by pb_decode
        // (that's how we got here in the callback...)

        // Read the raw byte stream into buffer pointed to by *arg
        if (pb_read(stream_p, *arg, stream_p->bytes_left)) {
            return true;
        } else {
            LOG_DBG( LOG_LEVEL_COMM, "ERROR srcid cb: %s\n", PB_GET_ERROR(stream_p) );
        }
    }

    return false;
}
//TODO BP needs to be moved 
bool SvcDataMsg_HdrDecodeDeviceType(pb_istream_t *stream_p, const pb_field_t *field, void **arg)
{
    if (stream_p->bytes_left <= MAXSOURCEIDSIZE) {

        // The TAG and Length field have already been decoded by pb_decode
        // (that's how we got here in the callback...)

        // Read the raw byte stream into buffer pointed to by *arg
        if (pb_read(stream_p, *arg, stream_p->bytes_left)) {
            return true;
        } else {
            LOG_DBG( LOG_LEVEL_COMM, "ERROR devid cb: %s\n", PB_GET_ERROR(stream_p) );
        }
    }

    return false;
}


bool SvcDataMsg_HdrEncodeSourceId(pb_ostream_t *stream_p, const pb_field_t *field, void * const *arg)
{
    // Get Device SourceId
    uint8_t * sourceId_p = ConfigSvcData_GetSourceId();


#if 0
    printf("BEGIN: SvcDataMsg_HdrEncodeSourceId:");
    SvcDataMsg_PrintOStreamBytes(stream_p);// TODO remove debugprint
#endif
    if (!pb_encode_tag_for_field(stream_p, field)) {
        return false;
    }

    if (!pb_encode_string(stream_p, sourceId_p, strlen((char *)sourceId_p))) {
        return false;
    }
#if 0
    printf("END: SvcDataMsg_HdrEncodeSourceId:");
    SvcDataMsg_PrintOStreamBytes(stream_p);// TODO remove debugprint
#endif
    return true;
}




void SvcDataMsg_PrintIStreamBytes(pb_istream_t *stream_p)
{
    uint32_t byteCount = 0;
    uint32_t maxByteCount = 80; // TBD
    pb_istream_t copyStream = *stream_p; // Local copy so the caller keeps the original state

    while ((copyStream.bytes_left > 0) && (byteCount < maxByteCount))
    {
        uint8_t byte;
        copyStream.callback(&copyStream, &byte, 1);
        copyStream.bytes_left--;
        byteCount++;
        LOG_DBG( LOG_LEVEL_COMM, "%02X ", byte);
    }

    LOG_DBG( LOG_LEVEL_COMM, "\n");
    if (byteCount == maxByteCount) LOG_DBG( LOG_LEVEL_COMM, "IStream debug print limit reached, more characters in buffer (%d) than printed (%d).\n",stream_p->bytes_left, maxByteCount);
}

void SvcDataMsg_PrintOStreamBytes(pb_ostream_t *stream_p)
{

    uint32_t i;
    uint8_t *buf= (uint8_t *) stream_p->state;

    buf=&buf[- (int)stream_p->bytes_written];
    LOG_DBG( LOG_LEVEL_COMM, "Printing Output Stream: %08x (%d) ", stream_p, stream_p->bytes_written);
    for (i=0; i< stream_p->bytes_written; i++) {
        LOG_DBG( LOG_LEVEL_COMM, "%02X ", buf[i]);
    }

    LOG_DBG( LOG_LEVEL_COMM, "\n");
}

/**
 * EncodeSubmessageTag
 * @brief This function writes the requested submessage tag to the stream
 * @param stream_p output stream
 * @param subMsgFields_p Submessage fields type
 * @param parentMsgFields_p Parentmessage fields type
 * @return true when successful, false otherwise
 */
bool SvcDataMsg_EncodeSubmessageTag(pb_ostream_t *stream_p, const pb_field_t *subMsgFields_p, const pb_field_t *parentMsgFields_p)
{
    const pb_field_t *field;
    for (field = parentMsgFields_p; field->tag != 0; field++)
    {
        if (field->ptr == subMsgFields_p)
        {
            // This is our field: encode tag
            if (!pb_encode_tag_for_field(stream_p, field))
                return false;

            return true;
        }
    }

    // Field not found
    return false;
}

/**
 * DecodeSubmessageTag
 *
 * @brief This function expects the next tag to belong to the specified expectedTag and
 *        that the wire type indicates a submessage (PB_WT_STRING)
 * @param stream_p input stream
 * @param parentFields_p the parent message that holds the expected submessage
 * @param expectedTag the expected submessage tag
 * @return pointer to the submessage fields when successful, NULL otherwise
 */
const pb_field_t *DecodeSubmessageTag(pb_istream_t *stream_p, const pb_field_t *parentFields_p, uint32_t tagTarget)
{
    pb_wire_type_t wire_type;
    uint32_t tag;
    bool eof;

    if (!pb_decode_tag(stream_p, &wire_type, &tag, &eof))
        return NULL;

    if (tag != tagTarget)
        return NULL;

    if (wire_type == PB_WT_STRING)
    {
        const pb_field_t *field;
        for (field = parentFields_p; field->tag != 0; field++)
        {
            if (field->tag == tag && (field->type & PB_LTYPE_SUBMESSAGE))
            {
                /* Found our field. */
                return field->ptr;
            }
        }
    }

    return NULL;
}



/**
 * getSubStreamForTargetFields
 *
 * @brief This function creates a substream if the target fields are present of a submessage.
 * With this substream the submessage can be decoded.
 *
 * @param stream_p input stream
 * @param parentFields_p the parent message that holds the expected submessage
 * @param targetFields_p the expected field of the submessage
 * @param expectedTag the expected submessage tag
 * @return true if decoding
 */
bool getSubStreamForTargetFields(pb_istream_t *stream_p, const pb_field_t *parentFields_p,
	const pb_field_t *targetFields_p, uint32_t expectedTag, const pb_field_t **fields_p,
	pb_istream_t *substream_p ){

	// Decode store data tag and will return SKF_Reply_fields pointer when found
	*fields_p = DecodeSubmessageTag(stream_p, parentFields_p , expectedTag);
	if (*fields_p != targetFields_p)
		return false;

	// so, indeed the tag for the next field is for the Reply
	// the message part contains elements of unknown size, so this is passed as a string item and must be handled as a substream ???


	//    LOG_DBG( LOG_LEVEL_COMM, "\nRequest2: Stream bytes left: %d\n", stream_p->bytes_left);
	//    SvcDataMsg_PrintIStreamBytes(stream_p);

	// Decode substream
	if (!pb_make_string_substream(stream_p, substream_p))
		return false;

	return true;
}


/*
 * SvcDataMsg_DecodeHeader
 *
 * @brief Decode the message header
 * @param stream_p protobuf input stream
 * @param msg_p    pointer to message structure that will be filled
 * @return true on success, false otherwise
 */
bool SvcDataMsg_DecodeHeader(pb_istream_t *stream_p, SKF_SvcDataMsg *msg_p)
{
    const pb_field_t *fields_p;
    pb_istream_t substream;
    bool status = false;

// from the svcdata.proto file
// message Header {
//    required  int32         version = 1 [default = 512]; // [0x02] [0x00]
//    required  MsgType       type = 2;
//    required  bytes         source_id = 3;
//    required  FunctionId    function_id = 4;
//    required  int32         message_id = 5;
// }

//    LOG_DBG( LOG_LEVEL_COMM, "Stream bytes left: %d\n", stream_p->bytes_left);
//    LOG_DBG( LOG_LEVEL_COMM, "Msg: ");
//    PrintIStreamBytes(stream_p);

    // Get the substream for the Required Header message
    if(!getSubStreamForTargetFields(stream_p, // Protobuf stream
    		SKF_SvcDataMsg_fields,		// Parent fields
    		SKF_Header_fields, 			// Target (child) fields
			SKF_SvcDataMsg_hdr_tag, 	// Tag that is looked for
			&fields_p, 			// Return pointer if it found  valid fields
			&substream))		// Return substream for decoding the submessage
    	return false;

    // Decode the message
    status = pb_decode(&substream, fields_p, &msg_p->hdr);

    // Do not forget to close the substream after usage
    pb_close_string_substream(stream_p, &substream);

//    LOG_DBG( LOG_LEVEL_COMM, "Stream bytes left: %d\n", stream_p->bytes_left);
//    LOG_DBG( LOG_LEVEL_COMM, "Msg: ");
//    PrintIStreamBytes(stream_p);

    return status;
}


bool SvcDataMsg_EncodeUint8(pb_ostream_t *stream_p, const pb_field_t *field, void * const *arg)
{
    uint8_t tmp;
    uint32_t idx;
    uint32_t startIdx, count;

    const DataDef_t * dataDef_p = *arg;

    if (!pb_encode_tag_for_field(stream_p, field)) {
        return false;
    }

    // check if we are busy with a multi message block transfer
    if (svcData_GetBlockInfo(&startIdx, &count)) {
        if (startIdx+count > dataDef_p->length) count = dataDef_p->length - startIdx;// correct for last  buffer which could be less elements
    } else {
        startIdx = 0;
        count = dataDef_p->length;
    }

    // implement 'own' writestring
    if (!pb_encode_varint(stream_p, (uint64_t) (sizeof(tmp) * count)))
        return false;


    for (idx = startIdx; idx < startIdx+count; idx++) {
#if 1
        if (false == DataStore_BlockGetUint8(dataDef_p->objectId, idx, 1, &tmp)) return false;

#else
        tmp = ((uint8_t *) dataDef_p->address)[idx];      // a lot faster then using DataStore_GetUint16()
#endif
        pb_write(stream_p, (uint8_t*) &tmp, sizeof(tmp));
    }

    return true;
}


bool SvcDataMsg_EncodeUint16(pb_ostream_t *stream_p, const pb_field_t *field, void * const *arg)
{
    uint16_t tmp;
    uint32_t idx;
    uint32_t startIdx, count;

    const DataDef_t * dataDef_p = *arg;

    if (!pb_encode_tag_for_field(stream_p, field)) {
        return false;
    }

    if (svcData_GetBlockInfo(&startIdx, &count)) {
        if (startIdx+count > dataDef_p->length) count = dataDef_p->length - startIdx;// correct for last  buffer which could be less elements
    } else {
        startIdx = 0;
        count = dataDef_p->length;
    }

    // implement 'own' writestring
    if (!pb_encode_varint(stream_p, (uint64_t) (sizeof(tmp) * count)))
        return false;

    for (idx = startIdx; idx < startIdx+count; idx++) {
#if 1
        if (false == DataStore_BlockGetUint16(dataDef_p->objectId, idx, 1, &tmp)) return false;
#else
        tmp = ((uint16_t *) dataDef_p->address)[idx];      // a lot faster then using DataStore_GetUint16()
#endif
        tmp = __builtin_bswap16(tmp);// convert to high byte first network order
        pb_write(stream_p, (uint8_t*) &tmp, sizeof(tmp));
    }

    return true;
}

#if 0
bool SvcDataMsg_EncodeUint32(pb_ostream_t *stream_p, const pb_field_t *field, void * const *arg)
{
    uint32_t tmp;
    uint32_t idx;
    uint32_t startIdx, count;

    const DataDef_t * dataDef_p = *arg;
    // swap uint32_

    if (!pb_encode_tag_for_field(stream_p, field)) {
        return false;
    }

    if (svcData_GetBlockInfo(&startIdx, &count)) {
        if (startIdx+count > dataDef_p->length) count = dataDef_p->length - startIdx;// correct for last  buffer which could be less elements
    } else {
        startIdx = 0;
        count = dataDef_p->length;
    }

    // implement 'own' writestring
    if (!pb_encode_varint(stream_p, (uint64_t) (sizeof(tmp) * count)))
        return false;
//printf("SvcDataMsg_EncodeUint32: %d values\n",count);
    for (idx = startIdx; idx < startIdx+count; idx++) {
#if 1
        if (false == DataStore_BlockGetUint32(dataDef_p->objectId, idx, 1, &tmp)) return false;
#else
        tmp = ((uint32_t *) dataDef_p->address)[idx];      // a lot faster then using DataStore_GetUint32()
#endif
        tmp = __builtin_bswap32(tmp);// convert to high byte first network order
        pb_write(stream_p, (uint8_t*) &tmp, sizeof(tmp));
    }

    return true;
}
#else

// attempt to make a faster version
bool SvcDataMsg_EncodeUint32(pb_ostream_t *stream_p, const pb_field_t *field, void * const *arg)
{
    uint32_t tmp[16];
    uint32_t idx;
    uint32_t startIdx, count;
    uint32_t buf_items = sizeof(tmp)/sizeof(*tmp);

    const DataDef_t * dataDef_p = *arg;
    // swap uint32_

    if (!pb_encode_tag_for_field(stream_p, field)) {
        return false;
    }

    if (svcData_GetBlockInfo(&startIdx, &count)) {
        if (startIdx+count > dataDef_p->length) count = dataDef_p->length - startIdx;// correct for last  buffer which could be less elements
    } else {
        startIdx = 0;
        count = dataDef_p->length;
    }


    if (buf_items>count) buf_items=count;

    // implement 'own' writestring
    if (!pb_encode_varint(stream_p, (uint64_t) (sizeof(*tmp) * count)))
        return false;
//printf("SvcDataMsg_EncodeUint32: %d values\n",count);
    if (stream_p->callback == NULL) {
        // nanoproto 'size' probe call, don't do the real work, just simulate we have done it.
        stream_p->bytes_written += (sizeof(*tmp) * count);
    } else {
        while (count) {
            if (false == DataStore_BlockGetUint32(dataDef_p->objectId, startIdx, buf_items, &tmp[0])) return false;
            // and now networkbyte order
            for (idx=0; idx<buf_items; idx++) {
                tmp[idx] =  __builtin_bswap32(tmp[idx]);
            }
            pb_write(stream_p, (uint8_t*) &tmp[0], buf_items * sizeof(*tmp));
            startIdx += buf_items;
            count -= buf_items;
            if (buf_items>count) buf_items=count;
        }
    }

    return true;
}

#endif


bool SvcDataMsg_EncodeUint64(pb_ostream_t *stream_p, const pb_field_t *field, void * const *arg)
{
    uint64_t tmp;
    uint32_t idx;
    uint32_t startIdx, count;

    const DataDef_t * dataDef_p = *arg;

    if (!pb_encode_tag_for_field(stream_p, field)) {
        return false;
    }

    if (svcData_GetBlockInfo(&startIdx, &count)) {
        if (startIdx+count > dataDef_p->length) count = dataDef_p->length - startIdx;// correct for last  buffer which could be less elements
    } else {
        startIdx = 0;
        count = dataDef_p->length;
    }

    // implement 'own' writestring
    if (!pb_encode_varint(stream_p, (uint64_t) (sizeof(tmp) * count)))
        return false;

    for (idx = startIdx; idx < startIdx+count; idx++) {
#if 1
       if (false == DataStore_BlockGetUint64(dataDef_p->objectId, idx, 1, &tmp)) return false;
#else
       tmp = ((uint64_t *) dataDef_p->address)[idx];      // a lot faster then using DataStore_GetUint32()
#endif
       tmp = __builtin_bswap64(tmp);// convert to high byte first network order
        pb_write(stream_p, (uint8_t*) &tmp, sizeof(tmp));
    }

    return true;
}

#if 0
bool SvcDataMsg_EncodeSingle(pb_ostream_t *stream_p, const pb_field_t *field, void * const *arg)
{
    union {
        float f;
        uint32_t u;
    } tmp;
    uint32_t idx;
    uint32_t startIdx, count;

    const DataDef_t * dataDef_p = *arg;
    // swap uint32_

    if (!pb_encode_tag_for_field(stream_p, field)) {
        return false;
    }

    if (svcData_GetBlockInfo(&startIdx, &count)) {
        if (startIdx+count > dataDef_p->length) count = dataDef_p->length - startIdx;// correct for last  buffer which could be less elements
    } else {
        startIdx = 0;
        count = dataDef_p->length;
    }

    // implement 'own' writestring
    if (!pb_encode_varint(stream_p, (uint64_t) (sizeof(tmp) * count)))
        return false;

    for (idx = startIdx; idx < startIdx+count; idx++) {
#if 1
       if (false == DataStore_BlockGetSingle(dataDef_p->objectId, idx, 1, &tmp.f)) return false;
#else
        tmp.f = ((float *) dataDef_p->address)[idx];      // a lot faster then using DataStore_GetUint32()
#endif
        tmp.u =  __builtin_bswap32( tmp.u);// convert to high byte first network order
        pb_write(stream_p, (uint8_t*) &tmp.f, sizeof(tmp.f));
    }

    return true;
}
#else
bool SvcDataMsg_EncodeSingle(pb_ostream_t *stream_p, const pb_field_t *field, void * const *arg)
{
    union {
        float f;
        uint32_t u;
    } tmp[16];

    uint32_t idx;
    uint32_t startIdx, count;
    uint32_t buf_items = sizeof(tmp)/sizeof(*tmp);

    const DataDef_t * dataDef_p = *arg;
    // swap uint32_

    if (!pb_encode_tag_for_field(stream_p, field)) {
        return false;
    }

    if (svcData_GetBlockInfo(&startIdx, &count)) {
        if (startIdx+count > dataDef_p->length) count = dataDef_p->length - startIdx;// correct for last  buffer which could be less elements
    } else {
        startIdx = 0;
        count = dataDef_p->length;
    }

    if (buf_items>count) buf_items=count;

    // implement 'own' writestring
    if (!pb_encode_varint(stream_p, (uint64_t) (sizeof(*tmp) * count)))
        return false;

    if (stream_p->callback == NULL) {
        // nanoproto 'size' probe call, don't do the real work, just simulate we have done it.
        stream_p->bytes_written += (sizeof(*tmp) * count);
    } else {
        while (count) {

            if (false == DataStore_BlockGetSingle(dataDef_p->objectId, startIdx, buf_items, &tmp[0].f)) return false;
            // and now networkbyte order
            for (idx=0; idx<buf_items; idx++) {
                tmp[idx].u =  __builtin_bswap32(tmp[idx].u);
            }
            pb_write(stream_p, (uint8_t*) &tmp[0].u, buf_items * sizeof(tmp[0].u));
            startIdx += buf_items;
            count -= buf_items;
            if (buf_items>count) buf_items=count;
        }
    }

    return true;
}
#endif

bool SvcDataMsg_EncodeDouble(pb_ostream_t *stream_p, const pb_field_t *field, void * const *arg)
{
    union {
        double d;
        uint64_t ul;
    } tmp;
    uint32_t idx;
    uint32_t startIdx, count;

    const DataDef_t * dataDef_p = *arg;
    // swap uint32_

    if (!pb_encode_tag_for_field(stream_p, field)) {
        return false;
    }

    if (svcData_GetBlockInfo(&startIdx, &count)) {
        if (startIdx+count > dataDef_p->length) count = dataDef_p->length - startIdx;// correct for last  buffer which could be less elements
    } else {
        startIdx = 0;
        count = dataDef_p->length;
    }

    // implement 'own' writestring
    if (!pb_encode_varint(stream_p, (uint64_t) (sizeof(tmp) * count)))
        return false;

    for (idx = startIdx; idx < startIdx+count; idx++) {
#if 1
       if (false == DataStore_BlockGetDouble(dataDef_p->objectId, idx, 1, &tmp.d)) return false;
#else
        tmp.d = ((double *) dataDef_p->address)[idx];      // a lot faster then using DataStore_GetUint32()
#endif
        tmp.ul = __builtin_bswap64(tmp.ul);
        pb_write(stream_p, (uint8_t*) &tmp.d, sizeof(tmp.d));

    }

    return true;
}


bool SvcDataMsg_EncodeString(pb_ostream_t *stream_p, const pb_field_t *field, void * const *arg)
{
    uint32_t len;

    const DataDef_t * dataDef_p = *arg;

    if (!pb_encode_tag_for_field(stream_p, field)) {
        return false;
    }
#if 1
    {
        // why this clumsy implementation ?
        // the string can be in external flash, and length is not fixed etc.
        // first find out the real length of the string, which can be smaller than the reserved space.
        // incredibly inefficient at the moment, but does not need extra buffering...

        uint8_t tmpbuf = '\0';
        bool rc_ok;
        uint32_t idx;

        len = 0;

        do {
            rc_ok = DataStore_GetString(dataDef_p->objectId, len, 1, &tmpbuf);// read 1 char from the string (TODO: read a small block to speed things up)
            if (tmpbuf) len++;
        } while ((tmpbuf != '\0') && len < (dataDef_p->length)  && rc_ok);
        if (rc_ok == false) return false;

        if (!pb_encode_varint(stream_p, (uint64_t) len))
            return false;

        // now do the same inefficient string read for actually sending the bytes out
        for (idx=0; (idx < len) ; idx++) {
            DataStore_GetString(dataDef_p->objectId, idx, 1, &tmpbuf);// read 1 char from the string (TODO: read a small block to speed things up)
            pb_write(stream_p, (uint8_t*) &tmpbuf, 1);
        }

    }
#else

    len = strlen((char *) dataDef_p->address);

    // implement 'own' writestring
    if (!pb_encode_varint(stream_p, (uint64_t) len))
        return false;

    pb_write(stream_p, (uint8_t*)dataDef_p->address, len);
#endif
    return true;
}


bool SvcDataMsg_EncodeParameterValue(pb_ostream_t *stream_p, const pb_field_t *field, void * const *arg)
{
    // Iterate over parameters referenced by the dataListId (in *arg)
    // Get list length
    // Get index 0,1,2...length-1 and write to param value
    const IDEF_paramid_t * parvals_p =  ((SvcDataParamValueGroup_t * )  *arg)->ParamValue_p;
    uint32_t idx;


    //What structure does the  dataListId point to ?
    //      length, param_val1,param_val2,param_val3...
    //

     for (idx=0; idx< ((SvcDataParamValueGroup_t * ) *arg)->numberOfParamValues; idx++) {

        const DataDef_t * dataDef_p;
        uint32_t DataDefId;

        SKF_ParameterValue parVal = SKF_ParameterValue_init_default;

        //LOG_DBG( LOG_LEVEL_COMM, "SvcDataMsg_EncodeParameterValue: encoding parameter ID : 0x%08x, stream_output_pointer = %08x\n",parvals_p[idx], stream_p->callback);


        DataDefId = SvcData_IdefIdToDataStoreId(parvals_p[idx]);
        if (DataDefId == DATASTORE_RESERVED) {
            // not found !
            LOG_DBG( LOG_LEVEL_COMM, "SvcDataMsg_EncodeParameterValue: ignoring unknown parameter ID 0x%08x\n", parvals_p[idx]);
            // datadef ID DATASTORE_RESERVED will result in a "Unknown Parameter" string sent over
            // return true;// do not make an error of this, the parameter will simply not in the encoded stream
        }

        // retrieve data type from datastore
        dataDef_p = getDataDefElementById(DataDefId);
        if (dataDef_p == NULL) {
            LOG_DBG( LOG_LEVEL_COMM, "SvcDataMsg_EncodeParameterValue: dadastore lookup parameter ID : 0x%08x failed\n",parvals_p[idx]);
            return false;
        }

        if (!pb_encode_tag_for_field(stream_p, field)) {
             return false;
         }

        // fill in common stuff, or defaults
        parVal.parameter_id =  parvals_p[idx]; // idef param ID (not the pb one)
        parVal.has_offset = svcData_GetBlockInfo((uint32_t *) &parVal.offset, NULL);
        parVal.value.data.arg = (void *) dataDef_p;

        switch (dataDef_p->type) {

        case DD_TYPE_BOOL:
             parVal.value.value_type = SKF_Value_t_BOOL;
             parVal.value.data.funcs.encode =  &SvcDataMsg_EncodeUint8;// convertfunc (u)int32 to networkorder.
             break;
        case DD_TYPE_SBYTE:
             parVal.value.value_type = SKF_Value_t_BYTE;
             parVal.value.data.funcs.encode =  &SvcDataMsg_EncodeUint8;// convertfunc (u)int32 to networkorder.
             break;
        case DD_TYPE_BYTE:
             parVal.value.value_type = SKF_Value_t_BYTE;
             parVal.value.data.funcs.encode =  &SvcDataMsg_EncodeUint8;// convertfunc (u)int32 to networkorder.
             break;

        case DD_TYPE_INT16:
             parVal.value.value_type = SKF_Value_t_INT16;
             parVal.value.data.funcs.encode =  &SvcDataMsg_EncodeUint16;// convertfunc (u)int32 to networkorder.
             break;
        case DD_TYPE_UINT16:
             parVal.value.value_type = SKF_Value_t_UINT16;
             parVal.value.data.funcs.encode =  &SvcDataMsg_EncodeUint16;// convertfunc (u)int32 to networkorder.
             break;

        case DD_TYPE_INT32:
             parVal.value.value_type = SKF_Value_t_INT32;
             parVal.value.data.funcs.encode =  &SvcDataMsg_EncodeUint32;// convertfunc (u)int32 to networkorder.
             break;
        case DD_TYPE_UINT32:
             parVal.value.value_type = SKF_Value_t_UINT32;
             parVal.value.data.funcs.encode =  &SvcDataMsg_EncodeUint32;// convertfunc (u)int32 to networkorder.
             break;

        case DD_TYPE_INT64:
             parVal.value.value_type = SKF_Value_t_INT64;
             parVal.value.data.funcs.encode =  &SvcDataMsg_EncodeUint64;// convertfunc (u)int32 to networkorder.
             break;
        case DD_TYPE_DATETIME:
        case DD_TYPE_UINT64:
             parVal.value.value_type = SKF_Value_t_UINT64;
             parVal.value.data.funcs.encode =  &SvcDataMsg_EncodeUint64;// convertfunc (u)int32 to networkorder.
             break;

        case DD_TYPE_SINGLE:
              parVal.value.value_type = SKF_Value_t_SINGLE;
              parVal.value.data.funcs.encode =  &SvcDataMsg_EncodeSingle;// convertfunc (u)int32 to networkorder.
              break;
        case DD_TYPE_DOUBLE:
              parVal.value.value_type = SKF_Value_t_DOUBLE;
              parVal.value.data.funcs.encode =  &SvcDataMsg_EncodeDouble;// convertfunc (u)int32 to networkorder.
              break;

        case DD_TYPE_STRING:
             parVal.value.value_type = SKF_Value_t_STRING;
             parVal.value.data.funcs.encode =  &SvcDataMsg_EncodeString;// convertfunc (u)int32 to networkorder.
             break;

        default:
            // unhandled type, return with error
            LOG_DBG( LOG_LEVEL_COMM, "SvcDataMsg_EncodeParameterValue: datadef type unknown parameter ID : 0x%08x failed\n",parvals_p[idx]);

            return false;
            break;
        }

        if (!pb_encode_submessage(stream_p, SKF_ParameterValue_fields, &parVal)) {
                    return false;
        }
     }

     return true;
}


// just return true only once, because next call iter will be !=0
static bool dummyDataData_cb(uint32_t iter,  const SvcDataParamValueGroup_t * dpvg_p)
{
    return iter==0;
}

bool SvcDataMsg_EncodeParameterValueGroup(pb_ostream_t *stream_p, const pb_field_t *field, void * const *arg)
{
    // Definition: Only one group per PubData message is supported
    // If needed, iterate over multiple groups in this function
    const SvcDataParamValueGroup_t * parvalgroups_p = ((SvcDataData_t * ) *arg)->ParamValueGroup_p;
    tSvcDataDataCallbackFuncPtr dataData_cb = ((SvcDataData_t * ) *arg)->dataData_cb;

    const DataDef_t * dataDef_p;
    uint32_t idx;

    if (dataData_cb == NULL) {
        dataData_cb = (tSvcDataDataCallbackFuncPtr) &dummyDataData_cb;// install loop only once callback
    }

    for (idx=0; idx< ((SvcDataData_t * ) *arg)->numberOfParamGroups; idx++) {

        uint32_t iter=0;
        // if there is a user supplied callback function, we loop until it returns false. This means the buffer full responsibility is also the users.
        while ( dataData_cb(iter++, parvalgroups_p) ) {

			if (!pb_encode_tag_for_field(stream_p, field)) {
				return false;
			}

			SKF_ParameterValueGroup parValGrp = SKF_ParameterValueGroup_init_default;


			// get the timestamp
			//        parValGrp.timestamp = ConfigSvcData_GetIDEFTime(); // get from datastore (paramValueGroup_p[idx].timestampId)


			dataDef_p = getDataDefElementById(parvalgroups_p[idx].timestampDatastoreId);
			if (dataDef_p == NULL) {
				return false;
			}
			if (dataDef_p->type != DD_TYPE_DATETIME) {
				return false;
			}
			if (false==DataStore_BlockGetUint64 (dataDef_p->objectId, 0, 1, &parValGrp.timestamp)) {
				return false;
			}

			parValGrp.param_values.funcs.encode = &SvcDataMsg_EncodeParameterValue; // TODO Encode function for repeated ParameterValue
			parValGrp.param_values.arg = (void *) &parvalgroups_p[idx];//*arg; // TODO Get parameter list pointer


			if (!pb_encode_submessage(stream_p, SKF_ParameterValueGroup_fields, &parValGrp)) {
				return false;
			}
        } // while user callback loop
    }

    return true;
}


typedef struct {
    uint32_t bytes;
    uint8_t * bufStart;
} tValueDataInfo;


bool SvcDataMsg_DecodeValueData(pb_istream_t *stream_p, const pb_field_t *field_p, void **arg)
{
    bool status = true;
    //SKF_Value Val;

    tValueDataInfo *valueData_p = *arg;

//    printf("We got at SvcDataMsg_DecodeValueData %08x %08x %08x\n", stream_p, field_p, arg);
//    printf("bytes left = %d\n",stream_p->bytes_left);
//    SvcDataMsg_PrintIStreamBytes(stream_p);

//    // Defined submessages and union for all data types
//    message Value {
//        required Value_t value_type = 1;
//        required bytes data = 2;
//    }
    LOG_DBG( LOG_LEVEL_COMM,"SvcDataMsg_DecodeValueData: stream: bytes_left = %d state = %08x\n",stream_p->bytes_left, stream_p->state  );

    // make use of the internal nanopb way a buffer is handled,
    // stream_p->state points to where we are in the input buffer
    // stream_p->bytes_left is the length in bytes of our data.
    // record the start and length of this, so we do not need an intermediate buffer, and can do the byteswap conversion (if desired) in place
    valueData_p->bytes = stream_p->bytes_left;
    valueData_p->bufStart = stream_p->state ;
    stream_p->state = (uint8_t*)stream_p->state + stream_p->bytes_left;
    stream_p->bytes_left = 0;

 #if 0
    while (stream_p->bytes_left) {
        uint8_t tmp;

        status = pb_read(stream_p, &tmp, 1);

        printf(" %02x", tmp);
    }
    printf("\n");
#endif
    return status;
}

// to swap integers with a 'count' length of bytes, which are at arbitrary byte boundaries
// so no hardfault as when using the __builtin swap
static void swapper(uint8_t * buf, uint16_t count)
{
    uint16_t i;
    uint16_t end = count-1;
    uint8_t tmp;
    for (i=0; i<count/2; i++) {
        tmp = buf[i];
        buf[i]=buf[end-i];
        buf[end-i]=tmp;
    }
}


bool SvcDataMsg_DecodeParameterValue(pb_istream_t *stream_p, const pb_field_t *field_p, void **arg)
{
    bool status = true;
    SKF_ParameterValue parVal;

//    printf("We got at SvcDataMsg_DecodeParameterValue %08x %08x %08x\n", stream_p, field_p, arg);
//    printf("bytes left = %d\n",stream_p->bytes_left);
//    SvcDataMsg_PrintIStreamBytes(stream_p);

//    // Parameter value definition
//    message ParameterValue {
//        required  sint32 parameter_id = 1;
//        required  Value  value = 2;
//        optional  sint32 offset = 3;
//    }

    tValueDataInfo valueData = { .bytes = 0, .bufStart = NULL};
    uint32_t dataStoreId;
    uint32_t count = 0;

    parVal.value.data.funcs.decode = &SvcDataMsg_DecodeValueData;
    parVal.value.data.arg = &valueData;

    if (false == (status = pb_decode(stream_p, SKF_ParameterValue_fields, &parVal))) {
        LOG_DBG( LOG_LEVEL_COMM,"SvcDataMsg_DecodeParameterValue: pb_decode failed\n");
        if (stream_p->errmsg) LOG_DBG( LOG_LEVEL_COMM, "pb errmsg = %s\n",stream_p->errmsg);
        return false;
    };

    // some extra checking
    if (valueData.bufStart == NULL) {
        LOG_DBG( LOG_LEVEL_COMM,"SvcDataMsg_DecodeParameterValue: bufStart==NULL !\n");
        return false;
    };

    LOG_DBG( LOG_LEVEL_COMM,"parVal.value.value_type =%d\n", parVal.value.value_type);
    LOG_DBG( LOG_LEVEL_COMM,"parVal.has_offset = %d\n", parVal.has_offset);
    LOG_DBG( LOG_LEVEL_COMM,"parVal.offset     = %d\n", parVal.offset);
    LOG_DBG( LOG_LEVEL_COMM,"parVal.parameter_id= 0x%x (%s)\n", parVal.parameter_id, IDEF_paramid_to_str(parVal.parameter_id));
//    LOG_DBG( LOG_LEVEL_COMM,"valueData.bytes    = %d\n", valueData.bytes);
//    LOG_DBG( LOG_LEVEL_COMM,"valueData.bufStart = %08x\n", valueData.bufStart);
//    vTaskDelay(2000);


#if 1
    if (dbg_logging & LOG_LEVEL_COMM) {
        int i;
        printf("incoming bytes:");
        for (i=0; i<valueData.bytes; i++) {
            printf(" %02x",valueData.bufStart[i]);
        }
        printf("\n");
    }
#endif
    // convert from network byteorder to local byte order (on arm this means swap !)
    switch (parVal.value.value_type) {
     case SKF_Value_t_BOOL:
     case SKF_Value_t_BYTE:
    	 count = valueData.bytes/sizeof(uint8_t);
         break;

     case SKF_Value_t_INT16:
     case SKF_Value_t_UINT16:
         count = valueData.bytes/sizeof(uint16_t);
         for (int idx = 0; idx < count; idx++) {
             swapper((uint8_t *) &((uint16_t *) valueData.bufStart)[idx], 2);
         }
         break;

     case SKF_Value_t_INT32:
     case SKF_Value_t_UINT32:
     case SKF_Value_t_SINGLE:
         count = valueData.bytes/sizeof(uint32_t);
         for (int idx = 0; idx < count; idx++) {
             swapper((uint8_t *) &((uint32_t *) valueData.bufStart)[idx], 4);
         }
         break;

     case SKF_Value_t_INT64:
     case SKF_Value_t_UINT64:
     case SKF_Value_t_DOUBLE:
         count = valueData.bytes/sizeof(uint64_t);
         for (int idx = 0; idx < count; idx++) {
             swapper((uint8_t *) &((uint64_t *) valueData.bufStart)[idx], 8);
          }
          break;


     case SKF_Value_t_STRING:
         count = valueData.bytes;
         break;

     default:
         // unhandled type, return with error
         LOG_DBG( LOG_LEVEL_COMM,"SvcDataMsg_PublishDecodeParameterValue: unknown value type %d\n",parVal.value.value_type);
         return false;
         break;

    }
#if 0
    if (dbg_logging & LOG_LEVEL_COMM) {
          int i;
          for (i=0; i<valueData.bytes; i++) {
              printf(" %02x",valueData.bufStart[i]);
          }
          printf("\n");
     }
#endif

    // now find convert IDEF parameter ID to the datastore ID
    if ( DATASTORE_RESERVED != (dataStoreId = SvcData_IdefIdToDataStoreId (parVal.parameter_id))) {
        if (parVal.has_offset == false) {
            parVal.offset = 0;
        }
        status = DataStore_BlockSet( dataStoreId, parVal.offset, count, (void *) valueData.bufStart, false /* because the service is not allowed to overwrite DD_RW items */);
        if (status == false ) {
            LOG_DBG( LOG_LEVEL_COMM,"SvcDataMsg_PublishDecodeParameterValue: DataStore_BlockSet failed IDEF ID (0x%x) datastore Id(%d) \n", parVal.parameter_id, dataStoreId);
        }

    } else {
        LOG_DBG( LOG_LEVEL_COMM,"SvcDataMsg_PublishDecodeParameterValue: IDEF parameter ID (0x%x) unknown, not handled\n", parVal.parameter_id);
    }

    return status;
}

/**
 * SvcDataMsg_DecodeParamValueGroup
 *
 * @brief Decode the contents of a publish-data (sub)message
 * @param stream_p input stream
 * @param field_p  pointer to field descriptor struct
 * @param
 * @return true on success, false otherwise
 */
bool SvcDataMsg_DecodeParameterValueGroup(pb_istream_t *stream_p, const pb_field_t *field_p, void **arg)
{

    bool status = true;
    SKF_ParameterValueGroup parValGrp;

    //printf("We got at SvcDataMsg_DecodeParamValueGroup %08x %08x %08x\n", stream_p, field_p, arg);
    //printf("bytes left = %d\n",stream_p->bytes_left);
    //SvcDataMsg_PrintIStreamBytes(stream_p);

//    // Group of parameter values
//    message ParameterValueGroup{
//        required uint64         timestamp = 1;
//        repeated ParameterValue param_values = 2;
//    }


    parValGrp.param_values.funcs.decode = &SvcDataMsg_DecodeParameterValue;
    parValGrp.param_values.arg = NULL;

    if (false == (status = pb_decode(stream_p, SKF_ParameterValueGroup_fields, &parValGrp))) {
        LOG_DBG( LOG_LEVEL_COMM, "SvcDataMsg_DecodeParameterValueGroup: pb_decode failed !\n");
        if (stream_p->errmsg) LOG_DBG( LOG_LEVEL_COMM, "pb errmsg = %s\n",stream_p->errmsg);
        //printf("bytes left = %d\n",stream_p->bytes_left);
        //SvcDataMsg_PrintIStreamBytes(stream_p);
    }
    LOG_DBG( LOG_LEVEL_COMM, "SvcDataMsg_DecodeParameterValueGroup() timestamp = %lld\n", parValGrp.timestamp);
    SvcData_SetTimestamp(parValGrp.timestamp);

    return status;
}



#ifdef __cplusplus
}
#endif