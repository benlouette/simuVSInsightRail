#ifdef __cplusplus
extern "C" {
#endif

/*
 * taskGnss.c
 *
 *  Created on: Jan 31, 2017
 *      Author: ka3112
 */




#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <timers.h>
#include <portmacro.h>

#include <Resources.h>
#include <xTaskDefs.h>

#include "PowerControl.h"
#include "pinDefs.h"
#include "pinConfig.h"
#include "Device.h"

/*-------------------------------------------------------------------------------------*
 |                                                                                     |
 *-------------------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "Log.h"
#include "taskGnssEvent.h"
#include "taskGnss.h"
#include "xTaskAppGnss.h"
#include "gnssIo.h"
#include "gnssMT3333.h"
#include "configGnss.h"
#include "CLIcmd.h"
#include "CS1.h"
#include "printgdf.h"
#include "ephemeris.h"

#ifdef GNSS_MEANSPEED
#include <math.h>
#endif

//
//
extern int strcasecmp(const char *, const char *);		// Mutes the compiler warning.
extern char *RtcUTCToString(uint32_t seconds);

void taskGnss(void *pvParameters);
static time_t gnssUtcConvert(float gnss_utc, uint32_t gnss_date);

#define TO_1s	1000
#define EVENTQUEUE_NR_ELEMENTS_GNSS  (8)     //! Event Queue can contain this number of elements

/*
 * Data
 */
static QueueHandle_t        _EventQueue_Gnss;
static QueueHandle_t        _EventQueue_GnssBin;
TaskHandle_t         _TaskHandle_Gnss;// not static because of easy print of task info in the CLI

static uint32_t queuefullcounter = 0;

tGnssStatus gnssStatus;
bool epoDebug = false;


/*
 * callback administration
 */

// what I will move to the header file
// what stays in the c file
#define GNSSMAXCALLBACKS   (2)


// will move to the status struct ?
static tGnssCallbackFuncPtr GnssCallbackFuncTable[gnss_cb_max][GNSSMAXCALLBACKS];

// functions to power the modem on and off
static void inline Gnss_PowerOn(void)
{
    powerGNSSOn();// gives no status back
}

static void inline Gnss_PowerOff(void)
{
    powerGNSSOff();// gives no status back
}

static void inline Gnss_ResetOn(void)
{
    // for the firefly reset is active low
    GPIO_DRV_ClearPinOutput(Device_GNSS_ResetPin());// gives no status back
}

static void inline Gnss_ResetOff(void)
{
    // for the firefly reset is active low
    GPIO_DRV_SetPinOutput(Device_GNSS_ResetPin());// gives no status back
}

// Unlock the GNSS global data
static void inline gnss_Unlock()
{
	xSemaphoreGive(gnssStatus.semAccess);
}

// Lock the GNSS global data
static bool gnss_Lock(uint32_t maxWait, const char * funcname)
{
	if (pdTRUE != xSemaphoreTake(gnssStatus.semAccess , (maxWait==portMAX_DELAY) ? portMAX_DELAY : maxWait/portTICK_PERIOD_MS))
	{
         LOG_EVENT(0, LOG_NUM_GNSS, ERRLOGWARN, "%s: access semTake failed\n", funcname );
         return false;
	}
	return true;
}

// Initialise local data with defined values (in this case everything null pointer)
static void GnssInitCallBack()
{
   for (int i = 0; i < gnss_cb_max; i++)
   {
        for (int j = 0; j < GNSSMAXCALLBACKS; j++)
        {
            GnssCallbackFuncTable[i][j] = NULL;
        }
    }
}

// remove the callback : safe to be called from another task
void GnssRemoveCallback(tGnssCallbackIndex cbIdx, tGnssCallbackFuncPtr cbFunc)
{
    if (cbIdx < gnss_cb_max)
    {
        for (int i = 0; i < GNSSMAXCALLBACKS; i++)
        {
            CS1_CriticalVariable();

            CS1_EnterCritical();

            // running in another task context, and a 32 bit pointer assign is not atomic on this processor, so be on the safe side
            if (GnssCallbackFuncTable[cbIdx][i] == cbFunc)
            {
                GnssCallbackFuncTable[cbIdx][i] = NULL;
            }

            CS1_ExitCritical();
        }
    }
}

// set the callback : safe to be called from another task
void GnssSetCallback(tGnssCallbackIndex cbIdx, tGnssCallbackFuncPtr cbFunc)
{
    if (cbIdx < gnss_cb_max)
    {
        GnssRemoveCallback(cbIdx,  cbFunc);// just to prevent multiple entries of this address

        for (int i = 0; i < GNSSMAXCALLBACKS; i++)
        {
            if (GnssCallbackFuncTable[cbIdx][i] == NULL)
            {
                CS1_CriticalVariable();

                CS1_EnterCritical();
                // running in another task context, and a 32 bit pointer assign is not atomic on this processor, so be on the safe side
                GnssCallbackFuncTable[cbIdx][i] = cbFunc;

                CS1_ExitCritical();
                break;// exit the for loop
            }
        }
    }
}

// call possible registered callback functions when we got a mote request
static uint8_t handleCallback (tGnssCallbackIndex cbIdx,  void * params)
{
    uint8_t rc = 0;
    if (cbIdx < gnss_cb_max)
    {
        for (int i = 0; i < GNSSMAXCALLBACKS; i++)
        {
            if (GnssCallbackFuncTable[cbIdx][i] != NULL)
            {
                uint8_t rc2 = GnssCallbackFuncTable[cbIdx][i](params); // call whoever is interested.
                if (rc2 != 0)
                {
                	rc = rc2; // last callback with error status wins
                }
            }
        }
    }
    return rc;
}


/*
 * end callback administration
 */

bool BinarytoNMEAReset()
{
	// OK, might be in binary mode so switch back into NMEA mode
	bool rc_ok = GNSS_BinarytoNMEA();

        if (!Device_PMICcontrolGPS())
        {
            // tacky but required
            vTaskDelay(TO_1s /  portTICK_PERIOD_MS ); // 1 sec delay

            Gnss_ResetOn();
            vTaskDelay(100);
            Gnss_ResetOff();
        }
	return rc_ok;
}


/*
 * gnssNmeaValidateChecksum
 *
 * @desc    checksum calculation : first char in buf must be the '$', then xor all until the '*'
 *
 * @param   gnss_buf pointer to the message, when valid, string will be terminatedbefore the checksum part
 *
 * @return false when checksum error
 */
#define TOUPPERMASK (~('A'^'a'))
#define ASCII_HEX_TO_INT(c) ((c > '9') ? (c & TOUPPERMASK) - 'A' + 10 : c - '0')
bool gnssNmeaValidateChecksum(struct gnss_buf_str *gnss_buf)
{
    uint8_t chksum = 0;
    bool rc_ok = false;

    for (int i = 1; i < gnss_buf->idx; i++)
    {
        if (gnss_buf->buf[i] == '*')
        {
            uint8_t chksum2 = (ASCII_HEX_TO_INT(gnss_buf->buf[i+1]) << 4) + ASCII_HEX_TO_INT(gnss_buf->buf[i+2]);
            if(chksum2 == chksum)
            {
            	gnss_buf->buf[i]='\0'; // terminate string so checksum is gone
            	rc_ok = true;
            }
            break;
        }
        else
        {
            chksum ^= gnss_buf->buf[i];
        }
    }
    return rc_ok;
}
#undef ASCII_HEX_TO_INT
#undef TOUPPERMASK

/*
 *  BEGIN decoding known NMEA messages, to move to separate file !
 *
 */

/**
* @brief Get token from string
* *str points to string, string will be modified !
* delimiter will be replaced by '\0'
* returns start of item, terminated by the '\0'
* on exit : *str points behind 'delimiter'
*
* after the last item, *str points to the terminating '\0'
*/
char *get_token(char **str, char delimiter)
{
    char *d = *str;	// at start of item
    char *c = (char *)strchr(*str, delimiter);
    if(c == NULL)
    {
        *str = *str+strlen(*str); // points at the final '\0'
    }
    else
    {
    	*c = 0; // replace delimiter with the '\0'
    	*str = c+1; // points just after delimiter
    }
    return d;
}

float get_token_atof(char **str)
{
	return atof(get_token(str, ','));
}

int get_token_atoi(char **str)
{
	return atoi(get_token(str, ','));
}

char get_token_char(char **str)
{
	return *get_token(str, ',');
}

static void gnssDecodeRmc(char **strt, tNmeaDecoded *nmeaDecoded_p);
static void gnssDecodeVtg(char **strt, tNmeaDecoded *nmeaDecoded_p);
static void gnssDecodeGga(char **strt, tNmeaDecoded *nmeaDecoded_p);
static void gnssDecodeGsa(char **strt, tNmeaDecoded *nmeaDecoded_p);
static void gnssDecodeGsv(char **strt, tNmeaDecoded *nmeaDecoded_p);
static uint8_t handleNmeaResponse(tNmeaDecoded * nmeaDecoded_p);

const static struct
{
    tNmeaType type;
    char  *code;
    void (*gnssDecode)(char **, tNmeaDecoded *nmeaDecoded_p);
} nmea_codes[] =
{
        {nmea_GLL, "GLL", NULL},
        {nmea_RMC, "RMC", gnssDecodeRmc},
        {nmea_VTG, "VTG", gnssDecodeVtg},
        {nmea_GGA, "GGA", gnssDecodeGga},
        {nmea_GSA, "GSA", gnssDecodeGsa},
        {nmea_GSV, "GSV", gnssDecodeGsv},
        {nmea_ZDA, "ZDA", NULL},
};

#define GNSSLOG_INCOMMING (1)
#define GNSSLOG_DECODED (2)

static uint8_t logmsg[nmea_LAST] = {0};

// example : $GNRMC,120230.000,A,5201.8959,N,00505.7139,E,0.20,1.42,100217,,,
// expects a valid zero terminated string
static void gnssDecodeRmc(char **strt, tNmeaDecoded *nmeaDecoded_p)
{
	nmeaDecoded_p->nmeaData.rmc.utc_time = get_token_atof(strt);
	nmeaDecoded_p->nmeaData.rmc.valid = ('A' == get_token_char(strt));
	nmeaDecoded_p->nmeaData.rmc.latitude = get_token_atof(strt);
	nmeaDecoded_p->nmeaData.rmc.NS = get_token_char(strt);
	nmeaDecoded_p->nmeaData.rmc.longitude = get_token_atof(strt);
	nmeaDecoded_p->nmeaData.rmc.EW = get_token_char(strt);
	nmeaDecoded_p->nmeaData.rmc.speed_knots = get_token_atof(strt);
	nmeaDecoded_p->nmeaData.rmc.course_degrees = get_token_atof(strt);
	nmeaDecoded_p->nmeaData.rmc.date = get_token_atoi(strt);
	nmeaDecoded_p->nmeaData.rmc.magvar = get_token_atof(strt);
	nmeaDecoded_p->nmeaData.rmc.magvar_EW = get_token_char(strt);
	nmeaDecoded_p->nmeaData.rmc.mode = get_token_char(strt);
}


// example $GPGSV,3,1,09,09,,,24,06,,,14,19,,,19,16,,,21
// $GPGSV,3,2,09,24,,,23,08,,,23,22,,,21,20,,,21
// $GPGSV,3,3,09,18,,,21

// expects a valid zero terminated string
static void gnssDecodeGsv(char **strt, tNmeaDecoded *nmeaDecoded_p)
{
	nmeaDecoded_p->nmeaData.gsv.number_of_msg = get_token_atoi(strt);
	nmeaDecoded_p->nmeaData.gsv.msg_num = get_token_atoi(strt);
	nmeaDecoded_p->nmeaData.gsv.sat_in_view = get_token_atoi(strt);

    for (int i=0; i<sizeof(nmeaDecoded_p->nmeaData.gsv.sat)/sizeof(*nmeaDecoded_p->nmeaData.gsv.sat); i++)
    {
		nmeaDecoded_p->nmeaData.gsv.sat[i].id = get_token_atoi(strt);// sat id should never be zero
		nmeaDecoded_p->nmeaData.gsv.sat[i].elevation = get_token_atoi(strt);
		nmeaDecoded_p->nmeaData.gsv.sat[i].azimuth = get_token_atoi(strt);
		nmeaDecoded_p->nmeaData.gsv.sat[i].snr = get_token_atoi(strt);
    }
}


// example $GNGGA,121752.000,5201.8962,N,00505.7125,E,1,9,1.10,-11.5,M,47.1,M,,
// expects a valid zero terminated string
static void gnssDecodeGga(char **strt, tNmeaDecoded *nmeaDecoded_p)
{
	nmeaDecoded_p->nmeaData.gga.utc_time = get_token_atof(strt);
	nmeaDecoded_p->nmeaData.gga.latitude = get_token_atof(strt);
	nmeaDecoded_p->nmeaData.gga.NS = get_token_char(strt);
	nmeaDecoded_p->nmeaData.gga.longitude = get_token_atof(strt);
	nmeaDecoded_p->nmeaData.gga.EW = get_token_char(strt);
	nmeaDecoded_p->nmeaData.gga.fix =  get_token_atoi(strt);
	nmeaDecoded_p->nmeaData.gga.SatsUsed =  get_token_atoi(strt);
	nmeaDecoded_p->nmeaData.gga.HDOP = get_token_atof(strt);
	nmeaDecoded_p->nmeaData.gga.MSL_altitude = get_token_atof(strt);
	nmeaDecoded_p->nmeaData.gga.MSL_Units = get_token_char(strt);
	nmeaDecoded_p->nmeaData.gga.geoidalSeparation = get_token_atof(strt);
	nmeaDecoded_p->nmeaData.gga.geoidalSeparation_Units = get_token_char(strt);
	nmeaDecoded_p->nmeaData.gga.AgeOfDiffCorr =  get_token_atoi(strt);
}

// example $GPGSA,A,3,30,05,07,13,20,28,,,,,,,2.43,1.05,2.19
// expects a valid zero terminated string
static void gnssDecodeGsa(char **strt, tNmeaDecoded *nmeaDecoded_p)
{
	nmeaDecoded_p->nmeaData.gsa.mode_1 = get_token_char(strt);
	nmeaDecoded_p->nmeaData.gsa.mode_2 = get_token_atoi(strt);
    for (int i=0; i<sizeof(nmeaDecoded_p->nmeaData.gsa.Satellite_Used)/sizeof(*nmeaDecoded_p->nmeaData.gsa.Satellite_Used); i++)
    {
        nmeaDecoded_p->nmeaData.gsa.Satellite_Used[i] = get_token_atoi(strt);
    }
	nmeaDecoded_p->nmeaData.gsa.PDOP = get_token_atof(strt);
	nmeaDecoded_p->nmeaData.gsa.HDOP = get_token_atof(strt);
	nmeaDecoded_p->nmeaData.gsa.VDOP = get_token_atof(strt);
}

// example $GNVTG,227.15,T,,M,0.14,N,0.27,K,A
// expects a valid zeroterminated string
static void gnssDecodeVtg(char **strt, tNmeaDecoded *nmeaDecoded_p)
{
	nmeaDecoded_p->nmeaData.vtg.course1 = get_token_atof(strt);
	nmeaDecoded_p->nmeaData.vtg.reference1 = get_token_char(strt);
	nmeaDecoded_p->nmeaData.vtg.course2 = get_token_atof(strt);
	nmeaDecoded_p->nmeaData.vtg.reference2 = get_token_char(strt);
	nmeaDecoded_p->nmeaData.vtg.speed1 = get_token_atof(strt);
	nmeaDecoded_p->nmeaData.vtg.speed_unit1 = get_token_char(strt);
	nmeaDecoded_p->nmeaData.vtg.speed2 = get_token_atof(strt);
	nmeaDecoded_p->nmeaData.vtg.speed_unit2 = get_token_char(strt);
	nmeaDecoded_p->nmeaData.vtg.mode = get_token_char(strt);
}

/*
 * gnssDecode
 *
 * @desc    find out what message came out of the GNSS module
 *
 * @param   gnss_buf pointer to the message
 *
 * @return false when something went wrong.
 */

static bool gnssDecode(struct gnss_buf_str *  gnss_buf)
{
	const char sCLR_EPO[] = "$CLR,EPO,";
	const char sCDACK[] = "$CDACK,";

	// first check for a manufacturer proprietary command/response
	if (gnss_buf->buf[1] == 'P')
	{
		return (0 == handleCallback(gnss_cb_proprietary, gnss_buf));
	}

	if(0 == strncmp((char*)gnss_buf->buf, sCLR_EPO, strlen(sCLR_EPO)))
	{
		// handle code returned by 5.1.2+ firmware
		return true;
	}

	if(0 == strncmp((char*)gnss_buf->buf, sCDACK, strlen(sCDACK)))
	{
		// handle code returned by 5.1.2+ firmware
		return true;
	}

	// is it a message we should know ?
	for (int idx = 0; idx < sizeof(nmea_codes)/sizeof(*nmea_codes); idx++)
	{
		if (0 == strncmp((char *) &gnss_buf->buf[3], nmea_codes[idx].code, strlen(nmea_codes[idx].code) ))
		{
			// found the one
			char * strt = (char *) &gnss_buf->buf[7];// just after the '$GNRMC,'
			tNmeaDecoded nmeaDecoded;

			if (logmsg[nmea_codes[idx].type] & GNSSLOG_INCOMMING)
			{
				LOG_DBG( LOG_LEVEL_GNSS,"decode %s : %s\n", nmea_codes[idx].code, gnss_buf->buf);
			}
			memset(&nmeaDecoded,0,sizeof(nmeaDecoded));

			nmeaDecoded.type = nmea_codes[idx].type;
			nmeaDecoded.system = gnss_buf->buf[2];
			if(!nmea_codes[idx].gnssDecode)
			{
				// known message type but no decode available
				return false;
			}

			// Note the called function does not check for a valid format but that should not happen
			// as we have only attempted to parse a string with a valid CRC"
			nmea_codes[idx].gnssDecode(&strt, &nmeaDecoded);
			uint8_t rc = handleNmeaResponse(&nmeaDecoded);

            if (Device_PMICcontrolGPS())
            {
                MT3333_giveSemAcK(); // (JJ) If HW-REV >= 13 PMIC can control GNSS module - this is needed to avoid MK24 restarting GNSS module
            }
		    return ((0 == rc) && (0 == handleCallback(gnss_cb_nmea, &nmeaDecoded)));
		}
	}


#ifdef DEBUG
	if (dbg_logging & LOG_LEVEL_GNSS)
	{
		printf("gnss: ");
		for (int i = 0; i < gnss_buf->idx; i++)
		{
			put_ch(gnss_buf->buf[i]);
		}
	}
#endif

	return false;
}

/*
 *  END decoding known NMEA messages, to move to separate file !
 *
 */

// check if HDOP has become accurate enough
// return true when the semaphore is released in this function
static bool checkHDOP(tNmeaDecoded * nmeaDecoded_p)
{
    bool semReleased = false;
    if (gnssStatus.collectedData.valid && (gnssStatus.collectedData.HDOP !=0.0) &&  (gnssStatus.collectedData.HDOP <= gnssStatus.HDOPlimit) )
    {
        // time to first accurate fix is noteworthy statistical data
        if (gnssStatus.collectedData.firstAccurateFixTimeMs == 0)
        {
            gnssStatus.collectedData.firstAccurateFixTimeMs = gnssStatus.collectedData.currentFixTimeMs ;

            // we do not know what happens in the callback, so we must release the semaphore here,
            // and we can, because we updated the struct data already !
            gnss_Unlock();
            semReleased = true;
            /* rc_ok = */ handleCallback(gnss_cb_first_accurate_fix, nmeaDecoded_p);// ignore return status for now
        }
    }
    else
    {
        // we lost fix or accuracy, reset ??
        // gnssStatus.collectedData.firstAccurateFixTimeMs = 0;
    }
    return semReleased;
}



// 300ms and no longer ?
#define MAXGNSS_STATUSSEMWAIT (300)

static uint8_t handleNmeaResponse(tNmeaDecoded * nmeaDecoded_p)
{
    bool semReleased = false;

    if (!gnss_Lock(MAXGNSS_STATUSSEMWAIT, __func__))
    {
        return 0xff;
    }

    switch (nmeaDecoded_p->type)
    {
    case nmea_RMC:
        gnssStatus.collectedData.valid = nmeaDecoded_p->nmeaData.rmc.valid;
        gnssStatus.collectedData.utc_time = nmeaDecoded_p->nmeaData.rmc.utc_time;
        gnssStatus.collectedData.utc_date = nmeaDecoded_p->nmeaData.rmc.date;
        gnssStatus.collectedData.latitude = nmeaDecoded_p->nmeaData.rmc.latitude;
        gnssStatus.collectedData.NS = nmeaDecoded_p->nmeaData.rmc.NS;
        gnssStatus.collectedData.longitude = nmeaDecoded_p->nmeaData.rmc.longitude;
        gnssStatus.collectedData.EW = nmeaDecoded_p->nmeaData.rmc.EW;
        gnssStatus.collectedData.speed_knots = nmeaDecoded_p->nmeaData.rmc.speed_knots;
        gnssStatus.collectedData.course_degrees = nmeaDecoded_p->nmeaData.rmc.course_degrees;
#ifdef GNSS_FIRST_VALID_TIME
        // 'no time', gets date of 1980 from this gnss unit, RMC only reports two dyear digits, we assume that we must add 2000
        {
            bool validYear = (gnssStatus.collectedData.utc_date % 100) < 80;
            if (validYear)
            {
                if (!gnssStatus.collectedData.validTime)
                {
                    uint32_t utc = gnssUtcConvert(gnssStatus.collectedData.utc_time, gnssStatus.collectedData.utc_date);
                    LOG_DBG(LOG_LEVEL_GNSS,"GNSS: first valid time %s\n", RtcUTCToString(utc));
                    SetRTCFromGnssData(&gnssStatus.collectedData);
                    gnssStatus.collectedData.validTime = true;
                }
            }
            else
            {
                gnssStatus.collectedData.validTime = false;// lost the time...
            }
        }
#endif

        {
            uint16_t idx;
            idx = gnssStatus.history.idx ; // points to oldest value, to overwritten by the current measurement

#ifdef GNSS_MEANSPEED
            if (gnssStatus.history.count ==0)
            {
                // first data to enter, init some stuff, maybe better done elsewhere
                gnssStatus.history.k=0;
                gnssStatus.history.n=0;
                gnssStatus.history.ex=0;
                gnssStatus.history.ex2=0;
                //printf("mean_speed start\n");
            }
            if (gnssStatus.history.count == MAXGNSS_SPEEDHIST)
            {
                // we have to remove the oldest value from the list

                if (gnssStatus.history.data[idx].valid)
                {
                    float tmpf = gnssStatus.history.data[idx].speed - gnssStatus.history.k;
                    gnssStatus.history.n -= 1;
                    gnssStatus.history.ex -= tmpf;
                    gnssStatus.history.ex2 -= tmpf*tmpf;
                    //printf("idx=%d n=%d remove %7.3f\n",idx, gnssStatus.history.n, gnssStatus.history.data[idx].speed);
                }
            }
            // add this measurement (if valid of coarse)
            if (gnssStatus.collectedData.valid)
            {
                float tmpf;
                if (gnssStatus.history.n == 0) gnssStatus.history.k = gnssStatus.collectedData.speed_knots;
                tmpf = gnssStatus.collectedData.speed_knots - gnssStatus.history.k;
                gnssStatus.history.n += 1;
                gnssStatus.history.ex += tmpf;
                gnssStatus.history.ex2 += tmpf*tmpf;
                //printf("     n=%d add %7.3f\n", gnssStatus.history.n, gnssStatus.collectedData.speed_knots);

            }

            if (gnssStatus.history.n > 1)
            {
                gnssStatus.history.mean_speed = gnssStatus.history.k + gnssStatus.history.ex / gnssStatus.history.n;
                gnssStatus.history.std_speed  = sqrt((gnssStatus.history.ex2 - (gnssStatus.history.ex*gnssStatus.history.ex)/gnssStatus.history.n)/(gnssStatus.history.n -1 ));
            }
            else
            {
                gnssStatus.history.mean_speed = 0;
                gnssStatus.history.std_speed = 0;
            }
            // hidden feature !!!
            if (logmsg[nmea_RMC] & 4)
            {
                LOG_DBG(LOG_LEVEL_GNSS,"utc= %10.3f HDOP= %5.2f, course= %6.2f deg, V= %7.3f, (%2d) mean= %7.3f, std= %7.3f [km/h] lat= %5.4f, lon= %6.4f\n",
                        gnssStatus.collectedData.utc_time,
                        gnssStatus.collectedData.HDOP,
                        gnssStatus.collectedData.course_degrees,
                        gnssStatus.collectedData.speed_knots* 1.852,// convert knots to km/h
                        gnssStatus.history.n,
                        gnssStatus.history.mean_speed* 1.852,
                        gnssStatus.history.std_speed* 1.852,
                        gnssStatus.collectedData.latitude,
                        gnssStatus.collectedData.longitude
                        );
            }

#endif
            idx = gnssStatus.history.idx;
            gnssStatus.history.data[idx].valid = gnssStatus.collectedData.valid;
            gnssStatus.history.data[idx].speed = gnssStatus.collectedData.speed_knots;
            gnssStatus.history.data[idx].utc = gnssStatus.collectedData.utc_time;
            gnssStatus.history.data[idx].HDOP = gnssStatus.collectedData.HDOP;
            gnssStatus.history.data[idx].lat = gnssStatus.collectedData.latitude;
            gnssStatus.history.data[idx].NS = gnssStatus.collectedData.NS;
            gnssStatus.history.data[idx].lon = gnssStatus.collectedData.longitude ;
            gnssStatus.history.data[idx].EW = gnssStatus.collectedData.EW;
            if (++gnssStatus.history.idx >= MAXGNSS_SPEEDHIST) gnssStatus.history.idx=0;
            if (gnssStatus.history.count < MAXGNSS_SPEEDHIST) gnssStatus.history.count++;
        }
        gnssStatus.collectedData.currentFixTimeMs = xTaskGetTickCount()/portTICK_PERIOD_MS;

        if (gnssStatus.collectedData.valid)
        {
            // time to first fix is noteworthy statistical data
            if (gnssStatus.collectedData.firstFixTimeMs == 0)
            {
                gnssStatus.collectedData.firstFixTimeMs = gnssStatus.collectedData.currentFixTimeMs ;

                // we do not know what happens in the callback, so we must release the semaphore here,
                // and we can, because we updated the struct data already !
                gnss_Unlock();
                semReleased = true;
                LOG_DBG(LOG_LEVEL_GNSS,"GNSS: first position fix in %d seconds\n", (gnssStatus.collectedData.firstFixTimeMs - gnssStatus.collectedData.powerupTimeMs)/1000);
            }

        }

        if (logmsg[nmea_RMC] & GNSSLOG_DECODED)
        {
            LOG_DBG(LOG_LEVEL_GNSS, "RMC: system=%c, utc=%8.3f valid=%d\n"
            						"     lat=%5.4f %c, lon=%6.4f %c, speed=%5.2f knots, course=%4.2f\n"
            						"     date=%d, magvar=%3.2f degrees %c, mode=%c\n\n",
                    nmeaDecoded_p->system,
                    nmeaDecoded_p->nmeaData.rmc.utc_time,
                    nmeaDecoded_p->nmeaData.rmc.valid,
                    nmeaDecoded_p->nmeaData.rmc.latitude,
                    nmeaDecoded_p->nmeaData.rmc.NS,
                    nmeaDecoded_p->nmeaData.rmc.longitude,
                    nmeaDecoded_p->nmeaData.rmc.EW,
                    nmeaDecoded_p->nmeaData.rmc.speed_knots,
                    nmeaDecoded_p->nmeaData.rmc.course_degrees,
                    nmeaDecoded_p->nmeaData.rmc.date,
                    nmeaDecoded_p->nmeaData.rmc.magvar,
                    nmeaDecoded_p->nmeaData.rmc.magvar_EW,
                    nmeaDecoded_p->nmeaData.rmc.mode);
        }
        break;

    case nmea_GSV:
        {
            struct gnss_satInfo *satInfo = NULL;
            if (nmeaDecoded_p->system == sat_gps)
            {
                satInfo = &gnssStatus.gpsSat;
            }
            if (nmeaDecoded_p->system == sat_glonass)
            {
                satInfo = &gnssStatus.glonassSat;
            }
            if (satInfo != NULL)
            {
                //LOG_DBG(LOG_LEVEL_GNSS, "GSV: Rxed & decoded, msg num %d / total msg %d.....\n",nmeaDecoded_p->nmeaData.gsv.msg_num, nmeaDecoded_p->nmeaData.gsv.number_of_msg);
                if (nmeaDecoded_p->nmeaData.gsv.msg_num == 1)
                {
                    // first message of a set, clear array's
                    memset(satInfo, 0, sizeof(*satInfo));
                    satInfo->numsat = 0;// just to make it very obvious
                }

                for (int i=0; i< 4 ; i++)
                {
                    if (nmeaDecoded_p->nmeaData.gsv.sat[i].id == 0 ) break;
                    if (satInfo->numsat < MAX_SATID && satInfo->numsat < nmeaDecoded_p->nmeaData.gsv.sat_in_view)
                    {
                    	// rangecheck, we do not wwant to write outside the boundaries
                        satInfo->id[satInfo->numsat] = nmeaDecoded_p->nmeaData.gsv.sat[i].id;
                        satInfo->snr[satInfo->numsat] = nmeaDecoded_p->nmeaData.gsv.sat[i].snr;
                        satInfo->numsat++;
                    }
                }
                if (nmeaDecoded_p->nmeaData.gsv.msg_num == nmeaDecoded_p->nmeaData.gsv.number_of_msg)
                {
                    // last of the set of messages, copy to collected data

                    if (nmeaDecoded_p->system == sat_gps)
                    {
                        gnssStatus.collectedData.gpsSat = gnssStatus.gpsSat;//struct copy !
                    }
                    if (nmeaDecoded_p->system == sat_glonass)
                    {
                        gnssStatus.collectedData.glonassSat = gnssStatus.glonassSat;//struct copy !
                    }
                }
            }
        }
        if (logmsg[nmea_GSV] & GNSSLOG_DECODED)
        {
            LOG_DBG(LOG_LEVEL_GNSS, "GSV: system=%c numOfMsg=%2d msgNum=%2d satsInView = %3d\n"
                    				"     id  el  az  snr\n",
                    nmeaDecoded_p->system,
                    nmeaDecoded_p->nmeaData.gsv.number_of_msg,
                    nmeaDecoded_p->nmeaData.gsv.msg_num,
                    nmeaDecoded_p->nmeaData.gsv.sat_in_view);

            for (int i = 0; i < 4 ; i++)
            {
                if (nmeaDecoded_p->nmeaData.gsv.sat[i].id == 0) break;

                LOG_DBG(LOG_LEVEL_GNSS,"    %3d %3d %3d %3d\n",
                        nmeaDecoded_p->nmeaData.gsv.sat[i].id,
                        nmeaDecoded_p->nmeaData.gsv.sat[i].elevation,
                        nmeaDecoded_p->nmeaData.gsv.sat[i].azimuth,
                        nmeaDecoded_p->nmeaData.gsv.sat[i].snr
                        );
            }
        }

        break;

    case nmea_GGA:
        gnssStatus.collectedData.HDOP = nmeaDecoded_p->nmeaData.gga.HDOP;
        semReleased = checkHDOP(nmeaDecoded_p);

        if (logmsg[nmea_GGA] & GNSSLOG_DECODED)
        {
            LOG_DBG(LOG_LEVEL_GNSS,"GGA: system=%c utc=%6.3f lat=%5.4f %c, lon=%6.4f %c, fix=%d SatsUsed=%d\n",
                    nmeaDecoded_p->system,
                    nmeaDecoded_p->nmeaData.gga.utc_time,
                    nmeaDecoded_p->nmeaData.gga.latitude,
                    nmeaDecoded_p->nmeaData.gga.NS,
                    nmeaDecoded_p->nmeaData.gga.longitude,
                    nmeaDecoded_p->nmeaData.gga.EW,
                    nmeaDecoded_p->nmeaData.gga.fix,
                    nmeaDecoded_p->nmeaData.gga.SatsUsed
                    );

            LOG_DBG(LOG_LEVEL_GNSS,"     HDOP=%6.2f, MSL_Alt=%6.2f %c, geoSep=%6.2f %c, age=%d\n",
                    nmeaDecoded_p->nmeaData.gga.HDOP,
                    nmeaDecoded_p->nmeaData.gga.MSL_altitude,
                    nmeaDecoded_p->nmeaData.gga.MSL_Units,
                    nmeaDecoded_p->nmeaData.gga.geoidalSeparation,
                    nmeaDecoded_p->nmeaData.gga.geoidalSeparation_Units,
                    nmeaDecoded_p->nmeaData.gga.AgeOfDiffCorr);
        }
        break;

    case nmea_GSA:
        gnssStatus.collectedData.HDOP = nmeaDecoded_p->nmeaData.gsa.HDOP;
        semReleased = checkHDOP(nmeaDecoded_p);

        if (logmsg[nmea_GSA] & GNSSLOG_DECODED)
        {
            LOG_DBG(LOG_LEVEL_GNSS, "GSA: system=%c, mode_1=%c, mode_2=%d\n"
            						"     SatUsed ",
                    nmeaDecoded_p->system,
                    nmeaDecoded_p->nmeaData.gsa.mode_1,
                    nmeaDecoded_p->nmeaData.gsa.mode_2
                    );
            for (int i=0; i< sizeof(nmeaDecoded_p->nmeaData.gsa.Satellite_Used)/sizeof(*nmeaDecoded_p->nmeaData.gsa.Satellite_Used); i++)
            {
                if (nmeaDecoded_p->nmeaData.gsa.Satellite_Used[i])
                {
                	LOG_DBG(LOG_LEVEL_GNSS," %3d", nmeaDecoded_p->nmeaData.gsa.Satellite_Used[i]);
                }
            }

            LOG_DBG(LOG_LEVEL_GNSS, "\n"
            						"     PDOP=%4.2f, HDOP=%4.2f, VDOP=%4.2f\n",
                    nmeaDecoded_p->nmeaData.gsa.PDOP,
                    nmeaDecoded_p->nmeaData.gsa.HDOP,
                    nmeaDecoded_p->nmeaData.gsa.VDOP
                );
        }
        break;

    case nmea_VTG:
        if (logmsg[nmea_VTG] & GNSSLOG_DECODED)
        {
            LOG_DBG(LOG_LEVEL_GNSS, "VTG: system=%c, course1=%6.2f ref1 %c, course2=%6.2f ref2 %c,\n"
            						"     speed=%6.2f %c, %6.2f %c, mode=%c\n",
                    nmeaDecoded_p->system,
                    nmeaDecoded_p->nmeaData.vtg.course1,
                    nmeaDecoded_p->nmeaData.vtg.reference1,
                    nmeaDecoded_p->nmeaData.vtg.course2,
                    nmeaDecoded_p->nmeaData.vtg.reference2,
                    nmeaDecoded_p->nmeaData.vtg.speed1,
                    nmeaDecoded_p->nmeaData.vtg.speed_unit1,
                    nmeaDecoded_p->nmeaData.vtg.speed2,
                    nmeaDecoded_p->nmeaData.vtg.speed_unit2,
                    nmeaDecoded_p->nmeaData.vtg.mode

                    );
        }
        break;

    default:
        LOG_DBG(LOG_LEVEL_GNSS,"handleNmeaResponse not yet for type %d\n",nmeaDecoded_p->type);
        break;
    }

    // we are done
    if (semReleased == false)
    {
    	gnss_Unlock();
    }

    return 0;
}

static uint8_t handleFirstAccurateFixResponse( void * params)
{
    //tNmeaDecoded * nmeaDecoded_p = (tNmeaDecoded *)  params;

    LOG_DBG(LOG_LEVEL_GNSS,"GNSS: first Accurate position fix (HDOP=%f) in %d seconds\n", gnssStatus.collectedData.HDOP, (gnssStatus.collectedData.firstAccurateFixTimeMs - gnssStatus.collectedData.powerupTimeMs)/1000);

    return 0;
}

/**
 * taskGnss_Init
 *
 * @brief Initialize the GNSS task and its resources
 *
 */
void taskGnss_Init(bool nodeAsATestBox)
{
    /*
     * Initialize task resources
     */
    // Create event queue
    _EventQueue_Gnss         = xQueueCreate( EVENTQUEUE_NR_ELEMENTS_GNSS, sizeof(t_GnssEvent));
    vQueueAddToRegistry(_EventQueue_Gnss, "_EVTQ_GNSS");
    // create binary event queue
    _EventQueue_GnssBin      = xQueueCreate( EVENTQUEUE_NR_ELEMENTS_GNSS, sizeof(t_GnssEvent));
    vQueueAddToRegistry(_EventQueue_GnssBin, "_EVTQ_GNSS_BIN");


    gnssStatus.semAccess =  xSemaphoreCreateBinary(  );
    gnssStatus.HDOPlimit = 2.0 /* 9999 */;// start very high, means no limit at all

    GnssInitCallBack();

    // Create task
    xTaskCreate( taskGnss,               // Task function name
                 "GNSS",                  // Task name string
                 STACKSIZE_XTASK_GNSS,    // Allocated stack size on FreeRTOS heap
				 (void*)nodeAsATestBox,   // (void*)pvParams
                 PRIORITY_XTASK_GNSS,     // Task priority
                 &_TaskHandle_Gnss );     // Task handle
}

/*
 * according the MT3333 command packet manual
 *
Supported NMEA Sentences
0 NMEA_SEN_GLL, // GPGLL interval - Geographic Position - Latitude longitude
1 NMEA_SEN_RMC, // GPRMC interval - Recommended Minimum Specific GNSS Sentence
2 NMEA_SEN_VTG, // GPVTG interval - Course over Ground and Ground Speed
3 NMEA_SEN_GGA, // GPGGA interval - GPS Fix Data
4 NMEA_SEN_GSA, // GPGSA interval - GNSS DOPS and Active Satellites
5 NMEA_SEN_GSV, // GPGSV interval - GNSS Satellites in View
6 //Reserved
7 //Reserved
13 //Reserved
14 //Reserved
15 //Reserved
16 //Reserved
17 NMEA_SEN_ZDA, // GPZDA interval – Time & Date
18 NMEA_SEN_MCHN, // PMTKCHN interval – GPS channel status

 *
 *
 */


/*
 * firmware version "$PMTK605*31\r\n"
 *
 *  // Packet Type: 314 PMTK_API_SET_NMEA_OUTPUT
    // 1 =  NMEA_SEN_RMC, GPRMC interval - Recommended Minimum Specific GNSS Sentence
    "$PMTK314,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0*29\r\n";
 *
 */

/*-------------------------------------------------------------------------------------*
 |                                                                                     |
 *-------------------------------------------------------------------------------------*/
void taskGnss(void *pvParameters)
{

    t_GnssEvent theGnssEvent;
    bool nodeIsATestBox = (bool)pvParameters;

    //tPvParams   *pvParams = (tPvParams*)pvParameters;

    uint32_t lastqueuefullcounter = 0;
    /*
     * Init
     */

    // clear/init history data
    memset(& gnssStatus.history,0, sizeof(gnssStatus.history));

    // GnssSetCallback(gnss_cb_proprietary, handleUnknownResponse);
    // TODO: next callback should be modified, as soon as the real routine is written !
    GnssSetCallback(gnss_cb_first_accurate_fix, handleFirstAccurateFixResponse);

#ifdef GNSS_FIRST_VALID_TIME
    gnssStatus.collectedData.validTime = false;
#endif
    gnssStatus.state = GNSSSTATE_DOWN;

    LOG_DBG(LOG_LEVEL_GNSS,"\nGNSS task has started\n");

    gnss_Unlock(); // access to status struct now allowed

    // TODO: move this call to somewhere in the application
    extern bool MT3333_init(bool nodeIsATestBox);
    MT3333_init(nodeIsATestBox);

    while (1)
    {
        if (xQueueReceive( _EventQueue_Gnss, &theGnssEvent, 10000/portTICK_PERIOD_MS  /* portMAX_DELAY */) )
        {
            switch(theGnssEvent.Descriptor)
            {
             case Gnss_Evt_GnssDataReceived:
            	 //JMCR printf("%s(%d) @ 0x secure%08X\n", __func__, theGnssEvent.processing->idx, &theGnssEvent.processing->buf[0]);
                 // check message checksum
                 if (true == gnssNmeaValidateChecksum( theGnssEvent.processing))
                 {
                     // now, decode response
                     if(false == gnssDecode(theGnssEvent.processing))
                     {
                         LOG_DBG(LOG_LEVEL_GNSS,"GNSS error decoding : %s\n",theGnssEvent.processing->buf);
                     }

                 }
                 else
                 {
                     LOG_DBG(LOG_LEVEL_GNSS,"GNSS handler: NMEA message checksum failure\n");

                     BinarytoNMEAReset();
                 }
                 theGnssEvent.processing->idx = 0;// so the interrupt routine knows this buffer is 'free'
                 break;

             default:
                 LOG_DBG(LOG_LEVEL_GNSS,"GNSS handler: unknown event %d\n", theGnssEvent);
                 break;
             }

        }
        else
        {
            // queue receive failed
            if (queuefullcounter != lastqueuefullcounter)
            {
                LOG_DBG(LOG_LEVEL_GNSS,"Gnss Queue timeout, queuefullcounter = %d\n", queuefullcounter - lastqueuefullcounter);
                lastqueuefullcounter = queuefullcounter;
            }
        }

    } // while forever loop
}






/**
 * gnssSendNmeaCommand
 *
 * @desc    Sends a 'NMEA' command to the gnss
 *              the bare command is expected, the starting '$' and finishing checksum is generated in this function
 *
 * @param   cmd : the NMEA command (null terminated string) without the starting
 * @param   maxNMEAWait: how long to wait for the completion of the NMEA command
 *
 *
 *
 * @return false when something wrong
 */

bool gnssSendNmeaCommand( char * cmd,  uint32_t maxNMEAWaitMs)
{
    uint32_t maxWait = (maxNMEAWaitMs==portMAX_DELAY ) ? portMAX_DELAY : 2* maxNMEAWaitMs;// derive the freeRtos wait time from the maximum time the command should take
    bool rc_ok = false;

    if ( gnssStatus.state == GNSSSTATE_DOWN)
    {
        //  wrong state !
        LOG_DBG(LOG_LEVEL_MODEM, "%s: wrong  state : %d\n", __func__, gnssStatus.state );
    }
    else
    {
        // grab access semaphore
        if (gnss_Lock(maxWait, __func__))
        {
        	do
        	{
				uint8_t chksum = 0;
				char chksumbuf[] = "*00\r\n" ;// checksum and command termination template

				// start sending the starting '$'
				if(false == Gnss_put_ch('$'))
				{
					break;
				}

				// then the command
				if (false == Gnss_put_s((char *) cmd))
				{
					break;
				}

				// calculate checksum
				while (*cmd)
				{
					chksum ^= *cmd++;
				}

				// simplistic version of : %02X
				chksumbuf[1] = (chksum >>  4) > 9 ? (chksum >>  4) + ('A' - 10) : (chksum >>  4) + '0';
				chksumbuf[2] = (chksum & 0xf) > 9 ? (chksum & 0xf) + ('A' - 10) : (chksum & 0xf) + '0';

				// send the checksum and command termination to the serial port
				if(false == Gnss_put_s((char *) chksumbuf))
				{
					break;
				}
				// command successful so let the good people know
				rc_ok = true;
        	} while(0);

            // we are done, so unlock the resource
            gnss_Unlock();
        }
    }

    return rc_ok;
}

/**
 * gnssStartupModule
 *
 * @desc    Brings the driver and module in a usable state (poswerup, init state etc.)
 *
 * @param   baudrate: required baudrate for session (Usually 9600 for Version 3, 115200 for V4 onwards)
 *
 * @param   maxStartupWait: how long to wait for the completion of the NMEA command
 *
 * @return false when something wrong
 */

bool gnssStartupModule(uint32_t baudrate,  uint32_t maxStartupWait)
{
    bool rc_ok = false;

    if ( gnssStatus.state != GNSSSTATE_DOWN)
    {
        LOG_DBG(LOG_LEVEL_MODEM, "%s: wrong  state : %d\n", __func__, gnssStatus.state );
    }
    else
    {
        if (gnss_Lock(maxStartupWait, __func__))
        {
            Gnss_init_serial(GNSS_UART_IDX, baudrate);
            if (!Device_PMICcontrolGPS())
            {
                Gnss_ResetOn();
                Gnss_PowerOn();
                Gnss_ResetOff();
            }
            else
            {
                Gnss_PowerOn();
            }
            gnssStatus.state = GNSSSTATE_UP;

            gnssStatus.collectedData.powerupTimeMs = xTaskGetTickCount()/portTICK_PERIOD_MS;// TODO : take time which does not flip over (not needed for railway app, because it reboots every time)
            gnssStatus.collectedData.firstFixTimeMs = 0;// 0 means here  no fix yet,
            gnssStatus.collectedData.firstAccurateFixTimeMs = 0;
            gnssStatus.collectedData.currentFixTimeMs = 0;
#ifdef GNSS_FIRST_VALID_TIME
            gnssStatus.collectedData.validTime = false;
#endif
            // we are done, so unlock the resource
            gnss_Unlock();
            rc_ok =  true;
        }
    }
    return rc_ok;
}

/**
 * gnssShutdownModule
 *
 * @desc    Brings the driver and module in a unusable state (poswerdown.)
 *
 * @param   maxNMEAWait: how long to wait for the completion of the shutdown

 * @return false when something wrong
 */

bool gnssShutdownModule( uint32_t maxShutdownWaitMs)
{
    bool rc_ok = false;

    if (gnss_Lock(maxShutdownWaitMs, __func__))
    {
        Gnss_ResetOn();
        Gnss_PowerOff();
        gnssStatus.state = GNSSSTATE_DOWN;
        gnssStatus.collectedData.valid = false;

        // we are done, so unlock the resource
        gnss_Unlock();
        rc_ok = true;
    }

    return rc_ok;
}


/**
 * @brief   Copies the gnss collected data from the internal structure to the destination pointer
 *          To prevent halfway updated data, it is guarded by the access semaphore
 *
 * @param[out]   collectedData_p: destination pointer to the data struct
 * @param[in]   maxWaitMs: how long to wait for the completion of the shutdown
 * @return false when something wrong
 */
bool gnssRetrieveCollectedData( tGnssCollectedData * collectedData_p, uint32_t maxWaitMs )
{
    bool rc_ok = false;

    if (gnss_Lock(maxWaitMs, __func__))
    {
    	if (collectedData_p)
    	{
    		*collectedData_p = gnssStatus.collectedData;
    	}

        // we are done, so unlock the resource
        gnss_Unlock();
        rc_ok = true;
    }
    return rc_ok;
}

/**
 * gnssSetHDOPlimit
 *
 * @desc    set inmternal HDOP limit to use for signalling first fix
 *
 * @param   HDOP    : HDOP limit, when received HDOP is below this value, an event is generated
 *
 * @param   maxNMEAWait: how long to wait for the completion of the shutdown

 * @return false when something wrong
 */

bool gnssSetHDOPlimit( float HDOP, uint32_t maxWaitMs)
{
    bool rc_ok = false;

    if (gnss_Lock(maxWaitMs, __func__))
    {
		gnssStatus.HDOPlimit = HDOP;

		// we are done, so unlock the resource
		gnss_Unlock();
		rc_ok = true;
    }
    return rc_ok;
}

/**
 * Gnss_NotifyRxData_ISR
 *
 * @brief Notify gnss task to handle incoming message
 *
 * return   task start info for freeRtos
 *
 */
BaseType_t Gnss_NotifyBinRxData_ISR(struct gnss_buf_str * processing)
{
    t_GnssEvent evt;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    evt.Descriptor = Gnss_Evt_GnssDataReceived;
    evt.processing = processing;

    if ( pdPASS != xQueueSendToBackFromISR( _EventQueue_GnssBin, &evt, &xHigherPriorityTaskWoken ))
    {
    	queuefullcounter++;
    }

    return xHigherPriorityTaskWoken;
}

struct gnss_buf_str * Gnss_WaitBinEvent()
{
    t_GnssEvent theGnssEvent;
	if (xQueueReceive( _EventQueue_GnssBin, &theGnssEvent, 10000/portTICK_PERIOD_MS  /* portMAX_DELAY */) )
	{
		return theGnssEvent.processing;
	}
	return NULL;
}

/*
 * Gnss_NotifyRxData_ISR
 *
 * @brief Notify gnss task to handle incoming message
 *
 * return   task start info for freeRtos
 *
 */
BaseType_t Gnss_NotifyRxData_ISR(struct gnss_buf_str * processing)
{
    t_GnssEvent evt;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    evt.Descriptor = Gnss_Evt_GnssDataReceived;
    evt.processing = processing;

    if ( pdPASS != xQueueSendToBackFromISR( _EventQueue_Gnss, &evt, &xHigherPriorityTaskWoken ))
    {
    	queuefullcounter++;
    }

    return xHigherPriorityTaskWoken;
}

/**
 * gnss time format to time_t convertion
 * gnss time : hhmmss.sss
 * gnss date : ddmmyy
 *
 * time_t : seconds since 1970
 */

static time_t gnssUtcConvert(float gnss_utc, uint32_t gnss_date)
{
    // convert gnss time to unix time
    struct tm tmp_tm;
    uint32_t tmpi;
    time_t epochtime;

    tmpi = gnss_utc;
    tmp_tm.tm_sec = tmpi % 100; tmpi /= 100;
    tmp_tm.tm_min = tmpi % 100; tmpi /= 100;
    tmp_tm.tm_hour= tmpi;
    tmpi = gnss_date;
    tmp_tm.tm_year = (tmpi % 100) + 2000 - 1900; tmpi /= 100;
    tmp_tm.tm_mon  = (tmpi % 100) - 1   ; tmpi /= 100;// month in the tm_struct go from 0..11, don't ask why
    tmp_tm.tm_mday =  tmpi;
    tmp_tm.tm_isdst = 0;

    epochtime = mktime(&tmp_tm);// execution expensive ?

    return epochtime;
}

void print_gnss_status(tGnssStatus * stat)
{
    printf("GNSS task state = %d\n",stat->state);
    printf("powerupTime[ms]            = %d, firstFixTime[ms] = %d, diff[ms] = %d\n",stat->collectedData.powerupTimeMs, stat->collectedData.firstFixTimeMs, stat->collectedData.firstFixTimeMs != 0 ?  stat->collectedData.firstFixTimeMs - stat->collectedData.powerupTimeMs : 0);
    printf("CurrentFixTime[ms]         = %d, RMC data is%s valid.\n",stat->collectedData.currentFixTimeMs, stat->collectedData.valid ? " " : " NOT");
    printf("FirstAccurateFixTime[ms]   = %d\n",stat->collectedData.firstAccurateFixTimeMs);
    printf("utc = %9.3f, date= %d, -> unix_time = %lu\n",stat->collectedData.utc_time, stat->collectedData.utc_date, gnssUtcConvert(stat->collectedData.utc_time, stat->collectedData.utc_date ));
    printf("lat = %7.2f %c long = %7.2f %c, speed = %7.2f knots, course = %7.2f deg, HDOP = %7.2f\n",  stat->collectedData.latitude, stat->collectedData.NS, stat->collectedData.longitude, stat->collectedData.EW, stat->collectedData.speed_knots, stat->collectedData.course_degrees, stat->collectedData.HDOP );

    printf("(valid) utc HDOP speed[knots] latitude longitude\n");

    for (int i=0; i<MAXGNSS_SPEEDHIST; i++)
    {
        uint16_t idx = (stat->history.idx + i) % MAXGNSS_SPEEDHIST ;
        printf("%d %9.1f %7.2f %7.3f   %7.3f %c   %7.3f %c\n", stat->history.data[idx].valid, stat->history.data[idx].utc, stat->history.data[idx].HDOP, stat->history.data[idx].speed,
                stat->history.data[idx].lat, stat->history.data[idx].NS, stat->history.data[idx].lon, stat->history.data[idx].EW
                );
    }
#ifdef GNSS_MEANSPEED
    printf("samples = %d, mean_speed = %7.3f, std_speed = %7.3f knots\n",stat->history.n, stat->history.mean_speed, stat->history.std_speed);
#endif
    printf("GPS sat id  snr\n");
    for(int i=0; i<stat->collectedData.gpsSat.numsat; i++)
    {
        printf("[%2d] %3d  %3d\n",i+1, stat->collectedData.gpsSat.id[i],  stat->collectedData.gpsSat.snr[i]);
    }
    printf("GLONASS sat id  snr\n");
    for(int i=0; i<stat->collectedData.glonassSat.numsat; i++)
    {
        printf("[%2d] %3d  %3d\n",i+1, stat->collectedData.glonassSat.id[i],  stat->collectedData.glonassSat.snr[i]);
    }

    GnssIo_print_info();
}

bool cliGnss( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    bool rc_ok = true;

    if (args >= 1)
    {
        if (strcmp((const char*)argv[0], "reset") == 0)
        {
            pinConfigDigitalOut(Device_GNSS_ResetPin(), kPortMuxAsGpio, 0, false);
        	vTaskDelay(100);
            pinConfigDigitalOut(Device_GNSS_ResetPin(), kPortMuxAsGpio, 1, false);
            printf("gnss reset complete\n");
        }
        else if (strcmp((const char*)argv[0], "rtc") == 0)
        {
            rc_ok = MT3333_SendPmtkCommand( "PMTK435", 535, 10000);
        }
        else if (strcmp((const char*)argv[0], "info") == 0)
        {
            print_gnss_status(&gnssStatus);
        }
        else if (strcmp((const char*)argv[0], "epostatus") == 0)
        {
        	Ephemeris_CheckStatus(NULL);
    	}
        else if (strcmp((const char*)argv[0], "epoerase") == 0)
        {
        	Ephemeris_Erase();
        }
        else if (strcmp((const char*)argv[0], "epoload") == 0)
        {
            int result = Ephemeris_LoadAndCheck();
            if(result < 0)
            {
            	printf("LoadEphemeris failed, error = %d\n", result);
            }
        }
        else if (strcmp((const char*)argv[0], "epodebug") == 0)
        {
        	epoDebug = argi[1];
        	printf("epoDebug = %s\n", epoDebug ? "TRUE" : "FALSE");
        }
        else if (strcmp((const char*)argv[0], "mode") == 0)
        {
        	if(args > 1)
        	{
        		if((argi[1] == 0) || (argi[1] == 1))
        		{
					MT3333_Startup(-1);
					if(argi[1] == 0)
					{
						// OK, back into NMEA mode
						GNSS_BinarytoNMEA();
					}
					else
					{
						// OK, back into binary mode
						GNSS_NMEAtoBinary();
					}
					return true;
        		}
        	}
        	printf("gnss mode 0/1\n");
        }
#ifdef GNSS_UPGRADE_AVAILABLE
        else if (strcmp((const char*)argv[0], "dump") == 0)
        {
        	extern void GNSS_dump();
        	GNSS_dump();
        }
        else if (strcmp((const char*)argv[0], "upgrade") == 0)
        {
        	extern void GNSS_upgrade();
        	GNSS_upgrade();
        }
        else if (strcmp((const char*)argv[0], "erase") == 0)
        {
        	extern void GNSS_eraseFlash();
        	GNSS_eraseFlash();
        }
        else if (strcmp((const char*)argv[0], "setota") == 0)
        {
#include "extFlash.h"
#include "configbootloader.h"
    		gBootCfg.cfg.ImageInfoFromLoader.OTAmaxImageSize = 0x100000;
    		gBootCfg.cfg.ImageInfoFromLoader.OTAstartAddrForApp = 0xE00000;
        }
#endif
        else if (strcmp((const char*)argv[0], "easy") == 0)
        {
        	if(args > 1)
        	{
        		bool rc_ok = MT3333_SendPmtkCommand( (argi[1] == 1) ? "PMTK869,1,1" : "PMTK869,1,0", 0, 10000);
        		(void)rc_ok;
        	}
        	else
        	{
        		bool rc_ok = MT3333_SendPmtkCommand( "PMTK869,0", 869, 10000);
        		(void)rc_ok;
        	}
        }
        else if (strcmp((const char*)argv[0], "hdop") == 0)
        {
            if (args >= 2)
            {
                gnssSetHDOPlimit(atof((char *) argv[1]),1000) ;
            }
            else
            {
                printf("HDOP limit = %f\n", gnssStatus.HDOPlimit);
            }
        }

#if 0
        // disabled to not to be confused with the command of controlling module (MT3333 etc.)
        else if (strcmp((const char*)argv[0], "on") == 0)
        {
            if (false == gnssStartupModule(1000))
            {
                LOG_DBG( LOG_LEVEL_GNSS, "GNSS power on: failed\n");
                rc_ok = false;
            }
        }
        else if (strcmp((const char*)argv[0], "off") == 0)
        {
            if (false == gnssShutdownModule(1000))
            {
                LOG_DBG( LOG_LEVEL_GNSS, "GNSS power off: failed\n");
                rc_ok = false;
            }
        }
#endif
        else if (strcmp((const char*)argv[0], "logmsg") == 0)
        {
            if (args>=2)
            {
                // find out which message logging should change

                if (strcmp((const char*)argv[1], "all") == 0)
                {
                    // lets do all
                    if (args >=3)
                    {

                        for (int i=1; i<sizeof(logmsg); i++ )
                        {
                            logmsg[i] = argi[2];
                        }
                    }
                    else
                    {
                        for (int i=0; i< sizeof(nmea_codes)/sizeof(*nmea_codes); i++ )
                        {
                            printf("%s %d\n", nmea_codes[i].code, logmsg[nmea_codes[i].type]);
                        }
                    }
                } else {
                    //individual message
                    bool found = false;
                    for (int i=0; i< sizeof(nmea_codes)/sizeof(*nmea_codes); i++)
                    {
                        if (strcasecmp((const char*)argv[1],nmea_codes[i].code ) == 0)
                        {
                            // found
                            found = true;
                            if (args>=3)
                            {
                                logmsg[nmea_codes[i].type] = argi[2];
                            }
                            else
                            {
                                printf("%s %d\n", nmea_codes[i].code, logmsg[nmea_codes[i].type]);
                            }
                        }
                    }
                    if (!found)
                    {
                        printf("unknown message code %s (to list known use: gnss logmsg\n",argv[1]);
                        rc_ok = false;
                    }
                }
            }
            else
            {
                // printout current logging
                for (int i=0; i<sizeof(nmea_codes)/sizeof(*nmea_codes) ; i++)
                {
                        printf("  %s %d\n", nmea_codes[i].code, logmsg[nmea_codes[i].type]);
                }
            }
        }
        else if (argv[0][0] == '$')
        {
            // NMEA command
            if (false == gnssSendNmeaCommand( (char *) &argv[0][1], 10000))
            {
                LOG_DBG( LOG_LEVEL_GNSS, "GNSS command failed: %s\n", &argv[0][1]);
                rc_ok = false;
            }
        }
    }
    return rc_ok;
}

/**
 * CLI command list
 */
bool cliGnssLongHelp( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    printf( "actions:\n"
    		"  reset\t\t\treset the GNSS device\n"
    		"  rtc\t\t\tdisplay GNSS RTC time\n"
    		"  easy\t\t\tdisplay EASY status\n"
            "  info\n"
    		"  mode 0-binaryToNMEA, 1-NMEAtoBinary\n"
			"  epostatus\t\tread EPO status\n"
			"  epoerase\t\terase EPO\n"
			"  epoload\t\tload EPO\n"
			"  epodebug <0/1>\treset/set debug flag\n"
 //           "  on/off\t\tpower module on/off\n"
            "  hdop <val>\t\tget/set HDOP limit\n"
            "  logmsg <type> <0..3>\tset logging levels\n"
            "  $<cmd>\t\tsend NMEA command to module (checksum will be added!)\n"
    		);
    return true;
}

static const struct cliCmd gnssCommands[] =
{
        GNSS_CLI,
};

bool gnssCliInit()
{
    return cliRegisterCommands(gnssCommands , sizeof(gnssCommands)/sizeof(*gnssCommands));
}


#ifdef __cplusplus
}
#endif