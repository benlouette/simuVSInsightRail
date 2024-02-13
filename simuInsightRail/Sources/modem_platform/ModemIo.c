#ifdef __cplusplus
extern "C" {
#endif

/*
 * MODEMio.c
 *
 *  Created on: 14 oct. 2015
 *      Author: D. van der Velde
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

#include "xTaskmodem.h"
#include "xTaskMODEMEvent.h"

#include "Resources.h"

#include "fsl_device_registers.h"
#include "fsl_uart_hal.h"
#include "fsl_sim_hal.h"
#include "DrvUart.h"

#include "ModemIo.h"

#include "CS1.h"

#define DIRTY_RTS_HACK

/*
 * Macros
 */
// maximum tcp packet is 1500 bytes
#ifdef DIRTY_RTS_HACK
#define MODEM_MAXRXBUF (32)
#else
#define MODEM_MAXRXBUF (128)
#endif
/*
 * Types
 */



/*
 * Data
 */



// volatile tModemIoStates modemIoState;




/* RX buffer */
static struct {
#ifdef DIRTY_RTS_HACK
    volatile TickType_t tickCountAtFifoFull;
#endif
    volatile uint16_t cnt;// beware, on arm thumb 32 bit units are not atomic !
    uint16_t in;
    uint16_t out;
    uint8_t data[MODEM_MAXRXBUF];
} rxBuf;

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
static struct {
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




// Debug variables, to see if, and when how often this happens.
uint32_t MODEM_uart_err_count = 0;
uint32_t MODEM_rx_fifo_overrun = 0;

/*
 * Function definition
 */




static UART_Type * MODEM_uartBase; // the baseaddress of the uart in use by the MODEM, initialized in the  MODEM_UART_Init call


static void Modem_UART_Init(uint32_t instance, uint32_t baudRate )
{
	UART_Type *  uartBase[UART_INSTANCE_COUNT] = UART_BASE_PTRS;
	UART_Type *base = uartBase[instance];

	modemStatus.ioState = MODEMIOSTATE_ATCOMMAND;  // when starting we assume the modem is in AT mode

	MODEM_uartBase = base; // for later use in the Rx and tx handling routines

	DrvUart_Init(instance, baudRate, kUartParityDisabled, true /* , 7, 8 */); // irq prio moved to resources.c
}



// begin isr context



/*
 * MODEM_InterruptRx
 *
 * @desc    handles the UART receive interrupts
 *          puts them in a software fifo (when room)
 *
 *
 * @param   -
 *
 * @returns -
 */
static bool ignoreLF = false;

void setIgnoreLF(bool bIgnoreLF)
{
	ignoreLF= bIgnoreLF;
}

static BaseType_t Modem_InterruptRx()
{
    bool sendEvent = false;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;


    // and now make more efficient use of the hardware fifo
     if (rxBuf.cnt>=MODEM_MAXRXBUF) {
    	 // hw flow control
    	 // stop the rx interrupts, and let the uart hardware control the flow control lines
    	 UART_CLR_C2(MODEM_uartBase, UART_C2_RIE_MASK ); /* Disable RX interrupt */

    	 // below code was for no flow control
    	 //UART_RD_D(MODEM_uartBase);/* Read an 8-bit character from the receiver */
         //MODEM_rx_fifo_overrun++;
#ifdef DIRTY_RTS_HACK
    	 rxBuf.tickCountAtFifoFull = xTaskGetTickCountFromISR();
#endif
    	 // if we are full, in ATCOMMAND mode, it may be nice to notify the task (again)
    	 sendEvent = (modemStatus.ioState == MODEMIOSTATE_ATCOMMAND);
     } else {
         while ( (rxBuf.cnt<MODEM_MAXRXBUF) &&  (UART_RD_S1(MODEM_uartBase) & UART_S1_RDRF_MASK )) {
        	rxBuf.data[rxBuf.in]=UART_RD_D(MODEM_uartBase);/* Read an 8-bit character from the receiver */
        	//when in AT command mode, scan for the end of the line '\n', then it is time to send an event to the higher task
        	if ( !sendEvent && !ignoreLF && (modemStatus.ioState == MODEMIOSTATE_ATCOMMAND)) {
        		sendEvent = (rxBuf.data[rxBuf.in] == '\n' || (rxBuf.cnt==(MODEM_MAXRXBUF-1)));
        	}

            rxBuf.in++;
            if (rxBuf.in>= MODEM_MAXRXBUF) rxBuf.in=0;

            rxBuf.cnt++;

        	if (modemStatus.ioState == MODEMIOSTATE_TRANSPARENT) {
//#define MODEM_RX_IRQ_BLOCKMODE
#ifndef MODEM_RX_IRQ_BLOCKMODE
        		if ((rxBuf.cnt ) <= 1 || (rxBuf.cnt > (MODEM_MAXRXBUF/2))) sendEvent = true; // TODO: make this more intelligent (for better performance)
#else
        		// TODO : needs debugging/testing
        		// now the little bit smarter implementation without refactoring the higher layers
        		// check if we have a read request in progress
        		if (modemStatus.transparentRxState.count) {
        		    // copy over what may be accumulated in the software fifo
        		    while (modemStatus.transparentRxState.count && rxBuf.cnt) {

        		        *modemStatus.transparentRxState.dest++ = rxBuf.data[ rxBuf.out++ ];
        		        if ( rxBuf.out >= MODEM_MAXRXBUF) rxBuf.out = 0;
                        rxBuf.cnt--;
                        modemStatus.transparentRxState.count--;
        		    }
                    if (modemStatus.transparentRxState.count == 0) {
                        // notify that the read is ready
                        xSemaphoreGiveFromISR (modemStatus.transparentRxState.readWait , &xHigherPriorityTaskWoken);
                        // did the read left behind any characters ? then notify the task
                        if (rxBuf.cnt ) sendEvent = true; // TODO: make this more intelligent (for better performance)
                    }
        		} else {
        		    // let the task know new character have arrived, which must be handled somewhere
        		    if ((rxBuf.cnt ) <= 1) sendEvent = true; // TODO: make this more intelligent (for better performance)
        		}
#endif
        	}
        }
     }
     if (sendEvent ) xHigherPriorityTaskWoken = Modem_NotifyRxData_ISR( 0 );//xQueueSendToFrontFromISR(_EventQueue_Modem, &evt, NULL);

     return  pdFALSE;// when using high baudrates, this task switch check takes too long

}

/*
 * MODEM_InterruptTx
 *
 * @desc    handles the UART transmit interrupts
 *          puts as many as possible characters in the output fifo
 *
 *
 * @param   -
 *
 * @returns -
 */

static BaseType_t Modem_InterruptTx()
{
    // and now make more efficient use of the hardware fifo
    uint16_t room = fifoSizeLookup[UART_HAL_GetRxFifoSize(MODEM_uartBase)] - UART_HAL_GetTxDatawordCountInFifo(MODEM_uartBase);

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

#ifdef TESTPINS
    // TODO do not forget to remove
     //testpin function
     GPIO_DRV_SetPinOutput( PTC11 ) ;
#endif

    while (room &&  txState.cnt > 0 ) {
    	txState.cnt--;
        room--;
        UART_WR_D(MODEM_uartBase, *txState.data++);
    }

    if ( txState.cnt == 0 ) {
        // we have nothing more to write, so disable irq

    	//UART_HAL_SetIntMode(MODEM_uartBase, kUartIntTxDataRegEmpty, false);
        UART_CLR_C2(MODEM_uartBase, UART_C2_TIE_MASK ); /* Disable TX interrupt */
        if (txState.notifyWhenDone) {
        	xSemaphoreGiveFromISR (txState.writeBlockDone, &xHigherPriorityTaskWoken);
        }
    }

#ifdef TESTPINS
    // TODO do not forget to remove
     //testpin function
     GPIO_DRV_ClearPinOutput( PTC11 );
#endif
     return xHigherPriorityTaskWoken;
}




/*
 * MODEM_UART_ISR
 *
 * @desc    direct called from the vector table,
 *          handles the UART status events interrupts (normal operation transmit and receive stuff)
 *          calls the appropriate Rx or Tx routine for the real work.
 * @param   -
 *
 * @returns -
 */

void /*__attribute__((interrupt))*/ MODEM_UART_RX_TX_IRQhandler(void)
{
	register uint16_t StatReg = MODEM_uartBase->S1;

    BaseType_t RxHigherPriorityTaskWoken = pdFALSE;
    BaseType_t TxHigherPriorityTaskWoken = pdFALSE;

    if (StatReg & UART_S1_RDRF_MASK) {   /* Is the receiver's interrupt flag set? */
        RxHigherPriorityTaskWoken = Modem_InterruptRx();        /* If yes, then invoke the internal service routine.  */
    }

    if ( UART_RD_C2(MODEM_uartBase) & UART_C2_TIE_MASK )   { /*  TX interrupt enabled ? */
        if (StatReg & UART_S1_TDRE_MASK) { /* Is the transmitter irq enabled and Is the transmitter empty? */
            TxHigherPriorityTaskWoken = Modem_InterruptTx();      /* If yes, then invoke the internal service routine. */
        }
    }
    if ((RxHigherPriorityTaskWoken == pdTRUE) || (TxHigherPriorityTaskWoken)) {
        vPortYieldFromISR();
    }
}

/*
 * MODEM_UART_ERR_ISR
 *
 * @desc    direct called from the vector table,
 *          handles the UART error events interrupts, both serial parity/framing/overrun as well as fifo errors (overflow/underflow)
 * @param   -
 *
 * @returns -
 */


void /*__attribute__((interrupt))*/ MODEM_UART_ERR_IRQhandler(void)
{
    MODEM_uart_err_count++; // just for some statistical purposes

    DrvUart_ErrorIrqhandling(MODEM_uartBase); // let the general routine do the cleanup
}

// end isr context

/*
 * MODEM_UART_GetCharsInRxBuf
 *
 * @desc    number of characters in the input buffer
 *
 *
 * @param   -
 *
 * @returns number of characters in the input buffer
 */

uint8_t Modem_UART_GetCharsInRxBuf()
{
    return rxBuf.cnt;
}


/*
 * get_ch
 *
 * @desc    get character from buffer when available, else blocks until one available
 *
 *
 * @param   -
 *
 * @returns  character in input buffer
 */
uint8_t Modem_UART_get_ch()
{
    uint8_t c;

    while ( rxBuf.cnt == 0) {
        /* block, set idle ? */
        vTaskDelay(1 / portTICK_PERIOD_MS); // 1ms is about ten char at 115 kbaud
    }
    c = rxBuf.data[ rxBuf.out++ ];
    if ( rxBuf.out >= MODEM_MAXRXBUF) rxBuf.out = 0;

#ifdef DIRTY_RTS_HACK
    {
        TickType_t ticks;
        // dirty hack to bring down the RTS toggles
        ticks = xTaskGetTickCount();

        if (rxBuf.cnt <2) {
            if ((ticks - rxBuf.tickCountAtFifoFull) <=1) {
                vTaskDelay(1);
            }
        }
    }
#endif
    // disable irq required ?
    CS1_CriticalVariable();
    CS1_EnterCritical();

    rxBuf.cnt--;

    // extra for HW flow control, which switches the rx interrupts off when software fifo full in the irq handler
    // when software fifo is empty, switch the RX interrupts on again
    if (rxBuf.cnt ==0) {
    	MODEM_uartBase->C2 |= UART_C2_RIE_MASK;/* Enable RX interrupt */
    }

    // enable irq
    CS1_ExitCritical();

    return c;
}


/*
 * alternate write function (preparing for change to DMA version
 */

// returns the number of written bytes
// if this is different from the specified amount, something is wrong !

uint32_t Modem_writeBlock(uint8_t *data, uint32_t len, uint32_t timeout)
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

    	MODEM_uartBase->C2 |= UART_C2_TIE_MASK;/* Enable TX interrupt */
	}

	// wait for semaphore or timeout
	if (rc_ok) {
		rc_ok = (xSemaphoreTake( txState.writeBlockDone, ( TickType_t ) timeout ) == pdTRUE);
		txState.busy = false;
		bytesWritten = len-txState.cnt;
		if ((rc_ok == false) || (bytesWritten != len) ) {
			// TODO : if timeout or error occurred, set modem state in error !
		    if (rc_ok==false) LOG_DBG(LOG_LEVEL_MODEM,"Modem_writeBlock: timeout\n");
		    if (bytesWritten != len) LOG_DBG(LOG_LEVEL_MODEM,"Modem_writeBlock: not all bytes written %d out of %d\n",bytesWritten, len );

			// cleanup current interrupted transfer
			CS1_CriticalVariable();
		    CS1_EnterCritical();

		    MODEM_uartBase->C2 &= ~UART_C2_TIE_MASK;/* Disable TX interrupt */
		    // reset hardware fifo ?
		    UART_WR_CFIFO(MODEM_uartBase, UART_CFIFO_TXFLUSH_MASK );

			txState.cnt = 0;
			txState.notifyWhenDone = false;

		    CS1_ExitCritical();
		}
	}
	return bytesWritten;
}
void Modem_writeBlockAbort()
{
    // nothing to abort when not writing
    if (txState.busy) {
        // just do as if we are ready, the difference between requested and actual transferred bytes will trigger the error handling
        xSemaphoreGive( txState.writeBlockDone);
    }
}

// these convenient (but depreciated) functions use a fixed character timeout of 10ms
bool Modem_put_ch(uint8_t c)
{
	bool rc_ok;

	rc_ok = (Modem_writeBlock( &c, 1, 10/portTICK_PERIOD_MS /* portMAX_DELAY */ ) == 1);
	if (rc_ok == false) LOG_DBG(LOG_LEVEL_MODEM,"MODEM_put_ch: write error\n");

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

bool Modem_put_s(char *str)
{
	bool rc_ok;
    uint16_t i = strlen(str);

    rc_ok =  (Modem_writeBlock( (uint8_t*)str, i, 10 * i/portTICK_PERIOD_MS/* portMAX_DELAY */) == i);

    if (rc_ok == false) LOG_DBG(LOG_LEVEL_MODEM,"MODEM_put_s: write error\n");

    return rc_ok;
}



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

void Modem_init_serial(uint32_t instance, uint32_t baudRate )
{
    rxBuf.cnt=0;
    rxBuf.in=0;
    rxBuf.out=0;


	if (txState.writeBlockDone == NULL) {
		// create semaphore only once, makes reinit of this function possible
		txState.writeBlockDone =  xSemaphoreCreateBinary();
	}
	txState.cnt = 0;
	txState.busy = false;
	txState.notifyWhenDone = false;

    Modem_UART_Init(instance, baudRate);// comport index and baudrate

}





void ModemIo_print_info()
{
	printf("modem serial info:\n");
	printf("rxBuf   in=%d, out=%d, cnt=%d\n", rxBuf.in, rxBuf.out, rxBuf.cnt);
	if (rxBuf.cnt) {
		uint16_t i;
		printf("rx buffer contains: ");
		for (i=0; i<rxBuf.cnt; i++) {
			printf("%c", rxBuf.data[(rxBuf.out + i) % MODEM_MAXRXBUF]);
		}
		printf("\n");
	}
	printf("txState cnt=%d, busy=%d\n", txState.cnt, txState.busy  );
	printf("MODEM_uart_err_count = %d\nMODEM_rx_fifo_overrun = %d\n",MODEM_uart_err_count, MODEM_rx_fifo_overrun);

	printf("Bytes in UART fifo's  tx: %d, rx: %d\n",UART_HAL_GetTxDatawordCountInFifo(MODEM_uartBase), UART_HAL_GetRxDatawordCountInFifo(MODEM_uartBase));
}


#ifdef __cplusplus
}
#endif