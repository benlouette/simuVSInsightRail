#ifdef __cplusplus
extern "C" {
#endif

/*
 * commCLI.c
 *
 *  Created on: 23 dec. 2015
 *      Author: Daniel van der Velde
 */

/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "printgdf.h"

#include "CLIcmd.h"

#include "Log.h"
#include "xTaskAppCommsTest.h"	// for publishImagesManifestUpdate();

/*
 * Macros
 */

/*
 * Types
 */

/*
 * Data
 */
static bool bIgnoreCommsAck = false;
static tCommHandle CommHandle = { .EventQueue_CommResp = NULL};

/*
 * Functions
 */
static bool cliCommsConnect( uint32_t args, uint8_t * argv[], uint32_t * argi);
static bool cliCommsDisconnect( uint32_t args, uint8_t * argv[], uint32_t * argi);
static bool cliCommsAckIgnore( uint32_t args, uint8_t * argv[], uint32_t * argi);

struct cliSubCmd commSubCmds[] =
{
	{"connect", 	cliCommsConnect},
	{"disconnect",  cliCommsDisconnect},
	{"AckIgnore", 	cliCommsAckIgnore},
};

bool cliComm( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    bool rc_ok = true;

    if (args)
    {
		rc_ok = cliSubcommand(args,argv,argi, commSubCmds, sizeof(commSubCmds)/sizeof(*commSubCmds)  );
		if (rc_ok == false)
		{
			printf("comm error");
		}
    }
    else
    {
        printf("Try typing: help comm\n");
    }

    return rc_ok;
}

static bool cliCommsConnect( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	CommHandle.CommResp.rc_ok = false;
	bool retval = false;
	retval = TaskComm_Connect(&CommHandle, portMAX_DELAY);
	if ((retval == false) || (CommHandle.CommResp.rc_ok == false))
	{
		LOG_DBG( LOG_LEVEL_CLI, "comm connect: failed\n");
	}

	return true;
}

static bool cliCommsDisconnect( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	CommHandle.CommResp.rc_ok = false;
	bool retval = false;
	retval = TaskComm_Disconnect(&CommHandle, portMAX_DELAY);
	if ((retval == false) || (CommHandle.CommResp.rc_ok == false))
	{
		LOG_DBG( LOG_LEVEL_CLI, "comm Disconnect: failed\n");
	}

	return true;
}

static bool cliCommsAckIgnore( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	if (args==1)
	{
		if(argi[0] == 1)
		{
			bIgnoreCommsAck = true;
			LOG_DBG( LOG_LEVEL_CLI, "FW ignores the COMMS Ack for the current COMMS cycle ONLY\n");
		}
		else if(argi[0] == 0)
		{
			bIgnoreCommsAck = false;
			LOG_DBG( LOG_LEVEL_CLI, "Comms Ack set to accept.\n");
		}
		else
		{
			LOG_DBG( LOG_LEVEL_CLI, "Invalid input, enter 1 to Ignore or 0 to accept.\n");
		}
	}
	else
	{
		printf("Only single parameter needed (enter 0 to ignore or 1 to accept), Current ACK test flag status:%d\n", bIgnoreCommsAck);
	}

	return true;
}

/*
 * commCLI_IsCommsAckIgnoreSet
 *
 * @desc    Fetches the Comms ACK Ignore flag status.
 *
 * @returns TRUE if Ack needs to be ignored, FALSE otherwise.
 */
bool commCLI_IsCommsAckIgnoreSet()
{
	return bIgnoreCommsAck;
}

static bool cliMqtt( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    bool rc_ok = true;

    // TODO
    if (args >= 1) {


        if (strcasecmp((const char*)argv[0], "pub") == 0)
        {
            if (args==2)
            {
                TaskComm_Publish( &CommHandle,  argv[1], strlen((char *)argv[1]), NULL,  portMAX_DELAY );
            }
            else
            {
                if (args==3)
                {
                    TaskComm_Publish( &CommHandle,  argv[1], strlen((char *)argv[1]), argv[2], portMAX_DELAY );
                }
            }
#if 0
        }  else {
            // TODO: remove this test function for big blocks !
            if (strcasecmp((const char*)argv[0], "testpub") == 0) {
                uint32_t size = 100;
                static uint8_t testingbuf[2000];
                uint32_t i;
                for (i=0; i<sizeof(testingbuf); i++) {
                    testingbuf[i] = 32+ (i % 64);
                }
                if (args>1) size=argi[1];
                if (size > sizeof(testingbuf)) size = sizeof(testingbuf);
                if (size == 0) size = 50;
                TaskComm_Publish( &CommHandle,  testingbuf, size, NULL,  portMAX_DELAY );
            }
#endif
        }

        if(strcasecmp((const char*)argv[0], "OTAmanifest")==0)
        {
        	publishManifest(argi[1]);
        }
        else if(strcasecmp((const char*)argv[0], "Imagesmanifest")==0)
        {
        	rc_ok = publishImagesManifestUpdate();
        }

    }

    return rc_ok;
}


/*
 * CLI command list
 */
static bool cliCommLongHelp( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    printf( "actions:\n"
            "  connect         		Connect to cloud services using IDEF/MQTT\n"
            "  disconnect      		Disconnect from cloud services\n"
    		"  ackIgnore <En/Dis>	\tIgnore the COMMS ACK\n");
    return true;
}

static bool cliMqttLongHelp( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    printf( "actions:\n"
            "  pub <msg> [<topic>]   Publish message from CLI to MQTT broker on <topic>\n"
    		"  Otamanifest <ImageType: 1->Loader, 2->App, 3->Pmic_App> Publish OTAcomplete manifest\n"
    		"  Imagesmanifest          Publish Images manifest\n"
            /*"  sub <topic>         Subscribe to <topic>; msgs are printed on the CLI\n" */);
    return true;
}

static const struct cliCmd commCommands[] = {
        {"comm", " [action] [param(s)]\tCommunication actions", cliComm, cliCommLongHelp },
        {"mqtt", " [action] [param(s)]\tMQTT actions", cliMqtt, cliMqttLongHelp }
};

bool commCliInit()
{
    if (CommHandle.EventQueue_CommResp == NULL) {
        if (false == TaskComm_InitCommands(&CommHandle)) {
            LOG_DBG( LOG_LEVEL_CLI, "comm initialisation Commands failed\n");
        }
    }
    return cliRegisterCommands(commCommands , sizeof(commCommands)/sizeof(*commCommands));
}



#ifdef __cplusplus
}
#endif