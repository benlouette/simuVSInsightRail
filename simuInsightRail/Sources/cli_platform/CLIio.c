#ifdef __cplusplus
extern "C" {
#endif

/*
 * CLIio.c
 *
 *  Created on: 14 oct. 2015
 *      Author: D. van der Velde
 */




/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>

#include "xTaskCLI.h"
#include "xTaskCLIEvent.h"
#include "Resources.h"

#include "fsl_os_abstraction.h"
#include "fsl_device_registers.h"
#include "fsl_uart_hal.h"
#include "fsl_sim_hal.h"
#include "DrvUart.h"

#include "binaryCLI.h"
#include "CLIio.h"

#include "CS1.h"

/*
 * Macros
 */
#ifndef CLI_MAXRXBUF
#define CLI_MAXRXBUF 32
#endif

#ifndef CLI_MAXRXBUF
#define CLI_MAXTXBUF 32
#endif
/*
 * Types
 */


/*
 * Data
 */
// flag which decides where the debug output goes
static uint8_t g_debug_output = 0;

/* RX buffer */
static struct {
    volatile uint8_t cnt;
    uint8_t in;
    uint8_t out;
    uint8_t data[CLI_MAXRXBUF];
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
    volatile uint8_t cnt;
    uint8_t in;
    uint8_t out;
    uint8_t data[CLI_MAXTXBUF];
} txBuf;

// Debug variables, to see if, and when how often this happens.
uint32_t CLI_uart_err_count = 0;
uint32_t CLI_rx_fifo_overrun = 0;

/*
 * Function definition
 */




UART_Type * CLI_uartBase; // the baseaddress of the uart in use by the CLI, initialized in the  CLI_UART_Init call


static void CLI_UART_Init(uint32_t instance, uint32_t baudRate )
{
	UART_Type *  uartBase[UART_INSTANCE_COUNT] = UART_BASE_PTRS;
	UART_Type *base = uartBase[instance];

	CLI_uartBase = base; // for later use in the Rx and tx handling routines

	DrvUart_Init(instance, baudRate, kUartParityDisabled, false /* , 7, 8 */); // irq prio moved to resources.c
}



// begin isr context

/*
 * CLI_InterruptRx
 *
 * @desc    handles the UART receive interrupts
 *          puts them in a software fifo (when room)
 *
 *
 * @param   -
 *
 * @returns -
 */

static BaseType_t CLI_InterruptRx()
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // and now make more efficient use of the hardware fifo
     if (rxBuf.cnt>=CLI_MAXRXBUF) {
    	 UART_RD_D(CLI_uartBase);/* Read an 8-bit character from the receiver */
         CLI_rx_fifo_overrun++;

     } else {
         while ( (rxBuf.cnt<CLI_MAXRXBUF) &&  (UART_RD_S1(CLI_uartBase) & UART_S1_RDRF_MASK )) {
        	rxBuf.data[rxBuf.in]=UART_RD_D(CLI_uartBase);/* Read an 8-bit character from the receiver */
            rxBuf.in++;

            if (rxBuf.in>= CLI_MAXRXBUF) rxBuf.in=0;
            rxBuf.cnt++;
        }

        xHigherPriorityTaskWoken = xTaskCLI_NotifyCliRxData_ISR(0);
     }
     return xHigherPriorityTaskWoken;
}

/*
 * CLI_InterruptTx
 *
 * @desc    handles the UART transmit interrupts
 *          puts as many as possible characters in the output fifo
 *
 *
 * @param   -
 *
 * @returns -
 */

static void CLI_InterruptTx()
{
    // and now make more efficient use of the hardware fifo
    uint16_t room = fifoSizeLookup[UART_HAL_GetRxFifoSize(CLI_uartBase)] - UART_HAL_GetTxDatawordCountInFifo(CLI_uartBase);

    while (room &&  txBuf.cnt > 0 ) {
        txBuf.cnt--;
        room--;

        UART_WR_D(CLI_uartBase, txBuf.data[ txBuf.out ]);
        if ( ++txBuf.out >= CLI_MAXTXBUF) txBuf.out=0;
    }

    if ( txBuf.cnt == 0 ) {
        // we have nothing more to write, so disable irq

    	// UART_HAL_SetIntMode(CLI_uartBase, kUartIntTxDataRegEmpty, false);
        UART_CLR_C2(CLI_uartBase, UART_C2_TIE_MASK ); /* Disable TX interrupt */

    }
}


/*
 * CLI_UART_ISR
 *
 * @desc    direct called from the vector table,
 *          handles the UART status events interrupts (normal operation transmit and receive stuff)
 *          calls the appropriate Rx or Tx routine for the real work.
 * @param   -
 *
 * @returns -
 */






void  /*__attribute__((interrupt))*/ CLI_UART_RX_TX_IRQhandler(void)
{
	register uint16_t StatReg = CLI_uartBase->S1;

	/* Is the receiver's interrupt flag set? */
    if(StatReg & UART_S1_RDRF_MASK)
    {
        switch(binaryCLI_getMode())
        {

        case E_CLI_MODE_BINARY:
        	binaryCLI_processBytes();
        	break;

        default:
        case E_CLI_MODE_COMMAND:
        	(void)CLI_InterruptRx();
        	break;
        }
    }

    /*  TX interrupt enabled ? */
    if(UART_RD_C2(CLI_uartBase) & UART_C2_TIE_MASK)
    {
        if (StatReg & UART_S1_TDRE_MASK)
        { /* Is the transmitter irq enabled and Is the transmitter empty? */
            CLI_InterruptTx();      /* If yes, then invoke the internal service routine. This routine is inlined ? */
        }
    }
}

/*
 * CLI_UART_ERR_ISR
 *
 * @desc    direct called from the vector table,
 *          handles the UART error events interrupts, both serial parity/framing/overrun as well as fifo errors (overflow/underflow)
 * @param   -
 *
 * @returns -
 */


void /*__attribute__((interrupt))*/ CLI_UART_ERR_IRQhandler(void)
{
    CLI_uart_err_count++; // just for some statistical purposes

    DrvUart_ErrorIrqhandling(CLI_uartBase); // let the general routine do the cleanup
}

// end isr context

/*
 * CLI_UART_GetCharsInRxBuf
 *
 * @desc    number of characters in the input buffer
 *
 *
 * @param   -
 *
 * @returns number of characters in the input buffer
 */

uint8_t CLI_UART_GetCharsInRxBuf()
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
 * @returns  character in inputbuffer
 */
uint8_t CLI_UART_get_ch()
{
    uint8_t c;
    while ( rxBuf.cnt == 0) {
        /* block, set idle ? */
        vTaskDelay(1 / portTICK_PERIOD_MS); // 1ms is about ten char at 115 kbaud
    }
    c = rxBuf.data[ rxBuf.out++ ];
    if ( rxBuf.out >= CLI_MAXRXBUF) rxBuf.out = 0;

    // disable irq required ?
    CS1_CriticalVariable();
    CS1_EnterCritical();

    rxBuf.cnt--;

    // enable irq
    CS1_ExitCritical();

    return c;
}

/*
 * put_ch
 *
 * @desc    put character in outputbuffer when room, else blocks until there is room
 *
 *
 * @param   the character to put
 *
 * @returns -
 */

void CLI_put_ch(uint8_t c)
{
    /* if (c=='\r')  delay_ms(5) */;
    while ( txBuf.cnt >= CLI_MAXTXBUF ) {
        //delay_ms(15);
        /* block, set  idle ? */
        vTaskDelay(1 / portTICK_PERIOD_MS); // 1ms is about ten char at 115 kbaud
    }
    txBuf.data[ txBuf.in++ ]= c;
    if (txBuf.in >= CLI_MAXTXBUF) txBuf.in=0;

    // disable irq required ?
    CS1_CriticalVariable();
    CS1_EnterCritical();

    if ( txBuf.cnt++ == 0) {
    	CLI_uartBase->C2 |= UART_C2_TIE_MASK;/* Enable TX interrupt */
    	// UART_HAL_SetIntMode(CLI_UART_DEVICE, kUartIntTxDataRegEmpty, true);
    }
    // enable irq
    CS1_ExitCritical();
}

//------------------------------------------------------------------------------
/// Flush the TX buffer
///
/// @return void
//------------------------------------------------------------------------------
void CLI_flushTx()
{
	while (txBuf.cnt > 0)
	{
		vTaskDelay(1 / portTICK_PERIOD_MS);
	}
}

/*
 * put_ch_nb
 *
 * @desc    put character in outputbuffer when room else it is lost !
 *
 *
 * @param   the character to put
 *
 * @returns -
 */

void CLI_put_ch_nb(uint8_t c)
{
    /* if (c=='\r')  delay_ms(5) */;

    if (txBuf.cnt < CLI_MAXTXBUF) CLI_put_ch(c);

}


void put_ch(uint8_t c)
{
	if(binaryCLI_getMode() == E_CLI_MODE_BINARY)
	{
		binaryCLI_addToASCIIbuffer(c);
	}
	else
	{
		cliIo[g_debug_output]->putChar(c);
	}
}

void put_ch_nb(uint8_t c)
{
    cliIo[g_debug_output]->putCharNB(c);
}

uint8_t get_ch()
{
    return cliIo[g_debug_output]->getChar();
}


/*
 * init_serial
 *
 * @desc    initialisation of the serial port routines
 *
 *
 * @param
 *
 * @returns -
 */

void CLI_init_serial(uint32_t instance, uint32_t baudRate )
{
    rxBuf.cnt=0;
    rxBuf.in=0;
    rxBuf.out=0;
    txBuf.cnt=0;
    txBuf.in=0;
    txBuf.out=0;

    CLI_UART_Init(instance, baudRate);// comport index and baudrate

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

int put_s(char *str) {
    int i;
    i = 0;
    while (str[i])
        put_ch((uint8_t) str[i++]);

    return (i);
}


void CLIio_setOutputUart(uint8_t uartId) {
	g_debug_output = uartId;
}
uint8_t CLIio_getOutputUart() {
	return g_debug_output;
}



#ifdef NEWLIB_NANO

int _read (int fd, const void *buf, size_t count)
{
	size_t cnt = count;
	while (cnt) {
		*(char *) buf++ = cliIo[g_debug_output]->getChar();
	}
	return count;
}

int _write (int fd, const void *buf, size_t count)
{
	size_t cnt = count;
	while(cnt--) put_ch(*(char *)buf++);
	return count;
}

#endif


#ifdef __cplusplus
}
#endif