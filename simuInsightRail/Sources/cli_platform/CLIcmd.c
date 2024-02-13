#ifdef __cplusplus
extern "C" {
#endif

/*
 * CLIcmd.c
 *
 *  Created on: Sep 16, 2014
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

#include "dataStore.h"
#include "configData.h"
#include "json.h"
#include "log.h"
#include "configBootloader.h"

// Defines the limit for CLI command / sub-command names.
#define CLI_CMD_SUBCMD_STRING_MAX_LEN		(50)

static struct cmdLine_str  mainCmd ;
// next structs should end in flash, to save ram space
const struct cliIo_str  cliIoMain = {
		CLI_UART_GetCharsInRxBuf,
		CLI_UART_get_ch,
		CLI_put_ch,
		CLI_put_ch_nb,
		"Main port",
		&mainCmd
};

extern int strcasecmp(const char *, const char *);		// Mutes the compiler warning.
extern void prompt(const struct cliIo_str *ioP);

#if 0
static bool cliwhatever( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	bool rc_ok = true;

	return rc_ok;
}
#endif


static bool cliDump( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	enum {
		tBYTE,
		tSHORT,
		tINT
	};
	bool rc_ok = true;
    if (args == 0)  {
        rc_ok = false;
    } else {
    	uint8_t dType = tBYTE;
        unsigned int i, cnt, plus = 0;
        uint32_t end = 0;
        uint8_t *pbb, *pb = (uint8_t *) argi[0];
        uint16_t *pbw, *pw = (uint16_t *) argi[0];
        uint32_t *pbi, *pi = (uint32_t *) argi[0];

        if (args>1) {
        	for(i = 1; i < args; i++)
        	{
				if (argv[i][0] == '+') {
					plus = argi[i];
				} else if (argv[i][0] == '-') {
					if(argv[i][1] == 'b')
					{
						dType = tBYTE;
					}
					else if(argv[i][1] == 'w')
					{
						dType = tSHORT;
					}
					else if(argv[i][1] == 'i')
					{
						dType = tINT;
					}
				} else {
					if (((unsigned char *)argi[i]) > pb) {
						end = argi[i];
					}
				}
        	}
        }
        switch(dType)
        {
        case tBYTE:
        	if(end == 0) end = (uint32_t)pb + ((plus) ? plus : 16);
			while (end > (uint32_t)pb) {
				pbb = pb;
				printf(" %08X :",(uint32_t)pb);
				cnt = (end - (uint32_t)pb);
				if(cnt>= 16) cnt = 16;

				for(i=0; i < cnt; i++) {
					if (i==8) printf(" ");
					printf(" %02X", *pb++);
				}
				// if required pad
				for(; i < 16; i++) printf("   ");
				printf("  ");
				for(i=0; i < cnt; i++, pbb++) {
					if (i==8) printf(" ");
					printf("%c",*pbb < 32 || *pbb> 126 ? '.' : *pbb);
				}

				put_s("\r\n");
			}
			break;

        case tSHORT:
        	if(end == 0) end = (uint32_t)pw + (((plus) ? plus : 8) * 2);
			while (end > (uint32_t)pw) {
				pbw = pw;
				printf(" %08X :",(uint32_t)pw);
				cnt = (end - (uint32_t)pw) / 2;
				if(cnt>= 8) cnt = 8;

				for(i=0; i < cnt; i++) {
					if (i==8) printf(" ");
					printf(" %04X", *pw++);
				}
				// if required pad
				for(; i < 8; i++) printf("     ");
				printf("  ");
				for(i=0; i < cnt; i++) {
					uint16_t v = *pbw++;
					if (i==4) printf(" ");
					for(int j = 0; j < 2; j++)
					{
						uint8_t b = (v >> (8 * j)) & 0xFF;
						printf("%c",b < 32 || b > 126 ? '.' : b);
					}
				}

				put_s("\r\n");
			}
        	break;

        case tINT:
        	if(end == 0) end = (uint32_t)pi + (((plus) ? plus : 4) * 4);
			while (end > (uint32_t)pi) {
				pbi = pi;
				// print the address
				printf(" %08X :",(uint32_t)pi);
				cnt = (end - (uint32_t)pi) / 4;
				if(cnt>= 4) cnt = 4;

				// dump values
				for(i=0; i < cnt; i++) {
					printf(" %08X",*pi++);
				}
				// if required pad
				for(; i < 4; i++) printf("         ");
				printf("  ");
				// ascii dump
				for(i=0; i < cnt; i++) {
					uint32_t v = *pbi++;
					if (i==2) printf(" ");
					for(int j = 0; j < 4; j++)
					{
						uint8_t b = (v >> (8 * j)) & 0xFF;
						printf("%c",b < 32 || b > 126 ? '.' : b);
					}
				}

				put_s("\r\n");
			}
        	break;
        }
    }
    return rc_ok;
}

static bool cliReboot( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	bool rc_ok = true;

	if(args == 1)
	{
		switch(argi[0])
		{
		case 1:
			gBootCfg.cfg.FromApp.activeApp = 1;
			if(!bootConfigWriteFlash(&gBootCfg))
			{
				LOG_EVENT(0, LOG_NUM_CLI, ERRLOGMAJOR,  "Internal flash failed to program (bootconfig)");
				return true;
			}
			Resources_Reboot(true, "to loader CLI");
			break;

		case 2:
			Resources_Reboot(false, "stay in app");
			break;

		default:
			return true;
			break;
		}
	}
	Resources_Reboot(true, "to loader");

	return rc_ok;
}


//
static bool cliTest( uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	int idx;
	printf("test argc=%d\n", argc);
	for (idx=0; idx<argc; idx++) {
		printf("%d : %s -> %u\n",idx, argv[idx], argi[idx]);
	}
	return true;
}
static bool cliExtHelpTest(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	int idx;
	printf("test extended help\n");
	for (idx=0; idx<argc; idx++) {
		printf("%d : %s -> %d\n",idx, argv[idx], argi[idx]);
	}
	return true;
}

static const char helphelp[] = {
		"The CLI accepts numeric and string arguments, no floats (yet).\r\n"
		"String arguments which contain white spaces must be enclosed in single or double quotes.\r\n"
		"The closing quote must be the same as the starting quote.\r\n"
		"Comment lines should begin with the '#' character.\r\n"
};

static bool cliExtHelpHelp(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	printf(helphelp);
	return true;
}

static bool cliWatchdog( uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	vTaskPrioritySet(NULL,99);
	while(1);
	return true;
}

static bool cliSleep( uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	int secs = argi[0];
	if(secs > 0 && secs <= 180)
	{
		vTaskDelay(secs * 1000);
	}
	else
	{
		printf("error seconds count 1..180\n");
	}
	return true;
}

static bool cliHelp( uint32_t argc, uint8_t * argv[], uint32_t * argi);

//
// list of component/system independent commands
//
static const struct cliCmd standardCommands[] = {
	{"help", "<command>", cliHelp, cliExtHelpHelp },
	{"clitest", "<values>", cliTest, cliExtHelpTest },

	{"dump","address [ end | +count] [-b | -w | -i]", cliDump, NULL},
	{"ver","\t\t\t\tPrint version info.", cliVer, NULL},
	{"reboot","\t\t\tSoftware restart [1 = loader CLI, 2 = app]", cliReboot, NULL},
	{"sleep","<seconds>",cliSleep,NULL},
	{"watchdog","\t\t\tforce watchdog reset", cliWatchdog, NULL}
};



#ifndef CLI_MAXCOMPONENTS
// number of subcomponents which can add commands to the CLI
#define CLI_MAXCOMPONENTS (5)
#endif

//
// limit size array of pointers to the command structs of the component who registered it.
//
static  struct {
	const struct cliCmd * cmds;
	uint32_t numCmds;
} cliAllCommands[CLI_MAXCOMPONENTS] = {
		{standardCommands,sizeof(standardCommands)/sizeof(*standardCommands)}
};

static uint32_t registerred_idx = 1;


/*
 * cliRegisterCommands
 *
 * @desc    passes a pointer to a list of new cli commands
 *
 * @param   cmds	pointer to the structure
 * @param   numCmds number of commands in the structure
 *
 * @returns false if no room to add the pointer
 */

bool cliRegisterCommands( const struct cliCmd * cmds, uint32_t numCmds)
{
	bool rc_ok = true;

	if (registerred_idx < CLI_MAXCOMPONENTS) {
		uint32_t idx;

		for (idx=0; idx < registerred_idx ; idx++) {
			if ( cliAllCommands[idx].cmds == cmds ) rc_ok = false ; // we already got that one
		}

		if (rc_ok) {
			cliAllCommands[registerred_idx].cmds = cmds;
			cliAllCommands[registerred_idx].numCmds = numCmds;
			registerred_idx++;
		}

	} else {
		rc_ok = false; // table full
	}
	return rc_ok;
}

static  const struct cliCmd *  cliFindCmd(uint8_t * cmdstr)
{
	uint32_t idxComp;

	for (idxComp = 0; idxComp < registerred_idx; idxComp++)
	{
		const struct cliCmd * compCmd = cliAllCommands[idxComp].cmds;
		uint32_t idxCmd;

		for (idxCmd = 0; idxCmd < cliAllCommands[idxComp].numCmds; idxCmd++)
		{
			if (strcasecmp(compCmd[idxCmd].cmdstr, (char *)cmdstr) == 0)
			{
				if (compCmd[idxCmd].cmdfunc != NULL)
				{
					return &compCmd[idxCmd]; // No null pointer, so lets hope it is a valid function
				}
				else
				{
					printf("ERROR: missing function pointer for command %s\n",cmdstr);
					return NULL; //command exist, but null pointer as function execute address, so no command.
				}
			}
		}
	}

	return NULL;
}

extern void storeCharacter(const struct cliIo_str *ioP, uint8_t c, bool silent);

bool cliTab(const struct cliIo_str *ioP)
{
	uint32_t idxComp;
	char *cmdstr = ioP->cmdline->buf;
	size_t length = ioP->cmdline->pos;
	int instances = 0;
	uint32_t idxCmd;
	const struct cliCmd * compCmd;

	// have we more than one command which contains the partial
	for (idxComp = 0; idxComp < registerred_idx; idxComp++)
	{
		compCmd = cliAllCommands[idxComp].cmds;

		for (idxCmd = 0; idxCmd < cliAllCommands[idxComp].numCmds; idxCmd++)
		{
			if (strncasecmp(compCmd[idxCmd].cmdstr, (char *)cmdstr, length) == 0)
			{
				instances++;
			}
		}
	}

	// if we've only one, fill in the rest
	if(instances == 1)
	{
		for (idxComp = 0; idxComp < registerred_idx; idxComp++)
		{
			compCmd = cliAllCommands[idxComp].cmds;

			for (idxCmd = 0; idxCmd < cliAllCommands[idxComp].numCmds; idxCmd++)
			{
				if (strncasecmp(compCmd[idxCmd].cmdstr, (char *)cmdstr, length) == 0)
				{
					const char *p = &compCmd[idxCmd].cmdstr[ioP->cmdline->pos];
					while(*p) storeCharacter(ioP, *p++, false);
					storeCharacter(ioP, ' ', false);
				}
			}
		}
	}

	// more than one command so display what's available
	if(instances > 1)
	{
		int i = 0;
		const struct cliCmd * sCompCmd = NULL;
		printf("\r\n");

		for (idxComp = 0; idxComp < registerred_idx; idxComp++)
		{
			compCmd = cliAllCommands[idxComp].cmds;

			for (idxCmd = 0; idxCmd < cliAllCommands[idxComp].numCmds; idxCmd++)
			{
				if (strncasecmp(compCmd[idxCmd].cmdstr, (char *)cmdstr, length) == 0)
				{
					// display two columns of commands which meet the partial command criterion
					char buf[32] = "                               ";
					strncpy(buf, compCmd[idxCmd].cmdstr, strlen(compCmd[idxCmd].cmdstr));
					printf(buf);
					if(!sCompCmd) sCompCmd = &compCmd[idxCmd];
					i++;
					if(!(i & 1)) printf("\r\n");
				}
			}
		}
		if(i & 1) printf("\r\n");

		// fill in matching characters to the partial command
		const char *p = &sCompCmd->cmdstr[ioP->cmdline->pos];
		sCompCmd = NULL;
		do
		{
			i = 0;
			storeCharacter(ioP, *p++, true);
			cmdstr = ioP->cmdline->buf;
			length = ioP->cmdline->pos;
			for (idxComp = 0; idxComp < registerred_idx; idxComp++)
			{
				compCmd = cliAllCommands[idxComp].cmds;

				for (idxCmd = 0; idxCmd < cliAllCommands[idxComp].numCmds; idxCmd++)
				{
					if (strncasecmp(compCmd[idxCmd].cmdstr, (char *)cmdstr, length) == 0)
					{
						i++;
					}
				}
			}
		} while (i == instances);
		storeCharacter(ioP, '\b', true);

		// now re-display the partial command
		ioP->putChar('\n');
		prompt(ioP);
		for(i = 0; i < ioP->cmdline->len; i++)
			ioP->putChar(ioP->cmdline->buf[i]);
		int len = ioP->cmdline->len - ioP->cmdline->pos;
		while(len--)
			ioP->putChar('\b');
	}

	return instances;
}

static bool cliHelp( uint32_t argc, uint8_t * argv[], uint32_t * argi)
{

	uint32_t idxComp;
	bool rc_ok = true;

	if (argc==0) {
		for (idxComp=0; idxComp < registerred_idx; idxComp++) {
			const struct cliCmd *const  compCmd = cliAllCommands[idxComp].cmds;
			uint32_t idxCmd;

			for (idxCmd=0; idxCmd<cliAllCommands[idxComp].numCmds; idxCmd++) {
				printf("# %s %s\n", compCmd[idxCmd].cmdstr, compCmd[idxCmd].short_helpstr);
			}
		}
	} else { // argument present, look now if it is a known command
		const struct cliCmd * cmd;
		if ( (cmd = cliFindCmd( argv[0]) )!=NULL) {
			if (cmd->long_help != NULL) {
				rc_ok=cmd->long_help(argc-1, &argv[1], &argi[1]);
			} else {
				printf("No extra help on %s\n",argv[0]);
			}
		} else {
			printf("Command %s not found\n",argv[0]);
		}
	}

	return rc_ok;// always return error for testing now
}


#define CLIMAXARGS (6)

/*
 * cliHandleCommand
 *
 * @desc    handles a command entered over the cli
 *
 * @param   cmdbuf	pointer character array with the entered command
 * @param   dbg_put_ch where to put the output to
 *
 * @returns false when something is wrong
 */
int CLI_handle_command(char * cmdbuf, uint8_t dbg_put_ch)
{
	uint8_t  *argv[CLIMAXARGS];
	uint32_t argi[CLIMAXARGS];
	uint32_t argc = 0;
	char *cp = cmdbuf;
	bool rc_ok=false;

	/* early exit if empty line or comment */
	if (cmdbuf[0] == '\0' || cmdbuf[0] == '#')
	{
		return false;
	}

	// isolate command and parameters
	while (argc < CLIMAXARGS && *cp != '\0')
	{
		// skip leading space,
		while (*cp == ' ')
		{
			cp++;
		}

		if (*cp != '\0')
		{
			uint8_t endchar = ' ';

			// allow strings enclosed within single or double quotes, string must end with the same type of starting quote
			if (*cp == '\"')
			{
				endchar = '\"';
				cp++;
			}

			if (*cp == '\'')
			{
				endchar = '\'';
				cp++;
			}

			argv[argc] = (uint8_t *) cp;

			while (*cp != '\0' && *cp != endchar )
			{
				cp++; // skip to end of command/parameter
			}

			if (*cp)
			{
				*cp++ = '\0'; // not yet end of string, so continue
			}

			if (argc > 0)
			{
				argi[argc] = strtoul((char *) argv[argc], NULL, 0);// we do not convert the command
			}
			argc++;
		}
	}

	if (argc)
	{
		// we have a command, now look it up
		const struct cliCmd * cmd;
		if ((cmd = cliFindCmd(argv[0])) != NULL)
		{
			rc_ok = cmd->cmdfunc(argc-1, &argv[1], &argi[1]);
		}
	}

	if (rc_ok==false)
	{
		printf("syntax error\n");
	}
	return rc_ok;
}


// TODO: experimental, must come with something smarter...
bool cliSubcommand(uint32_t argc, uint8_t * argv[], uint32_t * argi,  struct cliSubCmd * subCmds_p, uint16_t numSubCmds)
{
    bool rc_ok = false;
    bool found = false;

    if (argc)
    {
        for (int i = 0; i < numSubCmds && !found; i++)
        {
            if (strcasecmp((const char*)argv[0], subCmds_p[i].subcmdstr) == 0)
            {
                found = true;
                rc_ok = subCmds_p[i].cmdfunc(argc-1, &argv[1], &argi[1]);
            }
        }
        if (!found) printf("Error no subcommand : %s\n",argv[0]);
    }
    return rc_ok;
}




#ifdef __cplusplus
}
#endif