#ifdef __cplusplus
extern "C" {
#endif

/*
 * DrvUart.c
 *
 *  Created on: 1 okt. 2014
 *      Author: g100797
 */

/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>

#include "Resources.h"
#include "DrvUart.h"

#include "fsl_device_registers.h"
#include "fsl_interrupt_manager.h"
#include "fsl_uart_hal.h"
#include "fsl_clock_manager.h"

/*
 * Macros
 */

/*
 * Types
 */
struct sDrvUart
{
    UART_MemMapPtr      uartBaseAddress;
    uint32_t            baudrate;
    bool                coreClock; // Some UARTs run on CORE clock, others on BUS clock

    //tDrvUartIntCallback isrErrorCallbackFunc;
};

/*
 * Data
 */
static tUart drv_table[DRVUART_CFG_NUM_DEVICES] = DRVUART_INIT;

/*
 * Functions
 */

#if 0
/*!
 * GetBaudrateRegsSettings
 *
 * @brief       Calculate register settings for requested baudrate
 *
 * @param       uartClock   UART module clock (can be core clock or bus clock)
 * @param       baudrate    Requested baudrate
 * @param       pSbr        Pointer to SBR output variable (register BDH/BDL)
 * @param       pBrfa       Pointer to BRFA output variable (register C4)
 *
 * @returns     True for valid results, false otherwise
 */
static bool GetBaudrateRegsSettings( uint32_t  uartClock,
                                     uint32_t  baudrate,
                                     uint32_t *pSbr,
                                     uint32_t *pBrfa )
{
    uint32_t divider;
    uint32_t sbr;
    uint32_t brfa;

    // Prevent divide by zero
    if (baudrate == 0) {
        return false;
    }

    // Calculate the divider SBR value
    // Reference manual:
    // Baudrate = UART clock / ( 16 * (SBR+BRFA) )
    //   BRFA[4:0] = 0/32 .. 31/32
    //
    // Method:
    // - Reference manual formula is incorrect (results in half the baudrate)
    // - Also use one extra bit to enable proper rounding of the fine divider values
    //
    divider = ( (uartClock << 2) / baudrate ) + 1; // +1 is the rounding bit... (+0.5 equivalent)
    sbr = (divider >> 6) & 0x1FFFUL; // upper 13 bits
    brfa = (divider >> 1) & 0x1FUL;  // least significant 5 bits

    // TODO Check valid values

    *pSbr = sbr;
    *pBrfa = brfa;

    return true;
}
#endif

/*!
 * DrvUart_Open
 *
 * @brief       Open an instance of the driver
 *
 * @param       uartId   instance ID of the UART module
 *
 * @returns     Pointer to the tUart driver instance structure
 */
tUart *DrvUart_Open( uint32_t uartId )
{
    tUart *pDrv;

    if ( uartId >= DRVUART_CFG_NUM_DEVICES )
        return NULL;

    pDrv = &drv_table[uartId];

    return pDrv;
}

/*!
 * DrvUart_SetClockConfiguration
 *
 * @brief       Change the clock configuration for the UART instance referenced
 *              by pUart.
 *
 * @param       pUart      Pointer to UART driver instance
 * @param       clkConfig  Clock configuration ID defined by Processor Expert generated code
 *                         (CPU component definition)
 *
 * @returns     Pointer to the tUart driver instance structure
 */
bool DrvUart_SetClockConfiguration( tUart *pUart )
{
    uint32_t uartClock;

    // Determine the applicable UART module clock
    uartClock = (pUart->coreClock) ? CLOCK_SYS_GetCoreClockFreq() : CLOCK_SYS_GetBusClockFreq();

    // Set clock configuration dependent baudrate
    UART_HAL_SetBaudRate( pUart->uartBaseAddress, uartClock, pUart->baudrate );

    return true;
}




// TODO : make this into a more generic driver
/*
 * @brief  Initialize the CLI UART peripheral
 *
 * The UART driver is not used, code ported from srb22220 (which has CLI on UART4)
 *
 * @param instance  Instance number of the UART peripheral
 *
 * @param baudRate	baudrate to initialise the port with
 *
 * @param parity	none/odd/even in SDK enum value
 *
 * @param hwFlowControl if true, rts/cts flow control (only set in device, port multiplexer must be set elsewhere
 *
 * Note : irq priorities now set central in 'resources.c'
 * @param rxTxIrqPrio	interrupt priority for the uart tx and rx irq, for values see : NVIC_SetPriority
 *
 * @param errIrqPrio	interrupt priority for the uart error irq, for values see : NVIC_SetPriority
 *
 *  @returns		nothing
 */

void DrvUart_Init(uint32_t instance, uint32_t baudRate, uart_parity_mode_t parity, bool hwFlowControl )
{
// Custom initialization based on KSDK HAL (ported from PE UART_Init component)

	assert(instance < UART_INSTANCE_COUNT);

	UART_Type *  uartBase[UART_INSTANCE_COUNT] = UART_BASE_PTRS;
    IRQn_Type uartRxTxIrqId[UART_INSTANCE_COUNT] = UART_RX_TX_IRQS;
    IRQn_Type uartErrorIrqId[UART_INSTANCE_COUNT] = UART_ERR_IRQS;

	UART_Type *base = uartBase[instance];
	IRQn_Type uartRxTxIrq = uartRxTxIrqId[instance];
	IRQn_Type uartErrIrq = uartErrorIrqId[instance];


	/* SIM_SCGC1: UART4=1 */
	CLOCK_SYS_EnableUartClock(instance); // UART0=0, UART1=1, UART2=2

	// bring UART to a known state
	UART_HAL_Init(base);

	// not sure that the UART_HAL_init always clears the fifo stuff, so here probably  done again
	UART_WR_PFIFO(base, 0U);
    UART_WR_CFIFO(base, 0U);
    UART_WR_SFIFO(base, 0xC0U);
    UART_WR_TWFIFO(base, 0U);
    UART_WR_RWFIFO(base, 1U);

    /* Initialize UART baud rate, bit count, parity and stop bit. */
    UART_HAL_SetBaudRate(base, CLOCK_SYS_GetUartFreq(instance) , baudRate);
    UART_HAL_SetBitCountPerChar(base, parity==kUartParityDisabled ? kUart8BitsPerChar : kUart9BitsPerChar);
    UART_HAL_SetParityMode(base, parity);
    UART_HAL_SetTxFifoWatermark(base, 0);
    UART_HAL_SetRxFifoWatermark(base, 1);

    if (hwFlowControl) {
    	// set hardware flow control
    	UART_HAL_SetReceiverRtsCmd(base, true);
    	UART_HAL_SetTransmitterCtsCmd(base, true);
    }


    // so low level register read/write stuff instead

    // clear fifo status bits: partly covered in UART_HAL_Init,
     /* UART4_SFIFO: TXEMPT=1,RXEMPT=1,??=0,??=0,??=0,RXOF=1,TXOF=1,RXUF=1 */
    UART_WR_SFIFO(base, UART_SFIFO_TXEMPT_MASK |
                   UART_SFIFO_RXEMPT_MASK |
                   UART_SFIFO_RXOF_MASK |
                   UART_SFIFO_TXOF_MASK |
                   UART_SFIFO_RXUF_MASK);


     // flush fifo and enable fifo under/overflow errors : covered in UART_HAL_FlushTxFifo and UART_HAL_FlushRxFifo
     //	But no HAL functions for enable/disable the fifo overflow/underflow !
     /* UART4_CFIFO: TXFLUSH=1,RXFLUSH=1,??=0,??=0,??=0,RXOFE=0,TXOFE=1,RXUFE=1 */
    UART_WR_CFIFO(base, UART_CFIFO_TXFLUSH_MASK |
                   UART_CFIFO_RXFLUSH_MASK |
                   UART_CFIFO_TXOFE_MASK |
                   UART_CFIFO_RXUFE_MASK);

     // enable the fifo's
     /* UART4_PFIFO: TXFE=1,RXFE=1 */
    UART_SET_PFIFO(base,  UART_PFIFO_TXFE_MASK | UART_PFIFO_RXFE_MASK);


    (void) UART_RD_S1(base);                   /* Dummy read of the UART_S1 register to clear flags */
    (void) UART_RD_D(base);                    /* Dummy read of the UART_D register to clear flags */

    UART_HAL_FlushRxFifo(base);// the dummy read causes a fifo underflow, so repair that


    // enable irq's for : overflow error, noise error irq, framing error, parity error
    /* UART4_C3: R8=0,T8=0,TXDIR=0,TXINV=0,ORIE=1,NEIE=1,FEIE=1,PEIE=1 */
    UART_WR_C3(base, UART_C3_ORIE_MASK |
                  UART_C3_NEIE_MASK |
                  UART_C3_FEIE_MASK |
                  UART_C3_PEIE_MASK);

       // enable the tx/rx interrupts, and the rx and tx modules
       /* UART4_C2: TIE=1,TCIE=0,RIE=1,ILIE=0,TE=1,RE=1,RWU=0,SBK=0 */
    UART_WR_C2(base, UART_C2_TIE_MASK |
                  UART_C2_RIE_MASK |
                  UART_C2_TE_MASK |
                  UART_C2_RE_MASK);

    /* Enable UART interrupt on NVIC level. */
      // Set IRQ Priorities : moved to central place (resources.c)
    // NVIC_SetPriority(uartRxTxIrq, rxTxIrqPrio);
    // NVIC_SetPriority(uartErrIrq, errIrqPrio);

      // Enable IRQ
    INT_SYS_EnableIRQ(uartRxTxIrq);
    INT_SYS_EnableIRQ(uartErrIrq);

}


/*
 * DrvUart_ErrorIrqhandling
 *
 * @desc    handles the error interrupt from a uart, fixes any (fifo) overrun/underrun etc
 * 			It assumes that it is running in the interrupt context of the uart error interrupt
 * @param   base base address of the UART
 *
 *
 * @returns -
 */

void DrvUart_ErrorIrqhandling(UART_Type * uartBase )
{
	uint16_t StatReg = uartBase->S1; /* Read status register */
	 uint16_t FifoStatReg;


	    // most errors are cleared by reading S1 and then reading the data register (but reading an empty input fifo can also give problems.
	    // so test on this error after the empty read !
	    if (StatReg & (UART_S1_NF_MASK | UART_S1_OR_MASK | UART_S1_FE_MASK | UART_S1_PF_MASK)) { /* Is any error flag set? */
	    	UART_RD_D(uartBase); /* dummy read an 8-bit character from receiver */
	    }
	    // check fifo register
	    FifoStatReg = uartBase->SFIFO;
	    if (FifoStatReg & (UART_SFIFO_RXUF_MASK | UART_SFIFO_RXOF_MASK)  ) {
	        // something wrong with the receive fifo ? disable receiver, flush, enable receiver
	        // read following reference manual sections carefully :
	        //  45.3.5 UART Status Register 1 (UARTx_S1)
	        //  45.8.4 Overrun (OR) flag implications


	        // uartBase->C2 &= ~UART_C2_RE_MASK;/* Disable receiver. */
	        uartBase->SFIFO = (UART_SFIFO_RXUF_MASK | UART_SFIFO_RXOF_MASK); // Clear underflow/overflow flags
	        uartBase->PFIFO |= UART_PFIFO_RXFE_MASK;/* Enable RX FIFO, just to be sure */
	        uartBase->CFIFO |= UART_CFIFO_RXFLUSH_MASK; /* Flush RX  FIFO */

	        // to prevent an empty read in the rx interrupt routine, make sure the RDRF flag is S1 is cleared
	        // this can only be done when the fifo is empty, but then it leads to a fifo underflow, so temprary disable the underflow interrupt
	        uartBase->CFIFO &= ~UART_CFIFO_RXUFE_MASK;// disable rx fifo underflow interrupt
	        UART_RD_D(uartBase);/* dummy read an 8-bit character from receiver will cause fifo underflow, so flush again */
	        uartBase->CFIFO |= UART_CFIFO_RXFLUSH_MASK; /* Flush RX  FIFO */
	        uartBase->SFIFO = UART_SFIFO_RXUF_MASK; // Clear underflow flags
	        uartBase->CFIFO |= UART_CFIFO_RXUFE_MASK;// enable rx fifo underflow interrupt again

	        // NOTE: although this should work in my opinion, there still comes a second error interrupt when there is a (fifo) overrun

	        // uartBase->C2 |= UART_C2_RE_MASK;/* Enable receiver. */

	    }
	    if (FifoStatReg &  UART_SFIFO_TXOF_MASK ) {
	        // something wrong with the transmit fifo ? disable receiver, flush, enable receiver

	        //uartBase->C2 &= ~UART_C2_TE_MASK;/* Disable . */

	    	uartBase->SFIFO = UART_SFIFO_TXOF_MASK; // Clear flag
	    	uartBase->PFIFO |= UART_PFIFO_TXFE_MASK;/* Enable TX FIFO, just to be sure */
	    	uartBase->CFIFO |= UART_CFIFO_TXFLUSH_MASK; /* Flush TX FIFO */

	    	//uartBase->C2 |= UART_C2_TE_MASK;/* Enable . */

	    }

}


#ifdef __cplusplus
}
#endif