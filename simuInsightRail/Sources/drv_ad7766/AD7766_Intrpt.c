#ifdef __cplusplus
extern "C" {
#endif

/*
 * AD7766_Intrpt.c
 *
 *  Created on: Jan 20, 2016
 *      Author: Bart Willemse
 * Description: Interrupt-driven AD7766 ADC & sampling functionality.
 *              See companion document "Insight Firmware ADC and Sampling
 *              Implementation.docx".
 *              This initial implementation uses an ISR-based approach because
 *              this is the quickest way to get sampling working for the
 *              hardware engineers' immediate test needs.
 *
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "AD7766_Intrpt.h"
#include "AD7766_Common.h"
#include "PowerControl.h"
#include "PinDefs.h"
#include "fsl_dspi_master_driver.h"
#include "fsl_gpio_driver.h"
#include "fsl_ftm_hal.h"
#include "PinConfig.h"

// TODO: For testing only
#include "fsl_os_abstraction.h"

//..............................................................................

uint32_t g_NumSamplesRequired = 0;
uint16_t g_SamplesPerSec = 0;
dspi_master_state_t g_AD7766SpiState; // Global because must be persistent
dspi_device_t g_AD7766SpiDevice;
// The following are modified from an ISR and therefore need to be volatile
volatile uint8_t g_AD7766TxBuf[AD7766_NUM_BYTES_PER_SAMPLE] = {0x00, 0x00, 0x00};
volatile uint8_t g_AD7766RxBuf[AD7766_NUM_BYTES_PER_SAMPLE] = {0x00, 0x00, 0x00};
volatile uint32_t g_SampleIndex = 0;
volatile bool g_bAdcErrorDRDYHighWhileReading = false;
volatile bool g_bAdcErrorSpiNotCompleted = false;
volatile enum
{
    SAMPLINGSTATE_IDLE,
    SAMPLINGSTATE_AWAITING_FIRST_SAMPLE,
    SAMPLINGSTATE_MAIN_SAMPLING,
    SAMPLINGSTATE_FINISHED
} g_SamplingState = SAMPLINGSTATE_IDLE;

// N.B. AD7766 I/O lines are configured in powerAnalogOn()

//..............................................................................

/*
 * AD7766Intrpt_SpiInit
 *
 * @desc
 *
 * @param
 *
 * @returns -
 */
void AD7766Intrpt_SpiInit(void)
{
    //dspi_status_t SpiStatus;
    dspi_master_user_config_t SpiConfig;
    uint32_t calculatedBaudRate;

    // TODO: Delete SPI components from Processor Expert eventually

    // N.B. The Kinetis K series uses the "DSPI" SDK drivers, not the "spi" ones -
    // mentioned at beginning of SDK DSPI documentation.

    // Configure ADC_CSn for direct control from SPI peripheral
    pinConfigDigitalOut(ADC_CSn, kPortMuxAlt2, 0, false);

    //SpiConfig.isChipSelectContinuous = false;
    SpiConfig.isChipSelectContinuous = true;    // Keeps CS low for entire multi-byte transaction
    SpiConfig.isSckContinuous = false;
    SpiConfig.pcsPolarity = kDspiPcs_ActiveLow;
    SpiConfig.whichCtar = kDspiCtar0;
    SpiConfig.whichPcs = AD7766_DSPI_CHIP_SELECT;
    /*SpiStatus = */ DSPI_DRV_MasterInit(AD7766_DSPI_INSTANCE, &g_AD7766SpiState, &SpiConfig);

    // Configure SPI signal format
    AD7766_GetDataFormatConfig(8, &(g_AD7766SpiDevice.dataBusConfig));
    g_AD7766SpiDevice.bitsPerSec = 500000;  // ****************** TODO: Increase SPI clock rate
    /* SPIStatus = */ DSPI_DRV_MasterConfigureBus(AD7766_DSPI_INSTANCE,
                                                  &g_AD7766SpiDevice,
                                                  &calculatedBaudRate);
}

/*
 * AD7766Intrpt_SpiDeinit
 *
 * @desc
 *
 * @param
 *
 * @returns -
 */
void AD7766Intrpt_SpiDeinit(void)
{
    // dspi_status_t SPIStatus;

    // Configure ADC_CSn to GPIO, so can just hold high
    pinConfigDigitalOut(ADC_CSn, kPortMuxAsGpio, 1, false);

    //*********************************************
    //*********************************************
    // NOTE: The following call hung when configFRTOS_MEMORY_SCHEME in
    // FreeRTOSConfig.h was previously set to heap scheme of 1,
    // because de-allocation is specifically prohibited in this
    // heap model - see http://www.freertos.org/a00111.html

    /*SPIStatus = */ DSPI_DRV_MasterDeinit(AD7766_DSPI_INSTANCE);

    //*********************************************
    //*********************************************
}

/*
 * AD7766Intrpt_PerformSampleBurst
 *
 * @desc    Performs an AD7766 sample burst based upon the specified parameters.
 *          The sampling subsystem should be powered up and the AD7766 SPI
 *          peripheral on the Kinetis should be initialised before this
 *          function is called.
 *
 * @param   NumSamples: Up to AD7766_MAX_OUTPUT_SAMPLES
 * @param   SamplesPerSec: Between  AD7766_INTR_MIN_SAMPLES_PER_SEC and
 *          AD7766_INTR_MAX_SAMPLES_PER_SEC
 *
 * @returns true if no issues, false if problem during sampling
 */
bool AD7766Intrpt_PerformSampleBurst(uint32_t NumSamples, uint16_t SamplesPerSec)
{
    uint32_t i;
    uint32_t MclkFreqHz;

    if ((NumSamples > AD7766_MAX_OUTPUT_SAMPLES) ||
        (SamplesPerSec < AD7766_INTR_MIN_SAMPLES_PER_SEC) ||
        (SamplesPerSec > AD7766_INTR_MAX_SAMPLES_PER_SEC))
    {
        // TODO: Trigger error
    }

    // TODO: Needs to be done elsewhere eventually, exactly once per subsystem
    // power-on
    AD7766Intrpt_SpiInit();

    // Set up globals for sampling
    g_NumSamplesRequired = NumSamples;
    g_SampleIndex = 0;
    g_SamplesPerSec = SamplesPerSec;
    g_bAdcErrorDRDYHighWhileReading = false;
    g_bAdcErrorSpiNotCompleted = false;
    g_SamplingState = SAMPLINGSTATE_AWAITING_FIRST_SAMPLE;

    // TODO: FOR DEBUGGING ONLY - zero the sample array
    for (i = 0; i < SAMPLE_BUFFER_SIZE_WORDS; i++)
    {
        g_pSampleBuffer[i] = 0;
    }

    //..........................................................................

    AD7766_PerformResetSequence();

    //..........................................................................

    // Enable ADC-IRQ# interrupt (AD7766 DRDY#) for falling edge.
    // IMPORTANT NOTE: AD7766 generates very short DRDY# pulses (435ns typical
    // for AD7766-1 variant).
    // TODO: Need to ensure that this is long enough to trigger the Kinetis
    // interrupt! See also Kinetis interrupt pulse width requirements of
    // Kinetis datasheet section 2.3.2, General switching specifications
    pinConfigDigitalIn(ADC_IRQn, kPortMuxAsGpio, false, kPortPullDown,
                       kPortIntFallingEdge);

    //..........................................................................

    // Commence sampling by starting MCLK
    MclkFreqHz = SamplesPerSec * ((uint16_t)AD7766_OVERSAMPLING_RATIO);
    AD7766_MclkStart(MclkFreqHz);

    // The AD7766 will now start sampling, and the raw 24-bit values will
    // be put into SampleArray[].
    // NOTE: It will first perform the required number of filter settling MCLK
    // cycles without DRDY# going low. After this, the main sampling will start.
    // TODO: Check on scope that correct number of MCLK filter settling time
    // cycles elapse before main sampling actually commences.

    // Wait for sampling to complete
    // TODO: Put a timer on this eventually
    while(g_SamplingState != SAMPLINGSTATE_FINISHED);

    // Disable/de-initialise everything again
    AD7766_MclkStop();
    pinConfigDigitalIn(ADC_IRQn, kPortMuxAsGpio, false, kPortPullDown,
                       kPortIntDisabled);
    AD7766Intrpt_SpiDeinit();

    //..........................................................................
    // Raw samples will now be in g_pSampleBuffer[]

    // Convert all values from raw to signed - do here rather than in ISR
    // for performance reasons
    for (i = 0; (i < NumSamples) && (i < SAMPLE_BUFFER_SIZE_WORDS); i++)
    {
        g_pSampleBuffer[i] = AD7766_ConvertRawToSigned((uint32_t)g_pSampleBuffer[i]);
    }

    // Check for any errors
    if (g_bAdcErrorDRDYHighWhileReading || g_bAdcErrorSpiNotCompleted)
    {
        return false;
    }

    return true;
}

/*
 * AD7766Intrpt_ADCIRQn_ISR
 *
 * @desc    ISR to process the AD7766 sampling. Triggered by the DRDY#/ADC-IRQ
 *          signal from the AD7766.
 *          This ISR controls the reading of each 24-bit sample result from
 *          the AD7766 over SPI (in the form of 3 bytes). The overall cycle
 *          is as follows:
 *             - Stores the 24-bit result of the previous SPI sample-reading
 *               cycle into a RAM array
 *             - Triggers a new 3-byte SPI transaction
 *             - Sets a flag when complete
 *
 * @param
 *
 * @returns -
 */
void AD7766Intrpt_ADCIRQn_ISR(void)
{
    dspi_status_t SpiStatus;
    bool bTriggerNextSpi;
    uint32_t FramesTransferred;
    uint32_t RawSampleVal;

    // TODO: Check that DRDY# (ADC-IRQ) is low throughout reading - AD7766
    // datasheet specifies this
    // TODO: Check where these ADC_IRQn-low checks actually need to go
    if (GPIO_DRV_ReadPinInput(ADC_IRQn) != 0)
    {
        g_bAdcErrorDRDYHighWhileReading = true;
    }

    bTriggerNextSpi = false;
    switch (g_SamplingState)
    {
    case SAMPLINGSTATE_AWAITING_FIRST_SAMPLE:
        // First cycle, so no ADC result is available yet - trigger first SPI
        // transaction
        bTriggerNextSpi = true;
        g_SamplingState = SAMPLINGSTATE_MAIN_SAMPLING;
        break;

    case SAMPLINGSTATE_MAIN_SAMPLING:
        SpiStatus = DSPI_DRV_MasterGetTransferStatus(AD7766_DSPI_INSTANCE,
                                                     &FramesTransferred);
        if ((SpiStatus == kStatus_DSPI_Success) &&
            (FramesTransferred == AD7766_NUM_BYTES_PER_SAMPLE))
        {
            RawSampleVal = (((uint32_t)g_AD7766RxBuf[0]) << 16) |
                           (((uint32_t)g_AD7766RxBuf[1]) << 8) |
                           ((uint32_t)g_AD7766RxBuf[2]);

            if (g_SampleIndex < SAMPLE_BUFFER_SIZE_WORDS)  // Buffer overrun protection
            {
                g_pSampleBuffer[g_SampleIndex] = (int32_t)RawSampleVal;
                g_SampleIndex++;
                if (g_SampleIndex < g_NumSamplesRequired)
                {
                    bTriggerNextSpi = true;
                }
                else
                {
                    g_SamplingState = SAMPLINGSTATE_FINISHED;
                }
            }
            else
            {
                // ERROR - attempted buffer overrun
            }
        }
        else
        {
            // ERROR - SPI transfer for previous sample is not yet completed
            g_bAdcErrorSpiNotCompleted = true;
        }
        break;

    case SAMPLINGSTATE_FINISHED:
        break;

    default:
        // ERROR
        break;
    }

    if (bTriggerNextSpi)
    {
        // Data must not be read from device when DRDY# is high, so capture
        // if this occurs (e.g. due to delayed ISR). N.B. Would also ideally
        // check at end of SPI sequence, but can't do this with current ISR
        // approach
        if (GPIO_DRV_ReadPinInput(ADC_IRQn) != 0)
        {
            g_bAdcErrorDRDYHighWhileReading = true;
        }

        // Commence new SPI transfer of AD7766 sample bytes
        SpiStatus = DSPI_DRV_MasterTransfer(AD7766_DSPI_INSTANCE,
                                            &g_AD7766SpiDevice,
                                            (uint8_t *)g_AD7766TxBuf,
                                            (uint8_t *)g_AD7766RxBuf,
                                            AD7766_NUM_BYTES_PER_SAMPLE);
    }
}


//******************************************************************************
//******************************************************************************
// TODO: This function is deprecated for now, but simple reading of 3 bytes
// might be useful as a test function? So keep this function for now.
/*
 * AD7766Intrpt_ReadSampleRegisters
 *
 * @desc
 *
 * @param
 *
 * @returns -
 */
uint32_t AD7766Intrpt_ReadSampleRegisters(void)
{
    dspi_status_t SpiStatus;
    uint32_t FramesTransferred;
    uint32_t AD7766RegValue;

    // TODO: Hacktastic function - tidy up eventually. But does work well
    // enough for now.
    // N.B. This code currently produces a 15us SPI bus blip about 400us before
    // the main SPI transactions (see scope grabs in Bart's "Insight Firmware
    // ADC and Sampling Implementation" document in Box - this is probably
    // because of the hacky SPI bus setup sequence - need to investigate
    // further.

    // SPIStatus = DSPI_DRV_MasterSetDelay()  // TODO: See if needed

    // Use non-blocking function for now because doesn't require OS
    SpiStatus = DSPI_DRV_MasterTransfer(AD7766_DSPI_INSTANCE, &g_AD7766SpiDevice,
                                        (uint8_t *)g_AD7766TxBuf,
                                        (uint8_t *)g_AD7766RxBuf,
                                        AD7766_NUM_BYTES_PER_SAMPLE);
    while(1)    // Use timeout eventually
    {
        SpiStatus = DSPI_DRV_MasterGetTransferStatus(AD7766_DSPI_INSTANCE,
                                                     &FramesTransferred);
        if (SpiStatus == kStatus_DSPI_Success)
        {
            break;
        }
    }

    // TODO: Check if SPI IRQ is automatically put in place by SDK code - see DSPI_DRV_IRQHandler()

    AD7766RegValue = (((uint32_t)g_AD7766RxBuf[0]) << 16) |
                     (((uint32_t)g_AD7766RxBuf[1]) << 8) |
                     ((uint32_t)g_AD7766RxBuf[2]);

    return AD7766RegValue;
}
//******************************************************************************
//******************************************************************************






//******************************************************************************
#if 0
// Experimental code only - discard eventually
//******************************************************************************

//******************************************************************************
// EDMA_TriggerFromDebugUART
// EDMA triggering via debug UART, by simply re-starting the cycle each time.
// This is the original KSDK example functionality.
//
//******************************************************************************
void EDMA_TriggerFromDebugUART(void)
{
    edma_chn_state_t     chnState;
    edma_software_tcd_t *pSoftwareTcd;
    edma_state_t         EdmaState;
    edma_user_config_t   edmaUserConfig;
    edma_scatter_gather_list_t SourceSGList, DestSGList;

    osa_status_t         syncStatus;
    bool                 result;
    uint32_t             i, channel = 0;

    // Init OSA layer.
    OSA_Init();

    PRINTF("\r\n EDMA transfer from memory to memory \r\n");

    // Init eDMA modules.
    edmaUserConfig.chnArbitration = kEDMAChnArbitrationRoundrobin;
    edmaUserConfig.notHaltOnError = false;
    EDMA_DRV_Init(&EdmaState, &edmaUserConfig);

    // Create semaphore to synchronize edma transaction
    OSA_SemaCreate(&sema, 0);



    while (1)
    {
        // TODO: Modify code so that DMA is triggered from pushbutton switch
        // (rather than starting in "always running" mode when EDMA_DRV_StartChannel()
        // is called):
        //   - Set up GPIO so that pushbutton input triggers (test inside this
        //     main loop first)
        //   - Configure this pin for kPortDmaRisingEdge or kPortDmaFallingEdge
        //   - Change kDmaRequestMux0AlwaysOn62 below to the correct PORTn
        //     value


        // EDMA channel request.
        EDMA_DRV_RequestChannel(channel, kDmaRequestMux0AlwaysOn62, &chnState);

        // Fill zero to destination buffer
        for (i = 0; i < BUFFER_SIZE; i ++)
        {
            destAddr[i] = 0x00;
        }

        // Prepare memory pointing to software TCDs.
        pSoftwareTcd = OSA_MemAllocZero(STCD_SIZE(EDMA_CHAIN_LENGTH));

        // Configure EDMA channel.
        SourceSGList.address  = (uint32_t)srcAddr;
        DestSGList.address = (uint32_t)destAddr;
        SourceSGList.length   = BUFFER_SIZE;
        DestSGList.length  = BUFFER_SIZE;

        // configure single end descritptor chain.
        EDMA_DRV_ConfigScatterGatherTransfer(&chnState, pSoftwareTcd,
                                             kEDMAMemoryToMemory,
                                             EDMA_TRANSFER_SIZE,
                                             EDMA_WATERMARK_LEVEL,
                                             &SourceSGList, &DestSGList,
                                             EDMA_CHAIN_LENGTH);

        // Install callback for eDMA handler
        EDMA_DRV_InstallCallback(&chnState, EDMA_Callback, NULL);

        PRINTF("\r\n Starting EDMA channel No. %d to transfer data from addr 0x%x to addr 0x%x",  \
                                                                                        channel,  \
                                                                                (uint32_t)srcAddr,\
                                                                                (uint32_t)destAddr);

        // Initialize transfer.
        EDMA_DRV_StartChannel(&chnState);

        // Wait until transfer is complete
        do
        {
            syncStatus = OSA_SemaWait(&sema, OSA_WAIT_FOREVER);
        }while(syncStatus == kStatus_OSA_Idle);

        // Verify destAddr buff
        result = true;
        for (i = 0; i < BUFFER_SIZE; i ++)
        {
            if (destAddr[i] != srcAddr[i])
            {
                result = false;
                break;
            }
        }

        if (result == true)
        {
            PRINTF("\r\n Transfered with eDMA channel No.%d: successfull",channel);
        }
        else
        {
            PRINTF("\r\n Transfered with eDMA channel No.%d: fail",channel);
        }

        // Stop channel
        EDMA_DRV_StopChannel(&chnState);

        // Release channel
        EDMA_DRV_ReleaseChannel(&chnState);

        // Free pSoftwareTcd
        OSA_MemFree((void *)pSoftwareTcd);

        // Prepare for another channel
        PRINTF("\r\nPress any key to start transfer with other channel");
        GETCHAR();
        channel ++;
        if (channel == DMA_INSTANCE_COUNT * FSL_FEATURE_EDMA_MODULE_CHANNEL)
        {
            channel = 0;
        }
    }
}


//******************************************************************************
#endif // 0
//******************************************************************************





#ifdef __cplusplus
}
#endif