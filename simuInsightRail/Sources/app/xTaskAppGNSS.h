#ifdef __cplusplus
extern "C" {
#endif

/*
 * xTaskAppGnss.h
 *
 *  Created on: 24 Feb 2016
 *      Author: Rex Taylor
 *
 *  Application task interface definition specific to GNSS
 *
 */


#ifndef SOURCES_APP_XTASKAPPGNSS_H_
#define SOURCES_APP_XTASKAPPGNSS_H_

#include "taskGnss.h"

#if 0

uint32_t AccessGnssData(uint8_t measurementType, uint32_t* validDataTime);
uint32_t AccessGnssRMCData(uint32_t* validDataTime);
uint32_t AccessGnssGSVData(uint32_t* noOfSatellites);
void xTaskGnss_Init();
void ConvertUtc2Rtc();
uint8_t verifyChecksum(char* pStr);

// Event descriptors
typedef enum {
    GnssEvt_Undefined = 0,

	GnssEvt_Rmc,
	GnssEvt_Gsv,
	GnssEvt_PowerOff,
	GnssEvt_HandleGnss,

} tGnssEventDescriptor;

typedef struct
{
	bool bSuccessful;
	uint32_t* data;
} tGnssData;

// Event structure for is25 task
typedef struct
{
    // Event Descriptor
	tGnssEventDescriptor Descriptor;

	tGnssData gnssEventData;

	bool bCompleted;

} tGnssEvent;
#endif

bool gnssCommand(uint8_t command, uint32_t maxWaitMs);
bool GNSS_WaitForCompletion(uint32_t maxTimeoutMs);
void SetRTCFromGnssData(tGnssCollectedData* pGnssData);

enum
{
    GNSS_POWER_ON,
	GNSS_READ_RMC,
	GNSS_READ_GSV,
	GNSS_POWER_OFF,
	GNSS_VERSION,
	GNSS_READ_RMC_HDOP,// returns when HDOP is below configurable limit
};

// Setter functions.
void gnssRotnSpeedChangeTestEn(bool bTestEn);
void gnssSetPreMeasurementSpeed_Knots(float fSpeedInKnots);
void gnssSetPostMeasurementSpeed_Knots(float fSpeedInKnots);

// Getter functions
bool gnssIsRotnSpeedChangeTestEn();
float gnssGetPreMeasurementSpeed_Knots();
float gnssGetPostMeasurementSpeed_Knots();

bool GNSS_BinarytoNMEA();
bool GNSS_NMEAtoBinary(void);
void GNSS_BinaryPacketSetup(uint8_t *pkt, uint16_t size, uint16_t cmd);
int GNSS_maxSecondsToAquireValidData();
uint32_t GNSS_GetUTCFromGPS(uint16_t wn, uint32_t tow);


#endif	/* SOURCES_APP_XTASKAPPGNSS_H_*/


#ifdef __cplusplus
}
#endif