#ifdef __cplusplus
extern "C" {
#endif

/*
 * gnssIo.c
 *
 *  Created on: Feb 2, 2017
 *      Author: ka3112
 */




/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>

#include <portmacro.h>
#include <FreeRTOS.h>
#include <semphr.h>
#include <string.h>

#include "taskGnss.h"


#include "Resources.h"
#include "Log.h"

#include "fsl_device_registers.h"
#include "fsl_uart_hal.h"
#include "fsl_sim_hal.h"
#include "DrvUart.h"

#include "gnssIo.h"

#include "CS1.h"

/*
 * Types
 */
typedef enum {
    GNSS_BUF_SEARCH_START = 0, // waiting for the initial '$'
    GNSS_BUF_STARTED, // collect chars until buffer full or 'lf' found.
	GNSS_BIN_04,
	GNSS_BIN_24,
	GNSS_BIN_SZL,
	GNSS_BIN_SZH,
	GNSS_BIN_PKT,
#ifdef GNSS_UPGRADE_AVAILABLE
	GNSS_BIN_ONLY,
#endif
    GNSS_BUF_MAX
} tGnsBufferState;

#define MAX_GNSS_RX_BUFFERS 	4

 typedef  struct
 {
    tGnsBufferState bufferState, restartState;
    uint16_t binaryCount;
    struct gnss_buf_str *incomingBuf;
    struct gnss_buf_str buffers[MAX_GNSS_RX_BUFFERS];
 } tGnsIo_RxBuffers;


static const uint16_t fifoSizeLookup[] = {
        1,
        4,
        8,
        16,
        32,
        64,
        128,
        1 // reserved
};



/* TX buffer */
static struct
{
    volatile uint32_t cnt;// beware, on arm thumb 32 bit unit operations are not atomic !
    uint8_t *data;
    bool busy;
    bool notifyWhenDone;
    SemaphoreHandle_t writeBlockDone;
} txState = {
        .cnt = 0,
        .data = NULL,
        .busy = false,
        .notifyWhenDone = false,
        .writeBlockDone = NULL
};

// RX buffer data
static tGnsIo_RxBuffers gnssIoRx;


// Debug variables, to see if, and when how often this happens.
static uint32_t GNSS_uart_err_count = 0;
static uint32_t GNSS_rx_buf_overrun = 0;
static uint32_t GNSS_rx_buf_overflow = 0;
static uint32_t GNSS_rx_char_count = 0;



static UART_Type * GNSS_uartBase; // the baseaddress of the uart in use by the module, initialized in the GNSS_UART_Init call
uint32_t gnssBaudrate;	// baudrate selected for the device

void Gnss_UART_Init(uint32_t instance, uint32_t baudRate )
{
    UART_Type *  uartBase[UART_INSTANCE_COUNT] = UART_BASE_PTRS;
    UART_Type *base = uartBase[instance];



    GNSS_uartBase = base; // for later use in the Rx and tx handling routines

    DrvUart_Init(instance, baudRate, kUartParityDisabled, false /* , 7, 8 */); // irq prio moved to resources.c
}


// begin isr context



/*
 * Gnss_InterruptRx
 *
 * @desc    handles the UART receive interrupts
 *          puts them in a message buffer
 *
 *
 * @param   -
 *
 * @return -
 */

static BaseType_t Gnss_InterruptRx()
{
    int i;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // pull as much characters as possible from the uart/fifo
    while (  GNSS_uartBase->S1 & UART_S1_RDRF_MASK )
    {
        uint8_t gnssChar = GNSS_uartBase->D;/* Read an 8-bit character from the receiver */
        GNSS_rx_char_count++;

        // make sure we have a buffer
		if (NULL == gnssIoRx.incomingBuf)
		{
			// nope, so is one available
			for(i = 0; i < MAX_GNSS_RX_BUFFERS; i++)
			{
				if(gnssIoRx.buffers[i].idx == 0)
				{
					gnssIoRx.incomingBuf = &gnssIoRx.buffers[i];
					gnssIoRx.bufferState = gnssIoRx.restartState;
					break;
				}
			}
		}

		// Have we got a buffer yet?
		if(NULL == gnssIoRx.incomingBuf)
		{
			GNSS_rx_buf_overrun++;// count for debug analysis
			continue;		// still no buffer so forget it
		}

		switch (gnssIoRx.bufferState )
		{
		case GNSS_BIN_04:
			if (gnssChar == 0x04)
			{
				// start of new message
				gnssIoRx.incomingBuf->idx = 1;
				gnssIoRx.incomingBuf->buf[0] = gnssChar;
				gnssIoRx.bufferState = GNSS_BIN_24;
			}
			break;

		case GNSS_BIN_24:
			if (gnssChar == 0x24)
			{
				// getting there
				gnssIoRx.incomingBuf->buf[gnssIoRx.incomingBuf->idx++] = gnssChar;
				gnssIoRx.bufferState = GNSS_BIN_SZL;
			}
			else
			{
				gnssIoRx.bufferState = GNSS_BIN_04;
			}
			break;

		case GNSS_BIN_SZL:
			gnssIoRx.incomingBuf->buf[gnssIoRx.incomingBuf->idx++] = gnssChar;
			gnssIoRx.bufferState = GNSS_BIN_SZH;
			break;

		case GNSS_BIN_SZH:
			gnssIoRx.incomingBuf->buf[gnssIoRx.incomingBuf->idx++] = gnssChar;
			gnssIoRx.bufferState = GNSS_BIN_PKT;
			gnssIoRx.binaryCount = *(uint16_t*)&gnssIoRx.incomingBuf->buf[2] - 4;	// remaining characters to read
			break;

		case GNSS_BIN_PKT:
			if (gnssIoRx.incomingBuf->idx >= MAXGNSS_MSGBUFSIZE)
			{
				// buffer overflow, throw away what we have, and start looking for the '$' again
				GNSS_rx_buf_overflow++;// count for debug analysis
				gnssIoRx.bufferState = GNSS_BIN_04;
				break;
			}
			gnssIoRx.incomingBuf->buf[gnssIoRx.incomingBuf->idx++] = gnssChar;
			if(--gnssIoRx.binaryCount == 0)
			{
				if((*(uint16_t*)&gnssIoRx.incomingBuf->buf[4] == 2))
				{
					xHigherPriorityTaskWoken = Gnss_NotifyBinRxData_ISR(gnssIoRx.incomingBuf);
				}
				else
				{
					// discard buffer
					gnssIoRx.incomingBuf->idx = 0;
				}
				gnssIoRx.incomingBuf = NULL;
				gnssIoRx.bufferState = GNSS_BIN_04;
			}
			break;

#ifdef GNSS_UPGRADE_AVAILABLE
		case GNSS_BIN_ONLY:
			if (gnssIoRx.incomingBuf->idx >= MAXGNSS_MSGBUFSIZE)
			{
				// buffer overflow, throw away what we have, and start looking for the '$' again
				GNSS_rx_buf_overflow++;// count for debug analysis
				break;
			}
			gnssIoRx.incomingBuf->buf[gnssIoRx.incomingBuf->idx++] = gnssChar;
			break;
#endif
		case GNSS_BUF_SEARCH_START:
			// look for the '$' message start of the gnss module
			if (gnssChar == '$')
			{
				// start of new message
				gnssIoRx.incomingBuf->idx = 1;
				gnssIoRx.incomingBuf->buf[0] = gnssChar;
				gnssIoRx.bufferState = GNSS_BUF_STARTED;
			}
			break;

		case GNSS_BUF_STARTED:
			// now we are collecting characters, until the '\n' is found, or buffer full
			if (gnssIoRx.incomingBuf->idx >= MAXGNSS_MSGBUFSIZE)
			{
				// buffer overflow, throw away what we have, and start looking for the '$' again
				GNSS_rx_buf_overflow++;// count for debug analysis
				gnssIoRx.bufferState = GNSS_BUF_SEARCH_START;
				break;
			}
			// We have space in the buffer so let's do it
			gnssIoRx.incomingBuf->buf[gnssIoRx.incomingBuf->idx++] = gnssChar;
			if (gnssChar == '\n')
			{
				// A valid ASCII string format is "$.....*CC\r\n"
				// check that the '*' is in the right location
				if(gnssIoRx.incomingBuf->buf[gnssIoRx.incomingBuf->idx-5] == '*')
				{
				    // end of message found, terminate string the 'c' way,  swap pointers, and notify gnss task
				    gnssIoRx.incomingBuf->buf[gnssIoRx.incomingBuf->idx] = 0;// remember, we made the buf one char longer
				    // let them know there is new stuff to process
				    xHigherPriorityTaskWoken = Gnss_NotifyRxData_ISR(gnssIoRx.incomingBuf);
				}
                else
                {
                    gnssIoRx.incomingBuf->idx = 0;
                }

				// either way restart line collection
				gnssIoRx.incomingBuf = NULL;
				gnssIoRx.bufferState = GNSS_BUF_SEARCH_START;
			}
			break;

		default:
			// should never happen, but let it fallback to a known state
			gnssIoRx.bufferState = gnssIoRx.restartState;
			break;
		}
    }

     return xHigherPriorityTaskWoken;
}

/*
 * Gnss_InterruptTx
 *
 * @desc    handles the UART transmit interrupts
 *          puts as many as possible characters in the output fifo
 *
 *
 * @param   -
 *
 * @return -
 */

static BaseType_t Gnss_InterruptTx()
{
    // and now make more efficient use of the hardware fifo
    uint16_t room = fifoSizeLookup[UART_HAL_GetRxFifoSize(GNSS_uartBase)] - UART_HAL_GetTxDatawordCountInFifo(GNSS_uartBase);

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    while (room &&  txState.cnt > 0 ) {
        txState.cnt--;
        room--;
        UART_WR_D(GNSS_uartBase, *txState.data++);
    }

    if ( txState.cnt == 0 ) {
        // we have nothing more to write, so disable irq

        //UART_HAL_SetIntMode(GNSS_uartBase, kUartIntTxDataRegEmpty, false);
        UART_CLR_C2(GNSS_uartBase, UART_C2_TIE_MASK ); /* Disable TX interrupt */
        if (txState.notifyWhenDone) {
            xSemaphoreGiveFromISR (txState.writeBlockDone, &xHigherPriorityTaskWoken);
        }
    }

     return xHigherPriorityTaskWoken;
}




/*
 * GNSS_UART_ISR
 *
 * @desc    direct called from the vector table,
 *          handles the UART status events interrupts (normal operation transmit and receive stuff)
 *          calls the appropriate Rx or Tx routine for the real work.
 * @param   -
 *
 * @returns -
 */

void /*__attribute__((interrupt))*/ GNSS_UART_RX_TX_IRQhandler(void)
{
    register uint16_t StatReg = GNSS_uartBase->S1;

    BaseType_t RxHigherPriorityTaskWoken = pdFALSE;
    BaseType_t TxHigherPriorityTaskWoken = pdFALSE;

    if (StatReg & UART_S1_RDRF_MASK) {   /* Is the receiver's interrupt flag set? */
        RxHigherPriorityTaskWoken = Gnss_InterruptRx();        /* If yes, then invoke the internal service routine.  */
    }

    if ( UART_RD_C2(GNSS_uartBase) & UART_C2_TIE_MASK )   { /*  TX interrupt enabled ? */
        if (StatReg & UART_S1_TDRE_MASK) { /* Is the transmitter irq enabled and Is the transmitter empty? */
            TxHigherPriorityTaskWoken = Gnss_InterruptTx();      /* If yes, then invoke the internal service routine. */
        }
    }
    if ((RxHigherPriorityTaskWoken == pdTRUE) || (TxHigherPriorityTaskWoken)) {
        vPortYieldFromISR();
    }
}

/*
 * GNSS_UART_ERR_ISR
 *
 * @desc    direct called from the vector table,
 *          handles the UART error events interrupts, both serial parity/framing/overrun as well as fifo errors (overflow/underflow)
 * @param   -
 *
 * @returns -
 */


void /*__attribute__((interrupt))*/ GNSS_UART_ERR_IRQhandler(void)
{
    GNSS_uart_err_count++; // just for some statistical purposes

    DrvUart_ErrorIrqhandling(GNSS_uartBase); // let the general routine do the cleanup

    // we got an error so let's start again
    gnssIoRx.bufferState = gnssIoRx.restartState;

    /*
     * This *should* be pointing to a valid buffer (set by first rx character)
     * however if we get UART error before RX char, then won't be set
     * so we can just ignore it
     */
    if(gnssIoRx.incomingBuf)
    {
    	gnssIoRx.incomingBuf->idx = 0;
    }
}

// end isr context

/*
 * alternate write function
 */

// returns the number of written bytes
// if this is different from the specified amount, something is wrong !

uint32_t Gnss_writeBlock(uint8_t *data, uint32_t len, uint32_t timeout)
{

    bool rc_ok = true;
    uint32_t bytesWritten = 0;

    if (txState.busy) {
        rc_ok = false;
        // still busy with a transfer
    } else {

        txState.busy = true;
        txState.cnt = len;
        txState.data = data;
        txState.notifyWhenDone = true;

        GNSS_uartBase->C2 |= UART_C2_TIE_MASK;/* Enable TX interrupt */
    }

    // wait for semaphore or timeout
    if (rc_ok) {
        rc_ok = (xSemaphoreTake( txState.writeBlockDone, ( TickType_t ) timeout ) == pdTRUE);
        txState.busy = false;
        bytesWritten = len-txState.cnt;
        if ((rc_ok == false) || (bytesWritten != len) ) {
            // TODO : if timeout or error occurred, set state in error !

            // cleanup current interrupted transfer
            CS1_CriticalVariable();
            CS1_EnterCritical();

            GNSS_uartBase->C2 &= ~UART_C2_TIE_MASK;/* Disable TX interrupt */
            // reset hardware fifo ?
            UART_WR_CFIFO(GNSS_uartBase, UART_CFIFO_TXFLUSH_MASK );

            txState.cnt = 0;
            txState.notifyWhenDone = false;

            CS1_ExitCritical();
        }
    }
    return bytesWritten;
}

#if 0
static void Gnss_writeBlockAbort()
{
    // nothing to abort when not writing
    if (txState.busy) {
        // just do as if we are ready, the difference between requested and actual transferred bytes will trigger the error handling
        xSemaphoreGive( txState.writeBlockDone);
    }
}
#endif

// these convenient (but depreciated) functions use a fixed character timeout of 10ms
bool Gnss_put_ch(uint8_t c)
{
    bool rc_ok = (Gnss_writeBlock(&c, 1, 10/portTICK_PERIOD_MS) == 1);
    if (rc_ok == false) LOG_DBG(LOG_LEVEL_GNSS,"GNSS_put_ch: write error\n");

    return rc_ok;
}

/*
 * put_s
 *
 * @desc    put a zero terminated string out
 *
 *
 * @param   the zero terminated string
 *
 * @returns number of chars written
 */

bool Gnss_put_s(char *str)
{
    bool rc_ok;
    uint16_t i = strlen(str);

#if 1
    uint16_t written = Gnss_writeBlock( (uint8_t*)str, i,   10 * i/portTICK_PERIOD_MS /* portMAX_DELAY */ ) ;
    rc_ok = (written == i);
    if (rc_ok == false) LOG_DBG(LOG_LEVEL_GNSS,"GNSS_put_s: write error %d != %d\n",i, written);
#else
    rc_ok =  (Gnss_writeBlock( (uint8_t*)str, i,   10 * i/portTICK_PERIOD_MS /* portMAX_DELAY */ ) == i);
    if (rc_ok == false) LOG_DBG(LOG_LEVEL_GNSS,"GNSS_put_s: write error\n");
#endif
    return rc_ok;
}

#ifdef GNSS_UPGRADE_AVAILABLE
uint8_t Gnss_get_ch(uint32_t to)
{
	while(--to && (gnssIoRx.incomingBuf->idx == 0))
	{
		vTaskDelay(1);
	}
	if(0 == to) return 0xFF;

    CS1_CriticalVariable();

    CS1_EnterCritical();

    uint8_t byte = gnssIoRx.incomingBuf->buf[0];
	if(--gnssIoRx.incomingBuf->idx)
	{
		memcpy(&gnssIoRx.incomingBuf->buf[0],&gnssIoRx.incomingBuf->buf[1], gnssIoRx.incomingBuf->idx);
	}

    CS1_ExitCritical();
    return byte;
}
#endif

/*
 * init_serial
 *
 * @desc    initialization of the serial port routines
 *
 *
 * @param
 *
 * @returns -
 */

void Gnss_init_serial(uint32_t instance, uint32_t baudRate )
{

    // make sure state and input buffers are initialised before we start the IRQ service.
    gnssIoRx.bufferState = GNSS_BUF_SEARCH_START;
    for(int i = 0; i < MAX_GNSS_RX_BUFFERS; i++)
    {
    	gnssIoRx.buffers[0].idx = 0;
    }
    gnssIoRx.incomingBuf = &gnssIoRx.buffers[0];

    if (txState.writeBlockDone == NULL) {
        // create semaphore only once, makes re-init of this function possible
        txState.writeBlockDone =  xSemaphoreCreateBinary();
    }
    txState.cnt = 0;
    txState.busy = false;
    txState.notifyWhenDone = false;

    Gnss_UART_Init(instance, baudRate);// COM port index and baudrate
    gnssBaudrate = baudRate;
}

/*
 * GnssIo_setBinaryMode
 *
 * @desc    sets mode of operation, false=NMEA, true=Binary
 *
 *
 * @param
 *
 * @returns -
 */
void GnssIo_setBinaryMode(bool mode)
{
    CS1_CriticalVariable();

    CS1_EnterCritical();

    gnssIoRx.restartState =	gnssIoRx.bufferState = (mode) ? GNSS_BIN_04 : GNSS_BUF_SEARCH_START;
	if(gnssIoRx.incomingBuf)
	{
		gnssIoRx.incomingBuf->idx = 0;
	}

    CS1_ExitCritical();
}

#ifdef GNSS_UPGRADE_AVAILABLE
void GnssIo_setBinary()
{
    CS1_CriticalVariable();

    CS1_EnterCritical();

    gnssIoRx.restartState = gnssIoRx.bufferState = GNSS_BIN_ONLY;
	if(gnssIoRx.incomingBuf)
	{
		gnssIoRx.incomingBuf->idx = 0;
	}

    CS1_ExitCritical();
}
#endif

void GnssIo_print_info()
{
	int i;
    printf("GNSS serial info:\n");
    for(i = 0; i < MAX_GNSS_RX_BUFFERS; i++)
    {
    	printf("GNSS RX buf(%d) @ %08x (idx= %d ) : %s \n", i, (int)&gnssIoRx.buffers[i] , gnssIoRx.buffers[i].idx, (char*)&gnssIoRx.buffers[i].buf);
    }

    printf("txState cnt=%d, busy=%d\n", txState.cnt, txState.busy  );
    printf( "GNSS_uart_err_count = %d\n"
    		"GNSS_rx_buf_overrun = %d\n"
    		"GNSS_rx_buf_overflow = %d\n"
    		"GNSS_rx_char_count = %d\n", GNSS_uart_err_count, GNSS_rx_buf_overrun, GNSS_rx_buf_overflow, GNSS_rx_char_count);

    printf("Bytes in UART fifo's  tx: %d, rx: %d\n",UART_HAL_GetTxDatawordCountInFifo(GNSS_uartBase), UART_HAL_GetRxDatawordCountInFifo(GNSS_uartBase));
}




#ifdef __cplusplus
}
#endif