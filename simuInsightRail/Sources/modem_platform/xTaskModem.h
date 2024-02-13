#ifdef __cplusplus
extern "C" {
#endif

/*
 * xTaskComm.h
 *
 *  Created on: 30  Nov 2015
 *      Author: D. van der Velde
 */

#ifndef XTASKMODEMHANDLER_H_
#define XTASKMODEMHANDLER_H_

/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>

#include <FreeRTOS.h>
#include <timers.h>
#include <semphr.h>
#include <portmacro.h>


// #include "ModemIo.h"

#include "xTaskModemEvent.h"

#define MODEM_DEBUG_BUF_LEN				(12)
/*
 * status returned by the modem task, ok or an indication why a command is not done.
 */
typedef enum
{
    ModemRcOk = 0,
    ModemRcTimeout,
    ModemRcBusy,
	ModemRcWrongState,
	ModemRcCmd
} tModemTaskRc;

/*
 * some common status values from AT commands
 */
typedef enum
{
    AtOk = 0,
	AtConnect,
	AtError,
} tModemAtRc;


//void (*resultProcessor) (uint8_t * str, uint32_t len, struct modemAtResponse * respResult);
typedef void (* tResultProcessorFuncPtr)(uint8_t * str, uint32_t len, void * params);

typedef struct modemResultFunc {
	tResultProcessorFuncPtr  resultProcessor;
	void * params;
} tModemResultFunc;

struct modemAtCommandReq {
	uint8_t * cmd;
	tModemResultFunc * resultProcessor;//result processing function pointer
	uint32_t maxWait; // in ms
};


/*
 * Macros
 */

#define MAXATRESPONSEBUFSIZE (120)

/*
 * Types
 */
typedef enum {
	MODEMIOSTATE_DOWN = 0,	// modem not ready for use, powered down/ startup message not yet received etc.
	MODEMIOSTATE_ATCOMMAND, // modem is supposed to be in AT command mode
	MODEMIOSTATE_TRANSPARENT,   // modem is in transparent mode
	MODEMIOSTATE_MAX           // Max enumeration value (not a setting)
} tModemIoStates;

#define MODEM_IOSTATETOSTRING(m_level) \
	((m_level) == MODEMIOSTATE_DOWN) ? "MODEMIOSTATE_DOWN" : \
	((m_level) == MODEMIOSTATE_ATCOMMAND) ? "MODEMIOSTATE_ATCOMMAND" : \
	((m_level) == MODEMIOSTATE_TRANSPARENT) ? "MODEMIOSTATE_TRANSPARENT" : \
			"unknown"

typedef enum
{
    ATsend_echo = 0, // at command send/echo checking phase
	ATresponse,// modem sends response
	ATfinished,//
	ATreceivingUrc,
	ATfread

} tAtCommandState;

#define MODEM_ATCOMMANDSTATETOSTRING(m_level) \
	((m_level) == ATsend_echo) ? "ATsend_echo" : \
	((m_level) == ATresponse) ? "ATresponse" : \
	((m_level) == ATfinished) ? "ATfinished" : \
			"unknown"


typedef struct modem_status {
	SemaphoreHandle_t semAccess;// access control semaphore of this status struct (example: AT command in progress may not be interrupted by another AT command issued by another task)
	TimerHandle_t    _timerHandle_ModemTimeout;
	tModemIoStates ioState;// NOTE: also used in the RX interrupt routine !
	struct {
		SemaphoreHandle_t atWait;// after the response of one AT command, no new AT command may be issued for 100ms to give room for 'URC' messages
		tAtCommandState AtCommandState ;
		struct modemAtCommandReq AtCommand;
		uint16_t echoIdx;

		uint8_t ATresponseBuf[MAXATRESPONSEBUFSIZE+1];// for nul terminating the string
		uint16_t ATresponseBufIdx;
		bool ATlookForEnd;

		tModemAtRc atRc;
		uint8_t *ATcopyptr;
		uint32_t ATcopycount;
	} atCommand;
	struct {
		SemaphoreHandle_t readWait;
		uint8_t * dest;
		uint32_t amount;
		uint32_t count;
	} transparentRxState;
	uint32_t lastCharsInRxBufCount;// used to not spam callback calls when characters are received and nobody paying attention
} tModemStatus;




/*
 * Data
 */

extern tModemStatus modemStatus;

// callback functions, a separate one, for every modem originated urc/respons
typedef uint8_t (* tModemCallbackFuncPtr)(uint8_t * buf, uint32_t len , tModemResultFunc * params);
typedef enum
{
    Modem_cb_at_response = 0,
    Modem_cb_urc_received,
	Modem_cb_dcd_no_carrier,
	Modem_cb_incoming_data, // called when characters are received, but nobody is waiting for them
    Modem_cb_max
} tModemCallbackIndex;



/*
 * Functions
 */

void xTaskModem_Init(void);
void xTaskModem( void *pvParameters );


BaseType_t Modem_NotifyRxData_ISR( uint32_t modemInterface );
void Modem_DCD_ISR(uint32_t modemInterface );

uint8_t Modem_UART_GetCharsInRxBuf();

tModemTaskRc modemSendAt(tModemResultFunc * resultFunc, uint32_t maxAtWait,  tModemAtRc *pAtRc, const char *fmt, ...);
int Modem_write(uint8_t *data, uint32_t len, uint32_t timeoutMs);
int Modem_read(uint8_t *data, uint32_t len, uint32_t timeoutMs);


void ModemSetCallback(tModemCallbackIndex cbIdx, tModemCallbackFuncPtr cbFunc);
void ModemRemoveCallback(tModemCallbackIndex cbIdx, tModemCallbackFuncPtr cbFunc);

bool ModemInit(uint32_t baudrate, uint32_t maxWait);
void ModemTerminate(uint32_t maxWait);
void ModemSetDtr(bool val);
uint8_t Modem_handleDCD(uint8_t *buf, uint32_t len, tModemResultFunc * params);
bool Modem_IsDCDEventFlagSet();
void Modem_ClearDCDEventFlag();
void Modem_WriteModemDebugDataAsEvents();

extern uint32_t MODEM_uart_err_count;
extern uint32_t MODEM_rx_fifo_overrun;
extern uint16_t g_ModemDebugData[MODEM_DEBUG_BUF_LEN];

#endif /* XTASKMODEMHANDLER_H_ */



#ifdef __cplusplus
}
#endif