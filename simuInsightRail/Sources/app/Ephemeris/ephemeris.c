#ifdef __cplusplus
extern "C" {
#endif

/*
 * ephemeris.c
 *
 *  Created on: 11 Feb 2021
 *      Author: KC2663
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "ephemeris.h"
#include "log.h"
#include "xTaskDefs.h"
#include "NvmData.h"
#include "xTaskAppGNSS.h"
#include "xTaskAppCommsTest.h"
#include "NvmConfig.h"
#include "NvmData.h"
#include "Resources.h"
#include "gnssIo.h"
#include "gnssMT3333.h"
#include "rtc.h"
#include "SvcData.h"
#include "configMQTT.h"
#include "TaskComm.h"
#include "crc.h"

#define _CRT_SECURE_NO_WARNINGS

#define SAT_DATA_SIZE				72		// Satellite data size
#define EPO_SET_SIZE				2304	// Bytes in a set = SAT_DATA_SIZE * nrOfSatellites (32)
#define EPO_SETS_IN_14DAYS			56		// Sets in 14 days
#define EPO_BIN_PACKETSIZE_BYTES	227		// Binary EPO packet size
#define MTK14_SIZE					(EPO_SET_SIZE * EPO_SETS_IN_14DAYS) // Total file size
#define EPO_14DAY					598		// Total packets = ceil(MTK14_SIZE/SAT_DATA_SIZE)/3 = 597 + 1 (3 = nrof SAT data fields in packet)

#define MTK_BIN_EPO		723		// EPO binary packet type
#define MTK_BIN_PRE		0x2404	// Preamble
#define MTK_BIN_ACK		0x0002	// Command ID for acknowledgment packet
#define MTK_BIN_POST	0x0A0D	// Bin packet end word
#define BIN_END_SEQ		0xFFFF	// End sequence

#define SECS_IN_24HOURS	((24 * 60) * 60)
#define TO_1s			1000
#define TO_5s			5000
#define TO_10s			10000

#define EPHEMERIS_UPDATE_ATTEMPTS 2

#define EPO_UL_DELAY 20		//! EPO data upload loop delay. milliseconds

extern bool epoDebug;

static bool IsEphemerisExpired();
static bool IsEphemerisCloseToExpiry(uint32_t daysFromExpiry);
static enum EpoStatus DownloadEphemeris();


// CRC used for data integrity check in divided epo download procedure
uint32_t epo_crcd = 0;


/*
 * bool IsEphemerisExpired
 *
 * @brief	determine if the Ephemeris data is valid or expired
 *
 * ephemeris is expired (not valid) if:
 *	expiry = 0
 *	expiry >= current date + 14 day (too far into the future)
 *	expiry < current date +1 day  (too old or almost expired)
 *
 * @return true if expired otherwise false
 */
static bool IsEphemerisExpired()
{
    uint32_t seconds = GetDatetimeInSecs();

    // check if Ephemeris data has expired
    if(!gNvmData.dat.gnss.epoExpiry_secs || ((seconds + SECS_IN_24HOURS) >= gNvmData.dat.gnss.epoExpiry_secs))
    {
    	return true;
    }
    if( gNvmData.dat.gnss.epoExpiry_secs > (seconds + 14*SECS_IN_24HOURS) )
    {
        // if epo expirydate too far into future - reset it to 0
        gNvmData.dat.gnss.epoExpiry_secs = 0;
        return true;
    }
    return false;
}


/*
 * bool IsEphemerisCloseToExpiry
 *
 * @brief	determine if the Ephemeris data is nearing expiry date
 *
 * @param   days before expiry to update ephemeris
 *
 * @return true if expired otherwise false
 */
static bool IsEphemerisCloseToExpiry(uint32_t daysFromExpiry)
{
    uint32_t currentTime_s = GetDatetimeInSecs();
    int nextUploadTime_s = currentTime_s + gNvmCfg.dev.commConf.Upload_repeat;

    if(!gNvmData.dat.gnss.epoExpiry_secs || (abs(nextUploadTime_s - (int)gNvmData.dat.gnss.epoExpiry_secs) <= (daysFromExpiry * SECS_IN_24HOURS)))
    {
    	return true;
    }
    return false;
}


/*
 * Ephemeris_Erase
 *
 * @brief	Erase the ephemeris data stored in the GNSS
 *
 * @return	void
 */
void Ephemeris_Erase(void)
{
    (void)MT3333_SendPmtkCommand( "PMTK127", 0, TO_10s);
	gNvmData.dat.gnss.epoExpiry_secs = 0;
	NvmDataWrite(&gNvmData);
}


/*
 * Ephemeris_CheckStatus
 *
 * @brief	display the status of the ephemeris data stored in the GNSS
 *
 * @param	pointer to unsigned int for returning the EPO expiry time
 *
 * @return	true on success
 */
bool Ephemeris_CheckStatus(uint32_t *epoExpiry)
{
    bool rc_ok = MT3333_SendPmtkCommand( "PMTK607", E_pmtk_epo_info, TO_10s);

	if(true == rc_ok)
	{
		uint32_t seconds;
		char *s = MT3333_getLastResponseMsg();
		int sets, fwn, lwn, fcwn, lcwn;
		unsigned int  ftow, ltow, fctow, lctow;

		// check if correct response has arrived
		if( MT3333_getLastResponseType() != E_pmtk_epo_info)
		{
                    LOG_DBG(LOG_LEVEL_GNSS,  "\r\n<<<Ephemeris.c: MT3333_getLastResponeMsg() returned: %d %s >>>\r\n\n",
			    RtcUTCToString(MT3333_getLastResponseType()), s);

		    rc_ok = false;
		}
		else
		{
		// parse the response
		sscanf(s, "%d, %d, %u, %d, %u, %d, %u, %d, %u", &sets, &fwn, &ftow, &lwn, &ltow, &fcwn, &fctow, &lcwn, &lctow);

		seconds = GNSS_GetUTCFromGPS(fwn, ftow);
		printf("start:    %s\n", RtcUTCToString(seconds));
		seconds = GNSS_GetUTCFromGPS(lwn, ltow);
		printf("stop:     %s\n", RtcUTCToString(seconds));
		if(epoExpiry)
		{
			*epoExpiry = seconds;
		}
		seconds = GNSS_GetUTCFromGPS(fcwn, fctow);
		printf("start(c): %s\n", RtcUTCToString(seconds));
		seconds = GNSS_GetUTCFromGPS(lcwn, lctow);
		printf("stop(c):  %s\n", RtcUTCToString(seconds));
		printf("expires:  %s\n", RtcUTCToString((epoExpiry) ? *epoExpiry : gNvmData.dat.gnss.epoExpiry_secs));
                }
	}
	return rc_ok;
}


/*
 * WaitSequence
 *
 * @brief	Wait on an event and check for the reqired sequence number
 *
 * @param	seq, requested sequence number
 *
 * @return	true for sequence number, false if no event/seq
 */
static bool WaitSequence(uint16_t seq)
{
    struct gnss_buf_str *response;
    uint8_t chksum = 0;
    uint8_t *p;

    // wait for a response packet
    if(NULL == (response = Gnss_WaitBinEvent()))
    {
    	return false;
    }
    // check the packet
    p = &response->buf[0];
    if((MTK_BIN_PRE != *(uint16_t*)&p[0]) || (0xC != *(uint16_t*)&p[2]) ||
       (MTK_BIN_ACK != *(uint16_t*)&p[4]) || (seq != *(uint16_t*)&p[6]) ||
		(1 != p[8]) || (MTK_BIN_POST != *(uint16_t*)&p[10]))
    {
    	response->idx = 0;
    	return false;
    }
    // check the checksum
    p = &response->buf[2];
    for(int i = 0; i < 7; i++)
    {
    	chksum ^= *p++;
    }
    if(chksum != *p)
    {
    	response->idx = 0;
    	return false;
    }

    // Goody, we're valid
	response->idx = 0;
    return true;
}

/*
 * Ephemeris_LoadAndCheck
 *
 * @brief	Load the EPO set into the GNSS module, check and update the expiry date
 *
 * @return	EPO status
 */
enum EpoStatus Ephemeris_LoadAndCheck(void)
{
    uint32_t epoExpiry;
    uint16_t epoAck_seq=0;
    enum EpoStatus result = EPO_UPDATE_IN_PROGRESS;
    uint8_t EPObuffer[EPO_BIN_PACKETSIZE_BYTES];
    uint8_t *p = (uint8_t*)g_pSampleBuffer;

    // ok, let's turn on the GNSS chip and upgrade the little burger
	if(false == MT3333_Startup(TO_5s))
	{
		LOG_DBG(LOG_LEVEL_GNSS, "%s(): failed to power on the GNSS\n", __func__);
		return GNSS_POWER_ON_ERROR;
	}

	LOG_DBG(LOG_LEVEL_GNSS, "%s(): Loading ephemeris to GNSS chip started\n", __func__);

    GNSS_NMEAtoBinary(); // convert NMEA to binary. NO ack here. So we assume it is in Binary format
	vTaskDelay(TO_1s /  portTICK_PERIOD_MS ); // 1 sec delay

	// pad out the buffer for last packet. Note for 14 days only
	memset(p+(EPO_SET_SIZE * EPO_SETS_IN_14DAYS), 0, 120);

	// OK, let's get started on the download
	for(int i = 0; i < EPO_14DAY; i++)
	{
		// first, setup the buffer
		*(uint16_t*)&EPObuffer[6] = epoAck_seq;
		memcpy(&EPObuffer[8], p, SAT_DATA_SIZE*3);

		// now format the rest of the packet, pre-amble, checksum, post
		GNSS_BinaryPacketSetup(EPObuffer, EPO_BIN_PACKETSIZE_BYTES, MTK_BIN_EPO);
		if(epoDebug)
		{
			for(int j = 0; j <3; j++)
			{
				int pos = (j * SAT_DATA_SIZE) + 8;
				LOG_DBG(LOG_LEVEL_GNSS,"EPO block: %d/%08X, week=%04d, sat=%02d\n",
						epoAck_seq,
						(int)p+(j*SAT_DATA_SIZE),
						(*(uint32_t*)&EPObuffer[pos] & 0xFFFFFF)/0xA8,
						EPObuffer[pos+3]);
			}
		}
		// send the packet
                uint16_t written = Gnss_writeBlock( EPObuffer, EPO_BIN_PACKETSIZE_BYTES,   10 * sizeof(EPObuffer)/portTICK_PERIOD_MS);
                if(written != EPO_BIN_PACKETSIZE_BYTES)
                {
                    result = GNSS_WRITE_BLOCK_ERROR;
                    break;
                }

                // now wait on the ACK
                if(false == WaitSequence(epoAck_seq))
                {
                    result = GNSS_WRITE_ACK_ERROR;
                    break;
                }

                // bump up the source pointer
                p += (SAT_DATA_SIZE * 3);
                epoAck_seq++;

                // #437900 - To reduce download errors - add a small wait/delay before sending next block
                vTaskDelay( EPO_UL_DELAY /  portTICK_PERIOD_MS );
	}

	if(EPO_UPDATE_IN_PROGRESS == result)
	{
		do {
			// first, setup the buffer to end it all
			*(uint16_t*)&EPObuffer[6] = BIN_END_SEQ;
			memset(&EPObuffer[8], 0, SAT_DATA_SIZE*3);

			// now format the rest of the packet, pre-amble, checksum, post
			GNSS_BinaryPacketSetup(EPObuffer, EPO_BIN_PACKETSIZE_BYTES, MTK_BIN_EPO);

			// send the packet
			LOG_DBG(LOG_LEVEL_GNSS,"EPO block write: %d\n", BIN_END_SEQ);
			uint16_t written = Gnss_writeBlock( EPObuffer, EPO_BIN_PACKETSIZE_BYTES,   10 * sizeof(EPObuffer)/portTICK_PERIOD_MS);
			if(written != EPO_BIN_PACKETSIZE_BYTES)
			{
				result = GNSS_WRITE_END_BLOCK_ERROR;
				break;
			}
			// now wait on the ACK
			if(false == WaitSequence(BIN_END_SEQ))
			{
				result = GNSS_WRITE_END_ACK_ERROR;
				break;
			}
		} while(0);
	}

	// tacky but required
	vTaskDelay(TO_1s /  portTICK_PERIOD_MS ); // 1 sec delay

	// OK, back into NMEA mode
	GNSS_BinarytoNMEA();

	// tacky but required
	vTaskDelay(TO_1s /  portTICK_PERIOD_MS ); // 1 sec delay

	LOG_DBG(LOG_LEVEL_GNSS, "%s(): Loading ephemeris to GNSS chip complete, rc=%d\n", __func__, result);

	if(EPO_UPDATE_IN_PROGRESS == result)
	{
		// now we update the EPO expiry date?
		if(true == Ephemeris_CheckStatus(&epoExpiry))
		{
			// update the EPO expiry date
			gNvmData.dat.gnss.epoExpiry_secs = epoExpiry;
			NvmDataWrite(&gNvmData);
			result = EPO_UPDATED_OK;
		}
		else
		{
			result = GNSS_STATUS_ERROR;
		}
	}

    // power off the GNSS
    if(false == MT3333_shutdown(TO_1s))
    {
        LOG_DBG(LOG_LEVEL_GNSS,  "\r\n<<< %s(): failed to power off GNSS chip>>>\r\n", __func__);
    }

    return result;
}


/*
 * Ephemeris_CheckCommsError
 *
 * @desc	determine if the ephemeris error is a comms error
 *
 * @returns	true -  comms error
 *			false - not a comms error
 */
bool Ephemeris_CheckCommsError(enum EpoStatus result_code)
{
	return 	(result_code == MQTT_SUBSCRIBE_ERROR) ||
			(result_code == MQTT_PUBLISH_ERROR)   ||
			(result_code == DOWNLOAD_TIMEOUT_ERROR);
}


/*
 * Ephemeris_Update
 *
 * @desc	if required updates the GPS Ephemeris data
 *
 * @returns	Status of update attempt
 */
enum EpoStatus Ephemeris_Update(void)
{
	enum EpoStatus rc = NO_UPDATE;

    // check if Ephemeris data has expired
    LOG_DBG(LOG_LEVEL_GNSS,  "\r\n<<< GNSS Checking Ephemeris >>>\r\n");
    if(IsEphemerisExpired() || IsEphemerisCloseToExpiry(EPHEMERIS_DAYS_BEFORE_EXPIRY_TO_UPDATE))
    {
		for(int nDownloadAttempt = 1; nDownloadAttempt <= EPHEMERIS_UPDATE_ATTEMPTS; nDownloadAttempt++)
		{
			rc = DownloadEphemeris();
			LOG_DBG(LOG_LEVEL_GNSS,  "Download Ephemeris, attempt: %d, rc: %d\r\n", nDownloadAttempt, rc);

			if(rc == EPO_DOWNLOAD_OK)
			{
				break;
			}
		}

		if(rc == EPO_DOWNLOAD_OK)
		{
			for(int nLoadAttempt = 1; nLoadAttempt <= EPHEMERIS_UPDATE_ATTEMPTS; nLoadAttempt++)
			{
				rc = Ephemeris_LoadAndCheck();
				LOG_DBG(LOG_LEVEL_GNSS,  "Load Ephemeris, attempt: %d, rc: %d\r\n", nLoadAttempt, rc);

				if(rc == EPO_UPDATED_OK)
				{
					break;
				}
			}
		}

    }
    else
    {
    	// Ephemeris still valid, let the good people of the parish know
    	LOG_DBG(LOG_LEVEL_GNSS,  "\r\n<<< GNSS Ephemeris still valid; expires %s >>>\r\n\n",
    			RtcUTCToString(gNvmData.dat.gnss.epoExpiry_secs));
    }
	return rc;
}

/*
 * DownloadEphemeris
 *
 * @brief	Download the ephemeris data and CRC check
 *
 * @return	1 - success
 */
static enum EpoStatus DownloadEphemeris()
{
	// start the EPhemeris download
	int rc;

    if(0 != (rc = EPO_Start()))
    {
		char *epoTopic = mqttGetEpoTopic();
        LOG_DBG(LOG_LEVEL_GNSS, "failed to subscribe to broker (%s) error %d\n" , epoTopic, rc);
        return MQTT_SUBSCRIBE_ERROR;
    }

    char *pubTopic = mqttGetPubEpoTopic();

	// if it's blank or expired request a download
	LOG_DBG(LOG_LEVEL_GNSS, "\r\n<<< GNSS Updating Ephemeris >>>\r\n");

	// publish a download EPO request
	char s[128];
#define _CRT_SECURE_NO_WARNINGS
	// create the JSON request
    sprintf(s, "{\"IMEI\":\"%s\",\"bufsize\":%d,\"days\":%d}", mqttGetDeviceId(), EPO_BUFSIZE, EPO_DAYS);
	if (COMM_ERR_OK != TaskComm_Publish(getCommHandle(), s, strlen(s), (uint8_t*)pubTopic, 10000 /* 10 seconds OK? */ ))
	{
		// we already logged an event so no need to do it again
		// kill it baby!
		(void)EPO_Stop();
		return MQTT_PUBLISH_ERROR;
	}

	// wait for the download
	rc = EPO_Wait();

	// kill it baby!
	(void)EPO_Stop();

    if(pdTRUE != rc)
    {
    	LOG_DBG(LOG_LEVEL_GNSS, "%s(): timed out\n", __func__);
		return DOWNLOAD_TIMEOUT_ERROR;
    }

    // check the CRC is valid
    const uint32_t crc = crc32_hardware((void*)g_pSampleBuffer, MTK14_SIZE);
    const uint32_t crcd = *(uint32_t*)(/*(void*)*/g_pSampleBuffer + MTK14_SIZE);
	LOG_DBG(LOG_LEVEL_GNSS, "%s(): Crc Check, Calculated = 0x%08X, Downloaded = 0x%08X\n", __func__, crc, crcd);

    if(crc != crcd)
    {
    	// CRC invalid, so call a halt; we could in theory retry this operation
    	return DOWNLOAD_CRC_ERROR;
    }
	epo_crcd = crcd; // store for later usage

    return EPO_DOWNLOAD_OK;
}



/*
 * Ephemeris_Download
 *
 * @desc	if required downlaods the GPS Ephemeris data and stores in ext flash
 *
 * @returns	Status of download attempt
 */
enum EpoStatus Ephemeris_Download(void)
{
	enum EpoStatus rc = NO_UPDATE;

    // check if Ephemeris data has expired
    LOG_DBG(LOG_LEVEL_GNSS,  "\r\n<<< GNSS Checking Ephemeris >>>\r\n");
    if(IsEphemerisExpired() || IsEphemerisCloseToExpiry(EPHEMERIS_DAYS_BEFORE_EXPIRY_TO_UPDATE))
    {
		for(int nDownloadAttempt = 1; nDownloadAttempt <= EPHEMERIS_UPDATE_ATTEMPTS; nDownloadAttempt++)
		{
			rc = DownloadEphemeris();
			LOG_DBG(LOG_LEVEL_GNSS,  "Download Ephemeris, attempt: %d, rc: %d\r\n", nDownloadAttempt, rc);

			if(rc == EPO_DOWNLOAD_OK)
			{
				break;
			}
		}
    }
    else
    {
    	// Ephemeris still valid, let the good people of the parish know
    	LOG_DBG(LOG_LEVEL_GNSS,  "\r\n<<< GNSS Ephemeris still valid; expires %s >>>\r\n\n",
    			RtcUTCToString(gNvmData.dat.gnss.epoExpiry_secs));
    }
	return rc;
}



/*
 * Ephemeris_writeToGNSS
 *
 * @desc	requires that epo data present in global sample buffer
 *
 * @returns	Status of download attempt
 */
enum EpoStatus Ephemeris_epoToGNSS(void)
{
	enum EpoStatus rc = NO_UPDATE;

	// try writing it to gps module
	for(int nLoadAttempt = 1; nLoadAttempt <= EPHEMERIS_UPDATE_ATTEMPTS; nLoadAttempt++)
	{
		const uint32_t crc = crc32_hardware((void*)g_pSampleBuffer, MTK14_SIZE);
		const uint32_t crcd = *(uint32_t*)(/*(void*)*/g_pSampleBuffer + MTK14_SIZE);
		if ( (crc != crcd) && (crcd == epo_crcd) )
		{
			LOG_DBG(LOG_LEVEL_GNSS,  "Load Ephemeris, EPO data CRC error\r\n", nLoadAttempt, rc);

			return DOWNLOAD_CRC_ERROR;
			break;
		}
		rc = Ephemeris_LoadAndCheck();
		LOG_DBG(LOG_LEVEL_GNSS,  "Load Ephemeris, attempt: %d, rc: %d\r\n", nLoadAttempt, rc);

		if(rc == EPO_UPDATED_OK)
		{
			break;
		}
	}

	return rc;
}


#ifdef __cplusplus
}
#endif