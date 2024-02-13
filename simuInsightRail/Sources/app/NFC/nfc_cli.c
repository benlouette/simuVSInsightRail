#ifdef __cplusplus
extern "C" {
#endif

/*
 * nfc_cli.c
 *
 *  Created on: 29 Oct 2018
 *      Author: RZ8556
 */

#include "configFeatures.h"
#ifdef CONFIG_PLATFORM_NFC

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

#include "nfc.h"
#include "i2c.h"
#include "Device.h"
#include "NvmConfig.h"

#include "pmic.h"
#include "nfc_cli.h"

extern i2c_device_t nfc;

/**********************************************************************************************
 *
 * NFC CLI commands
 *
 * Production will use the following commands:
 *
 * nfc fd 1 	// set field detect pin to assert when NFC communicates
 * nfc write 	// update the tag. Update RTC prior to running this command
 *
 * TODO 		// need a way to set the Model string, which will then be written to tag
 *
 **********************************************************************************************/

#ifdef _MSC_VER
#define NO_OPTIMIZE_START __pragma(optimize("", off))
#define NO_OPTIMIZE_END   __pragma(optimize("", on))
#else
#define NO_OPTIMIZE_START _Pragma("GCC push_options") \
                          _Pragma("GCC optimize(\"O0\")")
#define NO_OPTIMIZE_END   _Pragma("GCC pop_options") // __attribute__((optimize("O0")))
#endif

#define MAX_NDEF_RECORD (30)
NO_OPTIMIZE_START
static bool  cliUpdateRecord(const char* tag, const char *newtext)
{
	bool rc_ok = false;
	int lenTag = strlen(tag);
	NDEF* pNDEF = NFC_GetNDEF();
	char* pc = (char*)pNDEF->contents;
	char* pend = pc + pNDEF->count + BYTES_PER_BLOCK;

	do
	{
		if(strncmp(tag, pc, 3) == 0)
		{
			// make sure!
			if((strncmp(tag + 3, pc + 3, lenTag - 3) == 0) && (*(pc + lenTag) == ':'))
			{
				rc_ok = true;
				break;
			}
		}
	} while(pc++ < pend);

	if(rc_ok)
	{
		int charcount = 0;
		while((charcount++ < MAX_NDEF_RECORD) && ((*pc++) != ':'))
		{
		}

		while((charcount++ < MAX_NDEF_RECORD) && ((uint8_t)(*pc) != 0xFE) && ((uint8_t)(*pc) != 0x11))
		{
			*pc++ = *newtext++;
		}
		if(charcount >= MAX_NDEF_RECORD)
		{
			rc_ok = false;
		}
	}

	return rc_ok;
}
NO_OPTIMIZE_END


/*
 * cliModeSet
 *
 * @desc    Write mode string to 'command' NDEF record
 *
 * @param   - string for new command (mode)
 *
 * @return - true if ok, otherwise false
 */

static bool cliModeSet(const char const* mode)
{
	bool rc_ok = false;

	// read the tag..
	rc_ok = NFC_getPMICtag();

	// update the record..
	if(rc_ok)
	{
		const char *p = "fact";

		/*
		 * c,s,t - set operating mode (commissioned, shippable, test)
		 * Must be 4 chars!
		 */
		switch(mode[0])
		{
		case 'c':
			p = "cmsd";
			break;

		case 's':
			p = "ship";
			break;

		case 't':
			p = "test";

		default:
			break;
		}

		rc_ok = cliUpdateRecord("mode", p);

		if(rc_ok)
		{
			NDEF* ndef = NFC_GetNDEF();
			int len = ndef->count;
			rc_ok =  NFC_WriteNDEF(len);
		}
	}

	return rc_ok;
}


/*
 * cliNFCDump(uint8_t start_block, uint8_t end_block)
 *
 * @desc    Read NFC tag, display UID and NDEF records if present
 *
 * @param	start_block	- read from this block (in multiples of 16 bytes)
 * 			end_block  	- last block number
 *
 * @return - true if OK, otherwise false
 */
static bool cliNFCDump(uint8_t start_block, uint8_t end_block)
{
    bool rc_ok = true;
    bool use_field_width = false;
    uint8_t buf[16] = {0};

    if(start_block > end_block)
    {
    	return false;
    }

    use_field_width = (start_block > 9) || (end_block > 9);

    if(Device_HasPMIC())
    {
    	rc_ok = NFC_getPMICtag();
    }

    uint8_t *pNDEF = NFC_GetNDEF()->contents;

#ifdef USE_NFC_MUTEX
    if (NFCTakeMutex())
    {
#endif
    for(int block=start_block; rc_ok && (block <= end_block); block++)
    {
    	if(!Device_HasPMIC())
     	{
    		rc_ok = I2C_ReadRegister(NFC_BUS, nfc, block, BYTES_PER_BLOCK, &buf[0]);
    	}
    	else
    	{
    		memcpy(buf, pNDEF+=BYTES_PER_BLOCK, BYTES_PER_BLOCK);
    	}

    	if(rc_ok)
    	{
    		printf((use_field_width?" %02d: ":" %d: "), block);
    		for(int i=0 ; i < BYTES_PER_BLOCK; i++)
    		{
    			printf("%02X ", buf[i]);
    		}

    		printf("     ");

    		for(int i=0 ; i < BYTES_PER_BLOCK; i++)
    		{
    		    if(isgraph(buf[i]))
    		    {
    		    	printf("%c", buf[i]);
    		    }
    		    else
    		    {
    		    	printf(".");
    		    }
    		}
    		printf("\n");
    	}
    	else
    	{
    	    printf("Failed to read tag for block %d\n", block);
    	}
    }
#ifdef USE_NFC_MUTEX
        // always give back when we took it
        if (NFCGiveMutex()==false) {
            rc_ok = false;
        }
    }
#endif

    return rc_ok;
}


/*
 * cliNFCRead
 *
 * @desc    Read NFC tag, display UID and NDEF records if present
 *
 * @param   - void
 *
 * @return - true if ok, otherwise false
 */
static bool cliNFCRead(void)
{
    bool rc_ok = false;
    int i;
    uint8_t buf[64] = {0};

    if(Device_HasPMIC())
	{
    	rc_ok = NFC_getPMICtag();

    	if(!rc_ok)
    	{
    		printf("Failed to get tag information from PMIC \n");
    		return false;
    	}
	}

#ifdef USE_NFC_MUTEX
    if (NFCTakeMutex())
#endif
    {
    	NDEF *pNDEF = NFC_GetNDEF();

    	if(!Device_HasPMIC())
    	{
    	   	rc_ok = I2C_ReadRegister(NFC_BUS, nfc, 0, BYTES_PER_BLOCK, &buf[0]);
    	   	rc_ok = I2C_ReadRegister(NFC_BUS, nfc, 1, BYTES_PER_BLOCK, &buf[16]);
    	}
    	else
    	{
    		memcpy(buf, pNDEF->contents, BYTES_PER_BLOCK*2);
    	}

    	printf("NFC Tag");
    	printf("\n  Serial number ............. ");
    	for(i = 0; i < 7; i++)
    	{
    		printf("%02X ", buf[i]);
    	}

    	printf("\n  Internal .................. ");
    	for(i = 7; i < 10; i++)
    	{
    		printf("%02X ", buf[i]);
    	}

    	printf("\n  Static lock bytes ......... ");
    	for(i = 10; i < 12; i++)
    	{
    		printf("%02X ", buf[i]);
    	}

    	printf("\n  Capability Container(CC) .. ");
    	for(i = 12; i < 16; i++)
    	{
    		printf("%02X ", buf[i]);
    	}

        if(CheckTagBufferEmpty(buf))
    	{
    	    printf("\n  Tag Empty\n");
    	}
        else
        {
        	uint8_t block = 1, size = buf[17] + 3;
        	uint8_t *p;

        	if(!Device_HasPMIC())
        	{
				for(i = 0; i < size; i += BYTES_PER_BLOCK)
				{
					rc_ok = I2C_ReadRegister(NFC_BUS, nfc, block, BYTES_PER_BLOCK, &pNDEF->contents[BYTES_PER_BLOCK+i]);
					if(rc_ok != true)
					{
						break;
					}
					block++;
				}
        	}

            p = &((pNDEF->contents)[2 + BYTES_PER_BLOCK]);

        	if(rc_ok)
        	{
				block = 0;
				do
				{
					uint8_t rSize = p[2] + 4;
					printf("\nRecord %d"
						   "\n  Type %s\n",
						   block,
						   (p[3] == NDEF_TYPE_TEXT ? "Text" : (p[3] == NDEF_TYPE_URI ? "URI" : "Unknown")));

					if(p[3] == NDEF_TYPE_TEXT)
					{
						memset(buf, 0, sizeof(buf));
						strncpy((char*)buf, (char *)&p[7], p[2] - 3);
						printf("  Lang '%c%c'\n", p[5], p[6]);
						printf("  Text '%s'", buf);
					}
					p += rSize;
					block++;

					if(block > MAX_NDEF/BYTES_PER_BLOCK)
					{
						printf("Too many blocks!\n");
						break;
					}

				} while(*p != 0xFE);
				printf("\n");
        	}
       		pNDEF->count = p - (pNDEF->contents) + 1 - BYTES_PER_BLOCK;
        	printf("Read %d NDEF bytes\n", pNDEF->count);
        }

#ifdef USE_NFC_MUTEX
        // always give back when we took it
        if (NFCGiveMutex()==false) {
            rc_ok = false;
        }
#endif
    }

    return rc_ok;
}

/*
 * cliNFCScan
 *
 * @desc    Scan I2C bus for devices and display any discovered
 *
 * @param   none
 *
 * @return Bool variable - Returns true if function completed correctly
 */

static bool cliNFCScan(void)
{
    bool rc_ok = false;
    uint8_t bufRecv[64] = {0};

    if(Device_HasPMIC())
	{
		printf("Command not supported!\n");
		return false;
	}

#ifdef USE_NFC_MUTEX
    if (NFCTakeMutex())
#endif
    {
		for(int i = 0; i < 0x80; i++)
		{
			nfc.address = i;
			if(I2C_ReadRegister(NFC_BUS, nfc, 0, 16, &bufRecv[0]))
			{
				printf("scanned=%02X\n", i);
			}
		}
#ifdef USE_NFC_MUTEX
        // always give back when we took it
		if (NFCGiveMutex()==false) {
            rc_ok = false;
        }
#endif
    }

    return rc_ok;
}


char NFCHelp[] = {
	"Sub commands:\r\n"
	"read [range]  	 - read NFC tag\n"
	"write      	 - write NFC tag\n"
	"erase      	 - erase NDEF area\n"
	"config     	 - dump the config registers\n"
	"session    	 - show current session\n"
	"pass <0,1>    	 - set pass-through mode\n"
	"scan       	 - scan for I2C devices on bus\n"
	"fix <addr> 	 - set I2C address back to 0xAA\n"
	"fd <0,1,2,3> 	 - set FD mode\n"
	"testmode        - set testmode (simulate App)\n"
	"setmode <c,s,t> - set mode (commissioned, shippable, test - default is factory)\n"
	"loop 			 - Run the NFC UT in a loop (10 times)"
};

/*
 * cliNFCHelp
 *
 * @desc    CLI help for NFC commands
 *
 * @param   - args params
 *
 * @return - true if ok, otherwise false
 */

bool NFC_CLI_Help(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	if((Device_GetHardwareVersion() >= MINIMUM_NFC_HARDWARE_LEVEL) || Device_HasPMIC())
	{
		printf(NFCHelp);
	}
	else
	{
		printf("NFC hardware not available\n");
	}

	return true;
}


/*
 * NFC_CLI_cli
 *
 * @desc    CLI for NFC commands
 *
 * @param   - args params
 *
 * @return - true if ok, otherwise false
 */

bool NFC_CLI_cli( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	if((Device_GetHardwareVersion() < MINIMUM_NFC_HARDWARE_LEVEL) && !Device_HasPMIC())
	{
		printf("NFC hardware not available\n");
		return true;
	}

	if(args)
	{
		if(strcmp((char*)argv[0], "reg") == 0)
		{
			uint8_t val;
			if(NFC_ReadRegister((NXP_Registers_t)argi[1], &val))
			{
				printf("REG %d = 0x%02X\n", argi[1], val);
			}
		}
		else if(strcmp((char*)argv[0], "read") == 0)
		{
			if(args == 2)
			{
				cliNFCDump((uint8_t)argi[1], (uint8_t)argi[1]);
			}
			else if(args > 2)
			{
				cliNFCDump((uint8_t)argi[1], (uint8_t)argi[2]);
			}
			else
			{
				cliNFCRead();
			}
		}
		else if((strcmp((char*)argv[0], "fix") == 0) && args >= 2)
		{
			NFC_Fix((uint8_t)argi[1]);
		}
		else if(strcmp((char*)argv[0], "scan") == 0)
		{
			cliNFCScan();
		}
		else if(strcmp((char*)argv[0], "write") == 0)
		{
			int len = NFC_BuildNDEF(eNDEF_SHIPMODE);
			NFC_WriteNDEF(len);
			printf("%d bytes written to NFC tag\n", len);
		}
		else if(strcmp((char*)argv[0], "erase") == 0)
		{
			NFC_EraseNDEF();
		}
		else if(strcmp((char*)argv[0], "config") == 0)
		{
			NFC_Config();
		}
		else if(strcmp((char*)argv[0], "testmode") == 0)
		{
			// create a 'self-test' NDEF record
			int len = NFC_BuildNDEF(eNDEF_SELFTEST);
			NFC_WriteNDEF(len);
			printf("%d bytes written to NFC tag\n", len);
		}
		else if(strcmp((char*)argv[0], "session") == 0)
		{
			NFC_Session();
		}
		else if((strcmp((char*)argv[0], "fd") == 0) && args >= 2)
		{
			NFC_ConfigSet((uint8_t)argi[1]);
		}
		else if((strcmp((char*)argv[0], "pass") == 0) && args >= 2)
		{
			NFC_SessionConfigSet((uint8_t)argi[1]);
		}
		else if((strcmp((char*)argv[0], "setmode") == 0) && args >= 2)
		{
			cliModeSet((const char*)argv[1]);
		}
		else if((strcmp((char*)argv[0], "loop") == 0))
		{
			for(int i = 0; i < 10; i++)
			{
				printf("\nTest for PMIC Comms Issue, LoopCount: %d\n", i+1);
				cliNFCRead();
				RunUnitTests("nfc");
				cliNFCRead();
			}
			for(int i = 0; i < 10; i++)
			{
				printf("\n---------NFC UT Test LoopCount: %d----------\n", i+1);
				RunUnitTests("nfc");
				cliNFCRead();
			}
		}
#ifdef CRC16
		else if(strcmp((char*)argv[0], "test") == 0)
		{
			testCRC16();
		}
#endif
		else
		{
			printf("unknown command - '%s'\n", argv[0]);
		}
	}
	else
	{
		printf("enter 'help nfc' to get additional help\n");
	}
    return true;
}

/**********************************************************************************************
 * End of NFC CLI commands
 **********************************************************************************************/
#endif



#ifdef __cplusplus
}
#endif