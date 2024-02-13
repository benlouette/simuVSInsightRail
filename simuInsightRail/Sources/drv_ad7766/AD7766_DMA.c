#ifdef __cplusplus
extern "C" {
#endif

/*
 * AD7766_DMA.c
 *
 *  Created on: Apr 27, 2016
 *      Author: Bart Willemse
 * Description: DMA-based AD7766 ADC & sampling functionality, for AD7766
 *              connected on Kinetis K24's DSPI 1 SPI port.
 *
 * .............................................................................
 * See companion document "Insight Firmware Sampling and DSP Implementation.docx".
 *
 * .............................................................................
 * NOTE: This module requires the following be done before use:
 *     - EDMA_DRV_Init() must be called
 *     - **************** TODO: Add all further prerequisites here
 * .............................................................................
 *
 * This AD7766 DSPI DMA driver code is a custom driver implementation to
 * support our required DSPI/ EDMA functional requirements. The KSDK 1.3.0
 * does provide DSPI_DRV_EdmaXXXX() driver functions, but these functions aren't
 * suitable for our AD7766 DSPI EDMA requirements because:
 *     - They only perform immediate software-triggered RAM-buffer <-> SPI data
 *       transfers, rather than the GPIO-triggered ones with ping-pong buffer
 *       support etc which we need
 *     - They use their own ISRs, which dont match our ping-pong ISR needs
 *     - They use ISRs triggered from the DSPI module (********** TODO: Is this actually the case? *************),
 *       whereas we only want ISRs triggered from the EDMA module
 *     - They request any available EDMA channel, whereas we need to have fixed
 *       channels/priorities (TODO: Still TBD)
 *
 * ***IMPORTANT ***
 * Do NOT use any KSDK DSPI_DRV_XXXX() driver-level functions for the DSPI 1
 * AD7766 instance in conjunction with this code. This is because this AD7766
 * module uses CUSTOM DSPI driver code which is NOT COMPATIBLE WITH THE SDK
 * DSPI_DRV_XXXX() functions. However, the DSPI_HAL_XXXX() HAL-level functions
 * could be used if necessary.
 *
 * .............................................................................
 * This AD7766 DMA-based approach uses the following Kinetis peripherals:
 *     - FlexTimer 0: Generates MCLK for AD7766 (see AD7766_FTM_INSTANCE)
 *     - GPIO: Receives DRDY signal from AD7766
 *     - DMAMUX: Routes triggering to TWO EDMA channels:
 *         - Routes GPIO input trigger for SPI Tx
 *         - Routes DSPI Rx-not-empty for SPI Rx
 *     - DSPI: Performs SPI transactions to read AD7766 over SPI, in conjunction
 *       with EDMA
 *     - EDMA: Performs DMA SPI transfers, using TWO DMA channels:
 *         - SPI Tx: Generates dummy SPI Tx transfers to the AD7766, to
 *           drive the required SPI transactions
 *         - SPI Rx: Performs the actual sample data transfer from the AD7766
 *           SPI to the RAM ping-pong buffers
 *
 * .............................................................................
 *
 * This code is currently written specifically for the AD7766 on SPI1 - if it
 * needs to go on a different SPI port then it will need to be modified. This
 * is because, on our Kinetis K24, its 3 DSPI module instances (and
 * aspects of their EDMA interactions) are not all the same. In particular,
 * SPI0 is different from SPI1/SPI2, for example:
 *     - Differing FIFO sizes - SPI0 has 4-deep FIFOs whereas SPI1/2 only have
 *       1-deep FIFOs - see K24 ref manual sections 3.9.3.4 TX FIFO size /
 *       3.9.3.5 RX FIFO size
 *     - DMA MUX request sources - SPI0 has separate Tx and Rx DMA request
 *       sources, whereas SPI1/SPI2 have combined sources - see K24F ref manual
 *       3.3.9.1 DMA MUX request sources
 *
 * .............................................................................
 *
 * Further notes:
 *     - Our Kinetis K series uses the "DSPI" KSDK drivers, not the "SPI" ones -
 *       mentioned at beginning of KSDK DSPI documentation
 *     - N.B. During the development of this code, the DSPI_DRV_EdmaMasterInit()
 *       and DSPI_DRV_EdmaMasterStartTransfer() functions were valuable
 *       reference points
 *
 */
#include <stdbool.h>
#include <string.h>
#include "AD7766_DMA.h"
#include "AD7766_Common.h"
#include "PinDefs.h"
#include "fsl_dspi_master_driver.h"
#include "fsl_dspi_shared_function.h"
#include "fsl_gpio_driver.h"
#include "fsl_edma_driver.h"
#include "fsl_clock_manager.h"
#include "PinConfig.h"
#include "Resources.h"
#include "AdcApiDefs.h"
#include "Timer.h"

//..............................................................................

// Pushbutton loopback testing - use for triggering individual DSPI EDMA
// transactions via a pushbutton on TEST_IO2, for test purposes.
//#define PUSHBUTTON_LOOPBACK_TESTING

// Define number of DSPI Tx dummy 16-bit words. Limited by the DSPI 1 only
// being able to accept 2 x Tx words pushed into it before it overflows.
// Various EDMA-related things assume that this value is 2 - DO NOT CHANGE
#define AD7766_SPI_TRANSFERS_PER_SAMPLE   (2)

// Define the DRDY trigger input pin (i.e. as defined by GPIO_MAKE_PIN() macro)
// for the sampling. Normally corresponds to the pin connected to AD7766 DRDY.
// However, can also be triggered from a test input pin for debugging purposes.
#ifndef PUSHBUTTON_LOOPBACK_TESTING
#define AD7766_DRDY_INPUT_PIN    ADC_IRQn
#else
#define AD7766_DRDY_INPUT_PIN    TEST_IO2
#endif

// NOTE: CANNOT CURRENTLY EASILY CHANGE g_AD7766DspiInstance without changing
// more of the code as well
const uint32_t g_AD7766DspiInstance = 1;  // For AD7766 use on DSPI 1
SPI_Type *g_pAD7766DspiBaseAddr;

uint32_t g_AD7766DrdyPortIndex;

static volatile bool g_bAD7766SamplingIsInProgress = false;

static bool g_bAD7766InitSuccess = false;

edma_chn_state_t g_AD7766TxEdmaChannelState;
edma_chn_state_t g_AD7766RxEdmaChannelState;

// Define DSPI Tx dummy data/command buffer - MUST BE 8-BYTE-MEMORY-ALIGNED -
// the 8-byte alignment value is specified by
// AD7766_SPI_TRANSFERS_PER_SAMPLE * sizeof(uint32_t). This alignment is
// required because the EDMA's "modulo" feature is used for cycling through
// the words in this buffer for every DSPI EDMA Tx trigger.
// ************** TODO: Come back to this, check that it is completely reliable! ******************
// TODO: Can change to aligned(sizeof(g_DspiTxPushWords)) - BUT would then also
// need the EDMA modulo setting in the Tx channel's TCD to change correspondingly
#ifdef _MSC_VER
uint32_t __declspec(align(8))  g_DspiTxPushWords[AD7766_SPI_TRANSFERS_PER_SAMPLE] ;
#else 
uint32_t g_DspiTxPushWords[AD7766_SPI_TRANSFERS_PER_SAMPLE] __attribute__((aligned(8)));
#endif // _MSC_VER



// TODO: CHECK THE FOLLOWING CAREFULLY, and compare with Daniel's implementation
// Define the Rx ping-pong buffer - MUST BE MEMORY-ALIGNED to the size of
// the buffer, for EDMA purposes. Total number of 4-byte entries is 256,
// i.e. 128 x 4-byte words per side. This corresponds to 1024 bytes in total,
// so kEDMAModulo1Kbytes is used for the rolling buffer size in the code.
// Do NOT change these #defines without also adjusting the code.
// TODO: Tie all of these calculations together eventually using #defines etc.
#define AD7766_RX_PINGPONGBUF_TOTAL_ENTRIES   (ADC_SAMPLES_PER_BLOCK * 2)
#define AD7766_RX_PINGPONGBUF_SIZE_BYTES   (AD7766_RX_PINGPONGBUF_TOTAL_ENTRIES * sizeof(uint32_t) )

// g_bPingSubBuffer indicates whether currently ping or pong sub-buffer
volatile bool g_bPingSubBuffer;

// ******** TODO: Check that buffer is aligned to its size
#ifdef _MSC_VER

uint32_t __declspec(align(1024))  g_AD7766RxPingPongBuf[AD7766_RX_PINGPONGBUF_TOTAL_ENTRIES] ;
#else
uint32_t g_AD7766RxPingPongBuf[AD7766_RX_PINGPONGBUF_TOTAL_ENTRIES]
                     __attribute__((aligned(AD7766_RX_PINGPONGBUF_SIZE_BYTES)));
#endif

// ADC ISR callback function pointer
tAdcISRCallback g_pAdcISRCallback = NULL;

volatile uint16_t g_AD7766CurrentBlockNum = 0;   // DO NOT make static, because
                                                 // shared with AD7766_Common.c

// g_BlockNumForMclkStopTest controls the performing of an MCLK stoppage test
// in the next sampling burst. See AD7766_SetUpMclkStopTest().
// If non-zero then specifies for which block number the MCLK needs to be
// stopped. Automatically zeroed (disabled) when AD7766_FinishSampling() is 
// subseqently called.
static uint16_t g_AD7766BlockNumForMclkStoppageTest = 0;

//..............................................................................

// TODO: My DSPI "BaseAddr" stuff (and similarly-named variables might NOT 
// actually be the DSPI's base address? Instead, it's a complex SPI_Type * !!!!
// IF SO THEN RENAME IT APPROPRIATELY EVERYWHERE

static void AD7766_DspiConfigure(dspi_which_pcs_config_t ChipSelect);
static void AD7766_DspiTxDummyBufInit(dspi_which_pcs_config_t ChipSelect);
static void AD7766_DspiStopAndDeconfigure(void);
static void AD7766_EdmaConfigure(void);
static void AD7766_EdmaStart(void);
static void AD7766_EdmaStopAndDeconfigure(void);
static void AD7766_EdmaRxCallbackISR(void *param, edma_chn_status_t ChannelStatus);

//..............................................................................

/*
 * AD7766_Init
 *
 * @desc    Initialises the AD7766 driver and requests the required EDMA
 *          channels. Must be called exactly once shortly after firmware
 *          start-up:
 *            - AFTER EDMA_DRV_Init() is called
 *            - BEFORE any other DMA channel requests are done by any other
 *              modules, including any KSDK functions which use EDMA. This
 *              is because this function requires fixed EDMA channels, which
 *              must not be grabbed by any other EDMA channel requests
 *
 * @param
 *
 * @returns -
 */
void AD7766_Init(void)
{
    dma_request_source_t EdmaRequestSource;
    uint8_t ReqChannelResult;
    bool bOK;

    g_pAD7766DspiBaseAddr = g_dspiBase[g_AD7766DspiInstance];
    g_AD7766DrdyPortIndex = GPIO_EXTRACT_PORT(AD7766_DRDY_INPUT_PIN);

    g_bAD7766InitSuccess = false;
    AD7766_ClearError();
    bOK = true;

    //..........................................................................
    // Tx direction

    // Initialise Tx dummy buffer
    AD7766_DspiTxDummyBufInit(AD7766_DSPI_CHIP_SELECT);

    // Get DRDY input's ADCMUX DMA request source from its port index
    switch (g_AD7766DrdyPortIndex)
    {
    case GPIOA_IDX:
        EdmaRequestSource = kDmaRequestMux0PortA;
        break;
    case GPIOB_IDX:
        EdmaRequestSource = kDmaRequestMux0PortB;
        break;
    case GPIOC_IDX:
        EdmaRequestSource = kDmaRequestMux0PortC;
        break;
    case GPIOD_IDX:
        EdmaRequestSource = kDmaRequestMux0PortD;
        break;
    case GPIOE_IDX:
        EdmaRequestSource = kDmaRequestMux0PortE;
        break;
    default:
        AD7766_SetError(AD7766ERROR_INVALID_PORT_INDEX);
        bOK = false;
        break;
    }

    if (bOK)
    {
        // Request Tx EDMA channel
        ReqChannelResult = EDMA_DRV_RequestChannel(EDMACHANNEL_AD7766_TX,
                                                   EdmaRequestSource,
                                                   &g_AD7766TxEdmaChannelState);
        if ((ReqChannelResult != EDMACHANNEL_AD7766_TX) ||
            (ReqChannelResult == kEDMAInvalidChannel))
        {
            AD7766_SetError(AD7766ERROR_EDMA_CHANREQ_TX);
            bOK = false;
        }
    }

    //..........................................................................
    // Rx direction
    if (bOK)
    {
        // Request Rx EDMA channel
        // TODO: The following line is SPI1-specific - would be good to abstract
        // better eventually (if practical?)
        // N.B. The DSPI 1 module for our AD7766 use has a SHARED RX/TX DMA
        // request - see the K24 reference manual section 3.3.9.1 "DMA MUX request
        // sources".
        ReqChannelResult = EDMA_DRV_RequestChannel(EDMACHANNEL_AD7766_RX,
                                                   kDmaRequestMux0SPI1,
                                                   &g_AD7766RxEdmaChannelState);

        if ((ReqChannelResult == EDMACHANNEL_AD7766_RX) &&
            (ReqChannelResult != kEDMAInvalidChannel))
        {
            // Install EDMA callback for ping-pong buffer interrupt indications -
            // for safety, do before enabling the interrupts further down
            EDMA_DRV_InstallCallback(&g_AD7766RxEdmaChannelState,
                                     AD7766_EdmaRxCallbackISR, NULL);

            g_bAD7766InitSuccess = true;
        }
        else
        {
            AD7766_SetError(AD7766ERROR_EDMA_CHANREQ_RX);
        }
    }

    //..........................................................................
}

/*
 * AD7766_InstallISRCallback
 *
 * @desc    Installs the specified callback ISR function of signature
 *          tAdcISRCallback.
 *
 * @param
 *
 * @returns -
 */
void AD7766_InstallISRCallback(tAdcISRCallback pAdcISRCallback)
{
    g_pAdcISRCallback = pAdcISRCallback;
}

/*
 * AD7766_PrepareMclkStoppageTest
 *
 * @desc    Prepares to perform an MCLK stoppage test for the next sampling
 *          burst, for the specified raw ADC sample block number. Can be used
 *          for testing failure response. The test is automatically disabled
 *          again when AD7766_FinishSampling() is called.
 *
 * @param   BlockNum: Raw ADC sample block number for which to trigger the MCLK
 *          stoppage test, with 1 being the first block (a value of 0 disables).
 *
 * @returns -
 */
void AD7766_PrepareMclkStoppageTest(uint16_t BlockNum)
{
    g_AD7766BlockNumForMclkStoppageTest = BlockNum;
}

/*
 * AD7766_StartSampling
 *
 * @desc
 *          AD7766_Init() must have been called before calling this function.
 *          If sampling is started successfully, then clears the AD7766 error code.
 *          If AD7766_PrepareMclkStoppageTest() was called just before this 
 *          function, then the MCLK stop test will be performed during sampling.
 *
 * @param
 *
 * @returns true if OK, false if error (use AD7766_GetFirstError() to get error)
 */
bool AD7766_StartSampling(uint32_t SamplesPerSec)
{
    uint32_t MclkFreqHz;
    uint32_t i;
    bool bStartedSamplingOK = false;

    if (g_bAD7766InitSuccess)
    {
        if (!g_bAD7766SamplingIsInProgress)
        {
            // Clear any errors - N.B. only does this if g_bAD7766InitSuccess
            // is true above, so will never erase an AD7766_Init() error (so
            // that it isn't lost)
            AD7766_ClearError();

            // Zero the ping-pong buffers, so can see whether sampling has written
            // fresh data (and avoid being misled by random or previous data if
            // sampling fails for some reason)
            for (i = 0; i < AD7766_RX_PINGPONGBUF_TOTAL_ENTRIES; i++)
            {
                g_AD7766RxPingPongBuf[i] = 0;
            }

            // Initialise to the ping sub-buffer (as opposed to the pong one)
            g_bPingSubBuffer = true;

            // Configure ADC_CSn for direct control from SPI peripheral - NOTE
            // that the other pins are set up in the power management
            // ************** TODO - move this to the power switching code?
            // TODO: Consolidate the chip-select stuff - hardware pin allocation
            // and Kinetis pin configuration option
            pinConfigDigitalOut(ADC_CSn, kPortMuxAlt2, 0, false);
            //***************************

            AD7766_DspiConfigure(AD7766_DSPI_CHIP_SELECT);

            // Start the transfer process in the hardware
            DSPI_HAL_StartTransfer(g_pAD7766DspiBaseAddr);

            //..................................................................
#ifdef PUSHBUTTON_LOOPBACK_TESTING
            // Performs DSPI EDMA MOSI/MISO loopback testing via DSP1, triggered by
            // a pushbutton connected into the TEST_IO2 pin.
            // Does not involve AD7766 at all - the AD7766 chip-select is kept high.
            // ******* NOTE: Need to connect MOSI->MISO loopback cable *******

            pinConfigDigitalOut(ADC_CSn, kPortMuxAsGpio, 1, false);

            AD7766_EdmaConfigure();

            AD7766_EdmaStart();

            //..................................................................
#else // NOT PUSHBUTTON_LOOPBACK_TESTING

            // ******* NOTE: ENSURE THAT MOSI->MISO LOOPBACK CABLE IS REMOVED *******

            AD7766_PerformResetSequence();

            // If a block-1 MCLK stoppage test is required then stop MCLK
            // output here. Note: Must be done AFTER AD7766_PerformResetSequence()
            // call above, which re-configures MCLK pin as well.
            if (g_AD7766BlockNumForMclkStoppageTest == 1)
			{
                pinConfigDigitalIn(ADC_MCLK, kPortMuxAsGpio, true,
                                   kPortPullDown, kPortIntDisabled);
			}

            AD7766_EdmaConfigure();

            AD7766_EdmaStart();

            // Commence sampling by starting MCLK
            MclkFreqHz = SamplesPerSec * ((uint32_t)AD7766_OVERSAMPLING_RATIO);

            // TODO: The following requires a critical section?? (so that
            // high-speed MCLK doesn't cause quick first-block-complete triggering
            // before the sampling variables below are set up).
            if (AD7766_MclkStart(MclkFreqHz))
            {
                bStartedSamplingOK = true;
                g_bAD7766SamplingIsInProgress = true;
                g_AD7766CurrentBlockNum = 1;
            }
            else
            {
                // Couldn't successfully start sampling - reset everything
                // TODO: CHECK / TEST THIS
                // TODO: This might cause a hard fault?? Because MCLK was
                // not actually started successfully, but AD7766_FinishSampling()
                // still tries to stop it (and also the DSPI and EDMA)
                AD7766_FinishSampling();
            }

#endif // NOT PUSHBUTTON_LOOPBACK_TESTING
            //..................................................................
        }
        else
        {
            AD7766_SetError(AD7766ERROR_ALREADY_SAMPLING);
        }
    }

    return bStartedSamplingOK;
}

/*
 * AD7766_FinishSampling
 *
 * @desc
 *          AD7766_Init() and AD7766_StartSampling() must have been called
 *          before calling this function.
 *
 * @param
 *
 * @returns true if OK, false if error (use AD7766_GetFirstError() to get error)
 */
bool AD7766_FinishSampling(void)
{
    g_bAD7766SamplingIsInProgress = false;

    // Disable MCLK stoppage testing
    g_AD7766BlockNumForMclkStoppageTest = 0;

    if (g_bAD7766InitSuccess)
    {
        // Stop MCLK. IMPORTANT: Must do before anything else
        AD7766_MclkStop();

        // De-configure EDMA
        AD7766_EdmaStopAndDeconfigure();

        // De-configure DSPI
        AD7766_DspiStopAndDeconfigure();
    }

    if (AD7766_ErrorIsSet())
    {
        return false;
    }
    return true;
}

/*
 * AD7766_GetCurrentBlockNum
 *
 * @desc    Gets the current raw ADC block number. N.B. Each raw ADC block
 *          contains ADC_SAMPLES_PER_BLOCK samples.
 *
 * @param   -
 *
 * @returns Raw ADC block number
 */
uint16_t AD7766_GetCurrentBlockNum(void)
{
    // N.B. Don't need critical section because g_AD7766CurrentBlockNum is
    // 16-bit and therefore reading is atomic.
    return g_AD7766CurrentBlockNum;
}

/*
 * AD7766_PreProcessRawSpiIntoSampleVal
 *
 * @desc    Checks and converts a single raw AD7766 sample word as received
 *          over SPI, into a clean SIGNED sample value:
 *              - Checks the AD7766 daisy-chain echo byte value
 *              - Swaps the LSWord and MSWord
 *              - Converts AD7766 output word to signed value
 *
 * @param   AdcRawValIn: Raw ADC value from DSPI EDMA
 * @param   pSampleValOut: Signed sample value output
 *
 * @returns true if OK
 */
bool AD7766_PreProcessRawSpiIntoSampleVal(uint32_t AdcRawValIn,
                                          int32_t *pSampleValOut)
{
    uint32_t AD7766Word;

    // TODO: Carefully check/test the following, and tidy it up & optimise it
    // for speed, because it will be called for every sample during real-time
    // processing.

    // Check that 4th dummy byte corresponds to that set up in the Tx
    // dummy buffer - works because AD7766 daisy-chain functionality
    // causes echo for this byte
#ifdef PUSHBUTTON_LOOPBACK_TESTING
    AD7766Word = ((AdcRawValIn & 0x000000FFU) << 16) |    // Top 16 bits
                 ((AdcRawValIn & 0xFFFF0000U) >> 16);    // Lower 8 bits
    *pSampleValOut = AD7766_ConvertRawToSigned(AD7766Word);

    if (((AdcRawValIn & 0x0000FF00U) >> 8) == 0xB3)        // ********* TODO: Tie in the check byte value in with the DSPI Tx buffer generation
#else
    // Get AD7766 word - the 2 x 16-bit halves are reversed, so needs some
    // moving about
    // TODO: CAREFULLY CHECK / TEST THIS MANIPULATION
    AD7766Word = ((AdcRawValIn & 0x0000FFFFU) << 8) |    // Top 16 bits
                 ((AdcRawValIn & 0xFF000000U) >> 24);    // Lower 8 bits
    *pSampleValOut = AD7766_ConvertRawToSigned(AD7766Word);

    if (((AdcRawValIn & 0x00FF0000U) >> 16) != 0xB3)        // ********* TODO: Tie in the check byte value in with the DSPI Tx buffer generation
#endif
    {
        AD7766_SetError(AD7766ERROR_SPI_ECHO_BYTE_MISMATCH);
        return false;
    }

    return true;
}

/*
 * AD7766_MaxBlockMillisecs
 *
 * @desc    Calculates the maximum worst-case duration (in milliseconds)
 *          needed for the AD7766 to perform sampling of one ADC sample block
 *          of length ADC_SAMPLES_PER_BLOCK, at a rate of ADCSamplesPerSec.
 *          Can be used for block timeout purposes - for safety, double the
 *          resultant worst-case duration and add a further generous time
 *          margin for latencies etc (e.g. 100ms).
 *          N.B. At low ADC sampling rates (e.g. 100Hz), each block duration
 *          might be seconds.
 *
 * @param   ADCSamplesPerSec: ADC sampling rate
 *
 * @returns Worst-case block duration in milliseconds, with 1 millisecond
 *          added to compensate for rounded-down small-millisecond
 *          values at high sampling rates
 */
uint32_t AD7766_MaxBlockMillisecs(uint32_t ADCSamplesPerSec)
{
    uint32_t AD7766NumSettlingSamples;
    uint32_t TotalWorstCaseNumSamples;
    uint32_t WorstCaseBlockMillisecs;

    // Worst-case block time is the first block in a sample burst, when the
    // AD7766 requires AD7766_NUM_SETTLING_MCLKS MCLK cycles to settle
    // internally before actually starting to output samples. Can convert
    // this to an equivalent number of samples.
    AD7766NumSettlingSamples = ((uint32_t)AD7766_NUM_SETTLING_MCLKS) /
                               AD7766_OVERSAMPLING_RATIO;

    TotalWorstCaseNumSamples = ADC_SAMPLES_PER_BLOCK + AD7766NumSettlingSamples;

    if (ADCSamplesPerSec != 0)  // Divide-by-zero protection
    {
        WorstCaseBlockMillisecs = ((((uint32_t)TotalWorstCaseNumSamples) * 1000) /
                                  ADCSamplesPerSec) + 1;
    }
    else
    {
        WorstCaseBlockMillisecs = 10000; // Probable safe value, but should never happen
    }

    return WorstCaseBlockMillisecs;
}

#if 0
// Test function
/*
 * AD7766_TEST_MaxBlockMillisecs
 *
 * @desc    Quick test function for AD7766_MaxBlockMillisecs().
 *
 * @param   -
 *
 * @returns -
 */
void AD7766_TEST_MaxBlockMillisecs(void)
{
    volatile uint32_t BlockMillisecs;   // volatile so can watch in debugger

    // *** NOTE ***: The following expected values shown in the comments are
    // for ADC_SAMPLES_PER_BLOCK=128 and the AD7766-1 16x oversampling
    // variant, and also include the AD7766 internal settling time. 
    // WILL BE DIFFERENT FOR OTHER CHIPS / FIRMWARE SAMPLING CONFIGURATIONS.
    // Single-step and check that the BlockMillisecs values correspond roughly.

                                           // Samples per sec
    BlockMillisecs = AD7766_MaxBlockMillisecs(1);         // ~200,000ms
    BlockMillisecs = AD7766_MaxBlockMillisecs(10);        // ~20,000ms
    BlockMillisecs = AD7766_MaxBlockMillisecs(100);       // ~2,000ms
    BlockMillisecs = AD7766_MaxBlockMillisecs(1000);      // ~200ms
    BlockMillisecs = AD7766_MaxBlockMillisecs(10240);     // ~19ms
    BlockMillisecs = AD7766_MaxBlockMillisecs(40960);     // ~5ms
    BlockMillisecs = AD7766_MaxBlockMillisecs(64000);     // ~3ms
    BlockMillisecs = AD7766_MaxBlockMillisecs(102400);    // ~2ms
}
#endif // 0

/*
 * AD7766_DspiConfigure
 *
 * @desc    Configures the Kinetis DSPI 1 module for AD7766 use.
 *          NOTE: Not easily adaptable for use on different DSPI instances -
 *          see comments at top of this file.
 *
 *          Heavily adapted from the KSDK's DSPI_DRV_EdmaMasterInit()
 *          and DSPI_DRV_MasterConfigureBus() functions.
 *
 *          NOTE: The DSPI I/O pins need to be configured before this function
 *          is called.
 *
 * @param
 *
 * @returns -
 */
static void AD7766_DspiConfigure(dspi_which_pcs_config_t ChipSelect)
{
    volatile uint32_t DspiSourceClock;
    dspi_data_format_config_t DspiDataFormat;

    // Enable DSPI clock - need to do this before making any register accesses
    CLOCK_SYS_EnableSpiClock(g_AD7766DspiInstance);

    // Initialise registers to default values, which disables the module
    DSPI_HAL_Init(g_pAD7766DspiBaseAddr);

    // Set to master mode
    DSPI_HAL_SetMasterSlaveMode(g_pAD7766DspiBaseAddr, kDspiMaster);

    // Disable continuous SCLK mode
    DSPI_HAL_SetContinuousSckCmd(g_pAD7766DspiBaseAddr, false);

    // Configure chip-select polarity
    DSPI_HAL_SetPcsPolarityMode(g_pAD7766DspiBaseAddr, ChipSelect, kDspiPcs_ActiveLow);

    // Disable FIFO operation, because SPI1 (for AD7766) only supports a 1-deep
    // FIFO anyway, and this will keep the operation more generalised across
    // different Kinetis SPI port instances if required in future.
    // K24 ref manual: "When the TX FIFO is disabled, SR[TFFF], SR[TFUF] and
    // SR[TXCTR] behave as if there is a one-entry FIFO"
    DSPI_HAL_SetFifoCmd(g_pAD7766DspiBaseAddr, false, false);

    // Initialise the configurable delays: PCS-to-SCK, prescaler = 0, scaler = 1
    DSPI_HAL_SetDelay(g_pAD7766DspiBaseAddr, kDspiCtar0, 0, 1, kDspiPcsToSck);

    // The following stuff is heavily adapted from DSPI_DRV_MasterConfigureBus() -
    // couldn't use the original function because it requires global stuff
    // to have been set up - not compatible with our needs here.
    // Get module clock frequency
    DspiSourceClock = CLOCK_SYS_GetSpiFreq(g_AD7766DspiInstance);
    // Set SPI clock rate to 4.096MHz (exact division of 24.576MHz master clock)
    /*CalculatedBaudRate = */DSPI_HAL_SetBaudRate(g_pAD7766DspiBaseAddr,
                                                  kDspiCtar0, 4096000U,
                                                  DspiSourceClock);

    // Configure data format for AD7766
    AD7766_GetDataFormatConfig(16, &DspiDataFormat);
    /*errorCode =*/ DSPI_HAL_SetDataFormat(g_pAD7766DspiBaseAddr, kDspiCtar0,
                                           &DspiDataFormat);

    // Configure Rx-not-empty triggering to EDMA
    DSPI_HAL_SetRxFifoDrainDmaIntMode(g_pAD7766DspiBaseAddr, kDspiGenerateDmaReq,
                                      true);

    // DSPI system enable
    DSPI_HAL_Enable(g_pAD7766DspiBaseAddr);
}

/*
 * AD7766_DspiTxDummyBufInit
 *
 * @desc    Fills the g_DspiTxPushWords[] array with dummy 32-bit words in the
 *          required DSPI Tx push word format. For each push word, The top
 *          16 bits need to contain command information, and the bottom 16 bits
 *          need to contain the actual word to be sent over SPI. This buffer
 *          is required by the EDMA for pushing the words to the DSPI Tx,
 *          thereby generating the required SPI transactions for the AD7766
 *          reading.
 *
 *          ********** CRITICALLY IMPORTANT: ******** The g_DspiTxPushWords[]
 *          array MUST be memory-aligned to a modulo corresponding to the byte
 *          size of the array! This is because the EDMA's modulo functionality
 *          is use for cycling through the array.
 *
 * @param   ChipSelect: Specifies which one of the DSPI's chip-selects that
 *          the AD7766 is connected to - this is schematic-dependent and
 *          Kinetis-pin-configuration-dependent.
 *
 * @returns -
 */
static void AD7766_DspiTxDummyBufInit(dspi_which_pcs_config_t ChipSelect)
{
    dspi_command_config_t TxCmdConfig;
    uint32_t TxCmdWord;
    // Define SPI Tx 16-bit dummy word sequence - meaningful so can view on
    // oscilloscope, and so that final byte might be used for AD7766
    // daisy-chain echo testing (TBD)
    const uint16_t TxDummyWords[AD7766_SPI_TRANSFERS_PER_SAMPLE] = {0xB334, 0x5582};

    // Set up Tx command word
    TxCmdConfig.isChipSelectContinuous = true;
    TxCmdConfig.whichCtar = kDspiCtar0;
    TxCmdConfig.whichPcs = ChipSelect;
    TxCmdConfig.isEndOfQueue = false;
    TxCmdConfig.clearTransferCount = false;
    // N.B. The first parameter (*base) isn't used in the function (it's not
    // an appropriate parameter for this function anyway), so just specify NULL
    TxCmdWord = DSPI_HAL_GetFormattedCommand(NULL, &TxCmdConfig);

    g_DspiTxPushWords[0] = TxCmdWord | (uint32_t)TxDummyWords[0];
    g_DspiTxPushWords[1] = TxCmdWord | (uint32_t)TxDummyWords[1];
}

/*
 * AD7766_DspiStopAndDeconfigure
 *
 * @desc
 *
 * @param
 *
 * @returns -
 */
static void AD7766_DspiStopAndDeconfigure(void)
{
    // TODO: Adapted the following stuff from DSPI_DRV_EdmaMasterDeinit()

    // Stop transfers
    DSPI_HAL_StopTransfer(g_pAD7766DspiBaseAddr);

    // Restore DSPI to defaults
    DSPI_HAL_Init(g_pAD7766DspiBaseAddr);

    // Disable the clock
    CLOCK_SYS_DisableSpiClock(g_AD7766DspiInstance);
}

/*
 * AD7766_EdmaConfigure
 *
 * @desc    Configures the two EDMA channels for AD7766 DSPI DMA use, one for
 *          the Tx Kinetis-to-AD7766 direction and one for the Rx
 *          AD7766-to-Kinetis direction. Also configures the
 *          AD7766_DRDY_INPUT_PIN pin correctly for use as the EDMA
 *          trigger.
 *
 * @param   -
 *
 * @returns -
 */
static void AD7766_EdmaConfigure(void)
{
    // TODO: TCD requirements: A TCD RAM mirror needs to be 32-byte aligned?? -
    // see the DSPI_DRV_EdmaMasterInit() function's edma_software_tcd_t
    // parameter (N.B. for DSPI_DRV_EdmaMasterInit(), this buffer needs to be
    // created externally and its addressed passed in I think)
    // NOTE: TxSoftwareTcd below does NOT need to be 32-bit aligned for this
    // function (because not used for scatter-gather / loop transfer) - it's
    // only used as a one-time setup buffer here. See also the same usage in
    // the DSPI_DRV_EdmaMasterStartTransfer() function.
    edma_software_tcd_t SoftwareTcd;
    edma_transfer_config_t TcdConfig;

    // Set up EDMA trigger input pin:
    //   - Clear any pending trigger flag
    //   - Configure it for falling-edge-triggered
    pinClearIntFlag(AD7766_DRDY_INPUT_PIN);
    pinConfigDigitalIn(AD7766_DRDY_INPUT_PIN, kPortMuxAsGpio, false,
                       kPortPullDown, kPortDmaFallingEdge);

    //..........................................................................
    // Set up EDMA channel for DSPI Tx
    // This DSPI Tx EDMA activity is required for driving the SPI transfers.
    // Triggered from the GPIO AD7766_DRDY_INPUT_PIN pin (normally
    // AD7766 DRDY). Each trigger transfers the 2 x 32-bit "push" words from
    // the g_DspiTxPushWords[] dummy Tx buffer to the DSPI. The DSPI Tx is
    // double-buffered and can therefore accept the 2 words immediately one
    // after the other.

    // Set up Transfer Control Descriptor for the AD7766 Tx EDMA channel
    memset(&TcdConfig, 0, sizeof(edma_transfer_config_t));

    // Source is the Tx dummy buffer
    TcdConfig.srcAddr = (uint32_t)(&g_DspiTxPushWords[0]);
    TcdConfig.srcTransferSize = kEDMATransferSize_4Bytes;
    TcdConfig.srcOffset = 4;
    TcdConfig.srcLastAddrAdjust = 0;
    // Set up modulo feature to keep cycling around Tx dummy buffer.
    // IMPORTANT: Dummy buffer needs to be 8-BYTE-ALIGNED for this modulo
    // usage - see DMA_TCDn_ATTR -> SMOD documentation in reference manual
    TcdConfig.srcModulo = kEDMAModulo8bytes;

    // Destination is the AD7766 DSPI's PUSHR transmit register
    TcdConfig.destAddr = DSPI_HAL_GetMasterPushrRegAddr(g_pAD7766DspiBaseAddr);
    TcdConfig.destTransferSize = kEDMATransferSize_4Bytes;
    TcdConfig.destOffset = 0;
    TcdConfig.destLastAddrAdjust = 0;
    TcdConfig.destModulo = kEDMAModuloDisable;

    // Set up the loop counts
    TcdConfig.majorLoopCount = 1;
    // K24 rRef manual 22.4.2 Fault reporting and handling: "The minor
    // loop byte count must be a multiple of the source and destination
    // transfer sizes
    TcdConfig.minorLoopCount = 8;

    memset(&SoftwareTcd, 0, sizeof(edma_software_tcd_t));
    EDMA_DRV_PrepareDescriptorTransfer(&g_AD7766TxEdmaChannelState,
                                       &SoftwareTcd, &TcdConfig,
                                       false, false);
    EDMA_DRV_PushDescriptorToReg(&g_AD7766TxEdmaChannelState,
                                 &SoftwareTcd);

    //..........................................................................
    // Set up EDMA channel for DSPI Rx
    // Triggered from the DSPI's Rx-not-empty signal. This channel transfers
    // the incoming pairs of 16-bit SPI words from the AD7766 DSPI into the
    // ping-pong buffers. Instead of separate "ping" and "pong" transfer
    // management, this EDMA channel uses the method of iterating across both
    // buffers in a linear fashion, with the EDMA "modulo" functionality being
    // used to roll from the end back to the beginning each time. The
    // "half-full" and "full" buffer interrupts are used to indicate when
    // each "ping" and "pong" sub-buffer is full.

    memset(&TcdConfig, 0, sizeof(edma_transfer_config_t));

    // Source is the AD7766 DSPI's POPR receive register
    TcdConfig.srcAddr = DSPI_HAL_GetPoprRegAddr(g_pAD7766DspiBaseAddr);
    TcdConfig.srcTransferSize = kEDMATransferSize_2Bytes;
    TcdConfig.srcOffset = 0;
    TcdConfig.srcLastAddrAdjust = 0;
    TcdConfig.srcModulo = kEDMAModuloDisable;

    // Destination is the Rx ping-pong buffer. Note that each DSPI Rx is
    // only a 16-bit word, so two of these are triggered for each sample
    TcdConfig.destAddr = (uint32_t)g_AD7766RxPingPongBuf;
    TcdConfig.destTransferSize = kEDMATransferSize_2Bytes;
    TcdConfig.destOffset = 2;
    TcdConfig.destLastAddrAdjust = 0;
    // Set up modulo feature to keep cycling around Rx buffer.
    // IMPORTANT: g_AD7766RxPingPongBuf[] needs to be 1024-BYTE-ALIGNED
    // for this modulo usage - see DMA_TCDn_ATTR -> DMOD documentation in
    // reference manual
#ifdef PUSHBUTTON_LOOPBACK_TESTING
    // For testing: Make modulo smaller, so can easily see looping
    TcdConfig.destModulo = kEDMAModulo32bytes;
    TcdConfig.majorLoopCount = 16;
#else
    TcdConfig.destModulo = kEDMAModulo1Kbytes;
    // Major loop count: Needs to cause exact full pass through total
    // combined ping-pong buffer for each major loop:
    //   - Total number of ping-pong buffer entries is ADC_SAMPLES_PER_BLOCK * 2
    //   - Each sample word entry is 4 bytes (sizeof(uint32_t))
    //   - Therefore, number of bytes in total ping-pong buffer is:
    //          (ADC_SAMPLES_PER_BLOCK * 2) * sizeof(uint32_t)
    //   - But each DSPI transaction is 2 bytes (N.B. this is minor loop count)
    //   - So major loop count needs to be:
    //          ((ADC_SAMPLES_PER_BLOCK * 2) * sizeof(uint32_t)) / 2
    //   - Which simplifies to:
    //        ADC_SAMPLES_PER_BLOCK * sizeof(uint32_t)
    TcdConfig.majorLoopCount = ADC_SAMPLES_PER_BLOCK * sizeof(uint32_t);
#endif

    // Set up minor loop count for 2 bytes (for each 16-bit DSPI Rx word)
    // K24 rRef manual 22.4.2 Fault reporting and handling: "The minor
    // loop byte count must be a multiple of the source and destination
    // transfer sizes
    TcdConfig.minorLoopCount = 2;

    memset(&SoftwareTcd, 0, sizeof(edma_software_tcd_t));
    // N.B. The following call also enables the "fully-complete" interrupt
    EDMA_DRV_PrepareDescriptorTransfer(&g_AD7766RxEdmaChannelState,
                                       &SoftwareTcd, &TcdConfig,
                                       true, false);
    // Configure enabling of half-complete interrupts
    EDMA_HAL_STCDSetHalfCompleteIntCmd(&SoftwareTcd, true);

    // Push to hardware TCD registers
    EDMA_DRV_PushDescriptorToReg(&g_AD7766RxEdmaChannelState,
                                 &SoftwareTcd);
}

/*
 * AD7766_EdmaStart
 *
 * @desc
 *
 * @param
 *
 * @returns -
 */
static void AD7766_EdmaStart(void)
{
    // Enable the EDMA channels. N.B. Return codes are always
    // kStatus_EDMA_Success so don't need to check
    EDMA_DRV_StartChannel(&g_AD7766TxEdmaChannelState);
    EDMA_DRV_StartChannel(&g_AD7766RxEdmaChannelState);
}

/*
 * AD7766_EdmaStopAndDeconfigure
 *
 * @desc
 *
 * @param
 *
 * @returns -
 */
static void AD7766_EdmaStopAndDeconfigure(void)
{
    // N.B. Return codes are always kStatus_EDMA_Success, so don't need to
    // check the return codes
    EDMA_DRV_StopChannel(&g_AD7766TxEdmaChannelState);
    EDMA_DRV_StopChannel(&g_AD7766RxEdmaChannelState);

    // Disable DRDY EDMA trigger input pin, and clear any pending trigger flag
    pinConfigDigitalIn(AD7766_DRDY_INPUT_PIN, kPortMuxAsGpio, false,
                       kPortPullDown, kPortIntDisabled);
    pinClearIntFlag(AD7766_DRDY_INPUT_PIN);
}

/*
 * AD7766_EdmaRxCallbackISR
 *
 * @desc
 *
 * @param
 *
 * @returns -
 */
static void AD7766_EdmaRxCallbackISR(void *param, edma_chn_status_t ChannelStatus)
{
    uint32_t *pPingOrPongBase;
    tAdcBlockData AdcBlockData;

    if (g_bPingSubBuffer)
    {
        pPingOrPongBase = &g_AD7766RxPingPongBuf[0];
        g_bPingSubBuffer = false;
    }
    else
    {
#ifdef PUSHBUTTON_LOOPBACK_TESTING
        // TODO: Hacky for testing - 4 words in each half-buffer
        // NOTE: At this small size, this ISR will be called at 1/4 of the raw
        // sampling rate, which could cause failure at higher sampling rates
        pPingOrPongBase = &g_AD7766RxPingPongBuf[4];
#else
        pPingOrPongBase = &g_AD7766RxPingPongBuf[ADC_SAMPLES_PER_BLOCK];
#endif
        g_bPingSubBuffer = true;
    }

    if (g_pAdcISRCallback != NULL)
    {
        AdcBlockData.AdcBlockResult = ADCBLOCKRESULT_DATAREADY;
        AdcBlockData.pSampleBlock = pPingOrPongBase;
        (g_pAdcISRCallback)(AdcBlockData);
    }

    g_AD7766CurrentBlockNum++;
    if ((g_AD7766BlockNumForMclkStoppageTest > 1) &&
        (g_AD7766BlockNumForMclkStoppageTest == g_AD7766CurrentBlockNum))
    {
        // MCLK stop test required - stop MCLK signal to Kinetis
        pinConfigDigitalIn(ADC_MCLK, kPortMuxAsGpio, true,
                           kPortPullDown, kPortIntDisabled);
    }
}




#ifdef __cplusplus
}
#endif