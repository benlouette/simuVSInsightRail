#ifdef __cplusplus
extern "C" {
#endif

#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <timers.h>
#include <portmacro.h>


/*-------------------------------------------------------------------------------------*
 |                                                                                     |
 *-------------------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "NvmConfig.h"
#include "NvmData.h"

#include "Insight.h"

// These are product-specific inclusions
// Better would be installable callbacks
// called at connection state changes
#include "xTaskApp.h"
#include "configData.h"
#include "device.h"

#include "xTaskModem.h"
#include "Modem.h"
#include "configModem.h"

#define TIMESTAMP(s)	\
	Modem_data.connectionMetrics.timeStamps.s = (xTaskGetTickCount() * portTICK_PERIOD_MS)

#define TS_OFFSET(s)	\
	TS_Offset(Modem_data.connectionMetrics.timeStamps.s)

// forward declarations
static bool shutdownModem(uint32_t maxWaitMs);

// event log codes
#define MDM_TIMEOUT		8000
#define MDM_SISO_0		8001
#define MDM_SISO_1		8002
#define MDM_POWER_OFF	8003
#define MDM_CSQ			8004
#define MDM_SIGNAL		8005
#define MDM_CONNECTION	8006

// this is the too lazy to type everything twice for a text string method I learned from 'John'  (so I do not get the blame that I created something cryptic)
#define FOREACH_ERROR(MODEM_ERROR) \
        MODEM_ERROR(MODEM_NO_ERROR)   \
        MODEM_ERROR(MODEM_NOT_POWERED)  \
        MODEM_ERROR(MODEM_SPOW)  \
        MODEM_ERROR(MODEM_Q3)  \
        MODEM_ERROR(MODEM_CELLULAR_CONFIG)  \
        MODEM_ERROR(MODEM_NOT_INITIALISED)  \
		MODEM_ERROR(MODEM_PROVIDER_CONFIG) \
		MODEM_ERROR(MODEM_SERVING_CELL) \
		MODEM_ERROR(MODEM_INTERNET_SVC_CONFIG) \
		MODEM_ERROR(MODEM_RETRIEVE_CSQ) \
		MODEM_ERROR(MODEM_SIGNAL_QUALITY) \
		MODEM_ERROR(MODEM_INTERNET_SVC_CONNECT) \
        MODEM_ERROR(MODEM_START_TRANSPARENT)

#define GENERATE_ENUM(err) err,
#define GENERATE_STRING(errs) #errs,


typedef enum {
    FOREACH_ERROR(GENERATE_ENUM)
} tModemErrCode;

static const char *modemErrorStrings[] = {
    FOREACH_ERROR(GENERATE_STRING)
};


/**
* @brief Get token from string
* *str points to string, string will be modified !
* delimiter will be replaced by '\0'
* returns start of item, terminated by the '\0'
* on exit : *str points behind 'delimiter'
*
* after the last item, *str points to the terminating '\0'
*/
static char *get_token(char **str)
{
    char *d = *str;	// at start of item
    char *c = strchr(*str, ',');
    if(c == NULL)
    {
        *str = *str + strlen(*str); // points at the final '\0'
    }
    else
    {
    	*c = 0; // replace delimiter with the '\0'
    	*str = c + 1; // points just behind delimiter
    }
    return d;
}

static int get_token_atoi(char **strt)
{
	return atoi(get_token(strt));
}

static float get_token_atof(char **strt)
{
	return atof(get_token(strt));
}

static long int get_token_strtol(char **strt)
{
	return strtol(get_token(strt),NULL,16);
}

/*
 * Modem startup/bringdown stuff
 */
enum eModem_state {
	powerdown = 0,
	poweredup ,
	initialised, // DTR line, sim pin code stuff is configured
	providerConfigDone,// cellular network config is set
	serviceConfigDone,// internet service config is set
	serviceConnected,
	transparent
} ;

// note, the enum number is also the array index of Modem_urc[] !!!
enum eModem_urcs {
	sysstart = 0,
	pbready,
	shutdown,
	sis, // Uses for both ^SIS or ^SISW
	urc_max
};

// urc handling stuff
static struct Modem_urc_struct
{
	bool received;
	const char * urcStr;
	const char * dbgStr;
} Modem_urc[] =
{
		/* sysstart */ 	{false,	"^SYSSTART\r", "sysstart"},
		/* pbready  */ 	{false, "+PBREADY\r", "pbready"},
		/* SHUTDOWN* */	{false, "^SHUTDOWN\r", "shutdown"},
		/* SIS */		{false,	"^SIS", "sis"}
};
static struct Modem_data_struct
{
	enum eModem_state state;
	SemaphoreHandle_t semUrc;
	enum eModem_urcs  waitingForUrc;
	uint32_t lastKnownUrcLen;
	uint8_t  lastKnownUrc[80];
	char allocated_ip[32];
	int mcc;					// mobile country code
	int mnc;					// mobile network code
	Modem_metrics_t connectionMetrics;
} Modem_data;

// return a pointer to thr metrics data structure
Modem_metrics_t * Modem_getMetrics()
{
	return &Modem_data.connectionMetrics;
}



bool bWakeupIsMAGSwipe = false;

static const char *StateToString(void)
{
	if(Modem_data.state == powerdown) return "powerdown";
	if(Modem_data.state == poweredup) return "poweredup";
	if(Modem_data.state == initialised) return "initialised";
	if(Modem_data.state == providerConfigDone) return "providerConfigDone";
	if(Modem_data.state == serviceConfigDone) return "serviceConfigDone";
	if(Modem_data.state == serviceConnected) return "serviceConnected";
	if(Modem_data.state == transparent) return "transparent";
	return "unknown";
}

// TODO : what to do with a tick counter overflow (32 bit, counter in ms, rolls over after 49 days uptime ?)
// it takes the remainint time, unless timeLimit !=0, it returns never more than the timeLimit
// use this to extra timelimit for commands which are known to not mtake a lot of time.
static uint32_t calcRemainingTimeMs(uint32_t abortTimeMs, uint32_t timeLimitMs)
{
	uint32_t curTimeMs = xTaskGetTickCount() * portTICK_PERIOD_MS;
	// This should be tick counter overflow safe ?
	uint32_t remainMs = ((abortTimeMs > curTimeMs) ? (abortTimeMs - curTimeMs) : 2000);

	remainMs = remainMs < 0x80000000  ? remainMs : 0 ;

	if (timeLimitMs && (remainMs > timeLimitMs))
	{
	    remainMs = timeLimitMs;
	}

	return remainMs  ;
}


static void clearUrc(enum eModem_urcs urc)
{
	if (urc <= urc_max)
	{
		for (int i = 0; i < (sizeof(Modem_urc)/sizeof(*Modem_urc)); i++)
		{
			Modem_urc[i].received = false;
		}
	}
	else
	{
		Modem_urc[urc].received = false;
	}

}

static bool waitForUrc(enum eModem_urcs urc, uint32_t maxWaitMs)
{
	bool rc_ok = false;
	if (urc < urc_max)
	{
		if (Modem_urc[urc].received == false)
		{
			xSemaphoreTake(	Modem_data.semUrc , 0); // clears semaphore
			Modem_data.waitingForUrc = urc;
			xSemaphoreTake(	Modem_data.semUrc , maxWaitMs/portTICK_PERIOD_MS);
		}
		rc_ok = Modem_urc[urc].received ;
	}
	return rc_ok;
}

/**
 * called from the modem task
 * to handle incoming URC in AT command mode, when no command is busy
 * running in the modem task context !
 */
static uint8_t handleURC(uint8_t *buf, uint32_t len, tModemResultFunc * params)
{
	for (int i = 0; i < sizeof(Modem_urc)/sizeof(*Modem_urc); i++)
	{
		if(0 == strncmp((char *) buf, (char *)Modem_urc[i].urcStr, strlen(Modem_urc[i].urcStr)))
		{
			// found a known urc
			Modem_urc[i].received = true;
			if(Modem_data.waitingForUrc == i)
			{
				Modem_data.lastKnownUrcLen = len > sizeof(Modem_data.lastKnownUrc) ? sizeof(Modem_data.lastKnownUrc) : len;
				memcpy(Modem_data.lastKnownUrc, buf, Modem_data.lastKnownUrcLen);
				xSemaphoreGive(Modem_data.semUrc);
			}
			LOG_DBG(LOG_LEVEL_MODEM,"handleURC: found %d:%s\n", i, Modem_urc[i].dbgStr);
			break;
		}
	}

    return 0;
}

/**
 * called from the modem task
 * to handle incoming data in AT command mode
 * running in the modem task context !
 */
static uint8_t handleAtResponse(uint8_t *buf, uint32_t len, tModemResultFunc * params)
{
	if (params)
	{
		tResultProcessorFuncPtr ttt = params->resultProcessor;
		ttt(buf,len, params->params);
	}

    return 0;
}

// debug function to retrieve some information to get a clue what the moden thinks it has to do.
void getModemSettings()
{
    if (dbg_logging  & LOG_LEVEL_MODEM )
    {
    // we are going to retrieve some extra info from the modem when the csq is 99
        printf("Retrieving some modem settings:\n");
        modemSendAt(NULL, 2000, NULL, "AT+CFUN?");
        modemSendAt(NULL, 2000, NULL, "AT+CREG?");
        modemSendAt(NULL, 2000, NULL, "AT+CGREG?");
        modemSendAt(NULL, 2000, NULL, "AT^SICS?");
        modemSendAt(NULL, 2000, NULL, "AT^SXRAT?");
        modemSendAt(NULL, 2000, NULL, "AT^SISS?");
        modemSendAt(NULL, 2000, NULL, "AT&V");
    }
}


/**
 * recovery mode as suggested by Simon.Mullenger@gemalto.com (e-mail 2016-09-06) :
 *
 You wrote: "sometimes, probably only in bad coverage area's, communication is lost, and never comes back from a node.
Bringing that node to a good coverage area, does not help. "
> Please periodically (say every 30 seconds) check coverage with AT Commands such as AT+CREG? / AT+CGREG? / AT^SMONI
> When you decide that you have lost coverage please use the following:
> AT+CFUN=4 / wait for "OK" then wait 5 seconds / AT+CFUN=1 and OK / AT^SXRAT=1,2 and OK / AT+COPS=0 and OK
> Then rebuild Internet Services
 *
 */
void toggleAirplaneMode()
{
    clearUrc(sysstart);
    modemSendAt(NULL, 2000, NULL, "AT+CFUN=4,0");
    waitForUrc(sysstart,5000 ); // 5 seconds OK ?
    vTaskDelay( 5000 /  portTICK_PERIOD_MS );
    clearUrc(sysstart);
    modemSendAt(NULL, 2000, NULL, "AT+CFUN=1,0");
    waitForUrc(sysstart,5000 ); // 5 seconds OK ?
    vTaskDelay( 1000 /  portTICK_PERIOD_MS );
    modemSendAt(NULL, 10000, NULL, "AT+COPS=0");
}

// 1200,2400,4800,9600,19200,38400,57600,115200,230400,460800,500000,750000,921600

static const uint32_t Modem_baudrates[] =
{
 115200*8 /* 921600 */,
 115200,
 115200*2 /* 230400 */,
 115200*4 /* 460800 */,
 500000,
 750000,
};

/**
 * find out modem baudrate after startup and not seeing the sysstart URC:
 * method, try for a list of plausible baudrates : sent AT command 'AT', and check if we get an OK back.
 * returns the baudrate with the OK response.
 */

bool Modem_tryBaudrates(uint32_t desired_baudrate)
{
    bool rc_ok = true;
    bool found = false;
    tModemAtRc AtRc  = ModemRcOk; // the AT command result
    uint32_t baudrate;

    for (int idx = 0; !found && idx < (sizeof(Modem_baudrates)/sizeof(*Modem_baudrates)); idx++)
    {
        baudrate = Modem_baudrates[idx];
        LOG_DBG(LOG_LEVEL_MODEM,"%s: try %d\n", __func__, baudrate);
        rc_ok = ModemInit(baudrate, 200);
        if (rc_ok)
        {
            rc_ok = (ModemRcOk == modemSendAt(NULL, 1000, &AtRc, "AT"));
        }
        if (rc_ok && (AtRc==AtOk))
        {
            LOG_DBG(LOG_LEVEL_MODEM,"%s: found  baudrate %d\n", __func__, baudrate);
            found = true;
        }
    }

    if (found && baudrate != desired_baudrate)
    {
        found = false;// some sanity check on the desired baudrate
        for (int idx = 0; !found && idx < (sizeof(Modem_baudrates)/sizeof(*Modem_baudrates)); idx++)
        {
           found = (desired_baudrate == Modem_baudrates[idx]);
        }
        if (!found)
        {
            LOG_DBG(LOG_LEVEL_MODEM,"%s: overruling desired baudrate %d, taking default 115200\n", __func__, desired_baudrate);
            desired_baudrate = 115200;
        }
        LOG_DBG(LOG_LEVEL_MODEM,"%s: set modem communication baudrate at %d\n", __func__, desired_baudrate);

        // AT+IPR=<rate>
        rc_ok = (ModemRcOk == modemSendAt(NULL, 1000, &AtRc, "AT+IPR=%d", (int)desired_baudrate));
        if (rc_ok && AtRc==AtOk)
        {
            // funny, would not expect that we got back a readable 'OK' at the new baudrate, lets send it another time
            LOG_DBG(LOG_LEVEL_MODEM,"Baudrate probably did not change, try again\n");
            rc_ok = (ModemRcOk == modemSendAt(NULL, 1000, &AtRc, "AT+IPR=%d", (int)desired_baudrate));
        }
        LOG_DBG(LOG_LEVEL_MODEM,"%s: set UART communication baudrate at %d\n", __func__, desired_baudrate);
        rc_ok = ModemInit(desired_baudrate, 200);
        if (rc_ok)
        {
            // check if communication is again OK, after baudrate changes on both sides.
            rc_ok = (ModemRcOk == modemSendAt(NULL, 1000, &AtRc, "AT"));// first may fail
            rc_ok = (ModemRcOk == modemSendAt(NULL, 1000, &AtRc, "AT"));// second may fail
            rc_ok = (ModemRcOk == modemSendAt(NULL, 1000, &AtRc, "AT"));// third will set the OK
            if (rc_ok && (AtRc==AtOk))
            {
                LOG_DBG(LOG_LEVEL_MODEM,"%s: communication now working at %d\n", __func__, desired_baudrate);
            }
            else
            {
                LOG_DBG(LOG_LEVEL_MODEM,"%s: communication NOT!! working at %d\n", __func__, desired_baudrate);
                rc_ok = false;
            }
        }
    }

    return rc_ok;
}

/**
 * Modem_powerup
 *
 * @desc    powerup the Modem and wait for the sysstart urc, which tells us it is ready to accept AT commands
 * 			Function assumes that the modem has no power, otherwise it will timeout on waiting for the 'sysstart' message
 * @param	maxWaitMs
 *
 * @returns true when 'sysstart' received
 */

bool Modem_powerup(uint32_t maxWaitMs)
{
	bool rc_ok = true;
	uint32_t baudrate = gNvmCfg.dev.modem.baudrate;
	uint32_t abortTimeMs = xTaskGetTickCount() * portTICK_PERIOD_MS + maxWaitMs;

	clearUrc(sysstart);
	if (rc_ok)
	{
		// powerup the modem
		rc_ok = ModemInit(baudrate, calcRemainingTimeMs(abortTimeMs,2000));
		LOG_DBG(LOG_LEVEL_MODEM,"Modem_powerup: %d\n",rc_ok);
		if ((rc_ok) && (Modem_data.connectionMetrics.timeStamps.poweron == 0))
		{
			TIMESTAMP(poweron);
		}
	}
	if (rc_ok)
	{
		rc_ok = waitForUrc(sysstart, calcRemainingTimeMs(abortTimeMs,15000) ); // 10 seconds OK ?
		LOG_DBG(LOG_LEVEL_MODEM,"wait sysstart: %d\n",rc_ok);
		if (rc_ok)
		{
		    TIMESTAMP(sysstart);
		}
		else
		{
		    // no systart ! maybe we are at the wrong baudrate ?
		    rc_ok = Modem_tryBaudrates(baudrate);
		}
	}
	if (rc_ok)
	{
		Modem_data.state = poweredup;

		//Delay to give time after SYSTART message for modem to be ready to receive first AT command.
		vTaskDelay(50/portTICK_PERIOD_MS);
	}
	return rc_ok;

}


static void result_imei(uint8_t * str, uint32_t len, void  * imei)
{
	// imei is 15 char long
	memcpy((uint8_t *) imei, str, MODEM_IMEI_LEN - 1);
	((uint8_t* )imei)[MODEM_IMEI_LEN - 1] = 0;
}

static void result_iccid(uint8_t * str, uint32_t len, void  * iccid)
{
	// iccid is 20 char long
	memcpy((uint8_t *) iccid, &str[7], MODEM_ICCID_LEN - 1);
	((uint8_t* )iccid)[MODEM_ICCID_LEN - 1] = 0;
}

static void result_csq(uint8_t * str, uint32_t len, void  * csq)
{
	// csq : +CSQ: 31,99
	* (uint32_t* ) csq= strtoul((char *) &str[6],NULL,10);
}

#define MAXSMONILENGTH (80)

static void result_smoni(uint8_t * str, uint32_t len, void  * smoni)
{
	// ^SMONI: 3G,10588,349,-2.5,-63,204,04,00DA,2CE7D39,13,52,NOCONN
	memset(smoni,'\0', MAXSMONILENGTH);
	memcpy((uint8_t *) smoni, &str[8], (len-8)<MAXSMONILENGTH-1 ? (len-8) : MAXSMONILENGTH-1 );
	uint8_t *tptr = (uint8_t *) strchr((char *) smoni,'\r');
	if (tptr)
	{
		*tptr = '\0';
	}
}

/**
 * observed 'smoni' result strings
 * ^SMONI: 2G,SEARCH,SEARCH
 * ^SMONI: 3G,SEARCH,SEARCH
 *
 * manual:
 * ME is not connected:
 * ME is camping on a GSM (2G) cell:
 * ^SMONI: ACT,ARFCN,BCCH,MCC,MNC,LAC,cell,C1,C2,NCC,BCC,GPRS,ARFCN,TS,timAdv,dBm,Q,ChMod
 * ^SMONI: 2G,71,-61,262,02,0143,83BA,33,33,3,6,G,NOCONN
 *
 * ME is camping on a UMTS (3G) cell:
 * ^SMONI: ACT,UARFCN,PSC,EC/n0,RSCP,MCC,MNC,LAC,cell,SQual,SRxLev,PhysCh, SF,Slot,EC/n0,RSCP,ComMod,HSUPA,HSDPA
 * ^SMONI: 3G,10564,296,-7.5,-79,262,02,0143,00228FF,-92,-78,NOCONN
 *
 * Syntax:
 * ^SMONI: ACT,ARFCN,BCCH,MCC,MNC,LAC,cell,C1,C2,NCC,BCC,GPRS,ARFCN,TS,timAdv,dBm,Q,ChMod
 * Example:
 * ^SMONI: 2G,673,-80,262,07,4EED,A500,35,35,7,4,G,643,4,0,-80,0,S_FR
 *
 * Syntax:
 * ^SMONI: ACT,UARFCN,PSC,EC/n0,RSCP,MCC,MNC,LAC,cell,SQual,SRxLev,PhysCh, SF,Slot,EC/n0,RSCP,ComMod,HSUPA,HSDPA
 * Example:
 * ^SMONI: 3G,10737,131,-5,-93,260,01,7D3D,C80BC9A,21,11,EDCH,256,4,-5,-93,0,01,06
 *
 * Syntax:
 * ^SMONI: ACT,EARFCN,Band,DL bandwidth,UL bandwidth,Mode,MCC,MNC,TAC,Global Cell ID,Physical Cell ID,TX_power,RSRP,RSRQ,Conn_state
 * Example:
 * ^SMONI: 4G,6300,20,10,10,FDD,262,02,BF75,0345103,350,90,-94,-7,CONN
 *
 */
// very limited info on what to expect for the values, some are decimal, some appear hexadecimal, some are strings of unknown length etc.
// what a mess
typedef struct smoni_str {
    enum eServiceState { SS_unknown, SS_CONN, SS_SEARCH, SS_NOCONN, SS_LIMSRV } serviceState;
    enum AccessTechology eAct;
    union {
        struct str_2G {
            int16_t ARFCN;//  decimal?
            int16_t BCCH; // decimal ?
            int16_t MCC; // decimal ?
            int16_t MNC;// decimal ?
            uint16_t LAC;// hexadecimal ?
            uint32_t cell;// hexadecimal ?
            int16_t C1;// decimal ?
            int16_t C2;// decimal ?
            int16_t NCC;// decimal ?
            int16_t BCC;// decimal ?
            char  GPRS[5];// ascii string ? max length ?
            int16_t ARFCNb;// decimal ?
            int16_t TS;// decimal ?
            int16_t timAdv;// decimal ?
            int16_t dBm;// decimal ?
            int16_t Q;// decimal ?
            char  ChMod[5]; // ascii string ? max length ?
        } Act_2G;
        struct str_3G {
            int16_t UARFCN;//  decimal?
            int16_t PSC;// decimal ?
            float EC_n0;// decimal ?
            int16_t RSCP;// decimal ?
            char MCC[4];				// Mobile Country Code (first part of the PLMN code - can have leading zero)
            char MNC[4];				// Mobile Network Code (second part of the PLMN code - MNC can be of two-digit form and three-digit form with leading zeros)
            uint16_t LAC;// hexadecimal ?
            uint32_t cell;// hexadecimal ?
            int16_t SQual;// decimal ?
            int16_t SRxLev;// decimal ?
            char  PhysCh[5];// ascii string ? max length ?
            int16_t SF;// decimal ?
            int16_t Slot;// decimal ?
            float EC_n0b;// decimal ?
            int16_t RSCPb;// decimal ?
            int16_t ComMod;// decimal ?
            uint16_t HSUPA;// hexadecimal ?
            uint16_t HSDPA;// hexadecimal ?
        } Act_3G;
        struct str_4G {
            int16_t EARFCN;				// E-UTRA Absolute Radio Frequency Channel Number
            int16_t Band;				// E-UTRA frequency band
            int16_t DL_Bandwidth;		// DL bandwidth
            int16_t UL_Bandwidth;		// UL bandwidth
            char Mode[4];				// FDD or TDD
            char MCC[4];				// Mobile Country Code (first part of the PLMN code - can have leading zero)
            char MNC[4];				// Mobile Network Code (second part of the PLMN code - MNC can be of two-digit form and three-digit form with leading zeros)
            uint16_t TAC;				// Tracking Area Code (see 3GPP 23.003 Section 19.4.2.3)
            char Global_Cell_ID[8];		// Global Cell ID
            uint32_t Physical_Cell_ID;	// Physical Cell ID
            int16_t SRxLev;				// RX level value for base station selection in dB
            int16_t RSRP;				// Reference Signal Received Power
            int16_t RSRQ;				// Reference Signal Received Quality
            float TX_Power;				// Used Uplink Power
        } Act_4G;
    } data;
} tSmoni;

/* el stupido lookup table derived from information given by gemalto expert
   , space consuming, but simple
     -> RSCp
  |
  V EC/n0
  */
static const uint8_t RSCP_EC_n0_table [16][10] = {
    /*            RSCP : -116, -112, -108, -104, -100, -96, -92, -88, -84, -80 */
        /* EC/n0 */
          /*  -4 */ {       0,    3,    4,    7,    8,   8,   9,   9,   9,  9 },
          /*  -5 */ {       0,    3,    4,    6,    7,   8,   8,   9,   9,  9 },
          /*  -6 */ {       0,    1,    3,    4,    6,   7,   8,   8,   9,  9 },
          /*  -7 */ {       0,    1,    3,    4,    5,   7,   8,   8,   9,  9 },
          /*  -8 */ {       0,    1,    3,    4,    5,   6,   7,   8,   8,  9 },
          /*  -9 */ {       0,    1,    2,    3,    4,   6,   7,   8,   8,  9 },
          /* -10 */ {       0,    1,    2,    3,    4,   5,   6,   7,   8,  8 },
          /* -11 */ {       0,    1,    2,    2,    3,   5,   6,   7,   8,  8 },
          /* -12 */ {       0,    1,    2,    2,    3,   4,   5,   6,   7,  8 },
          /* -13 */ {       0,    1,    2,    2,    2,   4,   5,   5,   7,  8 },
          /* -14 */ {       0,    1,    1,    2,    2,   3,   4,   4,   6,  7 },
          /* -15 */ {       0,    0,    1,    1,    2,   3,   4,   4,   5,  6 },
          /* -16 */ {       0,    0,    1,    1,    2,   2,   3,   3,   4,  5 },
          /* -17 */ {       0,    0,    0,    1,    2,   2,   2,   2,   4,  4 },
          /* -18 */ {       0,    0,    0,    1,    1,   1,   1,   1,   3,  3 },
          /* -19 */ {       0,    0,    0,    0,    0,   0,   0,   0,   0,  0 },
};

static uint8_t calc_3g_signal_strength( int16_t RSCP, int16_t EC_n0)
{
    if (RSCP < -116) RSCP = -116;
    if (RSCP > -80) RSCP = -80;
    RSCP = RSCP/4 +29;// now ranging from 0..9

    if (EC_n0 < -19) EC_n0 = -19;
    if (EC_n0 > -4) EC_n0 = -4;
    EC_n0 = -EC_n0 -4; // now ranging from 0..15

    return RSCP_EC_n0_table[EC_n0][RSCP];
}

/* Look up Table describing the quality of a 4g signal: 0 (Poor) -> 9 (Excellent) based on the metrics
 * Reference Signal Received Power (RSRP) and Reference Signal Received Quality (RSRQ)
     -> RSRP
  |
  V RSRQ
  */
static const uint8_t RSRP_RSRQ_table [6][8] = {
   /*       RSRP : -125, -120, -115, -110, -105, -100, -95, -90 */
   /* RSRQ */
   /*  -9  */ {       0,    3,    4,    7,    8,   8,   9,   9},
   /*  -10 */ {       0,    3,    4,    6,    7,   8,   8,   9},
   /*  -11 */ {       0,    2,    3,    5,    6,   7,   8,   8},
   /*  -12 */ {       0,    2,    3,    4,    5,   7,   8,   8},
   /*  -13 */ {       0,    1,    3,    4,    5,   6,   7,   7},
   /*  -14 */ {       0,    1,    2,    3,    4,   6,   6,   7},
};

static uint8_t calc_4g_signal_strength(int16_t RSRP, int16_t RSRQ)
{
	if (RSRP < -125) RSRP = -125;
	if (RSRP > -90) RSRP = -90;
	RSRP = (RSRP / 5) + 25; // Ranging from 0..7

	if (RSRQ < -14) RSRQ = -14;
	if (RSRQ > -9) RSRQ = -9;
	RSRQ = (-RSRQ - 9); // Ranging from 0..5

	return RSRP_RSRQ_table[RSRQ][RSRP];
}

static void printServiceState(int state)
{
	printf(" serviceState = %s\n",
        state == SS_SEARCH ? "SEARCH" :
		state == SS_CONN   ? "CONN"   :
        state == SS_NOCONN ? "NOCONN" :
        state == SS_LIMSRV ? "LIMSRV" : "unknown");
}

/**
 * @brief Decodes response of AT^SMONI 
 * 
 * Note:
 * Common SMONI responses for 4G:
 *     "4G,SEARCH"
 *     "4G,9460,28,10,10,FDD,240,07,B3B1,680112A,43,--,-65,-6.5,CONN"
 * A common SMONI response for 3G:
 *     "3G,10638,495,-5.0,-47,240,04,0425,45284FA,13,68,NOCONN"
 * Sometimes we got strange reply such as ",--,FFFFFFFF,275,37,-71,-8.0,CONN", 
 * which will return error (-1)
 * 
 * @param[in] smoni_string   Response of AT^SMONI from modem
 * @retval signal quality [0..9] or -1 if the smoni string is not understood, or
 *         still searching.
 */
static int16_t decode_smoni(char *smoni_string)
{
    int16_t signalQuality = -1;

    if (smoni_string)
    {
        tSmoni smoni;

        char * strt = (char *) &smoni_string[0];
        char * val;

        memset(&smoni,0,sizeof(smoni));// lets start with all zero's, which places also the terminating '\0' for the strings
        smoni.serviceState = SS_unknown;

        val = get_token(&strt);
        if (val)
        {
            if(0 == strncmp(smoni_string, "2G",2))
            {
                smoni.eAct = eACT_2G;
            }
            else if(0 == strncmp(smoni_string, "3G",2))
            {
                smoni.eAct = eACT_3G;
            }
            else if(0 == strncmp(smoni_string, "4G",2))
            {
                smoni.eAct = eACT_4G;
            }
            else
            {
                LOG_DBG(LOG_LEVEL_MODEM,"SMONI not 2G, 3G or 4G!\n");
                return signalQuality;  // we don't understand this
            }
            Modem_data.connectionMetrics.accessTechnology = smoni.eAct;
        }

		// skip by connection type
        val = get_token(&strt);

        // work out service state
		char * idx;
		if ((idx=strstr(strt,",NOCONN")) != 0)
		{
			smoni.serviceState = SS_NOCONN;
			*idx='\0';
		}
		else if ((idx=strstr(strt,",CONN")) != 0)
		{
			smoni.serviceState = SS_CONN;
			*idx='\0';
		}
		else if ((idx=strstr(strt,",LIMSRV")) != 0)
		{
			smoni.serviceState = SS_LIMSRV;
			*idx='\0';
		}
		else
		{
			char *err;
			if ((idx=strstr(strt,"SEARCH")) != 0)
			{
				smoni.serviceState = SS_SEARCH ;
				err = "still searching";
			}
			else
			{
				smoni.serviceState = SS_unknown;
				err = "service state unknown";
			}
			LOG_DBG(LOG_LEVEL_MODEM, "SMONI %s !\n", err);
			return signalQuality;// and exit we go
		}

        // we got this far, now the 2G, 3G and 4G paths differ
        if (smoni.eAct == eACT_2G)
        {
            //val = get_token(&strt); // we did already did a read in the check for 'search'
            if (val)
            {
                smoni.data.Act_2G.ARFCN = atoi(val);
            }
			smoni.data.Act_2G.BCCH = get_token_atoi(&strt);
			smoni.data.Act_2G.MCC = get_token_atoi(&strt);
			smoni.data.Act_2G.MNC = get_token_atoi(&strt);
			smoni.data.Act_2G.LAC = get_token_strtol(&strt);
			smoni.data.Act_2G.cell = get_token_strtol(&strt);
			smoni.data.Act_2G.C1 = get_token_atoi(&strt);
			smoni.data.Act_2G.C2 = get_token_atoi(&strt);
			smoni.data.Act_2G.NCC = get_token_atoi(&strt);
			smoni.data.Act_2G.BCC = get_token_atoi(&strt);
            val = get_token(&strt);
            if (val)
            {
                strncpy(smoni.data.Act_2G.GPRS, val, sizeof(smoni.data.Act_2G.GPRS)-1);
            }
            smoni.data.Act_2G.ARFCNb = get_token_atoi(&strt);
			smoni.data.Act_2G.TS = get_token_atoi(&strt);
			smoni.data.Act_2G.timAdv = get_token_atoi(&strt);
			smoni.data.Act_2G.dBm = get_token_atoi(&strt);
			smoni.data.Act_2G.Q = get_token_atoi(&strt);
            val = get_token(&strt);
            if (val)
            {
                strncpy(smoni.data.Act_2G.ChMod, val, sizeof(smoni.data.Act_2G.ChMod)-1);
            }

            // use BCCH as indication for signal quality between 0..9
            //
            // int16_t bcch_to_signal_table[] = {-105, -100, -95, -90, -85, -80, -75, -70, -65, -60 };
            // bcch/5 +21 -> -21..-12 + 21 = 0..9
            signalQuality = (smoni.data.Act_2G.BCCH /5) +21;
            if (signalQuality > 9) signalQuality = 9;
            if (signalQuality < 0) signalQuality = 0;

            // recreate string out of structure
			if (dbg_logging & LOG_LEVEL_MODEM)
			{
            	printf("decoded SMONI 2G:\n"
            			" ARFCN=%d\n"
            			" BCCH=%d\n"
            			" MCC=%d\n"
            			" MNC=%d\n"
            			" LAC=%x\n",
                    smoni.data.Act_2G.ARFCN,
                    smoni.data.Act_2G.BCCH,
                    smoni.data.Act_2G.MCC,
                    smoni.data.Act_2G.MNC,
                    smoni.data.Act_2G.LAC
                     );
            	printf(" cell=%x\n C1=%d\n C2=%d\n NCC=%d\n BCC=%d\n GPRS=%s\n",
                    smoni.data.Act_2G.cell,
                    smoni.data.Act_2G.C1,
                    smoni.data.Act_2G.C2,
                    smoni.data.Act_2G.NCC,
                    smoni.data.Act_2G.BCC,
                    smoni.data.Act_2G.GPRS
                    );
            	printf(" ARFCNb=%d\n TS=%d\n timAdv=%d\n dBm=%d\n Q=%d\n ChMod=%s\n",
                    smoni.data.Act_2G.ARFCNb,
                    smoni.data.Act_2G.TS,
                    smoni.data.Act_2G.timAdv,
                    smoni.data.Act_2G.dBm,
                    smoni.data.Act_2G.Q,
                    smoni.data.Act_2G.ChMod
                    );
            	printServiceState(smoni.serviceState);
            	printf(" 2G signalQuality = %d\n", signalQuality);
			}
        }// 2G
        else if (smoni.eAct == eACT_3G)
        {
            //val = get_token(&strt); // we did already did a read in the check for 'search'
            if (val)
            {
                smoni.data.Act_3G.UARFCN = atoi(val);
            }
			smoni.data.Act_3G.PSC = get_token_atoi(&strt);
			smoni.data.Act_3G.EC_n0 = get_token_atof(&strt);
			smoni.data.Act_3G.RSCP = get_token_atoi(&strt);
			val = get_token(&strt);
			if (val)
			{
			     strncpy(smoni.data.Act_3G.MCC, val, sizeof(smoni.data.Act_3G.MCC)-1);
			}
			val = get_token(&strt);
			if (val)
			{
			     strncpy(smoni.data.Act_3G.MNC, val, sizeof(smoni.data.Act_3G.MNC)-1);
			}
			smoni.data.Act_3G.LAC = get_token_strtol(&strt);
			smoni.data.Act_3G.cell = get_token_strtol(&strt);
			smoni.data.Act_3G.SQual = get_token_atoi(&strt);
			smoni.data.Act_3G.SRxLev = get_token_atoi(&strt);
            val = get_token(&strt);
            if (val)
            {
                strncpy(smoni.data.Act_3G.PhysCh, val, sizeof(smoni.data.Act_3G.PhysCh)-1);
            }
			smoni.data.Act_3G.SF = get_token_atoi(&strt);
			smoni.data.Act_3G.Slot = get_token_atoi(&strt);
			smoni.data.Act_3G.EC_n0b = get_token_atof(&strt);
			smoni.data.Act_3G.RSCPb = get_token_atoi(&strt);
			smoni.data.Act_3G.ComMod = get_token_atoi(&strt);
			smoni.data.Act_3G.HSUPA = get_token_strtol(&strt);
			smoni.data.Act_3G.HSDPA = get_token_strtol(&strt);

            signalQuality = calc_3g_signal_strength( smoni.data.Act_3G.RSCP, smoni.data.Act_3G.EC_n0);
			if (dbg_logging & LOG_LEVEL_MODEM)
			{
				printf("decoded SMONI 3G:\n"
						" UARFCN=%d\n"
						" PSC=%d\n"
						" EC_n0=%f\n"
						" RSCP=%d\n"
						" MCC=%s\n",
                    smoni.data.Act_3G.UARFCN,
                    smoni.data.Act_3G.PSC,
                    smoni.data.Act_3G.EC_n0,
                    smoni.data.Act_3G.RSCP,
                    smoni.data.Act_3G.MCC
                     );
            	printf(" MNC=%s\n LAC=%x\n cell=%x\n SQual=%d\n SRxLev=%d\n PhysCh=%s\n",
                    smoni.data.Act_3G.MNC,
                    smoni.data.Act_3G.LAC,
                    smoni.data.Act_3G.cell,
                    smoni.data.Act_3G.SQual,
                    smoni.data.Act_3G.SRxLev,
                    smoni.data.Act_3G.PhysCh
                    );
            	printf(" SF=%d\n Slot=%d\n EC_n0b=%f\n RSCPb=%d\n ComMod=%d\n HSUPA=%x\n",
                    smoni.data.Act_3G.SF,
                    smoni.data.Act_3G.Slot,
                    smoni.data.Act_3G.EC_n0b,
                    smoni.data.Act_3G.RSCPb,
                    smoni.data.Act_3G.ComMod,
                    smoni.data.Act_3G.HSUPA
                    );
            	printf(" HSDPA=%x\n", smoni.data.Act_3G.HSDPA);
            	printServiceState(smoni.serviceState);
            	printf(" 3G signalQuality = %d\n", signalQuality);
			}
        }// 3G
        else if (smoni.eAct == eACT_4G)
        {
            //val = get_token(&strt); // we did already did a read in the check for 'search'
            if (val)
            {
                smoni.data.Act_4G.EARFCN = atoi(val);
            }
            smoni.data.Act_4G.Band = get_token_atoi(&strt);
            smoni.data.Act_4G.DL_Bandwidth = get_token_atoi(&strt);
            smoni.data.Act_4G.UL_Bandwidth = get_token_atoi(&strt);
            val = get_token(&strt);
            if (val)
            {
                strncpy(smoni.data.Act_4G.Mode, val, sizeof(smoni.data.Act_4G.Mode)-1);
            }
			val = get_token(&strt);
			if (val)
			{
				strncpy(smoni.data.Act_4G.MCC, val, sizeof(smoni.data.Act_4G.MCC)-1);
			}
			val = get_token(&strt);
			if (val)
			{
				strncpy(smoni.data.Act_4G.MNC, val, sizeof(smoni.data.Act_4G.MNC)-1);
			}
			smoni.data.Act_4G.TAC = get_token_strtol(&strt);
			val = get_token(&strt);
			if (val)
			{
				strncpy(smoni.data.Act_4G.Global_Cell_ID, val, sizeof(smoni.data.Act_4G.Global_Cell_ID)-1);
			}
			smoni.data.Act_4G.Physical_Cell_ID = get_token_atoi(&strt);
			smoni.data.Act_4G.SRxLev = get_token_atoi(&strt);
			smoni.data.Act_4G.RSRP = get_token_atoi(&strt);
			smoni.data.Act_4G.RSRQ = get_token_atoi(&strt);
			smoni.data.Act_4G.TX_Power = get_token_atof(&strt);

        	signalQuality = calc_4g_signal_strength(smoni.data.Act_4G.RSRP, smoni.data.Act_4G.RSRQ);
			if (dbg_logging & LOG_LEVEL_MODEM)
			{
				printf("decoded SMONI 4G:\n"
            	        " EARFCN=%d\n"
            			" BAND=%d\n"
            			" DL Bandwidth=%d\n"
            			" UL Bandwidth=%d\n"
            			" MODE=%s\n"
            			" MCC=%s\n"
						" MNC=%s\n",
                    smoni.data.Act_4G.EARFCN,
                    smoni.data.Act_4G.Band,
                    smoni.data.Act_4G.DL_Bandwidth,
                    smoni.data.Act_4G.UL_Bandwidth,
                    smoni.data.Act_4G.Mode,
					smoni.data.Act_4G.MCC,
					smoni.data.Act_4G.MNC
                     );
            	printf( " TAC=%04X\n"
            			" Global Cell ID=%s\n"
            			" Physical Cell ID=%d\n"
            			" SRxLev=%d\n"
            			" RSRP=%d\n"
            			" RSRQ=%d\n"
						" TX Power=%f\n",
					smoni.data.Act_4G.TAC,
                    smoni.data.Act_4G.Global_Cell_ID,
                    smoni.data.Act_4G.Physical_Cell_ID,
                    smoni.data.Act_4G.SRxLev,
                    smoni.data.Act_4G.RSRP,
                    smoni.data.Act_4G.RSRQ,
					smoni.data.Act_4G.TX_Power
					);
            	printServiceState(smoni.serviceState);
            	printf(" 4G signalQuality = %d\n", signalQuality);
			}
        }// 4G

    }
    return signalQuality;
}


// ^SISI: 1,4,0,1370,1370,0
// [^SISI: <srvProfileId>, <srvState>, <rxCount>, <txCount>, <ackData>, <unackData>]
typedef struct resp_sisi_str
{
	uint8_t srvProfileId;
	uint8_t srvState;
	uint32_t rxCount;
	uint32_t txCount;
	uint32_t ackData;
	uint32_t unackData;
} tSisiResp;

static void result_sisi(uint8_t * str, uint32_t len, void  * sisi)
{
	tSisiResp * sisip = (tSisiResp * ) sisi;

	if (len >8)
	{
		char * strt = (char *) &str[7];
		sisip->srvProfileId =  get_token_atoi(&strt);
		sisip->srvState =  get_token_atoi(&strt);
		sisip->rxCount =  get_token_atoi(&strt);
		sisip->txCount =  get_token_atoi(&strt);
		sisip->ackData =  get_token_atoi(&strt);
		sisip->unackData =  get_token_atoi(&strt);
	}
	else
	{
		memset(sisi,0,sizeof(*sisip)); // all zero, invalid sisi response
	}
}

bool Modem_stopTransparent(void)
{
	bool rc_ok = true;
	tModemAtRc AtRc  = ModemRcOk; // the AT command result

	if (Modem_data.state != powerdown)
	{
		// send the special 'get out of transparent mode' stuff
		rc_ok = (ModemRcOk == modemSendAt(NULL, 1000, &AtRc, (char*)NULL));
		if ((rc_ok) && (AtRc == AtConnect) )
		{
			LOG_DBG(LOG_LEVEL_MODEM,"MODEM State Changed from %s to serviceConnected\n", StateToString());
			Modem_data.state = serviceConnected; // although in AT mode, service is not disconnected by the modem
		}

		// wait for any SIS unsolicited URCs
		clearUrc(sis);
		while(waitForUrc(sis, 500))
		{
			clearUrc(sis);
		}
	}

	LOG_DBG(LOG_LEVEL_MODEM,"MODEM Switch from %s TO AT mode %s, rc_ok: %d, AtRc: %d\n",
			StateToString(),
			Modem_data.state == serviceConnected? "OK" : "FAILED", rc_ok, AtRc);

	return rc_ok;
}

bool Modem_startTransparent(uint8_t serviceProfile, uint32_t maxWaitMs)
{
	bool rc_ok = false;

	if (serviceConnected == Modem_data.state)
	{
		tModemAtRc AtRc  = ModemRcOk; // the AT command result

		// Enter transparent mode
		if((ModemRcOk == modemSendAt(NULL, maxWaitMs , &AtRc, "AT^SIST=%d", serviceProfile)) && (AtRc == AtConnect))
		{
			Modem_data.state = transparent;
			rc_ok = true;
		}
	}

	LOG_DBG(LOG_LEVEL_MODEM,"MODEM Switch from AT mode to transparent %s\n", (rc_ok ? "OK" : "FAILED"));

	return rc_ok;
}

// return if transparent mode is on/off
bool Modem_isTransparent(void)
{
	return (Modem_data.state == transparent);
}

/**
 * cellularConfig
 *
 * @desc    configure DTR line, message format and optional set simpin and retrieve phonenumber and imei number
 * @param   RadioAccessTechnology : input parameter for the AT^SXRAT command in integer format !
 * @param   simpin: input pointer, sim PIN code (4 character ascii string) (NULL when no sim required)
 * @param   phonenum: output pointer, phone number is retreived from modem and returned (if not NULL), phonenumber readback is not with all sims/providers possible !
 * @param   imei: output pointer, if not NULL, returns the modem imei number as a null terminated string, caller must supply buffer big enough to store the IMEI
 * @param   iccid output pointer, if not NULL, returns the simcard iccid number as a null terminated string, caller must supply buffer big enough to store the ICCID
 * @param
 *
 * @returns true when no errors encountered
 */

static bool cellularConfig(uint8_t RadioAccessTechnology, char * simpin, char * phonenum, char * imei, char * iccid, uint32_t maxWaitMs)
{
	bool rc_ok = false;
	tModemAtRc AtRc  = ModemRcOk; // the AT command result
	uint32_t abortTimeMs = xTaskGetTickCount() * portTICK_PERIOD_MS + maxWaitMs;

	ModemSetDtr(true);// we are configuring the DTR line for dropping out of transparent mode, lets give it a defined value

	do
	{
		// ERROR message format, for the moment verbose 9=2)
		if(ModemRcOk != modemSendAt(NULL, calcRemainingTimeMs(abortTimeMs,2000), &AtRc, "AT+CMEE=2"))
		{
			break;
		}

		ModemSetDtr(true);
		// use on->off of DTR to drop out of transparent mode, while retaining the data connection
		if(ModemRcOk != modemSendAt(NULL, calcRemainingTimeMs(abortTimeMs,2000), &AtRc, "AT^SCFG=\"GPIO/mode/DTR0\",\"std\""))
		{
			break;
		}
		// use on->off of DTR to drop out of transparent mode, while retaining the data connection
		if(ModemRcOk != modemSendAt(NULL, calcRemainingTimeMs(abortTimeMs,2000), &AtRc, "AT&D1"))
		{
			break;
		}
#if 1
		// get hardware line signalling that connection is lost
		if(ModemRcOk != modemSendAt(NULL, calcRemainingTimeMs(abortTimeMs,2000), &AtRc, "AT^SCFG=\"GPIO/mode/DCD0\",\"std\""))
		{
			break;
		}

		// DCD active when transparent TCP is connecting or up
		if(ModemRcOk != modemSendAt(NULL, calcRemainingTimeMs(abortTimeMs,2000), &AtRc, "AT&C2"))// signal is low when active, high when connection lost
		{
			break;
		}
#endif
		/* range check on RAT, different max for EHS5 and ELS61
		 *
		 * We use the SXRAT to set the desired RAT.
		 * Some options set the RAT directly,
		 * others are dual or triple mode where we also send a second and/or third parameter to set the preferred RAT
		 *
		 * For EHS5 second SXRAT parameter choices are:
		 * 0 RAT GSM
		 * 2 RAT UMTS
		 *
		 *
		 * For ELS61 second and third SXRAT parameter choices are:
		 * Access technology 1st and 2nd preferred
		 * 0 RAT GSM
		 * 2 RAT UMTS
		 * 3 RAT LTE
		 */
		int max_RAT = eRAT_GSM_UMTS_LTE, default_RAT = eRAT_UMTS_LTE;

		if(Device_GetHardwareVersion() < 10)
		{
			max_RAT = default_RAT = eRAT_UMTS;
		}

		if (RadioAccessTechnology > max_RAT)
		{
			LOG_DBG(LOG_LEVEL_MODEM,"RadioAccessType invalid value : %d, will use %d!\n", RadioAccessTechnology, default_RAT);
			RadioAccessTechnology = default_RAT;
		}

		tModemTaskRc rcOk = ModemRcWrongState;
		switch(RadioAccessTechnology)
		{
			case eRAT_GSM:
			case eRAT_UMTS:
			case eRAT_LTE:
				rcOk = modemSendAt(NULL, calcRemainingTimeMs(abortTimeMs,8000), &AtRc, "AT^SXRAT=%d", RadioAccessTechnology);
				break;

			case eRAT_GSM_UMTS:
			case eRAT_GSM_LTE:
			case eRAT_UMTS_LTE:
				rcOk = modemSendAt(NULL, calcRemainingTimeMs(abortTimeMs,8000), &AtRc, "AT^SXRAT=%d,%d", RadioAccessTechnology,
						(RadioAccessTechnology == eRAT_GSM_UMTS) ? eRAT_UMTS : eRAT_LTE);
				break;

			case eRAT_GSM_UMTS_LTE:
				rcOk = modemSendAt(NULL, calcRemainingTimeMs(abortTimeMs,8000), &AtRc, "AT^SXRAT=%d,%d,%d", RadioAccessTechnology, eRAT_LTE, eRAT_UMTS);
				break;

			default:
				break;
		}

		if(ModemRcOk != rcOk)
		{
			LOG_DBG(LOG_LEVEL_MODEM,"Unsupported RadioAccessType: %d\n", RadioAccessTechnology);
			break;
		}

	// The AT cmds used below are only for informational purpose, they could probably be removed or read once
	// and be stored in their corresponding datastore item.
#if 1
	// TODO : (12-may-2016) remove this test special for identifying sim communication errors
	
		if(dbg_logging & LOG_LEVEL_MODEM)
		{
			// retrieve modem version/firmware
			if(ModemRcOk != modemSendAt(NULL, calcRemainingTimeMs(abortTimeMs,2000), &AtRc, "ATI1"))
			{
				break;
			}
			// retrieve simcard info (IMSI)
			if(ModemRcOk != modemSendAt(NULL, calcRemainingTimeMs(abortTimeMs,2000), &AtRc, "AT+CIMI"))
			{
				break;
			}
		}
		// retrieve phone number, but phone number is only optional stored on the sim
		if(phonenum)
		{
			if(ModemRcOk != modemSendAt(NULL, calcRemainingTimeMs(abortTimeMs,2000), &AtRc, "AT+CNUM"))
			{
				break;
			}
		}
#endif

#if 0
		// reduce radio power
		if (ModemRcOk != modemSendAt(NULL, calcRemainingTimeMs(abortTimeMs,0), &AtRc, "AT^SCFG=\"Radio/OutputPowerReduction\",4"))
		{
			break;
		}
#endif


		if (simpin)
		{
			if (strlen(simpin)==4)
			{
				char atcmd[] = "AT+CPIN=\"0000\"";

				atcmd[9+0]=simpin[0];
				atcmd[9+1]=simpin[1];
				atcmd[9+2]=simpin[2];
				atcmd[9+3]=simpin[3];

				// SIM card needs pin code
				// TODO : make this configurable (SIM card/provider dependent)
				if(ModemRcOk != modemSendAt(NULL, calcRemainingTimeMs(abortTimeMs,2000), &AtRc, atcmd))
				{
					break;
				}
			}  
			else 
			{
				LOG_DBG(LOG_LEVEL_MODEM,"Simpin in config not 4 char long, assuming sim needs no pin!\n");
			}
		}

		if(waitForUrc(pbready, calcRemainingTimeMs(abortTimeMs,30000)) == false)
		{
			LOG_DBG(LOG_LEVEL_MODEM,"wait pbready: %d\n",rc_ok);
			break;
		}

		if(imei)
		{
			if(gNvmCfg.dev.modem.imei[0] == 0)
			{
				// retrieve imei number using AT+CGSN
				tModemResultFunc func =
				{
					.resultProcessor =  result_imei,
					.params = (void *) imei
				};
				*imei='\0'; // empty string to start with
				LOG_DBG(LOG_LEVEL_MODEM,"Retrieving IMEI from modem\n");
				if((ModemRcOk != modemSendAt(&func, calcRemainingTimeMs(abortTimeMs,2000), &AtRc, "AT+CGSN")) ||
				  (strlen(imei) != (MODEM_IMEI_LEN - 1)))
				{
					break;
				}
			}
			else
			{
				memcpy(imei, gNvmCfg.dev.modem.imei, sizeof(gNvmCfg.dev.modem.imei));
			}
		}

		if (iccid)
		{
			// retrieve iccid number using AT+CCID
			if(gNvmCfg.dev.modem.iccid[0] == 0)
			{
				tModemResultFunc func =
				{
					.resultProcessor =  result_iccid,
					.params = (void *) iccid
				};
				*iccid='\0'; // empty string to start with
				if((ModemRcOk != modemSendAt(&func, calcRemainingTimeMs(abortTimeMs,2000), &AtRc, "AT+CCID")) ||
				   (strlen(iccid) != (MODEM_ICCID_LEN - 1)))
				{
					break;
				}
			}
			else
			{
				memcpy(iccid, gNvmCfg.dev.modem.iccid, sizeof(gNvmCfg.dev.modem.iccid));
			}
		}
		// If it reaches here, then it means its going well, so lets return the status as true.
		Modem_data.state = initialised;
		LOG_DBG(LOG_LEVEL_MODEM,"\n=>>Modem State set to initialised in %s()\n", __func__);
		rc_ok = true;
	} while(0);

	return rc_ok;
}




static bool ProviderConfig( uint8_t providerProfile, char * apn, uint32_t maxWaitMs )
{
	bool rc_ok = false;
	tModemAtRc AtRc  = ModemRcOk; // the AT command result
	uint32_t abortTimeMs = xTaskGetTickCount() * portTICK_PERIOD_MS + maxWaitMs;

	// TODO: when I figure out that newlib-nano is safe to use, use snprintf for constructing AT commands with parameters

	// configure internet connection
	do
	{
		// packet switched ipv4 connection
		if(ModemRcOk != modemSendAt(NULL, calcRemainingTimeMs(abortTimeMs,2000), &AtRc, "AT^SICS=%d,\"conType\",\"GPRS0\"", providerProfile))
		{
			break;
		}

		// put this one at zero (according expert at gemalto/cinterion phone call 9-sept-2016
		if(ModemRcOk != modemSendAt(NULL, calcRemainingTimeMs(abortTimeMs,2000), &AtRc, "AT^SICS=%d,\"inactTO\",\"0\"", providerProfile))
		{
			break;
		}

		// access point name
		if(ModemRcOk != modemSendAt(NULL, calcRemainingTimeMs(abortTimeMs,5000), &AtRc, "AT^SICS=%d,\"apn\",\"%s\"", providerProfile, apn))
		{
			break;
		}

		Modem_data.state = providerConfigDone;
		LOG_DBG(LOG_LEVEL_MODEM,"\n=>>Modem State set to providerConfigDone in %s()\n", __func__);
		rc_ok = true;
	} while(0);

	return rc_ok;
}



/**
 * @brief Config Internet Service
 *
 * @param   serviceProfile 	: byte holding the number of the service profile [0..9]
 * @param   providerProfile : byte holding the number of the service profile [0..5]
 * @param   url				: ip address, example : lvs0217.dyndns.org or 10.11.12.13
 * @param 	portNr			: integer range [0..65535]
 *
 * @returns -
 */

static bool InternetServiceConfig( uint8_t serviceProfile, uint8_t providerProfile, uint8_t * url, uint16_t portNr, uint32_t maxWaitMs )
{
	bool rc_ok = false;
	uint32_t abortTimeMs = xTaskGetTickCount() * portTICK_PERIOD_MS + maxWaitMs;

	// only valid when we did register to a cellular network
	if (Modem_data.state != providerConfigDone )
	{
		LOG_DBG(LOG_LEVEL_MODEM,"%s: state error\n", __func__);
		return false;
	}

	// TODO: sanity check on the arguments ?

	// Configuration of Internet service
	do
	{
		if(ModemRcOk != modemSendAt(NULL, calcRemainingTimeMs(abortTimeMs,5000), NULL, "AT^SISS=%d,\"srvType\",\"Socket\"", serviceProfile))
		{
			break;
		}

		if(ModemRcOk != modemSendAt(NULL, calcRemainingTimeMs(abortTimeMs,5000), NULL, "AT^SISS=%d,\"ConId\",%d", serviceProfile, providerProfile))
		{
			break;
		}

		if(ModemRcOk != modemSendAt(NULL, calcRemainingTimeMs(abortTimeMs,5000), NULL, "AT^SISS=%d,\"address\",\"socktcp://%s:%d;etx;timer=200\"",
											serviceProfile, (char *)url, portNr))
		{
			break;
		}

		Modem_data.state = serviceConfigDone;
		LOG_DBG(LOG_LEVEL_MODEM,"\n=>>Modem State set to serviceConfigDone in %s()\n", __func__);
		rc_ok = true;
	} while(0);

	return rc_ok;
}

static void result_sici(uint8_t * str, uint32_t len, void  * sici)
{
	if(sici)
	{
		strcpy((char*)sici, "NONE");
	}
	if(strncmp((char*)str, "^SICI: 0,2,1,\"", 14) == 0)
	{
		char *p = strrchr((char*)str, '"');
		if(p && sici)
		{
			*p = 0;
			strcpy((char*)sici, (char*)(str + 14));
		}
	}
}

static bool logAndCloseConnection(uint8_t serviceProfile, uint32_t abortTimeMs, uint16_t eventCode)
{
	tModemAtRc AtRc  = ModemRcOk; // the AT command result
	tModemResultFunc func =
	{
		.resultProcessor =  result_sici,
		.params = (void *)Modem_data.allocated_ip
	};

	modemSendAt(&func, calcRemainingTimeMs(abortTimeMs,6000), &AtRc, "AT^SICI=%d", serviceProfile);
	bool rc_ok = (ModemRcOk == modemSendAt(NULL, calcRemainingTimeMs(abortTimeMs,5000), &AtRc, "AT^SISC=%d", serviceProfile));
	LOG_EVENT(eventCode, LOG_NUM_APP, ERRLOGINFO, "Allocated IP = %s; MCC/MNC = %d/%d", Modem_data.allocated_ip, Modem_data.mcc, Modem_data.mnc);
	return rc_ok;
}

/**
 * Connect to Internet Service (http/https)
 * 
 * Attempts to open internet connection to websites, either with plaintext or 
 * TLS.
 * 
 * @param[in] serviceProfile     service profile number
 * @param[in] maxWaitMs          max wait time, in ms
 * 
 * @retval true if successful, false otherwise
 */
static bool InternetServiceConnect( uint8_t serviceProfile, uint32_t maxWaitMs )
{
	bool rc_ok = true;
	tModemAtRc AtRc  = ModemRcOk; // the AT command result
	uint32_t abortTimeMs = xTaskGetTickCount() * portTICK_PERIOD_MS + maxWaitMs;
	uint16_t retryOpenConnection = 2;
	char urcOk[] = "^SISW: 0,1\r";
    // Either receives ^SISW: x,1 which is OK, or ^SIS: x,y which is not OK

	urcOk[7] += serviceProfile;

	if (Modem_data.state != serviceConfigDone)
	{
		return false;
	}

	tModemTaskRc taskRc;
	const uint32_t startTimeMs = xTaskGetTickCount() * portTICK_PERIOD_MS;

	while (retryOpenConnection-- && rc_ok && (Modem_data.state != serviceConnected))
	{
		clearUrc(sis);
		if (rc_ok)
		{
			// Open connection using internet profile
			rc_ok = (ModemRcOk == modemSendAt(NULL, calcRemainingTimeMs(abortTimeMs,0), &AtRc,"AT^SISO=%d", serviceProfile));
			if (rc_ok) rc_ok = (AtRc== AtOk);
		}

		if (rc_ok)
		{
			// wait for ^sis or ^sisw
			rc_ok = waitForUrc(sis, calcRemainingTimeMs(abortTimeMs,0) );
			LOG_DBG(LOG_LEVEL_MODEM,"wait ^SIS: %d\n", rc_ok);

			if (rc_ok)
			{
				if (0 == strncmp(urcOk, (char *)Modem_data.lastKnownUrc, Modem_data.lastKnownUrcLen))
				{
					Modem_data.state = serviceConnected;
					if (dbg_logging & LOG_LEVEL_MODEM)
					{
						printf("Modem state Connected....\n");
						// get some extra connection info printed
						modemSendAt(NULL, calcRemainingTimeMs(abortTimeMs,6000), &AtRc, "AT^SICI=%d", serviceProfile);
					}
				}
				else // did not receive urcOk
				{
					// Close connection using internet profile
					rc_ok = logAndCloseConnection(serviceProfile, abortTimeMs, MDM_SISO_0);
					vTaskDelay(1000/portTICK_PERIOD_MS);
					LOG_DBG(LOG_LEVEL_MODEM,"Internet connection Failed. Close it....\n");
				}
			}
			else
			{
				rc_ok = logAndCloseConnection(serviceProfile, abortTimeMs, MDM_SISO_1);
				LOG_DBG(LOG_LEVEL_MODEM,"Modem failed to enter transparent mode. Close it....\n");
			}
		}
	}

	return rc_ok;
}

// exit transparent mode, disconnect connections
static bool InternetServiceDisconnect( uint8_t serviceProfile, uint32_t maxWaitMs )
{
	tModemAtRc AtRc  = ModemRcOk; // the AT command result
	uint32_t abortTimeMs = xTaskGetTickCount() * portTICK_PERIOD_MS + maxWaitMs;

	bool rc_ok = Modem_stopTransparent();

	if((Modem_data.state == serviceConnected) && (Modem_IsDCDEventFlagSet() == false))
	{
		// terminate connections
		if (rc_ok && (dbg_logging & LOG_LEVEL_MODEM))
		{
#if 0
			// retrieve signal quality (consider AT^SMONI if csq is too simple)
			rc_ok = ( ModemRcOk == modemSendAt( (uint8_t *) "AT^SMONI" , NULL, 2000, &AtRc));
#endif
			if (rc_ok)
			{
				tSisiResp sisiResp;
				tModemResultFunc func =
				{
					.resultProcessor =  result_sisi,
					.params = (void *) &sisiResp
				};

				rc_ok = ( ModemRcOk == modemSendAt(&func, calcRemainingTimeMs(abortTimeMs,0), &AtRc, "AT^SISI=%d", serviceProfile));
				if (rc_ok)
				{
					Modem_data.connectionMetrics.tcpStatistics.bytesRx = sisiResp.rxCount;
					Modem_data.connectionMetrics.tcpStatistics.bytesTx = sisiResp.txCount;
					Modem_data.connectionMetrics.tcpStatistics.bytesAck = sisiResp.ackData;
					Modem_data.connectionMetrics.tcpStatistics.bytesNack = sisiResp.unackData;
					LOG_DBG(LOG_LEVEL_MODEM,"SISI data: state=%d,  rx= %d, tx=%d, ack=%d, Nack=%d\n", sisiResp.srvState, sisiResp.rxCount, sisiResp.txCount, sisiResp.ackData, sisiResp.unackData);
				}
			}
		}

		if (rc_ok)
		{
			// Close connection using internet profile 2
			rc_ok =  ( ModemRcOk == modemSendAt(NULL, calcRemainingTimeMs(abortTimeMs,0), &AtRc, "AT^SISC=%d", serviceProfile));
		}
	}
	if (rc_ok)
	{
		LOG_DBG(LOG_LEVEL_MODEM,"\n=>>Modem State %s in %s(), DCD Flag: %s\n",
				StateToString(), __func__, Modem_IsDCDEventFlagSet() == false? "False" : "True");
	}
	return rc_ok;
}

/**
 * Modem_readCellularIds
 *
 * @desc		reads back the id's of the modem and the sim, protected sims need a valid simpin
 *
 * @param   rat             : radioaccesstechnology
 * @param	simPin			: string of 4 numbers, when NULL, or not 4 char long, the simpin will not be set
 * @param 	imei			: when not NULL, it writes a string with the imei number of the modem (make sure the array is big enough)
 * @param	iccid			: when not NULL, it writes a string with the iccid number of the simcard (make sure the array is big enough)

 * @returns -
 */
bool Modem_readCellularIds(uint8_t rat,char * simPin,  uint8_t * imei, uint8_t * iccid, uint32_t maxWaitMs)
{
	bool rc_ok = true;
	bool powerDown = false;
	uint32_t abortTimeMs = xTaskGetTickCount() * portTICK_PERIOD_MS + maxWaitMs;

	if (powerdown == Modem_data.state)
	{
		powerDown = true;// unconditionally, if the powerup fails, it did start the sequence, but aborted it.
		rc_ok = Modem_powerup(calcRemainingTimeMs(abortTimeMs,0)) ;
	}

	// now modem should be ready for AT commands.
	if (poweredup == Modem_data.state)
	{
		rc_ok = cellularConfig(rat, simPin, NULL, (char * )imei, (char *) iccid, calcRemainingTimeMs(abortTimeMs,0));
	}
	else
	{
		rc_ok = false;
		LOG_DBG(LOG_LEVEL_MODEM,"Modem_readCellularIds, error wrong state : %s\n", StateToString());
	}

	if (powerDown)
	{
		shutdownModem(calcRemainingTimeMs(abortTimeMs,0));// gdf : ignore error from powering down this time, happens and prevents writing the already retrieved IMEI to the configuration
		ModemTerminate(1000);
		Modem_data.state = powerdown;
	}

	return rc_ok;
}

/**
 * Modem_getMaxTimeToConnectMs
 *
 * @desc		calculate the max wait time for making a connection in ms
 * @param   - logit         : log the result
 *
 * @returns - maxTimeMs		: max wait time in ms
 */
uint32_t Modem_getMaxTimeToConnectMs(bool logit)
{
	uint32_t maxTimeMs = gNvmCfg.dev.modem.maxTimeToConnectMs; // get this from a NV config parameter, or maybe another function parameter ?

	// increment wait for connection time once in a while (1:25) when we fail to get a connection
	// this is to 'catch' the case that the local cell/connection point has not yet cashed the provider of this SIM chip, and this initial connection has a timeout of 7 minutes.
	// we do not want to do this every time, because it eats batteries.
	if ((gNvmData.dat.comm.failed_connections_in_a_row % 25) == 24)
	{
	    maxTimeMs += 3*60*1000; // add 3 minutes
	    if ((gNvmData.dat.comm.failed_connections_in_a_row % 100) == 99)
	    {
	        maxTimeMs += 3*60*1000; // add another 3 minutes
	    }
	    if(logit)
	    {
	    	LOG_EVENT(MDM_TIMEOUT, LOG_NUM_APP, ERRLOGINFO, "After %d failed attempts, increased maxTimeToConnectMs this attempt to %d",
	    		gNvmData.dat.comm.failed_connections_in_a_row, maxTimeMs );
	    }
	}
	else if(bWakeupIsMAGSwipe)
	{
	    maxTimeMs += 4*60*1000; // add 4 minutes
	}

	return maxTimeMs;
}

static bool retrieveCSQ(uint32_t *csq, uint32_t abortTimeMs)
{
	uint32_t localCsq = 0;// well, thats the lowest we can receive

	// retrieve csq number AT+CSQ
	tModemResultFunc funcCsq =
	{
		.resultProcessor =  result_csq,
		.params = (void *) &localCsq
	};

	bool rc_ok = (ModemRcOk == modemSendAt(&funcCsq, calcRemainingTimeMs(abortTimeMs,2000), NULL, "AT+CSQ"));
	if (csq)
	{
		*csq = localCsq;
	}

	Modem_data.connectionMetrics.signalQuality.csq = localCsq;

	return rc_ok;
}

#define SMONI_TOTAL_TIMEOUT_MS 120000
#define SMONI_DELAY_BETWEEN_CALLS_MS 300

/**
 * @brief Get information of the serving cell (i.e. signal quality)
 * 
 * @param[out] signalQuality output signal quality, if connected to a cell
 * @param[in]  abortTimeMs   max time to abort operation
 * @return true     Received proper response, with valid signal quality
 * @return false    Received response without signal quality (e.g. not connected
 * 				    to any serving cell, etc.)
 */
static bool servingCell(int16_t *signalQuality, uint32_t abortTimeMs)
{
	char smoniOutput[MAXSMONILENGTH];
	int16_t tmpSignalQuality = -1;
	tModemResultFunc funcMoni =
	{
		.resultProcessor =  result_smoni,
		.params = (void *) smoniOutput
	};
	
	// Tests showed that SMONI would normally return signal strength with ~1 
	// second delay before 1st invocation (meaning modem connected to cell 
	// tower). However, if there is network issues or just weak signal, it would
	// take more attempts to get connected.
	// The delay between SMONI commands is selected so we don't query too 
	// infrequently (potentially wasting time). The total timeout for all SMONI 
	// commands is selected so we still try to make the current comms cycle work 
	// but at the same time don't waste too much energy unnecessary by waiting 
	// too long.
	for (int i = 0; (i < SMONI_TOTAL_TIMEOUT_MS / SMONI_DELAY_BETWEEN_CALLS_MS) && 
		 (calcRemainingTimeMs(abortTimeMs,0) > 10000); i++)
	{
		vTaskDelay( SMONI_DELAY_BETWEEN_CALLS_MS / portTICK_PERIOD_MS);
		
		// retrieve signal quality (AT^CSQ is not accurate for 3G/4G)
		if((ModemRcOk == modemSendAt(&funcMoni, calcRemainingTimeMs(abortTimeMs,3000), NULL, "AT^SMONI")) &&
			(strstr(smoniOutput,"NOCONN") || strstr(smoniOutput,"CONN")))
		{
			LOG_DBG(LOG_LEVEL_MODEM,"SMONI Output : %s\n",smoniOutput);
			tmpSignalQuality = decode_smoni(smoniOutput);
			if (-1 != tmpSignalQuality)
			{
				Modem_data.connectionMetrics.signalQuality.smoni = tmpSignalQuality;
				*signalQuality = tmpSignalQuality;
				return true;
			}
		}
	}

	LOG_EVENT(MDM_SIGNAL, LOG_NUM_APP, ERRLOGINFO, "signal quality = %d", tmpSignalQuality);

	return false;
}

/**
 * Modem_init
 *
 * @desc		assumes powerdown state
 * 				brings modem into AT command state, with all the initial modem settings and connect to the cellular network
 *  *
 * @param   rat             : radio access technology (0=2g only, 1=dualmode  3g preferred, 2=3g only, 3=4g)
 * @param	simPin			: string of 4 numbers, when NULL, or not 4 char long, the simpin will not be set
 * @param 	apn				: apn parameter as supplied by provider
 * @param   providerProfile : byte holding the number of the service profile [0..5]
 * @param   serviceProfile 	: byte holding the number of the service profile [0..9]
 * @param   url				: string with the ip address, example : lvs0217.dyndns.org or 10.11.12.13
 * @param 	portNr			: integer range [0..65535]
 * @param 	imei			: when not NULL, it writes a string with the imei number of the modem (make sure the array is big enough)
 * @param	iccid			: when not NULL, it writes a string with the iccid number of the simcard (make sure the array is big enough)
 * @param   csq	 			: when not NULL, the integer value of the signal quality is returned (result of the AT+CSQ command)
 * @param   sigQual         : when not NULL, the signal quality calculated out of the at^smoni response will be returned
 * @param   minSigQualVal   : connection will be aborted if the signal quality (from the SMONI) is below this value
 *
 * @returns - 0 on success or error code
 */

int Modem_init(uint8_t rat, char * simPin, char * apn, uint8_t providerProfile, uint8_t serviceProfile, uint8_t * url, uint16_t portNr,
                    uint8_t * imei, uint8_t * iccid, uint32_t *csq, int16_t *sigQual, uint8_t minSigQualVal )
{
	int rc = MODEM_NO_ERROR;
	int16_t signalQuality = -1;
	const uint32_t maxTimeMs = Modem_getMaxTimeToConnectMs(true);
	const uint32_t abortTimeMs =  xTaskGetTickCount() * portTICK_PERIOD_MS + maxTimeMs;

	memset(&Modem_data.connectionMetrics, 0, sizeof(Modem_data.connectionMetrics));
	strcpy(Modem_data.allocated_ip, "NONE");
	Modem_data.mcc = Modem_data.mnc = 0;
	Modem_data.connectionMetrics.timeStamps.validTimestamps = true;

	TIMESTAMP(start);

	clearUrc(pbready); // done here, because sometimes it comes very fast after powerup !
	if (Modem_data.state== powerdown)
	{
		Modem_powerup(calcRemainingTimeMs(abortTimeMs,0));
	}

	do
	{
		// now modem should be ready for AT commands.
		if(Modem_data.state != poweredup)
		{
			rc = MODEM_NOT_POWERED;
			break;
		}
#if 0
		// possible workaround for nodes never connecting anymore
	    // reset the AT command settings to factory defaults
	    if(ModemRcOk !=  modemSendAt(NULL, 2000, NULL, "AT&F"))
	    {
	    	rc = MODEM_ATF;
	    	break;
	    }

	    // force normal mode (if it came into airplane mode for whatever reason)
	    clearUrc(sysstart);
	    if(ModemRcOk !=  modemSendAt(NULL, 2000, NULL, "AT+CFUN=1,0")))
		{
			rc = MODEM_CFUN;
			break;
		}
	    waitForUrc(sysstart,5000 ); // 5 seconds OK ?
	    // end possible workaround
#endif
	    // possible workaround for nodes starting up in a non default  power save mode
        // set asc0 always on
        if(ModemRcOk !=  modemSendAt(NULL, 5000, NULL, "AT^SPOW=1,0,0"))
        {
        	rc = MODEM_SPOW;
        	break;
        }

        // for experiments with yes/no flow control possible workaround for nodes starting up in a non default  power save mode
        // AT\Q0 no flow control, AT\QQ3 RTS/CTS flow control
        if(ModemRcOk !=  modemSendAt(NULL, 5000, NULL, "AT\\Q3"))
        {
        	rc = MODEM_Q3;
        	break;
        }

		if(!cellularConfig( rat,  simPin, NULL, (char * )imei, (char *) iccid, calcRemainingTimeMs(abortTimeMs,0)))
		{
			rc = MODEM_CELLULAR_CONFIG;
			break;
		}

		// TODO - use below to connect to 2G
		// if(!Modem_cellularConfig( '0',  simPin, NULL, (char * )imei, (char *) iccid, calcRemainingTimeMs(abortTimeMs)))
		TIMESTAMP(cellularConfigDone);

		if(Modem_data.state < initialised)
		{
			rc = MODEM_NOT_INITIALISED;
			break;
		}

#if 0
		// with sim pin protected sims after pbready, or non protected sim, it automatically changes to AT+COPS=0 (automatic registration)
			// Manually deregister from network
		if(ModemRcOk !=  modemSendAt(NULL,calcRemainingTimeMs(abortTimeMs,0), &AtRc, "AT+COPS=2")))
		{
			rc = MODEM_COPS;
			break;
		}

		// Automatically register on network
		if(ModemRcOk !=  modemSendAt(NULL, calcRemainingTimeMs(abortTimeMs,0), &AtRc, "AT+COPS=0")))
		{
			rc = MODEM_COPS;
			break;
		}
#endif

		if(!ProviderConfig(providerProfile, apn, calcRemainingTimeMs(abortTimeMs,0)))
		{
			rc = MODEM_PROVIDER_CONFIG;
			break;
		}
		TIMESTAMP(providerConfigDone);

		if(!InternetServiceConfig(serviceProfile, providerProfile, url,  portNr, calcRemainingTimeMs(abortTimeMs,0)))
		{
			rc = MODEM_INTERNET_SVC_CONFIG;
			break;
		}
		TIMESTAMP(serviceConfigDone);

		if(!servingCell(&signalQuality, abortTimeMs))
		{
			rc = MODEM_SERVING_CELL;
			break;
		}
		else
		{
			if (-1 != signalQuality)
			{
				if (sigQual)
				{
					*sigQual = signalQuality;
				}
			}
		}

		if(!retrieveCSQ(csq, abortTimeMs))
		{
			rc = MODEM_RETRIEVE_CSQ;
			break;
		}

		TIMESTAMP(readSignalStrengthDone);

		if (signalQuality < minSigQualVal)
		{
			LOG_EVENT(MDM_CSQ, LOG_NUM_APP, ERRLOGINFO,
					"Poor signal quality; csq = %d, SMONI signal quality = %d (limit = %d) , aborting connection attempt!",
					Modem_data.connectionMetrics.signalQuality.csq, signalQuality, minSigQualVal);
			rc = MODEM_SIGNAL_QUALITY;
			break;
		}

		if(!InternetServiceConnect( serviceProfile, calcRemainingTimeMs(abortTimeMs,0) ))
		{
			rc = MODEM_INTERNET_SVC_CONNECT;
			break;
		}
		TIMESTAMP(serviceConnectDone);

		// TODO Better would be callback called at connection state change
		// Flash led x3 to indicate registered on network
		if (commsRecord.params.Wakeup_Reason == WAKEUP_CAUSE_MAGNET)
		{
			xTaskApp_flashStatusLed(3);
		}
		if(!Modem_startTransparent(serviceProfile, calcRemainingTimeMs(abortTimeMs,0)))
		{
			rc = MODEM_START_TRANSPARENT;
			break;
		}
		rc = MODEM_NO_ERROR;
	} while(0);

	if(rc != MODEM_NO_ERROR)
	{
	    // administer another failed attempt
	    gNvmData.dat.comm.failed_connections_in_a_row++;// dont care if 16 bit goes back to zero, value is used with modulo
		LOG_EVENT(MDM_CONNECTION, LOG_NUM_APP, ERRLOGWARN, "failed setting up a modem connection, Count:%d, ModemState:%s, Error:%s",
					  gNvmData.dat.comm.failed_connections_in_a_row, StateToString(), modemErrorStrings[rc]);
		Modem_data.connectionMetrics.timeStamps.validTimestamps = false;
	}
	else
	{
		// we got a connection which may be bad, but that is another story.
	    gNvmData.dat.comm.failed_connections_in_a_row = 0;
	}

	return rc;
}


static bool shutdownModem(uint32_t maxWaitMs)
{
	const uint32_t abortTimeMs = xTaskGetTickCount() * portTICK_PERIOD_MS + maxWaitMs;
	const char msgPowerInd[] = "EHS5E_POWER_IND input pin signals that the modem is %s !\n";
	const char msgPowerDown[] = "power down";
	const char msgPowered[] = "powered";

	clearUrc(shutdown);
	// Power off the module
	bool rc_ok = (ModemRcOk == modemSendAt(NULL, calcRemainingTimeMs(abortTimeMs,0), NULL, "AT^SMSO"));
	if (rc_ok)
	{
	    rc_ok = waitForUrc(shutdown, calcRemainingTimeMs(abortTimeMs,0));
		if (rc_ok == false)
		{
		    LOG_DBG(LOG_LEVEL_MODEM,"ERROR: wait for ^SHUTDOWN FAILED: %d\n", rc_ok);

		    if (!configModem_PowerCheck())
		    {
		        LOG_DBG(LOG_LEVEL_MODEM,"EHS5E_POWER_IND input pin signals that the modem is power down, but why no URC ?\n");
		        rc_ok = true;// lets hope the modem is really power down
		    }
		    else
		    {
		        // lets try again
		        rc_ok = (ModemRcOk == modemSendAt(NULL, calcRemainingTimeMs(abortTimeMs,0), NULL, "AT^SMSO"));
		        if (rc_ok)
		        {
		            rc_ok = waitForUrc(shutdown, calcRemainingTimeMs(abortTimeMs, 0));
		            LOG_DBG(LOG_LEVEL_MODEM,"wait ^SHUTDOWN: %d\n", rc_ok);
		        }
		    }
		}
	}

	LOG_DBG(LOG_LEVEL_MODEM, msgPowerInd, !configModem_PowerCheck() ? msgPowerDown : msgPowered);
    while (calcRemainingTimeMs(abortTimeMs,0) && (!configModem_PowerCheck() == false))
    {
        vTaskDelay(1000);
        LOG_DBG(LOG_LEVEL_MODEM, msgPowerInd, !configModem_PowerCheck() ? msgPowerDown : msgPowered);
	}

	return rc_ok;
}


// exit transparent mode, disconnect connections
bool Modem_terminate(uint8_t serviceProfile)
{
	bool rc_disconnect_ok = true;
	const uint32_t minWait_ms = 20000; // Defined by modemDefaults.maxTimeToTerminateMs
	// make sure we apply the lowest allowed
	const uint32_t maxTimeMs = gNvmCfg.dev.modem.maxTimeToTerminateMs > minWait_ms? gNvmCfg.dev.modem.maxTimeToTerminateMs: minWait_ms;
	const uint32_t abortTimeMs = xTaskGetTickCount() * portTICK_PERIOD_MS + maxTimeMs;

	if (Modem_data.state >= serviceConnected)
	{
		LOG_DBG(LOG_LEVEL_MODEM,"\nIn %s(), Modem State: %s.\n", __func__, StateToString());

		// terminate connections, try to do it the proper way,
		rc_disconnect_ok = InternetServiceDisconnect(serviceProfile, calcRemainingTimeMs(abortTimeMs,0));
		if(rc_disconnect_ok && Modem_data.connectionMetrics.timeStamps.validTimestamps)
		{
			TIMESTAMP(serviceDisconnectDone);
		}

		if (rc_disconnect_ok)
		{
			// Manually de-register from network
			rc_disconnect_ok = ( ModemRcOk == modemSendAt(NULL, calcRemainingTimeMs(abortTimeMs,0), NULL, "AT+COPS=2"));
			if(rc_disconnect_ok && Modem_data.connectionMetrics.timeStamps.validTimestamps)
			{
				TIMESTAMP(deregisterDone);
			}
		}
		else
		{
			// TODO, log this?
		}
	}

	bool rc_shutdown_ok = shutdownModem(calcRemainingTimeMs(abortTimeMs,0));
	if (rc_shutdown_ok)
	{
		TIMESTAMP(UHS5E_shutdown);
	}
	else
	{
		LOG_EVENT(MDM_POWER_OFF, LOG_NUM_APP, ERRLOGFATAL, "failed to correctly power modem off");
		Modem_data.connectionMetrics.timeStamps.validTimestamps = false;
		Modem_WriteModemDebugDataAsEvents();
	}

	// we tried to power down gracefully, but whatever the outcome of that was, we will remove the power anyway.
	ModemTerminate(1000);
	TIMESTAMP(poweroff);
	Modem_data.state = powerdown;

	return rc_disconnect_ok && rc_shutdown_ok;
}




/**
 * init data for the modem
 * must not be called before the modem driver is initialized !
 */
void Modem_drv_init()
{

	// init data struct(s)
	Modem_data.state = powerdown;
	// create urc semaphore
	Modem_data.semUrc = xSemaphoreCreateBinary( );
	Modem_data.waitingForUrc = urc_max;// invalid urc index
	clearUrc(urc_max);

	// clear statistics data
	memset(&Modem_data.connectionMetrics, 0, sizeof(Modem_data.connectionMetrics));

	// register urc handler
	ModemSetCallback(Modem_cb_urc_received, handleURC);
	ModemSetCallback(Modem_cb_at_response, handleAtResponse);
}

uint32_t TS_Offset(uint32_t ts)
{
	if(0 == ts)
	{
		return 0;
	}
	return (ts - Modem_data.connectionMetrics.timeStamps.start);
}

static void print_metrics()
{
	printf("Timings in ms, relative to start\n");
	printf("start                  : %8u\n", Modem_data.connectionMetrics.timeStamps.start);
	printf("poweron                : %8u\n", TS_OFFSET(poweron));
	printf("sysstart               : %8u\n", TS_OFFSET(sysstart));
	printf("cellularConfigDone     : %8u\n", TS_OFFSET(cellularConfigDone));
	printf("providerConfigDone     : %8u\n", TS_OFFSET(providerConfigDone));
	printf("serviceConfigDone      : %8u\n", TS_OFFSET(serviceConfigDone));
	printf("readSignalStrengthDone : %8u\n", TS_OFFSET(readSignalStrengthDone));
	printf("serviceConnectDone     : %8u\n", TS_OFFSET(serviceConnectDone));
	printf("serviceDisconnectDone  : %8u\n", TS_OFFSET(serviceDisconnectDone));
	printf("deregisterDone         : %8u\n", TS_OFFSET(deregisterDone));
	printf("UHS5E_shutdown         : %8u\n", TS_OFFSET(UHS5E_shutdown));
	printf("poweroff               : %8u\n", TS_OFFSET(poweroff));

	printf("\nsignal levels\n");
    printf("csq   : %5d\n",    Modem_data.connectionMetrics.signalQuality.csq);
    printf("smoni : %5d\n",    Modem_data.connectionMetrics.signalQuality.smoni);

	printf("\ntcp statistics\n");
	printf("bytesRx   : %8u\n",    Modem_data.connectionMetrics.tcpStatistics.bytesRx);
	printf("bytesTx   : %8u\n",    Modem_data.connectionMetrics.tcpStatistics.bytesTx);
	printf("bytesAck  : %8u\n",    Modem_data.connectionMetrics.tcpStatistics.bytesAck);
	printf("bytesNack : %8u\n",    Modem_data.connectionMetrics.tcpStatistics.bytesNack);
	printf("\n");
}


void Modem_print_info()
{
	printf("Modem:\nstate = %s\n", StateToString());
	if (!configModem_PowerCheck())
	{
		printf("EHS5E_POWER_IND input pin signals that the modem is power down !\n");
	}
	if (Modem_data.lastKnownUrcLen)
	{
		printf("Last relevant URC : %d\n", Modem_data.lastKnownUrc);
		for (int i = 0; i < Modem_data.lastKnownUrcLen; i++)
		{
			printf("%c",Modem_data.lastKnownUrc[i]);
		}
		printf("\n");
	}
	printf("\n");
	print_metrics();
}


#ifdef __cplusplus
}
#endif