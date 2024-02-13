#ifdef __cplusplus
extern "C" {
#endif

/*
 * AD7766_Common.c
 *
 *  Created on: Apr 29, 2016
 *      Author: Bart Willemse
 * Description:
 *
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "AD7766_Common.h"
#include "PinDefs.h"
#include "PinConfig.h"
#include "fsl_ftm_driver.h"
#include "fsl_ftm_hal.h"
#include "PowerControl.h"
#include "TestUART.h"
#include "Timer.h"
#include "Device.h"

//..............................................................................

ftm_pwm_param_t g_FtmPwmParams =
{
    .mode = kFtmEdgeAlignedPWM,
    .edgeMode = kFtmLowTrue,
    .uFrequencyHZ = 100,  // Dummy placeholder value
    .uDutyCyclePercent = 50,
    .uFirstEdgeDelayPercent = 0
};

// TODO: Temporary only, until improve test UART functionality. Declared here
// rather than locally inside function so doesn't use FreeRTOS stack space.
#define TEST_UART_STR_BUF_SIZE  (30)
char TestUartStrBuf[TEST_UART_STR_BUF_SIZE];

extern volatile uint16_t g_AD7766CurrentBlockNum;
volatile uint16_t g_AD7766ErrorBlockNum = 0;

volatile AD7766ErrorEnum g_AD7766Error = AD7766ERROR_NONE;

//..............................................................................


/*
 * AD7766_GetDataFormatConfig
 *
 * @desc    Fills *pDspiDataFormat with the required DSPI data format
 *          configuration for the AD7766.
 *
 * @param   BitsPerFrame: Bits per frame - specifiable to allow different
 *          value for interrupt-driven versus DMA-driven DSPI transfers
 *          pDspiDataFormat: Pointer to dspi_data_format_config_t buffer to
 *          fill
 *
 * @returns -
 */
void AD7766_GetDataFormatConfig(uint32_t BitsPerFrame,
                                dspi_data_format_config_t *pDspiDataFormat)
{
    pDspiDataFormat->bitsPerFrame = BitsPerFrame;
    // AD7766 SCLK phase needs to be "capture on following edge"
    pDspiDataFormat->clkPhase = kDspiClockPhase_SecondEdge;
    // AD7766 SCLK polarity needs to be inactive-low, which corresponds to
    // SDK's active-high
    pDspiDataFormat->clkPolarity = kDspiClockPolarity_ActiveHigh;
    pDspiDataFormat->direction = kDspiMsbFirst;
}

/*
 * AD7766_PerformResetSequence
 *
 * @desc    Performs the AD7766 reset sequence. Notes:
 *            - Datasheet says "implemented relative to the rising edges of MCLK"
 *            - Some timing requirements in this sequence aren't entirely clear,
 *              so decent-length delays are used between each sequence step
 *
 *          After completion, the AD7766 is ready to automatically produce the
 *          correct number of filter settling cycles while being clocked from
 *          ADC-MCLK, and should only bring DRDY# low for the first time after
 *          the required number of these MCLK filter settling cycles have
 *          elapsed (e.g. about 1186 cycles for the AD7766-1 variant - see
 *          Table 7 in datasheet). This is implied by datasheet page 17:
 *          "tSETTLING is measured from the first MCLK rising edge after the
 *          rising edge of SYNC/PD to the falling edge of DRDY". Also: "The
 *          DRDY output goes logic low after tSETTLING to indicate when valid
 *          data is available on SDO for readback."
 *
 *          TODO: CHECK/TEST THIS - test it MULTIPLE TIMES IN SUCCESSION, and
 *          also immediately after first subsystem power-on etc
 *          TODO: Also check against Jayant's implementation in MSP430 system -
 *          he had problems with this?
 *
 * @param
 *
 * @returns -
 */
void AD7766_PerformResetSequence(void)
{
    // Configure ADC-MCLK as GPIO output so that can manually control, and
    // bring low to begin with
    pinConfigDigitalOut(ADC_MCLK, kPortMuxAsGpio, 0, false);
    DelayMicrosecs(10);

    // Bring SYNC/PD pin low
    GPIO_DRV_ClearPinOutput(Device_ADC_PowerdownPin());
    DelayMicrosecs(10);

    // Clock using MCLK
    GPIO_DRV_SetPinOutput(ADC_MCLK);
    DelayMicrosecs(10);
    GPIO_DRV_ClearPinOutput(ADC_MCLK);
    DelayMicrosecs(10);

    // Bring SYNC/PD pin high - prepares for leaving power-down
    GPIO_DRV_SetPinOutput(Device_ADC_PowerdownPin());
    DelayMicrosecs(10);

    // Bring MCLK high - takes out of power-down
    GPIO_DRV_SetPinOutput(ADC_MCLK);
    DelayMicrosecs(10);

    // Configure MCLK as FlexTimer0 CH4 output
    // TODO: Carefully check for any ADC-MLCK glitching here, because
    // transitioning from GPIO output type to FlexTimer output type
    pinConfigDigitalOut(ADC_MCLK, kPortMuxAlt4, 0, false);
}

/*
 * AD7766_MclkStart
 *
 * @desc
 *
 * @param
 *
 * @returns true if OK
 */
bool AD7766_MclkStart(uint32_t FrequencyHz)
{
    const ftm_user_config_t FtmUserConfig =
    {
        .tofFrequency = 0,          // FTMx_CONF -> NUMTOF field
        .BDMMode = kFtmBdmMode_11,
        .isWriteProtection = false,
        .syncMethod = kFtmUseSoftwareTrig
    };

    // N.B. ADC-MCLK output on PTD4 is already configured in powerAnalogOn()

    // Always returns kStatusFtmSuccess, so don't check return value
    FTM_DRV_Init(AD7766_FTM_INSTANCE, &FtmUserConfig);

    FTM_DRV_SetClock(AD7766_FTM_INSTANCE, kClock_source_FTM_SystemClk, kFtmDividedBy1);

    // Start PWM mode to generate MCLK output on channel 4
    g_FtmPwmParams.uFrequencyHZ = FrequencyHz;
    if (FTM_DRV_PwmStart(AD7766_FTM_INSTANCE, &g_FtmPwmParams,
                         AD7766_FTM_CHANNEL) != kStatusFtmSuccess)
    {
        AD7766_SetError(AD7766ERROR_MCLK_START);
        return false;
    }
    return true;
}

/*
 * AD7766_MclkStop
 *
 * @desc
 *
 * @param
 *
 * @returns -
 */
void AD7766_MclkStop(void)
{
    FTM_DRV_PwmStop(AD7766_FTM_INSTANCE, &g_FtmPwmParams, AD7766_FTM_CHANNEL);
    FTM_DRV_Deinit(AD7766_FTM_INSTANCE);
}

/*
 * AD7766_ConvertRawToSigned
 *
 * @desc    Converts an AD7766 raw 24-bit sample value into signed int32_t
 *          variable form.
 *
 * @param   RawSampleVal: The raw 24-bit sample value direct from the AD7766
 *
 * @returns Signed 24-bit value representing the decimal value of the sample
 */
inline int32_t AD7766_ConvertRawToSigned(uint32_t RawSampleVal)
{
    int32_t SignedVal;

    // Convert from raw 24-bit to 32-bit signed 2's complement using arithmetic
    // (rather than bitwise operations), so that it's safe and independent of
    // compiler, storage format/endedness etc
    if (RawSampleVal <= 0x007FFFFFu)
    {
        // Raw word is zero or positive, so just do simple typecast
        SignedVal = (int32_t)RawSampleVal;
    }
    else
    {
        // Raw word is 24-bit negative value, so convert to 32-bit negative
        SignedVal = ((int32_t)RawSampleVal) - ((int32_t)0x01000000);
    }

    return SignedVal;
}

/*
 * AD7766_SendSamplesToTestUart
 *
 * @desc
 *
 * @param
 *
 * @returns -
 */
void AD7766_SendSamplesToTestUart(uint32_t NumSamples,
                                  uint32_t SamplesPerSec,
                                  int32_t *pSampleBuf)  // NOTE: SIGNED samples
{
    uint32_t i;

    // TODO: Quick test function for now - improve eventually
#if 1
    snprintf(TestUartStrBuf, TEST_UART_STR_BUF_SIZE, "NumSamples = %ld, ",
             NumSamples);
    TestUARTSendBytes((uint8_t *)TestUartStrBuf, strlen(TestUartStrBuf));

    snprintf(TestUartStrBuf, TEST_UART_STR_BUF_SIZE, "raw ADC samples/sec = %ld\r\n", SamplesPerSec);
    TestUARTSendBytes((uint8_t *)TestUartStrBuf, strlen(TestUartStrBuf));

    for (i = 0; i < NumSamples; i++)
    {
        snprintf(TestUartStrBuf, TEST_UART_STR_BUF_SIZE, "%6ld, %ld\r\n", i,
                 pSampleBuf[i]);
        TestUARTSendBytes((uint8_t *)TestUartStrBuf, strlen(TestUartStrBuf));
    }
#else
    // george likes it in something matlab eats
    for (i = 0; i < NumSamples; i++)
    {
        snprintf(TestUartStrBuf, TEST_UART_STR_BUF_SIZE, "%6ld %ld\r\n", i,
                 pSampleBuf[i]);
        TestUARTSendBytes((uint8_t *)TestUartStrBuf, strlen(TestUartStrBuf));
    }

#endif
}


// TODO: Move the following error functions to AD7766_DMA.c eventually,
// and make static

/*
 * AD7766_SetError
 *
 * @desc
 *          Only records the FIRST error after AD7766_ClearError() was called,
 *          because this gives the most valuable error information.
 *
 * @param
 *
 * @returns
 */
void AD7766_SetError(AD7766ErrorEnum AD7766Error)
{
    if (g_AD7766Error == AD7766ERROR_NONE)
    {
        g_AD7766Error = AD7766Error;
        g_AD7766ErrorBlockNum = g_AD7766CurrentBlockNum;
    }
}

/*
 * AD7766_ErrorIsSet
 *
 * @desc
 *
 * @param
 *
 * @returns
 */
bool AD7766_ErrorIsSet(void)
{
    if (g_AD7766Error == AD7766ERROR_NONE)
    {
        return false;
    }
    return true;
}

/*
 * AD7766_ClearError
 *
 * @desc
 *
 * @param
 *
 * @returns
 */
void AD7766_ClearError(void)
{
    g_AD7766Error = AD7766ERROR_NONE;
}

/*
 * AD7766_GetFirstError
 *
 * @desc
 *
 * @param
 *
 * @returns
 */
AD7766ErrorEnum AD7766_GetFirstError(uint16_t *pBlockNumRet)
{
    *pBlockNumRet = g_AD7766ErrorBlockNum;
    return g_AD7766Error;
}


//******************************************************************************
//******************************************************************************
// Test functions follow
//******************************************************************************
//******************************************************************************

/*
 * AD7766_TestRawToSignedConversion
 *
 * @desc    Tests the AD7766_ConvertRawToSigned() function, using an array of
 *          test input values and expected results.
 *
 * @param   -
 *
 * @returns true if testing passed, false if failed.
 */
bool AD7766_TestRawToSignedConversion(void)
{
    int32_t SignedVal;
    bool bResultsAreOK;
    uint8_t i;
    // Define expected conversion values - see AD7766 datasheet section
    // "AD7766/AD7766-1/AD7766-2 TRANSFER FUNCTION"
    static const struct   // Static const so that doesn't use stack
    {
        uint32_t RawSampleVal;
        int32_t SignedValExpected;
    } TestValues[] =
    {
        // Positive & zero values
        {0x7FFFFF,  0x7FFFFF},   // Largest 24-bit signed positive number
        {0x555555,  0x555555},   // Alternating 0/1 bits, starting with 1
        {0x3AAAAA,  0x3AAAAA},   // Alternating 0/1 bits, starting with 0
        {0x000001,       0x1},
        {0x000000,       0x0},
        // Negative values - expected results were calculated as
        // (0x1000000 - raw word)
        {0xFFFFFF,      -0x1},
        {0xD55555, -0x2AAAAB},   // Alternating 0/1 bits, starting with 1
        {0xAAAAAA, -0x555556},   // Alternating 0/1 bits, starting with 0
        {0x800000, -0x800000}    // Largest 24-bit signed negative number
    };

    bResultsAreOK = true;
    for (i = 0; i < (sizeof(TestValues) / sizeof(TestValues[0])); i++)
    {
        SignedVal = AD7766_ConvertRawToSigned(TestValues[i].RawSampleVal);
        if (SignedVal != TestValues[i].SignedValExpected)
        {
            bResultsAreOK = false;
        }
    }

    return bResultsAreOK;
}

/*
 * AD7766_TestIOLines
 *
 * @desc    Configures and toggles individual AD7766 and other digital lines
 *          so can single-step and check that they toggle electrically at
 *          AD7766 package or other circuit points.
 *
 *          ******* IMPORTANT: MUST BE CALLED FROM A FREERTOS TASK, because
 *          ******* the powerAnalogOn() function calls vTaskDelay()
 *
 * @param
 *
 * @returns -
 */
void AD7766_TestIOLines(void)
{
    powerAnalogOn();

    if (Device_GetHardwareVersion() == HW_PASSRAIL_REV1 && !Device_HasPMIC())
    {
        // ADC-DVDD-ON
        GPIO_DRV_ClearPinOutput(ADC_DVDD_ON);
        // NOTE: DON'T LEAVE THIS POWER OFF FOR LONG, because unpowered ADC will
        // be being driven by other I/O lines so will get bad parasitic currents
        GPIO_DRV_SetPinOutput(ADC_DVDD_ON);
    }

    // ADC-PWRDN#: AD7766 pin 7
    GPIO_DRV_ClearPinOutput(Device_ADC_PowerdownPin());
    GPIO_DRV_SetPinOutput(Device_ADC_PowerdownPin());

    // ADC-CS#: AD7766 pin 16: Finish with it high so disables AD7766 driving
    // SPI1-MISO line
    GPIO_DRV_ClearPinOutput(ADC_CSn);
    GPIO_DRV_SetPinOutput(ADC_CSn);

    // SPI-MOSI: AD7766 pin 15
    pinConfigDigitalOut(SPI1_MOSI, kPortMuxAsGpio, 0, false);
    GPIO_DRV_SetPinOutput(SPI1_MOSI);

    // SPI-MISO: AD7766 pin 10: ADC-CS# is high so AD7766 will not drive
    // SPI1-MISO line, so can temporarily test as output from Kinetis
    pinConfigDigitalOut(SPI1_MISO, kPortMuxAsGpio, 0, false);
    GPIO_DRV_SetPinOutput(SPI1_MISO);

    // SPI1-SCK: AD7766 pin 13
    pinConfigDigitalOut(SPI1_SCK, kPortMuxAsGpio, 0, false);
    GPIO_DRV_SetPinOutput(SPI1_SCK);

    // ADC-MCLK: AD7766 pin 14
    pinConfigDigitalOut(ADC_MCLK, kPortMuxAsGpio, 0, false);
    GPIO_DRV_SetPinOutput(ADC_MCLK);

    // Kinetis pins aren't configured correctly any more, so switch off subsystem
    powerAnalogOff();
}






#ifdef __cplusplus
}
#endif