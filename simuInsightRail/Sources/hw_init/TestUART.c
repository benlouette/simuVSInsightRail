#ifdef __cplusplus
extern "C" {
#endif

/*
 * TestUART.c
 *
 *  Created on: Feb 11, 2016
 *      Author: Bart Willemse
 * Description:
 *
 */

#include <stdbool.h>
#include "fsl_uart_driver.h"

// TODO: Initial quick implementation only for ADC output streaming use - need
// to make more efficient eventually

//..............................................................................

uart_state_t TestUARTState;     // Used by SDK UART driver internals
const uint32_t TestUARTInstance = UART4_IDX;

//..............................................................................

// Prototype declared here because SDK doesn't seem to declare it anywhere(?)
void UART_DRV_IRQHandler(uint32_t instance);


/*
 * TestUARTInit
 *
 * @desc
 *
 * @param
 *
 * @returns -
 */
void TestUARTInit(void)
{
    // TODO: Need to configure the UART clock source, prescalers etc -
    // where is this currently done - Processor Expert?
    uart_user_config_t UARTUserConfig =
    {
#if 1
        .baudRate = 115200,
#else
        // george likes it fast
        .baudRate = 921600,
#endif
        .bitCountPerChar = kUart8BitsPerChar,
        .parityMode = kUartParityDisabled,
        .stopBitCount = kUartOneStopBit
    };
    // uart_status_t UARTStatus;

#ifndef _MSC_VER
    /* UARTStatus = */ UART_DRV_Init(TestUARTInstance, &TestUARTState,
                                     &UARTUserConfig);
#endif // _MSC_VER
}

/*
 * TestUARTSendBytes
 *
 * @desc    Quick n' dirty initial function to send bytes to the test UART.
 *          Blocks until complete, although UART_DRV_SendData() seems to queue
 *          data, so if the queue isn't full then will return quickly.
 *          Not yet task-safe, no timeout mechanism yet.
 *          TODO: Improve eventually, and/or use DrvUart.c/h module.
 *
 * @param
 *
 * @returns -
 */
void TestUARTSendBytes(uint8_t *pBytes, uint8_t NumBytes)
{
#if 0
    //uart_status_t UARTStatus;
    uint32_t bytesRemaining;

    /* UARTStatus = */ UART_DRV_SendData(TestUARTInstance, pBytes,
                                         NumBytes);
    while(1)
    {
        if (UART_DRV_GetTransmitStatus(TestUARTInstance,
                                       &bytesRemaining) == kStatus_UART_Success)
        {
            break;
        }
    }
#else
    UART_DRV_SendDataBlocking(TestUARTInstance,
            pBytes,
            NumBytes,
            NumBytes * 2);// assume 2msec/byte max
#endif
}

/*
 * UART4_RX_TX_IRQHandler
 *
 * @desc
 *
 * @param
 *
 * @returns -
 */
// TODO: Move to a central top-level UART ISR file
void /*__attribute__((interrupt))*/ UART4_RX_TX_IRQHandler(void)
{
    UART_DRV_IRQHandler(TestUARTInstance);
}






#ifdef __cplusplus
}
#endif