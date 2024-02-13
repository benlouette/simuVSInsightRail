#ifdef __cplusplus
extern "C" {
#endif

/*
 * xTaskCLI.c
 *
 *  Created on: 14-Oct-2015
 *      Author: D. van der Velde
 *
 */

#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <portmacro.h>

#include <Resources.h>
#include <xTaskDefs.h>

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "xTaskCLIEvent.h"
#include "xTaskCLI.h"

#include "printgdf.h"
#include "Insight.h"

#include "CLIio.h"
#include "CLIcmd.h"
#include "binaryCLI.h"
#include "json.h"

#include "CS1.h"
#include "Device.h"

#include "linker.h"

#define BS '\b'
/*
 * Data
 */
// data for freeRTOS related resources
#ifndef EVENTQUEUE_NR_ELEMENTS_CLI
#define EVENTQUEUE_NR_ELEMENTS_CLI  2     //! Event Queue can contain this number of elements
#endif

extern bool cliTab(const struct cliIo_str *ioP);

/*
 * FreeRTOS local resources
 */
 TaskHandle_t   _TaskHandle_CLI;// not static because of easy print of task info in the CLI
//static QueueHandle_t  _EventQueue_CLI;
QueueHandle_t  _EventQueue_CLI;


// CLI related data

#ifndef CLI_HISTORY
// number of lines in the command history, can be overruled in the 'configCLI.h' project

#define CLI_HISTORY (4)
#endif

#ifndef CLI_BAUDRATE
#define CLI_BAUDRATE (115200)
#endif

struct cliCmdHistory_str {
	uint8_t prevCmd[CLI_HISTORY][CLI_CMDLEN];
	uint32_t lastIdx;
	uint32_t recallIdx;
} cmdHist;


/*
 * portPut_s
 *
 * @desc    put a zero terminated string out using passed character out routine
 *
 * @param   the zero terminated string
 *
 * @returns number of chars written
 */

static uint32_t portPut_s(void (*putChar)(uint8_t c), char *str) {
    int i;
    i = 0;
    while (str[i])
        putChar((uint8_t) str[i++]);

    return (i);
}

void prompt(const struct cliIo_str *ioP)
{
	portPut_s(ioP->putChar, "\rappl> ");
}

/*
 * init_CLI_UART_event
 *
 * @desc    initialise the static variables
 *
 * @param   -
 *
 * @returns -
 */

void init_CLI_UART_event()
{

    cliIoMain.cmdline->len = 0;
    cliIoMain.cmdline->pos = 0;
    //cliIoSecundary.cmdline->len = 0;

    CLIio_setOutputUart(CLI_DEBUG_PUT_CH);

    // command history buffers initialisation
    for (cmdHist.lastIdx=0; cmdHist.lastIdx < CLI_HISTORY; cmdHist.lastIdx++) {
    	cmdHist.prevCmd[cmdHist.lastIdx][0] = '\0';
    }
    cmdHist.lastIdx=0;
    cmdHist.recallIdx=0;

    CLI_init_serial(CLI_UART_IDX, CLI_BAUDRATE);
}

void functionKey(uint8_t key, uint8_t cliPort)
{
	if(key == 1)
	{
        CLI_handle_command("help", cliPort);
	}
	else
		printf("Function key F%d pressed\n", key);
}

/*
 * storeCharacter
 *
 * @desc    store character into line buffer
 *
 * @param   - ioP - pointer to device handler
 *
 * @param	- c - character to be stored
 *
 * @param	- silent - true don't display chracter
 *
 * @returns -
 */
void storeCharacter(const struct cliIo_str *ioP, uint8_t c, bool silent)
{
    if (c >= ' ' && c < 127 && ioP->cmdline->len < sizeof(ioP->cmdline->buf) - 1) {
    	if(ioP->cmdline->pos < ioP->cmdline->len)
    	{
    		char buf[CLI_CMDLEN];
    		int len = ioP->cmdline->len - ioP->cmdline->pos;
    		ioP->cmdline->buf[ioP->cmdline->len] = 0;
    		strcpy(buf, &ioP->cmdline->buf[ioP->cmdline->pos]);
			ioP->cmdline->buf[ioP->cmdline->pos++] = c;
			if(!silent) ioP->putChar(c);
			ioP->cmdline->len++;
    		strcpy(&ioP->cmdline->buf[ioP->cmdline->pos], buf);
    		if(!silent) portPut_s(ioP->putChar, buf);
			while(len--)
				if(!silent) ioP->putChar(BS);
    	}
    	else
    	{
			ioP->cmdline->buf[ioP->cmdline->len++] = c;
			ioP->cmdline->pos++;
			if(!silent) ioP->putChar(c);
    	}
    } else if (c == BS || c == 127)
    {
        if (ioP->cmdline->len) {
        	if(ioP->cmdline->len == ioP->cmdline->pos)
        	{
        		ioP->cmdline->len--;
        		ioP->cmdline->pos--;
        		if(!silent)
        		{
        			portPut_s(ioP->putChar, "\b \b");
        		}
        	}
        	else if(ioP->cmdline->pos)
        	{
        		ioP->cmdline->buf[ioP->cmdline->len] = 0;
        		strcpy(&ioP->cmdline->buf[ioP->cmdline->pos-1],&ioP->cmdline->buf[ioP->cmdline->pos]);
        		ioP->cmdline->pos--;
        		ioP->cmdline->len--;
        		if(!silent)
        		{
					ioP->putChar(BS);
					portPut_s(ioP->putChar, &ioP->cmdline->buf[ioP->cmdline->pos]);
					portPut_s(ioP->putChar, "\033[K");
					int len = ioP->cmdline->len - ioP->cmdline->pos;
					while(len--)
						ioP->putChar(BS);
        		}
        	}
        }
    }
}

/*
 * handleCR
 *
 * @desc    when \r entered, process command line
 *
 * @param	- cliPort - cli port to use
 *
 * @param   - ioP - poiter to device handler
 *
 * @returns -
 */
static void handleCR(uint8_t cliPort, const struct cliIo_str *ioP, bool b_is_silent)
{
    char cmd[CLI_CMDLEN];
    bool b_echo = (binaryCLI_getMode() == E_CLI_MODE_COMMAND) && !b_is_silent;

    if(b_echo)
    {
    	portPut_s(ioP->putChar, "\r\n");
    }
	ioP->cmdline->buf[ioP->cmdline->len] = 0;

	// do not store almost empty lines
    if (ioP->cmdline->len >1)
    {
		strncpy((char *) cmdHist.prevCmd[cmdHist.lastIdx],  ioP->cmdline->buf, sizeof(cmdHist.prevCmd[cmdHist.lastIdx])-1);
		cmdHist.lastIdx=(cmdHist.lastIdx+1) % CLI_HISTORY;
		cmdHist.recallIdx = cmdHist.lastIdx;
    }

    if (cliPort != CLIio_getOutputUart())
    {
    	// notify switch
    	if(b_echo)
    	{
    		put_s("CLI control switching to port: ");
    		put_s( ioP->notifyText);
    		put_s(".\r\n");
    	}
    	CLIio_setOutputUart(cliPort);
    	if(b_echo)
    	{
    		put_s("\r\nCLI control switched to here, the  ");
    		put_s(ioP->notifyText);
    		put_s(".\r\n");
    	}
    }
// This code not being hit at this position, moved up - TODO - Check with George
//#ifdef  CLI_NOTIFY_ACTIVITY_FUNCTION
//            CLI_NOTIFY_ACTIVITY_FUNCTION(ioP->cmdline->buf, cliPort);
//#endif

    strcpy(cmd, ioP->cmdline->buf);

    char *pCommand = cmd;
    char* pcheck = cmd;
    char* pNextCommand;

    while(pCommand)
    {
    	// look for a semicolon (not in quotes), zero that position and save a pointer
    	bool in_quotes = false;
    	pNextCommand = NULL;
		while(*pcheck)
		{
			if(*pcheck == '"')
			{
				in_quotes = !in_quotes;
			}

			if(!in_quotes && *pcheck == ';')
			{
				*pcheck = 0;
				pNextCommand = ++pcheck;
				break;
			}
			pcheck++;
		}

    	CLI_handle_command(pCommand, cliPort);
   		pCommand = pNextCommand;
    }

    if(b_echo)
    {
    	prompt(ioP);
    }

    ioP->cmdline->len = 0;
    ioP->cmdline->pos = 0;
}

/*
 * handle_CLI_UART_event
 *
 * @desc    collect characters until a line is complete, then call something to handle it
 *
 * @param   -
 *
 * @returns -
 */
static void handle_CLI_UART_event(uint8_t cliPort)
{
    unsigned int c, c2;
    const struct cliIo_str * ioP;
    static bool b_is_silent = false;

    if (cliPort > sizeof(cliIo)/sizeof(*cliIo))
    {
    	cliPort = CLI_DEBUG_PUT_CH; // range check, just to be safe
    }

    if ((ioP = cliIo[cliPort]) == NULL)
    {
    	// more safety actions
    	cliPort = CLI_DEBUG_PUT_CH;
    	ioP = cliIo[cliPort];
    }

#ifdef PLATFORM_APPLICATION
    // TODO - see TODO below
#ifdef  CLI_NOTIFY_ACTIVITY_FUNCTION
    CLI_NOTIFY_ACTIVITY_FUNCTION(ioP->cmdline->buf, cliPort);
#endif

#endif	// #ifdef PLATFORM_APPLICATION

    while (ioP->getCharsInRxBuf() > 0)
    {
        c = ioP->getChar();
        switch (c)
        {
        case '\011':	// tab
        	ioP->cmdline->buf[ioP->cmdline->len] = 0;
        	cliTab(ioP);
        	break;

        case '\033': // escape char
			//		Cursor Up		<ESC>[{COUNT}A		    Moves the cursor up by COUNT rows; the default count is 1.
			//		Cursor Down		<ESC>[{COUNT}B		    Moves the cursor down by COUNT rows; the default count is 1.
			//		Cursor Forward		<ESC>[{COUNT}C	    Moves the cursor forward by COUNT columns; the default count is 1.
			//		Cursor Backward		<ESC>[{COUNT}D		Moves the cursor backward by COUNT columns; the default count is 1.
			c = ioP->getChar();
			if (c == '[')
			{
				c = ioP->getChar();
			switch(c) {
				case 'A':// up
					cmdHist.recallIdx = (cmdHist.recallIdx -2) % CLI_HISTORY;
					// NO BREAK
				case 'B': //down
					cmdHist.recallIdx = (cmdHist.recallIdx +1) % CLI_HISTORY;
					strncpy ((char *) ioP->cmdline->buf, (char *) cmdHist.prevCmd[cmdHist.recallIdx], sizeof(ioP->cmdline->buf)-1);
					//did not work : portPut_s(ioP->putChar, "\r> \033[1K");//Erase End of Line	<ESC>[K	Erases from the current cursor position to the end of the current line.
					ioP->putChar('\r');
					ioP->cmdline->len += 6; // for the prompt length
					while (ioP->cmdline->len--) ioP->putChar(' ');
					prompt(ioP);
					portPut_s(ioP->putChar, ioP->cmdline->buf);
					ioP->cmdline->pos = ioP->cmdline->len = strlen(ioP->cmdline->buf);
					break;

				case 'C':	// right
					if(ioP->cmdline->pos < ioP->cmdline->len)
					{
						ioP->cmdline->pos++;
						portPut_s(ioP->putChar,  "\033[C");	// forward
					}
					break;

				case 'D':	// left
					if(ioP->cmdline->pos)
					{
						ioP->cmdline->pos--;
						ioP->putChar(BS);	// back
					}
					break;

				case '1':
					c2 = ioP->getChar();	// fetch the next character
					switch(c2)
					{
					case 0x7E:	// Home ^[[1~
						if(ioP->cmdline->pos)
						{
							int pos = ioP->cmdline->pos;
							while(pos--)
								ioP->putChar(BS);	// back
							ioP->cmdline->pos = 0;
						}
						break;
					case '1':	// F1 ^[[11~
					case '2':	// F2 ^[[12~
					case '3':	// F3 ^[[13~
					case '4':	// F4 ^[[14~
					case '5':	// F5 ^[[15~
					case '7':	// F6 ^[[17~
					case '8':	// F7 ^[[18~
					case '9':	// F8 ^[[19~
					{
						uint8_t map[] = { 1, 2, 3, 4, 5, 0, 6, 7, 8 };
						c = ioP->getChar();	// remove the 0x7E
						functionKey(map[c2 - '1'], cliPort);
					}
					break;
					default:
						break;
					}
					break;

				case '2':
					c2 = ioP->getChar();	// fetch the next character'
					switch(c2)
					{
					case 0x7E:	// Insert/Ins ^[[2~
						break;
					case '0':	// F9 ^[[20~
					case '1':	// F10 ^[[21~
					case '3':	// F11 ^[[23~
					case '4':	// F12 ^[[24~
					{
						uint8_t map[] = { 9, 10, 0, 11, 12 };
						c = ioP->getChar();	// remove the 0x7E
						functionKey(map[c2 - '0'], cliPort);
					}
					break;
					default:
						break;
					}
					break;

				case '3':	// Delete/Del
					if((ioP->cmdline->len) && (ioP->cmdline->len != ioP->cmdline->pos))
					{
						int count;
						ioP->cmdline->buf[ioP->cmdline->len--] = 0;
						strcpy(&ioP->cmdline->buf[ioP->cmdline->pos], &ioP->cmdline->buf[ioP->cmdline->pos+1]);
						portPut_s(ioP->putChar, &ioP->cmdline->buf[ioP->cmdline->pos]);
						portPut_s(ioP->putChar, "\033[K");
						count = ioP->cmdline->len - ioP->cmdline->pos;
						while(count--)
							ioP->putChar(BS);	// back
					}
					c = ioP->getChar();	// remove the 0x7E
					break;

				case '4':	// End
					if(ioP->cmdline->len)
					{
						int len = ioP->cmdline->len - ioP->cmdline->pos;
						while(len--)
							portPut_s(ioP->putChar,  "\033[C");
						ioP->cmdline->pos = ioP->cmdline->len;
					}
					c = ioP->getChar();	// remove the 0x7E
					break;

				default :
					break;
				}
			}		// c == '['
			else if(c == 'O')
			{
				c = ioP->getChar();
				switch(c)
				{
				case 'C':		// CTRL-right
					while((ioP->cmdline->pos < ioP->cmdline->len) && ioP->cmdline->buf[ioP->cmdline->pos] != ' ')
					{
						ioP->cmdline->pos++;
						portPut_s(ioP->putChar,  "\033[C");	// forward
					}
					while(ioP->cmdline->pos < ioP->cmdline->len)
					{
						if(ioP->cmdline->buf[ioP->cmdline->pos] != ' ')
							break;
						ioP->cmdline->pos++;
						portPut_s(ioP->putChar,  "\033[C");	// forward
					}
					break;
				case 'D':		// CTRL-left
					while(ioP->cmdline->pos && ioP->cmdline->buf[ioP->cmdline->pos-1] == ' ')
					{
						ioP->cmdline->pos--;
						ioP->putChar(BS);	// back
					}
					while(ioP->cmdline->pos)
					{
						ioP->cmdline->pos--;
						ioP->putChar(BS);	// back
						if(ioP->cmdline->buf[ioP->cmdline->pos-1] == ' ')
							break;
					}
					break;
				default:
					break;
				}
			}
			break;	// case '\033'

        case '\r':	// cr
        	handleCR(cliPort, ioP, b_is_silent);
        	b_is_silent = false;
            break;

        case BS:	// bs
        case 127:
        default:	// any other character
        	if((c == '@') && !b_is_silent)
			{
				b_is_silent = true;
			}
			else
			{
				storeCharacter(ioP, c, b_is_silent);
			}
            break;
        }
    }
}

/*
 * xTaskCLI_Init
 *
 * @brief Initialize task context before started by RTOS
 */
void xTaskCLI_Init(void) {

    /*
     * Setup task resources
     */

    // Create event queue
    _EventQueue_CLI    = xQueueCreate( EVENTQUEUE_NR_ELEMENTS_CLI, sizeof( tCLIEvent));
    vQueueAddToRegistry(_EventQueue_CLI, "_EVTQ_CLI");

    // Create task
    xTaskCreate( xTaskCLI,               // Task function name
                 "CLI",                  // Task name string
                 STACKSIZE_XTASK_CLI,    // Allocated stack size on FreeRTOS heap
                 NULL,                   // (void*)pvParams
                 PRIORITY_XTASK_CLI,     // Task priority
                 &_TaskHandle_CLI );     // Task handle

    /*
     * Initialize
     */

    init_CLI_UART_event();

}


/*-------------------------------------------------------------------------------------*
 |                                                                                     |
 *-------------------------------------------------------------------------------------*/
void xTaskCLI(void *pvParameters) {

    uint8_t theCLIEvent;

    /* Write title to CLI */
    // TODO CLI in TEST MODE
    printf("\n---------------------------APP------------------------------\n");
    metadataDisplay(INSIGHT_PRODUCT_NAME,__app_version, true);
    printf("Hardware PCB version detected: %s %s, %s Variant.\r\n",
    		Device_GetHardwareVersionString(Device_GetHardwareVersion()),
			Device_HasPMIC()?"(PMIC present)":"",
			Device_IsHarvester() ? "Harvester":"Battery");

    vTaskDelay(1000/portTICK_PERIOD_MS);

    // Test framework relies on this printf to see that the device has booted
    printf(SYSTEM_BOOT_MSG);

    // Delay is here to allow other messages to be displayed
    if(Device_GetEngineeringMode())
    {
    	vTaskDelay(8000);
    	prompt(cliIo[0]);
    }

    /*
     * Event handler
     */
    while (1) {

        if (xQueueReceive( _EventQueue_CLI, &theCLIEvent, portMAX_DELAY )) {

            switch(theCLIEvent) {

             case CLIEvt_mainCliDataReceived:
                 handle_CLI_UART_event(CLI_DEBUG_PUT_CH);
                 break;

             case CLIEvt_secundaryCliDataReceived:
                 handle_CLI_UART_event(CLI_SECUNDARY_DEBUG_PUT_CH);
                 break;

#ifdef REPEAT_COMMAND_HACK
             case 99:
             {
            	 char buf[20];
            	 sprintf(buf, "ut eventlog");
            	 CLI_handle_command(buf, 0);
             }
            	 break;
#endif

             default:
                 //LOG_EVENT( 0, LOG_NUM_APP, ERRLOGDEBUG, "CLI handler: unknown event %d", theCLIEvent);
                 break;
             }

        }

    }
}

/*
 * xTaskCLI_NotifyCliRxData_ISR
 *
 * @brief Notify CLI task to handle incoming char
 *
 * @param cliInterface   interface on which the character was received
 *
 */
BaseType_t xTaskCLI_NotifyCliRxData_ISR( uint32_t cliInterface )
{
    tCLIEvent evt;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    evt.Data.placeholder = 0;

    switch ( cliInterface ) {
    case 1:
        evt.Descriptor = CLIEvt_secundaryCliDataReceived;
        break;
    default: // Always assume main interface (0)
        evt.Descriptor = CLIEvt_mainCliDataReceived;
        break;
    }

    xQueueSendToBackFromISR( _EventQueue_CLI, &evt, &xHigherPriorityTaskWoken );
    return xHigherPriorityTaskWoken;
}



#ifdef __cplusplus
}
#endif