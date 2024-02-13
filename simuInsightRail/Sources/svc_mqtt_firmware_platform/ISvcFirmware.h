#ifdef __cplusplus
extern "C" {
#endif

/*
 * ISvcFirmware.h
 *
 *  Created on: 26 jan. 2016
 *      Author: Daniel van der Velde
 */

#ifndef MOD_SVC_MQTT_FIRMWARE_ISVCFIRMWARE_H_
#define MOD_SVC_MQTT_FIRMWARE_ISVCFIRMWARE_H_

/*
 * Include
 */

// callback functions, a separate one, for every SvcFirmware originated received message
typedef uint32_t (* tSvcFirmwareCallbackFuncPtr)(void * buf );
typedef enum
{
    SvcFirmware_cb_updateNotification = 0,
    SvcFirmware_cb_blockRequest,
    SvcFirmware_cb_blockReply,
	SvcFirmware_cb_max
} tSvcFirmwareCallbackIndex;

typedef enum {
    ISVCFIRMWARE_STATE_DISABLED = 0,
    ISVCFIRMWARE_STATE_STARTED,
} ISvcFirmwareState_t;


/*
 * Functions
 */

//ISvcFirmware_Rx_ReqFirmwareDownload(fwversion_string, size_bytes, productid, hwid, imageid); // Conncectivity needs alive device
//// answer: ok, error
//
//ISvcFirmware_Send_ReqFirmwareBlock(imageid, offset, block_size);
//// answer: ok, error
//
//ISvcFirmware_Rx_ReqStoreFirmwareBlock(imageid, offset, block_size, block_data);
//// answer: ok, error
//
//ISvcFirmware_Notify_DownloadReady(imageid);
//
//ISvcFirmware_Send_ReqFirmwareActivate();
//// answer: ok, error
//
//// Update mandatory status properties on-change


#endif /* MOD_SVC_MQTT_FIRMWARE_ISVCFIRMWARE_H_ */


#ifdef __cplusplus
}
#endif