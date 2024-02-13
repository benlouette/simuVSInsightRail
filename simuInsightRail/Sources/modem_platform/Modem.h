#ifdef __cplusplus
extern "C" {
#endif

/*
 * Modem.h
 *
 *  Created on: Jan 20, 2016
 *      Author: George de Fockert
 */

#ifndef SOURCES_MODEM_PLATFORM_MODEM_H_
#define SOURCES_MODEM_PLATFORM_MODEM_H_

enum AccessTechology {Unknown, eACT_2G, eACT_3G, eACT_4G}; // 2G, 3G or 4G

// some statistical etc. data over the last connection (attempt)
// time stamping the various communication actions in milliseconds
typedef struct Modem_metrics_str
{
	//Available after smoni message received
	enum AccessTechology accessTechnology;

	// Available after connecting to provider connection
	struct signalQuality_str
	{
		uint8_t csq;
		int16_t smoni;
	} signalQuality;

	// Available after closing connection
	struct tcpStatistics_str
	{
		uint32_t bytesRx;
		uint32_t bytesTx;
		uint32_t bytesAck;
		uint32_t bytesNack;
	} tcpStatistics;

	struct timeStamps_str
	{
		uint32_t start;// set at the start of the init call
		uint32_t poweron;// power applied, modem power 1.8V etc. detected
		uint32_t sysstart; // modem message systart received
		uint32_t cellularConfigDone;// preferred io/pins etc. configured
		uint32_t providerConfigDone;// simpin/apn stuff, includes pbready
		uint32_t serviceConfigDone;// url, portnr etc.
		uint32_t readSignalStrengthDone;// AT^SMONI/ AT+CSQ
		uint32_t serviceConnectDone;// transparent tcp connection
		uint32_t serviceDisconnectDone;// closed tcp
		uint32_t deregisterDone;// deregister at cellular network
		uint32_t UHS5E_shutdown;// closed provider connection, wait for modem shutdown message
		uint32_t poweroff;// power from modem removed
		bool 	 validTimestamps;	// When set to True indicates the timestamps are valid.
	} timeStamps;

} Modem_metrics_t;

/*
Radio access technology (RAT)
0 GSM
1 GSM / UMTS dual mode
	If this mode is selected additionally a preferred RAT can be configured, which
	is stored in NVRAM.
	In dual mode, GSM and UMTS Access Technology will be active and full Inter-
	RAT measurements and handovers are provided.
2 UMTS
3 LTE
4 UMTS / LTE dual mode
	If this mode is selected additionally a preferred RAT can be configured, which
	is stored in NVRAM.
	In dual mode, UMTS and LTE Access Technology will be active and full Inter-
	RAT measurements and handovers are provided.
5 GSM / LTE dual mode
	If this mode is selected additionally a preferred RAT can be configured, which
	is stored in NVRAM.
	In dual mode, GSM and LTE Access Technology will be active and full Inter-
	RAT measurements and handovers are provided.
6(D) GSM / UMTS / LTE triple mode
	If this mode is selected additionally two preferred RATs can be configured,
	which is stored in NVRAM.
	In triple mode, GSM, UMTS and LTE Access Technology will be active and full
	InterRAT measurements and handovers are provided.
*/
enum Modem_RAT_Settings {eRAT_GSM, eRAT_GSM_UMTS, eRAT_UMTS, eRAT_LTE, eRAT_UMTS_LTE, eRAT_GSM_LTE, eRAT_GSM_UMTS_LTE};


Modem_metrics_t * Modem_getMetrics() ; // return pointer to the connection metrics struct

// all in one bring up the modem and powerdown functions
int Modem_init(uint8_t rat, char * simPin, char * apn, uint8_t providerProfile, uint8_t serviceProfile, uint8_t * url, uint16_t portNr,
                    uint8_t * imei, uint8_t * iccid, uint32_t *csq, int16_t *sigQual, uint8_t minSigQualVal);
bool Modem_terminate(uint8_t serviceProfile);
bool Modem_powerup(uint32_t maxWaitMs);
bool Modem_readCellularIds(uint8_t rat, char * simPin,  uint8_t * imei, uint8_t * iccid, uint32_t maxWaitMs);
bool Modem_stopTransparent(void);
bool Modem_startTransparent(uint8_t serviceProfile, uint32_t maxWaitMs);
bool Modem_isTransparent(void);
bool  Modem_tryBaudrates(uint32_t desired_baudrate);
uint32_t Modem_getMaxTimeToConnectMs(bool logit);

#endif /* SOURCES_MODEM_PLATFORM_MODEM_H_ */


#ifdef __cplusplus
}
#endif