#ifdef __cplusplus
extern "C" {
#endif

/*
 * gnssMT3333.c
 *
 *  Created on: Feb 13, 2017
 *      Author: ka3112
 */


#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <timers.h>
#include <portmacro.h>

#include <Resources.h>
#include <xTaskDefs.h>



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
#include "gnssIo.h"
#include "configGnss.h"
#include "gnssMT3333.h"
#include "CLIcmd.h"
#include "printgdf.h"
#include "CLIcmd.h"
#include "NvmData.h"
#include "device.h"

extern bool	BinarytoNMEAReset();

/*
 * Mediatek MT3333 handling, these are special commands/protocols not part of the general NMEA messages
 * see also the 'MT3333 PMTK Command Packet .pdf'
 * The commands $PMTK<whatever> give a ACK back, we should wait for that.
 *
 * minimal command/response handling implemented, too many different responses to decode them all
 */

#define MT3333_MAXTXTMSG (64)

typedef struct {
    tPmtkType type;
    union {
        struct {
            uint16_t cmd;
            uint16_t flag;
        } ack;

        struct {
            uint16_t msg;
        } sys_msg;

        struct {
            char msg[MT3333_MAXTXTMSG];// I have no idea what size we can expect
        } txt_msg;


    } data;
} tPmtkDecoded;

typedef enum {
    mt3333_unknown = 0,
    mt3333_starting_up,
    mt3333_up
} tMT3333_state;



struct {
    tMT3333_state     state;
    SemaphoreHandle_t semAcc;// access for controlling only one MT3333 action at the same time
    SemaphoreHandle_t semAck;
    tPmtkType         waitingFor;
    uint16_t          lastPMTKcommandnum;
    tPmtkDecoded      lastResponse;
    bool              nodeIsATestBox;	// set to true when node is being used as a test box
} MT3333_data = {
    .semAck = NULL,
    .semAcc = NULL,
	.nodeIsATestBox = false
};

void MT3333_giveSemAcK(void )
{
    xSemaphoreGive(MT3333_data.semAck);// we should use sendqueue with the message in it, but that is for when we do more with this chip
};

//
// status struct with latest module message info processed !
//


//
// callback function called from the gnss task to handle the $P<whatever messages>
//
static uint8_t handleProprietaryResponse( void * params)
{
    struct gnss_buf_str * gnss_buf = (struct gnss_buf_str *) params;

    if (strncmp((char *) &gnss_buf->buf[1], "PMTKLOG", 7) == 0)
    {
        extern int get_token_atoi(char **);
        char *strt = (char *) &gnss_buf->buf[9];// just after the '$PMTKLOG,'
        LOG_DBG( LOG_LEVEL_GNSS,"decode $PMTK : %s\n",gnss_buf->buf);
        int serial = get_token_atoi(&strt);
        int type = get_token_atoi(&strt);
        char *mode = get_token(&strt, ',');
        int content = get_token_atoi(&strt);
        int interval = get_token_atoi(&strt);
        int distance = get_token_atoi(&strt);
        int speed = get_token_atoi(&strt);
        int status = get_token_atoi(&strt);
        int logNumber = get_token_atoi(&strt);
        int percent = get_token_atoi(&strt);
        printf( "  serial #   = %d\n"
        		"  type       = %s\n"
				"  mode       = %s\n"
				"  content    = %d\n"
				"  interval   = %d\n"
				"  distance   = %d\n"
				"  speed      = %d\n"
				"  status     = %s\n"
        		"  log number = %d\n"
				"  percent    = %d\n"
        		, serial
				, ((type) ? "Full Stop" : "Overlap")
				, mode
				, content
				, interval
				, distance
				, speed
				, ((status) ? "Stop Logging" : "Logging")
        		, logNumber
				, percent );
        MT3333_data.waitingFor = E_pmtk_ack;
        //xSemaphoreGive( MT3333_data.semAck  );// we should use sendqueue with the message in it, but that is for when we do more with this chip
    }
    else if (strncmp((char *) &gnss_buf->buf[1], "PMTK", 4) == 0)
    {
        char * val;
        char * strt ;
        tPmtkType type;
        tPmtkDecoded pmtkDecoded;

        memset(&pmtkDecoded,0,sizeof(pmtkDecoded));// clean at start, saves setting the zero for the string(s)

        LOG_DBG( LOG_LEVEL_GNSS,"decode $PMTK : %s\n",gnss_buf->buf);
        // decode $PMTK<number>,
        /*
         * example
          Packet Meaning:
            Acknowledge of PMTK command
          Data Field:
            PMTK001,Cmd,Flag
          Cmd: The command / packet type the acknowledge responds.
            Flag: ‘0’ = Invalid command / packet.
            ‘1’ = Unsupported command / packet type
            ‘2’ = Valid command / packet, but action failed
            ‘3’ = Valid command / packet, and action succeeded
          Example:
            $PMTK001,604,3*32<CR><LF>
         */
        type = (tPmtkType) atoi((char *) &gnss_buf->buf[5]);


        if (MT3333_data.waitingFor == type) {
            pmtkDecoded.type = type;
            strt = (char *) &gnss_buf->buf[9];// just after the '$PMTK000,'

            switch (type) {

            case E_pmtk_ack:
                val = get_token(&strt,',');
                if (val) {
                    pmtkDecoded.data.ack.cmd = atoi(val);
                    val = get_token(&strt,',');
                }
                if (val) {
                    pmtkDecoded.data.ack.flag = atoi(val);
                }
                if (pmtkDecoded.data.ack.cmd == MT3333_data.lastPMTKcommandnum)  {
                    MT3333_data.lastResponse = pmtkDecoded;// struct copy
                }
                break;

            case E_pmtk_sys_msg:
                val = get_token(&strt,',');
                if (val) {
                    pmtkDecoded.data.sys_msg.msg = atoi(val);
                    MT3333_data.lastResponse = pmtkDecoded;// struct copy
                }
                break;
            case E_pmtk_txt_msg:
                val = get_token(&strt,',');
                if (val) {
                    strncpy(pmtkDecoded.data.txt_msg.msg, val, sizeof(pmtkDecoded.data.txt_msg.msg)-1 );// -1 make sure we have a zero terminated string
                    MT3333_data.lastResponse = pmtkDecoded;// struct copy
                }
                break;

            case E_pmtk_epo_info:
                MT3333_data.lastResponse.type = type;
            	strncpy(MT3333_data.lastResponse.data.txt_msg.msg, (char *)&gnss_buf->buf[9], MT3333_MAXTXTMSG);
                break;

            default:
                MT3333_data.lastResponse.type = type;
            	strncpy(MT3333_data.lastResponse.data.txt_msg.msg, (char *)&gnss_buf->buf[9], MT3333_MAXTXTMSG);
                break;
            }
            xSemaphoreGive( MT3333_data.semAck  );// we should use sendqueue with the message in it, but that is for when we do more with this chip
        }
        else
        {
            LOG_DBG( LOG_LEVEL_GNSS,"MT3333 not expecting : %s\n",gnss_buf->buf);
        }

    }
    else if (strncmp((char *) &gnss_buf->buf[1], "PGACK,EPE,", 10) == 0)
    {
    	// EPE ACK
    	// LOG_DBG( LOG_LEVEL_GNSS,"MT3333 EPE response : %s\n",gnss_buf->buf)
    }
    else if (strncmp((char *) &gnss_buf->buf[1], "PGACK,", 5) == 0)
    {
    	// EPE ACK
        LOG_DBG( LOG_LEVEL_GNSS,"decode $PGACK : %s\n",gnss_buf->buf)
        xSemaphoreGive( MT3333_data.semAck  );// we should use sendqueue with the message in it, but that is for when we do more with this chip
    }
    else
    {
        // no PMTK, do not handle
        LOG_DBG( LOG_LEVEL_GNSS,"MT3333 unhandled $P : %s\n",gnss_buf->buf);
    }

    return 0;
}


// MT3333_init
//  init data structures, register callbacks.
//
// MT3333_powerup
//      call powerup of NMEA driver,
//      wait for powerup message
//
// MT3333_sendCommand
//          send PMTK command, and wait for 'ack' with timeout.
//
//  how to handle the MTK binary mode for ephemeris download ?

/*
 * MT3333_init
 *
 * @desc    initialize the datastructures needed (+ creating semaphore)
 *
 * @return false when something wrong
 */

bool MT3333_init(bool nodeIsATestBox)
{
    bool rc_ok = true;
    // init status struct
    MT3333_data.nodeIsATestBox = nodeIsATestBox;
    MT3333_data.state = mt3333_unknown;

    // create ack semaphore
   if (MT3333_data.semAck==NULL)  MT3333_data.semAck = xSemaphoreCreateBinary();
   if (MT3333_data.semAcc==NULL)  MT3333_data.semAcc = xSemaphoreCreateBinary();

   if (MT3333_data.semAck==NULL || MT3333_data.semAcc ==NULL) {
       rc_ok = false;
   } else {
       xSemaphoreGive(MT3333_data.semAcc);// start with 'available'
   }

   MT3333_data.waitingFor = E_pmtk_max;// we are not expecting anything yet

   GnssSetCallback(gnss_cb_proprietary, handleProprietaryResponse);// for the PMTK messages

   return rc_ok;
}

/*
 * MT3333_Startup
 *
 * @desc    starts up the gnss module, and waits for the signon message
 *
 * @param   maxStartupWait: how long to wait for the signon
 *
 * @return false when something wrong
 */
bool MT3333_Startup( uint32_t maxStartupWaitMs)
{
	const uint32_t baudrates[2][8] =
	{
			{ 9600, 115200, 0, 0, 0, 0, 0, 0 },
			{ 115200, 9600, 57600, 38400, 19200, 14400, 4800, 0 },
			// { 230400, 460800, 912600, 115200, 0, 0, 0, 0 },
	};

	int hw = (((Device_GetHardwareVersion() >= HW_PASSRAIL_REV4) || Device_HasPMIC()) ? 1 : 0);

	// is it a testbox?
	if((-1 == maxStartupWaitMs) || MT3333_data.nodeIsATestBox == true)
	{
		gnssStartupModule(baudrates[hw][0], 1000);
		MT3333_data.lastPMTKcommandnum = 0;
		MT3333_data.waitingFor = E_pmtk_max;
		MT3333_data.state = mt3333_up;
		LOG_DBG( LOG_LEVEL_GNSS,"\n%s: Passed For Test Box ONLY !!!\n", __func__);
		return true;
	}

	uint32_t storedBaud = gNvmData.dat.gnss.baud_rate;
	int i = 0;
	uint32_t try_baud = 0;

	if(storedBaud > 0)
	{
		// we will try the stored one first
		i = -1;
		try_baud = storedBaud;
	}
	else
	{
		try_baud = baudrates[hw][0];
	}

    bool rc_ok = false;

    do
    {
    	// GNSS should be down before we start
    	if (MT3333_data.state != mt3333_unknown)
		{
			LOG_DBG(LOG_LEVEL_MODEM,"%s: wrong  state : %d\n", __func__, gnssStatus.state );
			break;
		}

    	// take control of the GNSS
        if (pdTRUE != xSemaphoreTake(MT3333_data.semAcc , maxStartupWaitMs))
        {
            LOG_DBG( LOG_LEVEL_GNSS,"%s: access semtake failed\n", __func__);
            break;
        }

        // as long as we're not up and still got baudrates to try
        while(MT3333_data.state != mt3333_up && try_baud != 0)
        {
			// clear administration
			MT3333_data.lastPMTKcommandnum = 0;
			MT3333_data.waitingFor = E_pmtk_sys_msg;

			// clear semaphore
			xSemaphoreTake(MT3333_data.semAck , 0);

            LOG_DBG( LOG_LEVEL_GNSS,"%s: baudrate=%d\n", __func__, try_baud);
            if (!Device_PMICcontrolGPS())
            {
                gnssShutdownModule(1000);	// just in case
            }

            // try a baudrate
			if(gnssStartupModule(try_baud, maxStartupWaitMs))
			{
				MT3333_data.state = mt3333_starting_up;

				// wait for signon message from chip
				if (pdTRUE == xSemaphoreTake(MT3333_data.semAck , maxStartupWaitMs))
				{
					rc_ok = true;
				}
				else
				{
					// try again in case we are in binary mode but don't wait so long
					// clear administration
					MT3333_data.lastPMTKcommandnum = 0;
					MT3333_data.waitingFor = E_pmtk_sys_msg;

					BinarytoNMEAReset();
					if((-1 == i) && (pdTRUE == xSemaphoreTake(MT3333_data.semAck , maxStartupWaitMs/3)))
					{
						rc_ok = true;
					}
					else
					{
                        MT3333_data.state = mt3333_unknown;
                        MT3333_data.waitingFor = E_pmtk_max;
                        gnssShutdownModule(maxStartupWaitMs);// try to cleanup, unconditionally
                        LOG_DBG( LOG_LEVEL_GNSS,"%s: failed\n", __func__);
                    }
				}
				if(rc_ok)
				{
					MT3333_data.state = mt3333_up;
					LOG_DBG( LOG_LEVEL_GNSS,"%s: code %d\n", __func__, MT3333_data.lastResponse.data.sys_msg.msg);
				}
				// we are done waiting for this
				MT3333_data.waitingFor = E_pmtk_max;
			}

			if(MT3333_data.state != mt3333_up)
			{
				try_baud = baudrates[hw][++i];
			}
        }

        /*
         * Store new baud rate if different to before
         */
        if(try_baud != 0 && try_baud != storedBaud)
        {
        	gNvmData.dat.gnss.baud_rate = try_baud;
        }

		xSemaphoreGive(MT3333_data.semAcc);

    } while(0);

#if 0
    if(rc_ok)
    {
    	char buf[32];
    	sprintf(buf, "PMTK356,%d.%d",
    			((int)(gnssStatus.HDOPlimit * 100)) / 100,	// done this way because the sprintf
    			((int)(gnssStatus.HDOPlimit * 100)) % 100);	// function doesn't support floats
        MT3333_SendPmtkCommand(buf, 356, 1000);				// set HDOP value
        // MT3333_SendPmtkCommand( "PMTK352,0", 0, 1000);	// set QZSS only required by Japan
        MT3333_SendPmtkCommand("PMTK328,30", 0, 1000);		// set HACC to < 30m
    }
#endif

    return rc_ok;
}

/*
 * MT3333_shutdown
 *
 * @desc    just a wrapper around gnssShutdownModule which also sets the stat of the MT3333 part to 'off'
 *
 * @param   maxNMEAWait: how long to wait for the completion of the shutdown

 * @return false when something wrong
 */

bool MT3333_shutdown( uint32_t maxShutdownWait)
{
    bool rc_ok = false;

    if (pdTRUE != xSemaphoreTake(MT3333_data.semAcc , maxShutdownWait)) {
        LOG_DBG( LOG_LEVEL_GNSS,"MT3333_shutdown: access semtake failed\n");
        rc_ok = false;
    } else {

        rc_ok = gnssShutdownModule(maxShutdownWait);
        if (rc_ok) {
            MT3333_data.state = mt3333_unknown;
            MT3333_data.waitingFor = E_pmtk_max;
        }
        xSemaphoreGive(MT3333_data.semAcc);
    }

    // if we get a valid fix update lastPO.
    if(gnssStatus.collectedData.firstAccurateFixTimeMs)
    {
    	extern uint32_t GetDatetimeInSecs();
    	gNvmData.dat.gnss.lastPO = GetDatetimeInSecs();
    }

    return rc_ok;
}


/*
 * MT3333_SendPmtkCommand
 *
 * @desc    Sends a 'PMTK' command to the gnss
 *              the bare command is expected, the starting '$' and finishing checksum is generated in this function
 *              it waits for the acknowledge from the chip (with specified timeout)
 *
 * @param   cmd : the PMTK command (null terminated string) without the starting
 * @param   type: 0 = look for an ack else look for the response packet type
 * @param   maxPMTKWait: how long to wait for the completion/ack of the PMTK command
 *
 * @return false when something wrong
 */

bool MT3333_SendPmtkCommand( char * cmd,  uint32_t type, uint32_t maxPMTKWaitMs)
{
    bool rc_ok = false;

    if (pdTRUE != xSemaphoreTake(MT3333_data.semAcc , maxPMTKWaitMs)) {
        LOG_DBG( LOG_LEVEL_GNSS,"MT3333_SendPmtkCommand: access semtake failed\n");
        rc_ok = false;
    } else {

        if (MT3333_data.state == mt3333_up) {
            MT3333_data.lastPMTKcommandnum = atoi( &cmd[5]);
            MT3333_data.waitingFor = ((type) ? type : E_pmtk_ack);
            // clear semaphore
            xSemaphoreTake(MT3333_data.semAck , 0);
            rc_ok = gnssSendNmeaCommand(cmd, maxPMTKWaitMs);
            if (rc_ok) {
                // wait for ack/response
                rc_ok = (pdTRUE == xSemaphoreTake(MT3333_data.semAck , maxPMTKWaitMs));
                if (!rc_ok) {
                    LOG_DBG( LOG_LEVEL_GNSS,"MT3333_SendPmtkCommand: wait for ack failed\n");
                }

            } else {
                LOG_DBG( LOG_LEVEL_GNSS,"MT3333_SendPmtkCommand: failed sending command : $%s\n",cmd);
            }
            MT3333_data.waitingFor = E_pmtk_max;// no more expected messages
        } else {
            LOG_DBG( LOG_LEVEL_GNSS,"MT3333_SendPmtkCommand: error, not in 'up' state\n");
        }
        xSemaphoreGive(MT3333_data.semAcc);
    }

    return rc_ok;
}

/*
 * MT3333_reporting
 *
 * @desc    Stops or starts the gnss chip of reporting gnss data
 *
 * @param   on : true to enable reporting
 * @param   maxPMTKWait: how long to wait for the completion/ack of the PMTK command
 *
 * @return false when something wrong
 */
bool MT3333_reporting( bool on, uint32_t maxPMTKWait)
{
    bool rc_ok = true;

    if(MT3333_data.nodeIsATestBox == false)
    {
		if (on) {
			// noGLL, RMC, VTG, GGA, GSA, GSV (interval 1 second)
			rc_ok = MT3333_SendPmtkCommand( "PMTK314,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0", 0, maxPMTKWait);
		}else {
			rc_ok = MT3333_SendPmtkCommand( "PMTK314,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0", 0, maxPMTKWait);
		}
    }
    return rc_ok;
}

/*
 * MT3333_getLastResponseMsg
 *
 * @desc    Returns the mt3333 last response
 *
 * @param   none
 *
 * @return pointer to the last reponse
 */
char *MT3333_getLastResponseMsg(void)
{
	return MT3333_data.lastResponse.data.txt_msg.msg;
}

/*
 * MT3333_getLastResponseType
 *
 * @desc    Returns the mt3333 last response type
 *
 * @param   none
 *
 * @return to the last reponse
 */
tPmtkType MT3333_getLastResponseType(void)
{
	return MT3333_data.lastResponse.type;
}

/*
 * MT3333_version
 *
 * @desc    Returns the mt3333 version information string
 *
 * @param   maxPMTKWait: how long to wait for the completion/ack of the PMTK command
 *
 * @return false when something wrong
 */
bool MT3333_version(uint32_t maxPMTKWait)
{
    bool rc_ok = true;

    if(MT3333_data.nodeIsATestBox == false)
    {
		rc_ok = MT3333_SendPmtkCommand( "PMTK605", 705, maxPMTKWait);
		if(true == rc_ok)
		{
			printf("GNSS Version = %s\n", MT3333_data.lastResponse.data.txt_msg.msg);
		}
    }
    return rc_ok;
}

static bool cliMT3333on(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	bool rc_ok = MT3333_Startup(5000);
	if(false == rc_ok) {
        LOG_DBG( LOG_LEVEL_GNSS, "MT3333 power on: failed\n");
    }
	return rc_ok;
}

static bool cliMT3333off(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	bool rc_ok = MT3333_shutdown(1000);
    if (false == rc_ok) {
        LOG_DBG( LOG_LEVEL_GNSS, "MT3333 power off: failed\n");
    }
	return rc_ok;
}

static bool cliMT3333pause(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	bool rc_ok = MT3333_reporting(false, 1000);
	if (false == rc_ok) {
		LOG_DBG( LOG_LEVEL_GNSS, "MT3333_reporting: failed\n");
	}
	return rc_ok;
}

static bool cliMT3333resume(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	bool rc_ok = MT3333_reporting(true, 1000);
	if (false == rc_ok) {
		LOG_DBG( LOG_LEVEL_GNSS, "MT3333_reporting: failed\n");
	}
	return rc_ok;
}

static bool cliMT3333version(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	bool rc_ok = MT3333_version(1000);
	if (false == rc_ok) {
		LOG_DBG( LOG_LEVEL_GNSS, "MT3333_version: failed\n");
	}
	return rc_ok;
}

static bool cliMT3333cold(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	gnssStatus.collectedData.validTime = false;
	gnssStatus.collectedData.firstFixTimeMs = gnssStatus.collectedData.firstAccurateFixTimeMs = 0;
	gNvmData.dat.gnss.lastPO = 0;
	bool rc_ok = MT3333_SendPmtkCommand( "PMTK103", 0, 1000);
	if (false == rc_ok) {
		LOG_DBG( LOG_LEVEL_GNSS, "cold start failed\n");
	}
	return rc_ok;
}

struct cliSubCmd mt3333SubCmds[] = {
        { "on"      , cliMT3333on },
        { "off"     , cliMT3333off },
        { "cold"    , cliMT3333cold },
        { "pause"   , cliMT3333pause },
        { "resume"  , cliMT3333resume },
        { "version" , cliMT3333version },
};

bool cliMT3333( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    bool rc_ok = true;

    if (args >= 1) {
        if ((strncmp(( const char *) argv[0],"$PMTK",5) == 0) || (strncmp(( const char *) argv[0],"$PGCMD",6) == 0))
        {
    		uint32_t type = ((args == 2) ? argi[1] : 0);
            if ( false == MT3333_SendPmtkCommand( (char *) &argv[0][1], type, 10000)) {
                LOG_DBG( LOG_LEVEL_GNSS, "MT3333 PMTK command failed: %s\n", &argv[0][1]);
                rc_ok = false;
            }
        } else {
            rc_ok = cliSubcommand(args,argv,argi, mt3333SubCmds, sizeof(mt3333SubCmds)/sizeof(*mt3333SubCmds)  );
        }
        printf("Command %s\n", (rc_ok?"OK":"FAILED"));
    } else {
    	printf("Try typing: help mt3333\n");
    }
    return rc_ok;
}

/*
 * CLI command list
 */
bool cliMT3333LongHelp( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    printf( "actions:\n"
            "  on/off\t\tpower module on/off\n"
            "  cold\t\t\tcold start module\n"
            "  pause\t\t\tstop the reporting of the MT3333 device\n"
            "  resume\t\tResume reporting\n"
    		"  version\t\tDisplay GNSS version info\n"
            "  $<cmd> <response>\tsend PMTK NMEA command to module <optional response code> (checksum will be added!)\n" );
    return true;
}

static const struct cliCmd MT3333Commands[] = {
        MT3333_CLI,
};

bool MT3333CliInit()
{
    return cliRegisterCommands(MT3333Commands , sizeof(MT3333Commands)/sizeof(*MT3333Commands));
}



#ifdef __cplusplus
}
#endif