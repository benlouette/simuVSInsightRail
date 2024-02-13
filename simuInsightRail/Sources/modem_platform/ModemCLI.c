#ifdef __cplusplus
extern "C" {
#endif

/*
 * modemCLI.c
 *
 *  Created on: Oct 30, 2015
 *      Author: George de Fockert
 */

#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
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

#include "CLIio.h"
#include "CLIcmd.h"
#include "printgdf.h"

#include "PinDefs.h"

//
// board/project specific commands
//
#include "xTaskModem.h"
#include "xTaskModemEvent.h"
#include "ModemIo.h"
#include "modemCLI.h"
#include "configCLI.h"



#include "configModem.h"
#include "Modem.h"
#include "NvmConfig.h"

#define COPY_SIZE 	1344
#define EPO_SET_SIZE		1920	// no of bytes in a set
#define EPO_SETS_IN_14DAYS	56		// no of sets in 14 days

extern uint32_t  Modem_readBlock( uint8_t *data, uint32_t len, uint32_t timeout );

static void result_ls(uint8_t * str, uint32_t len, void  *mem)
{
	if((0 == strncmp((char*)str, "^SFSA: ", 7)) && (0 != strcmp((char*)str, "^SFSA: 0\r\n")))
	{
		printf((char*)&str[6]);
	}
}

static bool cliModemLs(uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	tModemResultFunc func =
	{
		.resultProcessor =  result_ls,
		.params = (void *)g_pSampleBuffer

	};
	tModemAtRc AtRc;
	uint32_t dbg_logging_save = dbg_logging;

	dbg_logging &= ~LOG_LEVEL_MODEM;

	do
	{
	    if((ModemRcOk !=  modemSendAt(&func, 2000, &AtRc, "AT^SFSA=ls,a:/")) || (AtRc != AtOk))
	    	break;
	} while(0);

	dbg_logging = dbg_logging_save;

	return true;
}

uint32_t readcount;

static void result_read(uint8_t * str, uint32_t len, void  *mem)
{
	modemStatus.atCommand.ATcopyptr = mem;
	modemStatus.atCommand.ATcopycount = COPY_SIZE;
	modemStatus.atCommand.AtCommandState = ATfread;
	setIgnoreLF(true);
}

static bool cliModemCopy( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	tModemResultFunc func;
	tModemAtRc rcode;
	uint32_t dbg_logging_save = dbg_logging;

	func.resultProcessor =  result_read;
	func.params = (void *)g_pSampleBuffer;
	dbg_logging &= ~LOG_LEVEL_MODEM;

	printf("copy a:/MTK14.EPO to memory\n");
	do
	{
	    if((ModemRcOk !=  modemSendAt(NULL, 2000, &rcode, "AT^SFSA=open,a:/MTK14.EPO,0")) || (rcode != AtOk))
	    {
	    	printf("error: file not found MTK14.EPO\n");
	    	break;
	    }
	    do
	    {
	    	if((ModemRcOk !=  modemSendAt(&func, 2000, &rcode, "AT^SFSA=read,0,%d", COPY_SIZE))  || (rcode != AtOk))
	    	{
	    		break;
	    	}
	    	func.params = (uint8_t*)func.params + COPY_SIZE;
	    } while((int)func.params < (int)((uint8_t*)g_pSampleBuffer + (EPO_SET_SIZE * EPO_SETS_IN_14DAYS)));
	    if(ModemRcOk !=  modemSendAt(NULL, 2000, NULL, "AT^SFSA=close,0"))
	    {
	    	break;
	    }
	    if(rcode == AtOk)
	    {
	    	printf("Copied %d bytes\ncopy complete!\n", (int)func.params-(int)g_pSampleBuffer );
	    }
	} while(0);

	dbg_logging = dbg_logging_save;

	return true;
}

static bool cliModemFtpEPO( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	//const char cmdLogin[] = "AT^SISS=1,address,ftp://gtopepo:gtop1234@ftp.gtop-tech.com/"
	const char cmdLogin[] = "AT^SISS=1,address,ftp://swirgnss:SWirePO288@ftp5.sierrawireless.com:21/";
	bool rc_ok = true;
	tModemAtRc AtRc;

	do
	{
	    if((ModemRcOk !=  modemSendAt(NULL, 2000, &AtRc, "AT^SICS=0,conType,GPRS0")) || (AtOk != AtRc)) break;
	    vTaskDelay(100);
	    if((ModemRcOk !=  modemSendAt(NULL, 2000, &AtRc, "AT^SICS=0,inactTO,0")) || (AtOk != AtRc)) break;
	    vTaskDelay(100);
	    if((ModemRcOk !=  modemSendAt(NULL, 2000, &AtRc, "AT^SICS=0,apn,EM")) || (AtOk != AtRc)) break;
	    vTaskDelay(100);
	    if((ModemRcOk !=  modemSendAt(NULL, 2000, &AtRc, "AT^SISS=1,srvType,Ftp")) || (AtOk != AtRc)) break;
	    vTaskDelay(100);
	    if((ModemRcOk !=  modemSendAt(NULL, 2000, &AtRc, "AT^SISS=1,conId,0")) || (AtOk != AtRc)) break;
	    vTaskDelay(100);
	    if((ModemRcOk !=  modemSendAt(NULL, 2000, NULL, cmdLogin)) || (AtOk != AtRc)) break;
	    vTaskDelay(100);
	    if((ModemRcOk !=  modemSendAt(NULL, 2000, &AtRc, "AT^SISS=1,cmd,fget")) || (AtOk != AtRc)) break;
	    vTaskDelay(100);
	    if((ModemRcOk !=  modemSendAt(NULL, 2000, &AtRc, "AT^SISS=1,ftMode,bin")) || (AtOk != AtRc)) break;
	    vTaskDelay(100);
	    if((ModemRcOk !=  modemSendAt(NULL, 2000, &AtRc, "AT^SISS=1,path,file:///a:/")) || (AtOk != AtRc)) break;
	    vTaskDelay(100);
	    if((ModemRcOk !=  modemSendAt(NULL, 2000, &AtRc, "AT^SISS=1,files,MTK14.EPO")) || (AtOk != AtRc)) break;
	    vTaskDelay((argi[0])?argi[0]:500);
	    if((ModemRcOk !=  modemSendAt(NULL, 2000, &AtRc, "AT^SISO=1")) || (AtOk != AtRc)) break;
	} while(0);

	return rc_ok;
}

static bool cliModemConnect( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	bool rc_ok = true;

	if (args)
	{
		uint8_t serviceProfile  = (gNvmCfg.dev.modem.service % MODEM_SERVICEPROFILES);
		uint8_t providerProfile = (gNvmCfg.dev.modem.provider % MODEM_PROVIDERPROFILES);

		if (argi[0])
		{
			char imei[MODEM_IMEI_LEN];
			char iccid[MODEM_ICCID_LEN];
			uint32_t csq;
			int16_t signalQuality;

			printf("Modem_init() returned : %d\n",
					Modem_init(
					    gNvmCfg.dev.modem.radioAccessTechnology,
						gNvmCfg.dev.modem.providerProfile[providerProfile].simpin,
						gNvmCfg.dev.modem.providerProfile[providerProfile].apn,
						providerProfile,
						serviceProfile,
						(uint8_t *) gNvmCfg.dev.modem.serviceProfile[serviceProfile].url,
						gNvmCfg.dev.modem.serviceProfile[serviceProfile].portnr,
						(uint8_t *) imei,
						(uint8_t *) iccid,
						&csq,
						&signalQuality,
						gNvmCfg.dev.modem.minimumCsqValue
						)
					);
			printf("imei: %s\niccid: %s\ncsq = %d signalQuality = %d\n",imei,iccid,csq, signalQuality);
		}
		else
		{
			Modem_terminate(serviceProfile);
		}
	}
	else
	{
		rc_ok = false;
	}

	return rc_ok;
}

static bool cliModemId( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	bool rc_ok = true;
	char imei[MODEM_IMEI_LEN];
	char iccid[MODEM_ICCID_LEN];
	uint32_t maxWaitMs = 30000;// max waittime in ms
	uint8_t providerProfile = gNvmCfg.dev.modem.provider % MODEM_PROVIDERPROFILES;

	if (args>0)
	{
	    maxWaitMs = argi[0];
	}

	rc_ok = Modem_readCellularIds(
	            gNvmCfg.dev.modem.radioAccessTechnology,
				gNvmCfg.dev.modem.providerProfile[providerProfile].simpin,
				(uint8_t *) imei,
				(uint8_t *) iccid,
				maxWaitMs
			);

	if (rc_ok)
	{
		if ( 0 != strncmp((char *) gNvmCfg.dev.modem.imei, imei, sizeof(gNvmCfg.dev.modem.imei) - 1))
		{
			strncpy( (char *) gNvmCfg.dev.modem.imei, imei, sizeof(gNvmCfg.dev.modem.imei));
		}
		if ( 0 != strncmp((char *) gNvmCfg.dev.modem.iccid, iccid, sizeof(gNvmCfg.dev.modem.iccid) - 1))
		{
			strncpy((char *) gNvmCfg.dev.modem.iccid, iccid, sizeof(gNvmCfg.dev.modem.iccid));
		}
		printf("imei: %s\niccid: %s\n",imei,iccid);
	}
	else
	{
		printf("Modem_readCellularIds failed\n");
	}
	return rc_ok;
}


static uint8_t dummydata[256];

static bool cliModemDummyWrite( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	bool rc_ok = true;

	// write a number of bytes to the modem (which should be in transparent mode)
	if (args>0) {
		// fill dummyarray with data
		uint32_t writecount, written, totalwritten = 0;
		uint32_t count = (argi[0]>=0 ? argi[0] : -(int)argi[0]);
		uint32_t timeout = (2 * count); // 2ms/character as default

		if (args>1) {
			timeout = argi[1];
		}
		// generate dummy data (lowercase abc..xyz)
		for (int i = 0; i < sizeof(dummydata); i++)
		{
			dummydata[i] = 'a'+ ( i % 26 );
		}

		while (count && rc_ok)
		{
			if (count<=sizeof(dummydata))
			{
				writecount = count;
			}
			else
			{
				writecount=sizeof(dummydata);
			}
			written = Modem_write(dummydata, writecount, timeout);
			totalwritten+=written;
			count -= written;
			printf("Written %d, to go %d\n",totalwritten, count);
			if (written != writecount)
			{
				printf("error during write, only %d out of %d written\n",totalwritten,  argi[0]);
				rc_ok = false;
			}
		}
		rc_ok = true;// prevent syntax error message
	}

	return rc_ok;
}

static bool cliModemDummyRead( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	bool rc_ok = true;

	// write a number of bytes to the modem (which should be in transparent mode)
	if(args > 0)
	{
		// fill dummyarray with data
		uint32_t readcount, read, totalread = 0;
		uint32_t count = (argi[0]>=0 ? argi[0] : -(int)argi[0]);
		uint32_t timeout = (2 * count);		// 2ms/character as default
		if (args>1)
		{
			timeout = argi[1];
		}

		while (count && rc_ok)
		{
			if (count<=sizeof(dummydata))
			{
				readcount = count;
			}
			else
			{
				readcount=sizeof(dummydata);
			}
			read = Modem_read(dummydata, readcount, timeout);
			totalread+=read;
			count -= read;
			printf("read %d, to go %d\n",totalread, count);
			if (read != readcount)
			{
				printf("error during read, only %d out of %d read\n",totalread,  argi[0]);
				rc_ok = false;
			}
		}
		rc_ok = true;// prevent syntax error message
	}

	return rc_ok;
}


static bool cliModemTransparent( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	bool rc_ok = true;

	if (args==0)
	{
		printf("Modem is%s in transparent mode\n",Modem_isTransparent() ? " " : " NOT");
	}
	else
	{
		if(argi[0])
		{
			rc_ok = Modem_startTransparent(gNvmCfg.dev.modem.service, 5000);
		}
		else
		{
			rc_ok = Modem_stopTransparent();
		}
		if (rc_ok != true)
		{
			printf("transparent mode switch failed\n");
		}
	}

	return true;
}

static bool cliModemInfo( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	bool rc_ok = true;

	void Modem_print_info();
	void Modem_print_info();
	void ModemIo_print_info();

	Modem_print_info();
	Modem_print_info();
	ModemIo_print_info();

	return rc_ok;
}


static bool cliModemBaud( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    bool rc_ok = true;
    uint32_t baudrate = 115200;

    if (args>0) {
        baudrate = argi[0];
    } else {
        printf("Supported baudrates : 115200 230400 460800 500000 750000 921600\n");
        printf("Current configured baudrate = %d\n",gNvmCfg.dev.modem.baudrate);
        return true;
    }

    rc_ok = ModemInit(baudrate, 10000);

    vTaskDelay(15000);

    Modem_tryBaudrates(baudrate);

    Modem_terminate( 0);// shutdown modem, provider unimportant here

    return rc_ok;
}

static bool cliModemPower( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    bool rc_ok = true;

    if (args)
    {
        if (argi[0])
        {
            printf("Modem_powerup(20000) returned %d\n", Modem_powerup(20000));
        }
        else
        {
            printf("Modem_terminate(0) returned %d\n",Modem_terminate(0));
        }
    }
    else
    {
    	printf("EHS5E_POWER_IND input pin signals that the modem is powered %s !\n", !configModem_PowerCheck() ? "down" : "on");
    }

    return rc_ok;
}

static bool cliModemDebug( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    bool rc_ok = true;

    for(int i = 0; i < MODEM_DEBUG_BUF_LEN; i++)
    {
    	printf("g_ModemDebugData[%d] = %d\n", i, g_ModemDebugData[i]);
    }

    return rc_ok;
}


static const char modemhelp[] = {
        "modem subcommands:\n"
        " at<modemcommand>\tSend AT command to modem port\n"
        " id  maxwait\t\tRead IMEI, ICCID from modem, optional maxwait (ms), store in RAM, use configwrite 1 to store in flash!\n"
        " con val\t\tConnect, preprogrammed series commands to bring modem up/down (val=1/0)\n"
        " write count timeout\tWrite dummy buffer of 'count' bytes, optional timeout in ms\n"
        " read count timeout\tRead dummy buffer of 'val' bytes, optional timeout in ms\n"
        " trans val\t\tTransparent mode 0=off, 1=on (only when already connected to cellular network)\n"
        " info \t\t\tPrint internal modem states\n"
        " baud <newbaudrate>\tSet Baudrate of the modem\n"
        " power <0|1>\t\tSwitch modem power on/off\n"
		" ftp \t\t\tftp EPO file to modem FFS\n"
		" copy\t\t\tcopy file from FFS to memory\n"
		" ls\t\t\tlist files on drive a:/\n"
		" showdebug\t\t\tdisplay the modem debug variables\r\n"
};

static bool cliExtModemHelp(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
    printf(modemhelp);
    return true;
}

struct cliSubCmd modemSubCmds[] = {
        {"id"   , cliModemId },
        {"con"  , cliModemConnect },
        {"write", cliModemDummyWrite },
        {"read" , cliModemDummyRead },
        {"trans", cliModemTransparent},
        {"info" , cliModemInfo},
        {"baud" , cliModemBaud},
        {"power", cliModemPower},
        {"ftp",   cliModemFtpEPO},
        {"copy",  cliModemCopy},
        {"ls",    cliModemLs},
		{"showdebug",  cliModemDebug},
};

static bool cliModem( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    bool rc_ok = true;

    if (args)
    {
        // is it an AT command
    	if(toupper(argv[0][0]) == 'A' && toupper(argv[0][1]) == 'T')
        {
            for (int i=0; i<args; i++)
            {
                if(!Modem_put_s((char*) argv[i]))
                {
                	printf("Substring %d failed to send to modem\n", i);
                }
            }
            Modem_put_ch('\r');
        }
        else
        {
            rc_ok = cliSubcommand(args,argv,argi, modemSubCmds, sizeof(modemSubCmds)/sizeof(*modemSubCmds)  );
        }
    }
    else
    {
        printf("Try typing: help modem\n");
    }

    return rc_ok;
}

static const struct cliCmd modemCommands[] = {

		{"modem"," <modemcommand>\t\tSend command to modem port", cliModem, cliExtModemHelp},
#if 0
		{"modemid"," maxwait\t\tRead IMEI, ICCID from modem, optional maxwait (ms), store in RAM, use configwrite 1 to store in flash!", cliModemId, NULL},
		{"modemcon","val\t\t\tConnect, preprogrammed series commands to bring modem up/down (val=1/0)",cliModemConnect, NULL},
		{"modemwrite","count timeout\tWrite dummy buffer of 'count' bytes, optional timeout in ms",cliModemDummyWrite, NULL},
		{"modemread","count timeout\tRead dummy buffer of 'val' bytes, optional timeout in ms",cliModemDummyRead, NULL},
		{"modemtrans","val\t\tTransparent mode 0=off, 1=on (only when already connected to cellular network)",cliModemTransparent, NULL},
		{"modeminfo","\t\t\tPrint internal modem states",cliModemInfo, NULL},
        {"modembaud","<newbaudrate>\tSet Baudrate of the modem",cliModemBaud,NULL},
        {"modempower","<0|1>\t\tSwitch modem power on/off",cliModemPower, NULL},
#endif

};


bool modemCliInit()
{

	return cliRegisterCommands(modemCommands , sizeof(modemCommands)/sizeof(*modemCommands));
}






#ifdef __cplusplus
}
#endif