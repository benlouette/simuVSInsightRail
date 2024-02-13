#ifdef __cplusplus
extern "C" {
#endif

/*
 * xTaskModem.c
 *
 *  Created on: 14-Oct-2015
 *      Author: D. van der Velde
 *
 */

#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <timers.h>
#include <portmacro.h>

#include <Resources.h>
#include <xTaskDefs.h>
#include "event_groups.h"

/*-------------------------------------------------------------------------------------*
 |                                                                                     |
 *-------------------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdarg.h>

//#include "Log.h"
#include "xTaskModemEvent.h"
#include "xTaskModem.h"


#include "printgdf.h"
#include "Insight.h"


#include "Modemio.h"
#include "modemCLI.h"
#include "CS1.h"
#include "PinDefs.h"
#include "Log.h"
#include "Timer.h"

//
//
extern void setIgnoreLF(bool);

#define MODEM_RX	(1)
#define MODEM_DCD	(2)
#define FAILURE		(-1)

static void Modem_readBlockAbort();

/*
 * Data
 */
static EventGroupHandle_t _EventGroup_Modem;
TaskHandle_t         _TaskHandle_Modem;// not static because of easy print of task info in the CLI

// after a response on a AT command, during 100ms no new command must be issued (modem AT spec, to give URC's time to come in between)
#define ATCOMMANDBLOCKOUT_MS (100)

uint16_t g_ModemDebugData[MODEM_DEBUG_BUF_LEN];



tModemStatus modemStatus; // structure holding all modem data

// ------------------------------- Timer Handles -----------------------------------
TimerHandle_t       _timerHandle_ModemTimeout;

static long ModemtimerID = 1234; // not really usefull at the moment
static volatile bool no_carrier_event = false;
static uint32_t modemStopTicks = 0;
static uint32_t modemStartTicks = 0;


#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

//this is for pxTimer
static void Modem_TimerCallback(TimerHandle_t pxTimer)
{
    switch (modemStatus.ioState)
    {
    case MODEMIOSTATE_ATCOMMAND:
    	switch (modemStatus.atCommand.AtCommandState) {
    	default:
    		xSemaphoreGive( modemStatus.atCommand.atWait);
    		break;
    	}
    	break;

    default:

    	break;

    }


}
#pragma GCC diagnostic pop



static void createTimers( void)
{
    // timeout on modem action, mainly for the AT commands
    modemStatus._timerHandle_ModemTimeout = xTimerCreate(  "ModemTimeout", /* tick unit 1.0 sec */ 5000 / portTICK_PERIOD_MS,    /* no reload */ pdFALSE,
            /* timerID (for callbacks) */ &ModemtimerID, /* callback func */ &Modem_TimerCallback);
    if (modemStatus._timerHandle_ModemTimeout == NULL) {
        // LOG_EVENT( 0, LOG_NUM_APP, ERRLOGDEBUG, "_timerHandle_ModemTimeout creation failed");
    }
}



static void startTimer(uint32_t timeout_ms)
{
	 xTimerChangePeriod( modemStatus._timerHandle_ModemTimeout, (TickType_t) timeout_ms / portTICK_PERIOD_MS , (TickType_t) portMAX_DELAY);
	 xTimerStart( modemStatus._timerHandle_ModemTimeout, portMAX_DELAY);

}

#if 0
static void stopTimer()
{
	xTimerStop( modemStatus._timerHandle_ModemTimeout, portMAX_DELAY);
}
#endif
/*
 *
 */


static void startAtCommandBlockout()
{
	startTimer(ATCOMMANDBLOCKOUT_MS/portTICK_PERIOD_MS);
}


/*
 * callback administration
 */

// what I will move to the header file
// what stays in the c file
#define MODEMMAXCALLBACKS   (2)


// will move to the status struct ?
static tModemCallbackFuncPtr ModemCallbackFuncTable[Modem_cb_max][MODEMMAXCALLBACKS];



// Initialize local data with defined values (in this case everything null pointer)
static void ModemInitCallBack()
{
	int i,j;

   for (i=0; i<Modem_cb_max; i++) {
		for (j=0; j<MODEMMAXCALLBACKS; j++) {
			ModemCallbackFuncTable[i][j] = NULL;
		}
	}
}

// remove the callback : safe to be called from another task
void ModemRemoveCallback(tModemCallbackIndex cbIdx, tModemCallbackFuncPtr cbFunc)
{
    uint8_t i;
    if (cbIdx < Modem_cb_max) {

        for (i=0; (i<MODEMMAXCALLBACKS) ; i++) {

            CS1_CriticalVariable();

            CS1_EnterCritical();

            // running in another task context, and a 32 bit pointer assign is not atomic on this processor, so be on the safe side
            if (ModemCallbackFuncTable[cbIdx][i] == cbFunc) {
                ModemCallbackFuncTable[cbIdx][i] = NULL;
            }

            CS1_ExitCritical();
        }
    }
}
// set the callback : safe to be called from another task
void ModemSetCallback(tModemCallbackIndex cbIdx, tModemCallbackFuncPtr cbFunc)
{
    uint8_t i;

    if (cbIdx < Modem_cb_max) {

        ModemRemoveCallback(cbIdx,  cbFunc);// just to prevent multiple entries of this address

        for (i=0; (i < MODEMMAXCALLBACKS) ; i++) {

            if (ModemCallbackFuncTable[cbIdx][i] == NULL) {
                CS1_CriticalVariable();

                CS1_EnterCritical();
                // running in another task context, and a 32 bit pointer assign is not atomic on this processor, so be on the safe side
                ModemCallbackFuncTable[cbIdx][i] = cbFunc;

                CS1_ExitCritical();
                break;// exit the for loop
            }
        }
    }
}


// call possible registered callback functions when we got a mote request
static uint8_t handleCallback (tModemCallbackIndex cbIdx, uint8_t * buf, uint32_t len, void * params)
{
    uint8_t rc = 0;
    if (cbIdx < Modem_cb_max) {
        uint8_t i;
        for (i=0; i < MODEMMAXCALLBACKS ; i++) {

            if (ModemCallbackFuncTable[cbIdx][i] != NULL) {
                uint8_t rc2;

                rc2 = ModemCallbackFuncTable[cbIdx][i]( buf , len, params); // call whoever is interested.

                if (rc2 != 0) rc = rc2; // last callback with error status wins
            }
        }
    }
    return rc;
}


/*
 * end callback administration
 */





/*
 * init_MODEM_UART_event
 *
 * @desc    initialize the static variables
 *
 * @param   baudrate where the modem is supposed to work with (this sets the MCU serial port baudrate, not the modem baudrate !
 *
 * @returns -
 */

static void init_MODEM_UART_event(uint32_t baudrate)
{
	// not much yet, but more will come
	modemStatus.atCommand.ATresponseBufIdx = 0;

	// modemStatus.transparentRxState.readWait = xSemaphoreCreateBinary(); // moved to the task init, so we can call this init again without creating an extra semaphore
	modemStatus.transparentRxState.amount = 0;
	modemStatus.transparentRxState.count = 0;
	modemStatus.transparentRxState.dest = NULL;

	modemStatus.lastCharsInRxBufCount = 0;

	Modem_init_serial(MODEM_UART_IDX, baudrate);
}

// some 'known' responses which signal the end of a AT command response
struct knownATresponses_str {
	char * resp;
	tModemAtRc rc;
} ATresponses[] =
{
		{"ERROR\r\n", AtError },
		{"+CME ERROR:", AtError},
		{"CONNECT\r\n", AtConnect},// after this response, the modem is in transparent mode !
		{"OK\r\n", AtOk},
};




/*
 * debug print functions
 */
static uint8_t handleUnknownResponse(uint8_t *buf, uint32_t len, tModemResultFunc * params)
{

#ifdef DEBUG
    {
        if (dbg_logging & LOG_LEVEL_MODEM)
        {
        	// single message to avoid display corruption
			printf("received response: %s\n", buf);
        }
    }
#endif
    return 0;
}

/*
 * debug print functions
 */
static uint8_t handleURC(uint8_t *buf, uint32_t len, tModemResultFunc * params)
{

#ifdef DEBUG
    {
        if (dbg_logging & LOG_LEVEL_MODEM)
        {
        	// single message to avoid display corruption
        	printf("URC: %s\n", buf);
        }
    }
#endif
    return 0;
}

#if 0
static uint8_t handleIncomingData(uint8_t *buf, uint32_t len, tModemResultFunc * params)
{

#ifdef DEBUG
    {
        if (dbg_logging & LOG_LEVEL_MODEM) {
            printf("Incoming modem Data (%d) and nobody is  paying attention !\n", len);
        }
    }
#endif
    return 0;
}
#endif




/*
 * handle_MODEM_UART_event
 *
 * @desc    collect characters until a line is complete, then call something to handle it
 * 			process AT commands, the response (farmed out when different than OK or ERROR, and the URC's
 *
 * @param   -
 *
 * @returns -
 */
static void handle_MODEM_UART_event()
{
    unsigned int c;


    // Incoming data, what to do depends on what state we are in
    if (modemStatus.ioState == MODEMIOSTATE_ATCOMMAND) {

    	while (Modem_UART_GetCharsInRxBuf() > 0) {
        	// this will be an echo of the AT command, or an URC, collect it in a buffer
        	switch (modemStatus.atCommand.AtCommandState)
        	{
        	case ATfread:
        		*modemStatus.atCommand.ATcopyptr++ = Modem_UART_get_ch();
        		if(--modemStatus.atCommand.ATcopycount == 0)
        		{
					modemStatus.atCommand.AtCommandState = ATresponse;
					modemStatus.atCommand.ATresponseBufIdx=0;
					modemStatus.atCommand.ATlookForEnd=true;
					setIgnoreLF(false);
        		}
        		break;
        	case ATsend_echo:
        		// check the command echo
        		c=Modem_UART_get_ch();
        		if (c==modemStatus.atCommand.AtCommand.cmd[modemStatus.atCommand.echoIdx]) {
        			modemStatus.atCommand.echoIdx++;
        		} else {
        			// end of line ?
        			if (c=='\r' || c=='\n' || modemStatus.atCommand.AtCommand.cmd[modemStatus.atCommand.echoIdx]=='\0' ) {
        				if (c=='\n'  && modemStatus.atCommand.AtCommand.cmd[modemStatus.atCommand.echoIdx]=='\0' ) {
        					modemStatus.atCommand.AtCommandState = ATresponse;
        					modemStatus.atCommand.ATresponseBufIdx=0;
        					modemStatus.atCommand.ATlookForEnd=true;

        				} else {
        					if (c!='\r') {
        						// throw some error
        						modemStatus.atCommand.AtCommandState = ATfinished;

        						//stopTimer();
        						modemStatus.atCommand.atRc = AtError;
        						xSemaphoreGive(modemStatus.atCommand.atWait);// let the calling task know that we quit with this command

        						LOG_DBG(LOG_LEVEL_MODEM,"remote echo not what expected  %02x != %02x!\n", c, modemStatus.atCommand.AtCommand.cmd[modemStatus.atCommand.echoIdx] );
        					}
        				}
        			}
        		}
        		break;

        	case ATresponse:
        		// check the response, farm out to external delivered function if not NULL pointer
        		// finishes if line contains ERROR<cr><lf> or OK<cr><lf>
            	c = Modem_UART_get_ch();
            	if (modemStatus.atCommand.ATlookForEnd) {
            		if( modemStatus.atCommand.ATresponseBufIdx<sizeof(modemStatus.atCommand.ATresponseBuf)) {
            			modemStatus.atCommand.ATresponseBuf[modemStatus.atCommand.ATresponseBufIdx++]=c;
						if (c=='\n') {
							uint16_t i;
							// did we get a known response
							for(i=0; i<sizeof(ATresponses)/sizeof(*ATresponses); i++) {
								if (0==strncmp((char *) modemStatus.atCommand.ATresponseBuf, (char *)ATresponses[i].resp, strlen(ATresponses[i].resp))) {

                                    modemStatus.atCommand.ATresponseBuf[modemStatus.atCommand.ATresponseBufIdx] = '\0'; // makes it easier to use standard string functions
                                    LOG_DBG(LOG_LEVEL_MODEM,"found %s",(char *) modemStatus.atCommand.ATresponseBuf);

									modemStatus.atCommand.AtCommandState = ATfinished;
									modemStatus.atCommand.ATresponseBufIdx=0;

	        						if (ATresponses[i].rc == AtConnect) {
	        							modemStatus.ioState = MODEMIOSTATE_TRANSPARENT;
	        						}
	        						//stopTimer();
	        						modemStatus.atCommand.atRc = ATresponses[i].rc;
	        						xSemaphoreGive(modemStatus.atCommand.atWait);// let the calling task know that we are done with this command

									break;// fall out of the for loop
								}
							}
							if (modemStatus.atCommand.AtCommandState != ATfinished) {
								if ((modemStatus.atCommand.ATresponseBufIdx >2 ) || (modemStatus.atCommand.ATresponseBuf[0]!='\r')) {
									modemStatus.atCommand.ATresponseBuf[modemStatus.atCommand.ATresponseBufIdx] = '\0'; // makes it easier to use standard string functions
									handleCallback(Modem_cb_at_response, modemStatus.atCommand.ATresponseBuf, modemStatus.atCommand.ATresponseBufIdx, modemStatus.atCommand.AtCommand.resultProcessor);
								}
								modemStatus.atCommand.ATresponseBufIdx=0; // start looking again
							}
						}
            		} else {
            			modemStatus.atCommand.ATlookForEnd = false;
            			modemStatus.atCommand.ATresponseBufIdx = 0;
            		}

            	} else {
            		// only look for '\n'
            		modemStatus.atCommand.ATlookForEnd = modemStatus.atCommand.ATlookForEnd || (c =='\n');
            	}

        		break;

        	case ATfinished:
        		// got something in AT mode, but no AT command in progress, must be an URC
        		//next state
        		modemStatus.atCommand.AtCommandState = ATreceivingUrc;
        		modemStatus.atCommand.ATresponseBufIdx=0;// make sure we start at zero
        		// NO BREAK intended
        	case ATreceivingUrc:
        		// got something in AT mode, but no AT command in progress, must be an URC
            	c = Modem_UART_get_ch();
            	//put_ch(c);
            	if( modemStatus.atCommand.ATresponseBufIdx < sizeof(modemStatus.atCommand.ATresponseBuf)) {
            		modemStatus.atCommand.ATresponseBuf[modemStatus.atCommand.ATresponseBufIdx++]=c;
            		if (c=='\n') {
            			modemStatus.atCommand.ATresponseBuf[--modemStatus.atCommand.ATresponseBufIdx]='\0';

            			// if not an empty line, pass it along
            			if ((modemStatus.atCommand.ATresponseBufIdx != 1) || (modemStatus.atCommand.ATresponseBuf[0]!='\r')) {
            				handleCallback(Modem_cb_urc_received, modemStatus.atCommand.ATresponseBuf, modemStatus.atCommand.ATresponseBufIdx, NULL);
            			}
            			modemStatus.atCommand.ATresponseBufIdx=0;
            			modemStatus.atCommand.AtCommandState = ATfinished;
            		}
            	} else {
            		LOG_DBG(LOG_LEVEL_MODEM,"URC buffer overflow\n");
            		// send out what we got so far?
            		handleCallback(Modem_cb_urc_received, modemStatus.atCommand.ATresponseBuf, modemStatus.atCommand.ATresponseBufIdx, NULL);
            		modemStatus.atCommand.ATresponseBufIdx=0;
            		modemStatus.atCommand.AtCommandState = ATfinished;
            	}
        		break;
        	}
    	} // end while chars in buf
    }

	// we could have switch to transparent mode, with some chars left in the buffer
	if (modemStatus.ioState == MODEMIOSTATE_TRANSPARENT) {

        // not the most nice solution, but this will change anyway when going for uart DMA
        // and if not going for DMA, this must move to the ISR, but not today
        if (modemStatus.transparentRxState.count) {
            //bool log= (Modem_UART_GetCharsInRxBuf() > 0);
            //if (log) LOG_DBG(LOG_LEVEL_MODEM,"\ntransparent:");
#ifdef MODEM_RX_IRQ_BLOCKMODE
            CS1_CriticalVariable();
            CS1_EnterCritical();
#endif
            while ((Modem_UART_GetCharsInRxBuf() > 0) && modemStatus.transparentRxState.count ) {
                uint8_t c;
                c= Modem_UART_get_ch();
                *modemStatus.transparentRxState.dest++ = c;
                modemStatus.transparentRxState.count--;
                //LOG_DBG(LOG_LEVEL_MODEM,"%02x",c);
            }
#ifdef MODEM_RX_IRQ_BLOCKMODE
            CS1_ExitCritical();
#endif
            //if (log) LOG_DBG(LOG_LEVEL_MODEM,"\n");
            if (modemStatus.transparentRxState.count == 0) {
                xSemaphoreGive( modemStatus.transparentRxState.readWait );
                modemStatus.lastCharsInRxBufCount = 0;
            }
        } else {
            // nobody expects these, so maybe we should give a 'hint' ?
            uint32_t count = Modem_UART_GetCharsInRxBuf();

            if (count > modemStatus.lastCharsInRxBufCount) {
                modemStatus.lastCharsInRxBufCount = count;
                // callback !
                handleCallback(Modem_cb_incoming_data, NULL, count, NULL);
            }
        }
    }
}



/*
 * xTaskModem_Init
 *
 * @brief Initialize the modem task and its resources
 *
 */
void xTaskModem_Init()
{
    /*
     * Initialize task resources
     */
    // Create event queue
	_EventGroup_Modem = xEventGroupCreate();
    vQueueAddToRegistry(_EventGroup_Modem, "_EVTQ_MODEM");

    modemStatus.semAccess =  xSemaphoreCreateBinary();
    modemStatus.atCommand.atWait =  xSemaphoreCreateBinary();
    modemStatus.transparentRxState.readWait = xSemaphoreCreateBinary();

    ModemInitCallBack();

    // Create task
    xTaskCreate( xTaskModem,               // Task function name
                 "MODEM",                  // Task name string
                 STACKSIZE_XTASK_MODEM,    // Allocated stack size on FreeRTOS heap
                 NULL,                     // (void*)pvParams
                 PRIORITY_XTASK_MODEM,     // Task priority
                 &_TaskHandle_Modem );     // Task handle


    /*
     * initialize component
     */
    createTimers();

    // Init UART
    init_MODEM_UART_event(115200);// actual baudrate is in the configuration (flash)

    // Register Modem CLI commands
    modemCliInit();
}


/*-------------------------------------------------------------------------------------*
 |                                                                                     |
 *-------------------------------------------------------------------------------------*/
void xTaskModem(void *pvParameters) {

    /*
     * Init
     */

	ModemSetCallback(Modem_cb_at_response, handleUnknownResponse);
	ModemSetCallback(Modem_cb_urc_received, handleURC);
    ModemSetCallback(Modem_cb_dcd_no_carrier, Modem_handleDCD);
    //ModemSetCallback(Modem_cb_incoming_data, handleIncomingData); // to wake up some sleeping' process, called when data is received in transparent mode, and no read is requested

    modemStatus.atCommand.AtCommandState = ATfinished;
	modemStatus.ioState = MODEMIOSTATE_DOWN;

    xSemaphoreGive( modemStatus.semAccess );

    { // TODO do not forget to remove this after testing, and organize this differently after release 2
    	void Modem_drv_init();
    	Modem_drv_init();
    }

    while (1)
    {
    	EventBits_t bits = xEventGroupWaitBits(_EventGroup_Modem, (MODEM_DCD + MODEM_RX), pdTRUE, pdFALSE, portMAX_DELAY);
    	if(bits & MODEM_RX)
    	{
    		handle_MODEM_UART_event();
        }

    	if(bits & MODEM_DCD)
    	{
			// TODO flush serial buffers
			// TODO set driver in AT mode
			if (modemStatus.ioState != MODEMIOSTATE_DOWN)
			{
				 // inform potentially interested modules that this happened
				 handleCallback(Modem_cb_dcd_no_carrier, NULL, 0, NULL);
			}
			//LOG_DBG(LOG_LEVEL_MODEM,"\nNo Carrier event triggered\n");
			// only relevant in transparent mode
			// drop out of transparent mode into AT mode, reset buffersetc

			if (modemStatus.ioState == MODEMIOSTATE_TRANSPARENT)
			{
				 // we will drop out of transparent mode
				 modemStatus.atCommand.AtCommandState = ATfinished;
				 modemStatus.ioState = MODEMIOSTATE_ATCOMMAND;
				 // if read in progress, abort read
				 Modem_readBlockAbort();
				 // if write in progress, abort write
				 Modem_writeBlockAbort();
			}
    	}
    } // while forever loop
}



/*
 * this should be restructured when going to a DMA driver for the uart, now just to get it working without looking at the performance implications
 */
uint32_t Modem_readBlock( uint8_t *data, uint32_t len, uint32_t timeout )
{
	bool rc_ok = true;
	uint32_t bytesRead = 0;

    CS1_CriticalVariable();

    CS1_EnterCritical();
	modemStatus.transparentRxState.dest = data;
	modemStatus.transparentRxState.count = len;
	modemStatus.transparentRxState.amount = len;
    CS1_ExitCritical();

    // make sure semaphore is cleared
    xSemaphoreTake( modemStatus.transparentRxState.readWait, ( TickType_t ) 0 ) ;

    // kick off the read by a dummy event, does not harm
    xEventGroupSetBits(_EventGroup_Modem, MODEM_RX);

	// wait for semaphore or timeout
	if (rc_ok)
	{
		rc_ok = (xSemaphoreTake( modemStatus.transparentRxState.readWait, ( TickType_t ) timeout ) == pdTRUE);
	    CS1_CriticalVariable();
	    CS1_EnterCritical();

		bytesRead = len - modemStatus.transparentRxState.count;
		modemStatus.transparentRxState.count = 0; // essentially stops reading anything that arrives now.

	    CS1_ExitCritical();

		if (rc_ok == false)
		{
			g_ModemDebugData[0] = 1;
			g_ModemDebugData[1] = timeout;
			// TODO : if timeout occurred, set modem state in error ?
		}
	}
	else
	{
		g_ModemDebugData[0] = 2;
		g_ModemDebugData[1] = timeout;
	}
	return bytesRead;

}

static void Modem_readBlockAbort()
{
    // read is in progress, if there are still bytes to read
    if (modemStatus.transparentRxState.count) {
        // this will act as if the read is done by the ISR task, but bytesRead will be less then requested
        xSemaphoreGive(modemStatus.transparentRxState.readWait);
    }
}

/*
 * Modem_NotifyRxData_ISR
 *
 * @brief Notify Modem task to handle incoming char
 *
 * @param modemInterface   interface on which the character was received
 *
 */
BaseType_t Modem_NotifyRxData_ISR( uint32_t modemInterface )
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    xEventGroupSetBitsFromISR(_EventGroup_Modem, MODEM_RX, &xHigherPriorityTaskWoken);

    return xHigherPriorityTaskWoken;
}


/*
 * Modem_DCD_Irq
 *
 * @desc    Called in interrupt context (from event.c when using processor expert) when the DCD line signals loss of connection
 * 			(equivalent to the 'NO CARRIER' message you get in transparent mode)
 * 			will put a message to the modem task that signal is lost.
 *
 * @param   modemInterface interface on which the character was received (future extension, when we want more modems on one device)
 *
 * @returns -
 */
void Modem_DCD_ISR(uint32_t modemInterface )
{
		 // only relevant when the modem is in the transparent state
	if (modemStatus.ioState == MODEMIOSTATE_TRANSPARENT)
	{
		xEventGroupSetBitsFromISR(_EventGroup_Modem, MODEM_DCD, NULL);
		 }
	 }



// on->off causes terminate transparent mode (while connection remains active)
// this may be a modem specific hardware feature !
void ModemSetDtr(bool val)
{
	GPIO_DRV_WritePinOutput( CELLULAR_DTR , !val ); // hardware inverts DTR before entering the modem
}


/*
 * Terminate the modem and powerdown
 * set internal variables back to sane values
 */
void ModemTerminate(uint32_t maxWait)
{
	// grab access semaphore
	if (pdTRUE != xSemaphoreTake(	modemStatus.semAccess , maxWait/portTICK_PERIOD_MS)) {
		LOG_DBG(LOG_LEVEL_MODEM,"ModemTerminate: access semTake failed, continue anyway\n" );
	}
	configModem_PowerOff();

	modemStatus.atCommand.AtCommandState = ATfinished;
	modemStatus.ioState = MODEMIOSTATE_DOWN;
	// we are done, so unlock the resource
	xSemaphoreGive( modemStatus.semAccess );

}

/*
 * startup the modem and power
 * set internal variables back to sane values
 */
bool ModemInit(uint32_t baudrate, uint32_t maxWait)
{
	bool rc_ok = true;

	// grab access semaphore
	if (pdTRUE != xSemaphoreTake(	modemStatus.semAccess , maxWait/portTICK_PERIOD_MS)) {
		LOG_DBG(LOG_LEVEL_MODEM,"ModemTerminate: access semTake failed\n" );

		rc_ok = false;
	}
	else
	{
		modemStatus.atCommand.ATresponseBufIdx = 0;
		modemStatus.atCommand.AtCommandState = ATfinished;
		modemStatus.ioState = MODEMIOSTATE_ATCOMMAND;
		//
		rc_ok = configModem_PowerOn();
		// lets also re-init everything related to the serial port handling, because the poweron has been changing i/o pin configs
		init_MODEM_UART_event(baudrate);

		// we are done, so unlock the resource
		xSemaphoreGive( modemStatus.semAccess );
		xSemaphoreGive( modemStatus.atCommand.atWait );
	}

	return rc_ok;
}

/**
 * @brief   Sends an AT command to the modem
 *			Note the two result codes: one from the modem task (wrong state, command write timeout etc.),
 *			and the AT command result code
 *
 * @param[in,out] resultFunc   optional pointer to function to analyze the result string (when it is more complex than OK or ERROR etc.)
 * @param[in]     maxAtWait    how long to wait for the completion of the AT command
 * @param[out]    pAtRc   	   optional pointer to the AT command result code
 * @param[in]     cmd    	   the AT command (null terminated string) (if NULL pointer then the 'drop out of transparent mode' is done
 * @param	...
 *
 * @returns      modem task result code
 */
tModemTaskRc modemSendAt(tModemResultFunc * resultFunc, uint32_t maxAtWait, tModemAtRc *pAtRc, const char *cmd, ...)
{
	va_list ap;
	tModemTaskRc taskRc;
	uint32_t maxWait = 2* maxAtWait;// derive the freeRtos wait time from the maximum time the AT command should take
	taskRc = ModemRcOk;
	char fmtCmd[90];

	if(cmd)
	{
		va_start(ap, cmd);
		int len = vsnprintf(fmtCmd, sizeof(fmtCmd), cmd, ap);
		va_end(ap);
		if(len >= (sizeof(fmtCmd) - 2))
		{
			return ModemRcCmd;
		}
	}
	else
	{
		// just in case we need the message
		strcpy(fmtCmd, "(NULL)");
	}

	// command only accepted when in AT command mode, with exception the command to exit transparent mode)
	if ( !( (modemStatus.ioState == MODEMIOSTATE_ATCOMMAND) || ((cmd == NULL ) && (modemStatus.ioState == MODEMIOSTATE_TRANSPARENT) )))
	{
		// MODEM in wrong state !
		taskRc = ModemRcWrongState;
		LOG_DBG(LOG_LEVEL_MODEM,"%s: wrong modem state : %d, cmd:%s, lastcmd:%s\n",__func__, modemStatus.ioState, fmtCmd, modemStatus.atCommand);
	}
	else
	{
		// grab access semaphore
		if (pdTRUE != xSemaphoreTake(	modemStatus.semAccess , maxWait/portTICK_PERIOD_MS))
		{
			// modem access failed/timed out
			// return busy status
			 taskRc = ModemRcBusy;
			 LOG_DBG(LOG_LEVEL_MODEM,"%s: access semTake failed\n", __func__);
		}
		else
		{
			bool rc_ok = true;
			// If the command is NULL, then we intend to exit the transparent mode
			// so none of the following is necessary, go to the else part straight away.
			if(cmd != NULL)
			{
				// first be sure we have not just finished another AT command
				xSemaphoreTake(	modemStatus.atCommand.atWait , ATCOMMANDBLOCKOUT_MS/portTICK_PERIOD_MS); // should take 100ms or less


				if (modemStatus.atCommand.AtCommandState == ATreceivingUrc )
				{
					// unlikely, and may not take more than a few milliseconds
					// to prevent hanging in a URC receive state when it is probably because of received garbage,
					// give it some time to finish, and continue then anyway with the new AT command
					LOG_DBG(LOG_LEVEL_MODEM,"%s: receiving URC, wait a little.\n", __func__)
					vTaskDelay(10/portTICK_PERIOD_MS);
				}

				// it seems we can now send a string to the modem
				modemStatus.atCommand.AtCommand.resultProcessor = resultFunc /* tResultProcessorFuncPtr */;
				modemStatus.atCommand.AtCommand.cmd = (uint8_t*)fmtCmd;
				modemStatus.atCommand.AtCommandState = ATsend_echo;
				modemStatus.atCommand.echoIdx = 0;

				LOG_DBG(LOG_LEVEL_MODEM,"%s: sending to modem : %s\n", __func__, fmtCmd);
				if (rc_ok)
				{
					modemStartTicks = xTaskGetTickCount();
					// add CR to message
					strcat(fmtCmd, "\r");
					rc_ok = Modem_put_s((char *) fmtCmd);// send the command to the modem UART
	       			// remove the CR at the end of the command now that it's been sent
	       			fmtCmd[strlen(fmtCmd) - 1] = 0;
					if (rc_ok == false)
					{
						// reinit whole modem stuff !
						// TODO : recover from unexpected modem behaviour
						taskRc = ModemRcTimeout;
						modemStopTicks = xTaskGetTickCount();
						LOG_DBG(LOG_LEVEL_MODEM,"%s: %s write command timed out in : %d msecs\n", __func__, fmtCmd, (modemStopTicks - modemStartTicks));
					}
				}
			}
			else
			{
				// we will drop out of transparent mode
				// if read in progress, abort read
				Modem_readBlockAbort();
				// if write in progress, abort write
				Modem_writeBlockAbort();
				xSemaphoreGive( modemStatus.atCommand.atWait);
				modemStartTicks = xTaskGetTickCount();
				// the special sequence to drop out of transparent mode, it is just like an AT command,
				ModemSetDtr(1);
				//vTaskDelay( 1 / portTICK_PERIOD_MS);// to be sure, wait 1 millisec

				modemStatus.atCommand.AtCommand.cmd = (uint8_t*)cmd;
				modemStatus.atCommand.AtCommandState = ATreceivingUrc;
				modemStatus.atCommand.echoIdx = 0;
				modemStatus.atCommand.ATresponseBufIdx=0;
				modemStatus.ioState = MODEMIOSTATE_ATCOMMAND;
				ModemSetDtr(0); //  according to manual, dtr from on->off lets it drop out of transparent mode, but the 'OK' response comes after it is high again
				//vTaskDelay( 1 / portTICK_PERIOD_MS);// to be sure, wait 1 millisec
				DelayMicrosecs(1000);
				ModemSetDtr(1);
			}

       		if (rc_ok)
			{
       			// now wait until this command finishes, or times out.
				if (pdTRUE == xSemaphoreTake( modemStatus.atCommand.atWait   ,  maxAtWait/portTICK_PERIOD_MS))
				{
					taskRc = ModemRcOk; // the command is send but the modem returned result may be wrong though
					modemStopTicks = xTaskGetTickCount();
					LOG_DBG(LOG_LEVEL_MODEM,"%s: %s, MaxTime:%d, ResponseTime: %d (msec), Result(0->OK, 1->Connect, 2->Err): %d\n",
							__func__, fmtCmd, maxAtWait/portTICK_PERIOD_MS, (modemStopTicks - modemStartTicks), modemStatus.atCommand.atRc);
					if (pAtRc!= NULL) *pAtRc = modemStatus.atCommand.atRc;

				}
				else
				{
					// now what, recover somehow ?
					// reinit whole modem stuff !
					// TODO : recover from unexpected modem behavior
					taskRc = ModemRcTimeout;
					modemStopTicks = xTaskGetTickCount();
					LOG_DBG(LOG_LEVEL_MODEM,"%s: %s, wait for response timed out after: %d msecs!, MaxWait: %d msecs\n",
							 __func__, fmtCmd, (modemStopTicks - modemStartTicks), maxAtWait/portTICK_PERIOD_MS );
				}
       		}

       		// next AT command should not be issued directly after this one
       		startAtCommandBlockout();
			modemStopTicks = 0;
       		modemStartTicks = 0;
       		// we are done, so unlock the resource
       		xSemaphoreGive( modemStatus.semAccess );
		}
	}

	return taskRc;
}


// returns the number of bytes written
int Modem_write(uint8_t *data, uint32_t len, uint32_t timeoutMs)
{
	int writecount = FAILURE;

	// command only accepted when in AT command mode, with exception the command to exit transparent mode)
	if (modemStatus.ioState != MODEMIOSTATE_TRANSPARENT)
	{
		// MODEM in wrong state !
		// we wont write a single byte
		LOG_DBG(LOG_LEVEL_MODEM,"MODEM_write: wrong modem state : %s\n",MODEM_IOSTATETOSTRING(modemStatus.ioState));
	}
	else
	{
//	    SEGGER_SYSVIEW_OnUserStart(0x3);
		// grab access semaphore
		if (pdTRUE != xSemaphoreTake(modemStatus.semAccess , timeoutMs/portTICK_PERIOD_MS))
		{
			// we wont write a single byte
			LOG_DBG(LOG_LEVEL_MODEM,"MODEM_write: access semTake failed\n" );
		}
		else
		{
       		writecount = (int)Modem_writeBlock(data,  len,  timeoutMs/portTICK_PERIOD_MS);
       		// we are done, so unlock the resource
       		xSemaphoreGive( modemStatus.semAccess );
		}
//		SEGGER_SYSVIEW_OnUserStop(0x3);
	}
	return writecount;
}

// returns the number of bytes read
int Modem_read(uint8_t *data, uint32_t len, uint32_t timeoutMs)
{
	int readcount = FAILURE;

	// command only accepted when in AT command mode, with exception the command to exit transparent mode)
	if(modemStatus.ioState != MODEMIOSTATE_TRANSPARENT)
	{
		// MODEM in wrong state !
		// we wont write a single byte
		LOG_DBG(LOG_LEVEL_MODEM,"MODEM_read: wrong modem state : %d\n",modemStatus.ioState);
	}
	else
	{
//	    SEGGER_SYSVIEW_OnUserStart(0x4);
		// grab access semaphore
		if (pdTRUE != xSemaphoreTake(modemStatus.semAccess , timeoutMs/portTICK_PERIOD_MS))
		{
			// we wont write a single byte
			LOG_DBG(LOG_LEVEL_MODEM,"MODEM_read: access semTake failed\n" );
		}
		else
		{
       		readcount = (int)Modem_readBlock(data,  len,  timeoutMs/portTICK_PERIOD_MS);
       		// we are done, so unlock the resource
       		xSemaphoreGive( modemStatus.semAccess );
		}
//		SEGGER_SYSVIEW_OnUserStop(0x4);
	}
	return readcount;
}

void Modem_print_info()
{
	printf("Modem:\n");
	printf("ioState = %s\n", MODEM_IOSTATETOSTRING(modemStatus.ioState));
	printf("AtCommandState = %s\n", MODEM_ATCOMMANDSTATETOSTRING(modemStatus.atCommand.AtCommandState ) );
	printf("Characters in responseBuf = %d\n", modemStatus.atCommand.ATresponseBufIdx);
	printf("transparent mode read status:\n");
	printf("dest address=%08x, amount=%d, count=%d\n",modemStatus.transparentRxState.dest, modemStatus.transparentRxState.amount, modemStatus.transparentRxState.count);
}

/*
 * Modem_IsDCDEventFlagSet
 *
 * @desc    Function to check if the DCD event flag is set. If set, it indicates
 * 			a network disconnect.
 * @return 	True, if network is disconnected, false if the connection is intact.
 *
 */
bool Modem_IsDCDEventFlagSet()
{
	return no_carrier_event;
}

/*
 * Modem_ClearDCDEventFlag
 *
 * @desc    Function to clear the DCD event flag.
 *
 */
void Modem_ClearDCDEventFlag()
{
	no_carrier_event = false;
}

/*
 * Modem_handleDCD
 *
 * @desc    Function to handle the DCD event / No carrier event. The
 * 			no_carrier_event flag is set to TRUE when this event occurs,
 * 			indicating a network disconnect.
 * @return 	0.
 *
 */
uint8_t Modem_handleDCD(uint8_t *buf, uint32_t len, tModemResultFunc * params)
{
	no_carrier_event = true;
	LOG_EVENT(10, LOG_NUM_MODEM,ERRLOGINFO,"DCD (no carrier) event");
    return 0;
}

void Modem_WriteModemDebugDataAsEvents()
{
	char *pFormattedStrBuf = (((char*)(g_pSampleBuffer))+0x10000);

	// Write the modem debug string name into the buffer.
	int len = sprintf(pFormattedStrBuf, "g_ModemDebugData = %d", g_ModemDebugData[0]);

	// format all the modem debug data as string.
	for(int i = 1 ; i < MODEM_DEBUG_BUF_LEN; i++)
	{
		len += sprintf(&pFormattedStrBuf[len], ", %d", g_ModemDebugData[i]);
	}

	LOG_EVENT(10, LOG_NUM_MODEM, ERRLOGDEBUG, pFormattedStrBuf);
}


#ifdef __cplusplus
}
#endif