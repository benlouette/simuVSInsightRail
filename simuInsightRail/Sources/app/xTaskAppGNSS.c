#ifdef __cplusplus
extern "C" {
#endif

/*
 * xTaskAppGnss.c
 *
 *  Created on: 24 Feb 2016
 *      Author: Rex Taylor
 *
 *  Application task interface definition specific to GNSS
 *
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include "printgdf.h"

#include "xTaskAppEvent.h"
#include "xTaskDefs.h"

// Used Components

#include "xTaskAppGnss.h"

#include "log.h"

#include "gnssMT3333.h"
#include "gnssIo.h"
#include "Resources.h"
#include "fsl_rtc_hal.h"
#include "NvmConfig.h"
#include "NvmData.h"
#include "xTaskApp.h"
#include "rtc.h"
#include "device.h"
#include "ephemeris.h"
#include "utils.h"

#define TIMEOUT_REV3_S			120

static SemaphoreHandle_t semGnssReady;// quick and dirty task completion signaling
// Variable for Testing the Rotation Speed Change during sampling.
static bool m_bRotnSpeedChangeTestEn = false;
static float m_fTestSpeed1 = 0.0f;
static float m_fTestSpeed2 = 0.0f;

/**
 * @brief	Determine the TTFF timeout depending on hardware
 *
 * @return timeout
 */
int GNSS_maxSecondsToAquireValidData()
{
	// harvester or REV4
	if(Device_HasPMIC() || (Device_GetHardwareVersion() >= HW_PASSRAIL_REV4))
	{
		// same as hotfix-1.4.3
		return TIMEOUT_NO_EPHEMERIS_S;
	}

	// must be a REV3 so go for the full monty
	return TIMEOUT_REV3_S;
}

static uint8_t handleRmcResponse( void * params)
{

    tNmeaDecoded * nmeaDecoded_p = (tNmeaDecoded *)  params;

    switch (nmeaDecoded_p->type) {
    case nmea_RMC:
        if (nmeaDecoded_p->nmeaData.rmc.valid) {
            xSemaphoreGive(semGnssReady);
        }
        break;
    default:
        break;
    }
    return 0;
}

static uint8_t handleGsvResponse( void * params)
{
    tNmeaDecoded * nmeaDecoded_p = (tNmeaDecoded *)  params;

    switch (nmeaDecoded_p->type) {
    case nmea_GSV:
        //LOG_DBG(LOG_LEVEL_GNSS, "GSV decoded: number_of_msg %d, msg_num %d, sat_in_view %d\n", nmeaDecoded_p->nmeaData.gsv.number_of_msg, nmeaDecoded_p->nmeaData.gsv.msg_num, nmeaDecoded_p->nmeaData.gsv.sat_in_view );
        if (nmeaDecoded_p->nmeaData.gsv.msg_num == nmeaDecoded_p->nmeaData.gsv.number_of_msg)
        {
            vTaskDelay(50); //Need to wait a bit to get data for both GPS and GLONASS
            xSemaphoreGive(semGnssReady);
        }
        break;
    default:
        break;
    }
    return 0;
}

static uint8_t handleRmcHdopResponse( void * params)
{
     xSemaphoreGive(semGnssReady);
    return 0;
}


/*
 * gnssCommand
 *
 * @brief    mimic as much as possible the way it worked before the GNSS redesign, to keep things working as before (awaiting serious app redesign !)
 *           when the maxWait is zero, user should wait with the GNSS_WaitForCompletion()
 * @param   command yes, the command describing what the function should do
 *
 * @param   maxWaitMs   maximum wait time before the function exits because of timeout
 *
 * @return true is if went without problems.
 */
bool gnssCommand(uint8_t command, uint32_t maxWaitMs)
{
	//tgGnssEvent gGnssEvent;
	bool retval = false;

	// hack to keep it as 'compatible' as possible with the old 'rex' implementation of gnss
	if (semGnssReady == NULL) {
	    // lazy initialization, I hate it.
	    semGnssReady = xSemaphoreCreateBinary();
	}

    // cleanup, remove all callbacks we may have set, the user may not have called the wait routine which does that normally
    GnssRemoveCallback(gnss_cb_nmea, handleRmcResponse);
    GnssRemoveCallback(gnss_cb_nmea, handleGsvResponse);
    GnssRemoveCallback(gnss_cb_first_accurate_fix, handleRmcHdopResponse);

    //make sure the semaphore is empty, take it with no wait  (not nice when more than one task sends commands !)
    xSemaphoreTake(semGnssReady,0);

    switch(command)
    {
    case GNSS_POWER_ON:
        retval = MT3333_Startup(maxWaitMs);
        // this one is synchronous, but to make the interface identical, also give the semaphore
        xSemaphoreGive(semGnssReady);
        break;
    case GNSS_READ_RMC:
        //
        GnssSetCallback(gnss_cb_nmea, handleRmcResponse);
        retval = true;
        break;
    case GNSS_READ_RMC_HDOP:
        //
    	gnssStatus.collectedData.firstAccurateFixTimeMs = 0;// TODO: This is dirty and potentially dangerous, direct change of variables which belong to another tasks cntext!!!
        GnssSetCallback(gnss_cb_first_accurate_fix, handleRmcHdopResponse);
        retval = true;
        break;
    case GNSS_READ_GSV:
        GnssSetCallback(gnss_cb_nmea, handleGsvResponse);
        retval = true;
        break;
    case GNSS_POWER_OFF:
        retval = MT3333_shutdown(maxWaitMs);
        // this one is synchronous, but to make the interface identical, also give the semaphore
        xSemaphoreGive(semGnssReady);
        break;
    case GNSS_VERSION:
        retval = MT3333_version(maxWaitMs);
        // this one is synchronious, but to make the interface identical, also give the semaphore
        xSemaphoreGive(semGnssReady);
        break;

    }

    if ((maxWaitMs != 0) && retval) retval = GNSS_WaitForCompletion(maxWaitMs);

	return retval;
}

/**
 * @brief   Wait for GNSS completion
 *
 * @param[in] maxTimeoutMs    Max waiting time, in ms
 *
 * @return - true if semaphore obtained before timeout, false otherwise
 */
bool GNSS_WaitForCompletion(uint32_t maxTimeoutMs)
{
    bool rc_ok;
    rc_ok = (pdTRUE==xSemaphoreTake(semGnssReady ,  maxTimeoutMs==portMAX_DELAY ? portMAX_DELAY : maxTimeoutMs/portTICK_PERIOD_MS));

    // cleanup, remove all callbacks we may have set
    GnssRemoveCallback(gnss_cb_nmea, handleRmcResponse);
    GnssRemoveCallback(gnss_cb_nmea, handleGsvResponse);
    GnssRemoveCallback(gnss_cb_first_accurate_fix, handleRmcHdopResponse);

    return rc_ok;
}

/*
 * SetRTCFromGnssData
 *
 * @desc    Set Rtctime from gnss data
 *
 * @param   -
 *
 * @return - true if successful, otherwise false
 */
void SetRTCFromGnssData(tGnssCollectedData* pGnssData)
{
	rtc_datetime_t datetime;
	// convert the gnss time format to rtc time format
	ConvertGnssUtc2Rtc(pGnssData->utc_time, pGnssData->utc_date, &datetime);
	// set the rtc :
	if (RtcSetTime(&datetime)==false)
	{
		LOG_EVENT(996, LOG_NUM_APP, ERRLOGMAJOR, "Set RTC time failed");
	}
}

/*
 * GNSS_GetUTCFromGPS
 *
 * @desc	converts GPS week number and time of week into UTC time in seconds
 *
 * @param 	wn GPS week number
 *
 * @param	tow GPS time of the week
 *
 * @returns UTC time
 */
uint32_t GNSS_GetUTCFromGPS(uint16_t wn, uint32_t tow)
{
	uint32_t utc = 315964800;	// number of seconds from UTC day 1 to GPS day 1
	utc += ((((wn * 7) * 24) *60) * 60);
	utc += tow;

	return utc;
}

/*
 * GNSS_NMEAtoBinary
 *
 * @brief	To load the ephemeris, convert to Binary mode
 *
 * @return	true
 */
bool GNSS_NMEAtoBinary(void)
{
	char buf[40];

    LOG_DBG(LOG_LEVEL_GNSS,"switching to binary mode\n");
    sprintf(buf, "PMTK253,1,%d", (int)gnssBaudrate);
    vTaskDelay(1000);
    GnssIo_setBinaryMode(true);
    MT3333_SendPmtkCommand( buf, 0, 1000);
    return true;
}

/*
 * GNSS_BinarytoNMEA
 *
 * @brief	Binary to NMEA mode once the loading of ephermis is done
 *
 * @return	true
 */
bool GNSS_BinarytoNMEA(void)
{
    uint8_t pkt[14];

    LOG_DBG(LOG_LEVEL_GNSS,"switching to NMEA mode\n");
    pkt[6] = 0; 					// PMTK protocol
    *(uint32_t*)&pkt[7] = gnssBaudrate;	// baud rate
    GNSS_BinaryPacketSetup(pkt, sizeof(pkt), 0xFD);
    uint16_t written = Gnss_writeBlock( pkt, sizeof(pkt),   10 * sizeof(pkt)/portTICK_PERIOD_MS);
    GnssIo_setBinaryMode(false);
    return (written == sizeof(pkt));
}

/*
 * GNSS_BinaryPacketSetup
 *
 * @brief	Sets up a packet for binary transmission to the GNSS
 * 			Adds pre-amble, post and calculates the checksum and saves to the packet
 *
 * @param	pkt pointer to the packet
 * @param	size, the packet size
 * @param	cmd, the packet command
 *
 * @return	void
 */
void GNSS_BinaryPacketSetup(uint8_t *pkt, uint16_t size, uint16_t cmd)
{
	uint8_t chksum = 0;
	uint8_t *p = (pkt + 2);

	pkt[0] = 0x04;					// pre-amble
	pkt[1] = 0x24;
	*(uint16_t *)&pkt[2] = size;	// packet size
	*(uint16_t *)&pkt[4] = cmd;		// command
	// calculate checksum from size upto chksum location
	for(int i = 0; i < size - 5; i++)
	{
		chksum ^= *p++;
	}
	pkt[size-3] = chksum;			// add checksum
	pkt[size-2] = 0x0D;				// CR
	pkt[size-1] = 0x0A;				// LF
}

/*
 * gnssRotnSpeedChangeTestEn
 *
 * @brief	Enables / Disables the GNSS rotation speed change test during
 * 			Sampling. When it is enabled the speed from the GPS data is
 * 			ignored and user specified speed is used. This function is
 * 			primarily meant for test purposes and hence the user specifies
 * 			speed via the cliGnssRotnSpeedChangeTest().
 *
 * @param	true  - The rotation speed change test is Enabled.
 * 			false - The rotation speed change test is Disabled.
 *
 * @return void
 */
void gnssRotnSpeedChangeTestEn(bool bTestEn)
{
	m_bRotnSpeedChangeTestEn = bTestEn;
}

/*
 * gnssSetPreMeasurementSpeed_Knots
 *
 * @brief	Sets the pre measurement speed in Knots. This speed is then used
 * 			to determine the rpm / cycles before the measurement is begun.
 *
 * @param	fSpeedInKnots (float) - Speed in Knots before the start of sampling.
 *
 * @return void
 */
void gnssSetPreMeasurementSpeed_Knots(float fSpeedInKnots)
{
	m_fTestSpeed1 = fSpeedInKnots;
}

/*
 * gnssSetPostMeasurementSpeed_Knots
 *
 * @brief	Sets the post measurement speed in Knots. This speed is then used
 * 			to determine the rpm / cycles after the measurement is done.
 *
 * @param	fSpeedInKnots (float) - Speed in Knots after the completion of sampling.
 *
 * @return void
 */
void gnssSetPostMeasurementSpeed_Knots(float fSpeedInKnots)
{
	m_fTestSpeed2 = fSpeedInKnots;
}
/*
 * gnssIsRotnSpeedChangeTestEn
 *
 * @brief	Queries if the Rotation speed change test is enabled.
 *
 * @return	true  - The rotation speed change test is Enabled.
 * 			false - The rotation speed change test is Disabled.
 */
bool gnssIsRotnSpeedChangeTestEn()
{
	return m_bRotnSpeedChangeTestEn;
}

/*
 * gnssGetPreMeasurementSpeed_Knots
 *
 * @brief	Gets the pre measurement speed in Knots. This speed is then used
 * 			to determine the rpm / cycles before the measurement is begun.
 *
 * @return	fSpeedInKnots (float) - Speed in Knots before the start of sampling.
 */
float gnssGetPreMeasurementSpeed_Knots()
{
	return m_fTestSpeed1;
}

/*
 * gnssGetPostMeasurementSpeed_Knots
 *
 * @brief	Gets the post measurement speed in Knots. This speed is then used
 * 			to determine the rpm / cycles after the measurement is done.
 *
 * @return	fSpeedInKnots (float) - Speed in Knots after the completion of sampling.
 */
float gnssGetPostMeasurementSpeed_Knots()
{
	return m_fTestSpeed2;
}



#ifdef __cplusplus
}
#endif