#ifdef __cplusplus
extern "C" {
#endif

/*
 * xTaskMeasurement.c
 *
 *  Created on: 16 jun. 2014
 *      Author: Bart Willemse (adapted from module by Daniel van der Velde)
 */

#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "CS1.h"
#include <FreeRTOS.h>
#include <portmacro.h>
#include <task.h>
#include <timers.h>
#include <queue.h>
#include "Resources.h"
#include "xTaskDefs.h"
#include "xTaskMeasure.h"
#include "Device.h"
#include "printgdf.h"
#include "Log.h"
#include "AdcApiDefs.h"
#include "AD7766_DMA.h"
#include "PowerControl.h"

#ifdef PASSRAIL_DSP_NEW
#include "PassRailDSP.h"
#else
#include "PassRailDSP_MVP.h"
#endif
#ifdef FCC_TEST_BUILD
#include "FCCTest/FccTest.h"
#endif
//*******************************************
//*******************************************
// TODO: For line-toggling testing only
#include "PinConfig.h"
//*******************************************
//*******************************************

//..............................................................................
// Defines
#define EVENTQUEUE_NR_ELEMENTS_MEASURE      (4)

//..............................................................................
// Types

// Event descriptors
typedef enum
{
    MeasureEvt_Undefined = 0,
    // External
    MeasureEvt_StartRequest,
    // Internal
    MeasureEvt_RealTimeProcessData,
    MeasureEvt_AdcBlockTimeout,
} tMeasureEventDescriptor;

// Event structure for Measure task
typedef struct
{
    // Event Descriptor
    tMeasureEventDescriptor Descriptor;
    union
    {
        // Process samples event data
        tAdcBlockData AdcBlockData;

    } Data;
} tMeasureEvent;

//..............................................................................
// Data

TaskHandle_t _TaskHandle_Measure;   // Not static because referenced externally

static QueueHandle_t _EventQueue_Measure;
static bool QueueErrorFlag = false;

static TimerHandle_t TimerHandle_AdcBlockTimeout;
static long AdcBlockTimerID = 0;

static struct
{
    bool bRawAdcSampling;
    tMeasId MeasId;
    uint32_t NumOutputSamples;
    uint32_t AdcSamplesPerSecIfRawAdc;
    tMeasureCallback pMeasureCallback;
} g_MeasurementRequest;

static bool g_bMeasureSamplingIsInProgress = false;

// Minimum number of samples required for DSP settling (N.B. DSP settling
// samples are internally handled as ADC_SAMPLES_PER_BLOCK samples at a time)
static uint32_t g_DspSettlingNumAdcSamples = 0;

static uint32_t g_OutputSampleCount = 0;

static int32_t g_DspOutSampleBuf[ADC_SAMPLES_PER_BLOCK];

static volatile MeasureErrorEnum g_MeasureError = MEASUREERROR_NONE;
static volatile uint16_t g_MeasureErrorBlockNum = 0;

//..............................................................................

static void HandleMeasureStart(void);
static void HandleMeasureAdcRealTimeBlock(uint32_t *pAdcBlock);
static void HandleMeasureAdcBlockTimeout(void);

static void Measure_CallbackCall(void);
static bool Measure_ConvertAdcBlockToSampleBlock(uint32_t *pAdcBlockIn,
                                                 int32_t *pSampleBlockOut);
static bool Measure_DoRealTimeDSPToOutputBuf(int32_t *pSampleBlock);
static void Measure_AdcISRCallback(tAdcBlockData AdcBlockData);
static void AdcBlockTimeoutCallback(TimerHandle_t pxTimer);
static void Measure_SetError(MeasureErrorEnum MeasureError);
static void Measure_ClearError(void);

//..............................................................................

/*
 * xTaskMeasure_Init
 *
 * @desc
 *          IMPORTANT: AD7766_Init() must be called separately before this
 *          measurement task initialisation - see its function comments for
 *          more details.
 *
 *
 * @param   None
 *
 * @returns -
 */
void xTaskMeasure_Init(void)
{
    // Set up task queue
    _EventQueue_Measure = xQueueCreate(EVENTQUEUE_NR_ELEMENTS_MEASURE,
                                       sizeof(tMeasureEvent));
    vQueueAddToRegistry(_EventQueue_Measure, "_EVTQ_MEASURE");

    // Create task
    xTaskCreate(xTaskMeasure,
                "MEASURE",
                STACKSIZE_XTASK_MEASURE,
                NULL,
                PRIORITY_XTASK_MEASURE,
                &_TaskHandle_Measure);
}

/*
 * xTaskMeasure
 *
 * @desc    Measure task takes care of all state handling around measurements.
 *
 * @param   pvParameters:
 *
 * @returns -
 */
void xTaskMeasure(void *pvParameters)
{
    tMeasureEvent rxEvent;

    // Install AD7766 callback function
    AD7766_InstallISRCallback(Measure_AdcISRCallback);

    // Create ADC block timeout timer. During sampling, if this task
    // doesn't receive a new ADC sample block within the timeout period,
    // then it triggers an error. 
    TimerHandle_AdcBlockTimeout = xTimerCreate(
                         "MEASUREAdcBlockTimeout",
                         1000 / portTICK_PERIOD_MS,   // Dummy tick unit 1sec, changeable
                         pdFALSE,                     // No reload
                         &AdcBlockTimerID,            // timerID (for callbacks)
                         &AdcBlockTimeoutCallback);   // Callback function

    // Initial state
    //Measurement_SetState(MEASURESTATE_DISABLED);

    // Measure event handler
    for (;;)
    {
        if (xQueueReceive(_EventQueue_Measure, &rxEvent, portMAX_DELAY))
        {
            switch (rxEvent.Descriptor)
            {
            case MeasureEvt_StartRequest:
                HandleMeasureStart();
                break;

            case MeasureEvt_RealTimeProcessData:
#ifndef FCC_TEST_BUILD
                HandleMeasureAdcRealTimeBlock(rxEvent.Data.AdcBlockData.pSampleBlock);
#else
            	FccTest_StoreSamplesToSampleBuf(rxEvent.Data.AdcBlockData.pSampleBlock);
#endif
                break;

            case MeasureEvt_AdcBlockTimeout:
#ifndef FCC_TEST_BUILD
                HandleMeasureAdcBlockTimeout();
#endif
                break;

            default:
                // TODO Report unknown event in debug mode
                //LOG_EVENT( 0, LOG_NUM_APP, ERRLOGDEBUG, "Unhandled event %d", rxEvent.Descriptor);
                break;
            }
        }
    }
}

/*
 * Measure_Start
 *
 * @desc    Requests an immediate start of measurement.
 *          *** IMPORTANT: If bRawADCSampling is true then will NOT control ***
 *          *** the analog power on/off!!!                                   ***
 *          ********************************************************************
 *          ********************************************************************
 *          TODO: SPLIT INTO 2 SEPARATE FUNCTIONS EVENTUALLY (because input
 *          params and functionality is currently a bit confusing):
 *            - Measure_Start(MeasId, NumOutputSamples, pCallback): Using measurement IDs
 *                  N.B. pPreSamplingFunc and pPostSamplingFunc would be passed in (rather than called before & after sampling)
 *                  so that they can be run from the measurement task
 *            - MeasurementRawADC_Start(NumADCSamples, ADCSamplesPerSec): Raw ADC sampling - no measurement ID
 *          ********************************************************************
 *          ********************************************************************
 *
 * @param   bRawADCSampling: false if wanting to use measurement ID, true for
 *          raw ADC sampling WITHOUT power control etc
 * @param   MeasId: Measurement ID of type tMeasId. Only used if bRawADCSampling
 *          is false - specify the required samples per second in ADCSamplesPerSecIfRawADC
 * @param   NumOutputSamples: Number of output samples required
 * @param   ADCSamplesPerSecIfRawADC: Samples per second, for raw ADC sampling only
 * @param   pCallBackFunc: Pointer to sampling-complete callback with
 *          tMeasureCallback signature
 *
 * @returns true if the measurement request is accepted and queued, false for
 *          any error
 */
bool Measure_Start(bool bRawAdcSampling,
                   tMeasId MeasId,
                   uint32_t NumOutputSamples,
                   uint32_t AdcSamplesPerSecIfRawAdc,
                   tMeasureCallback pCallback) // TODO: Callback is hacky for now - improve eventually
{
    bool rval = false;
    tMeasureEvent startMeasureEvent;
    uint32_t i;

#ifdef SAMPLING_EXEC_TIME_EN
    g_nStartSamplingTick = xTaskGetTickCount();
#endif
    //TODO: Add input parameter validation here:
    //  - Check for NumOutput samples != 0 and <= max

    //*********************************************
    //*********************************************
    // ****************** TODO: If already sampling then don't continue, and return false -
    // check the g_bMeasureSamplingIsInProgress flag
    //*********************************************
    //*********************************************

#if 0
    // TODO

    // Check if a measurement definition exists for this measurement ID
    // NOTE: The following is old - if something like it is used, then needs
    // to be updated
    tMeasDef *pMeasDef = Measurement_GetMeasDef(measId);
    if (pMeasDef == NULL)
    {
        return false;
    }

    if (( numValues < pMeasDef->numSamplesMin) || (numValues > pMeasDef->numSamplesMax))
    {
        return false;
    }

#endif // 0

    // Zero the sampling buffer, so can see whether sampling has written fresh
    // data (and avoid being misled by random or previous data if sampling
    // fails for some reason)
    for (i = 0; i < SAMPLE_BUFFER_SIZE_WORDS; i++)
    {
        g_pSampleBuffer[i] = 0;
    }

    Measure_ClearError();

    // Store the sampling request parameters
    g_MeasurementRequest.bRawAdcSampling = bRawAdcSampling;
    g_MeasurementRequest.MeasId = MeasId;
    g_MeasurementRequest.NumOutputSamples = NumOutputSamples;
    g_MeasurementRequest.AdcSamplesPerSecIfRawAdc = AdcSamplesPerSecIfRawAdc;
    g_MeasurementRequest.pMeasureCallback = pCallback;

    // Create measurement start event (only a simple trigger event, doesn't
    // contain any data)
    startMeasureEvent.Descriptor = MeasureEvt_StartRequest;

    // TODO Check xQueueSend timeout (now 500ms)
    rval = xQueueSend(_EventQueue_Measure, &startMeasureEvent, 500/portTICK_PERIOD_MS);
    if (rval == false)
    {
        // TODO
        //LOG_EVENT(  0, LOG_NUM_APP, ERRLOGDEBUG, "xQueueSend failed" );
        return false;
    }

    return true;
}

/*
 * HandleMeasureStart
 *
 * @desc    Handler for the measurement start event. This functions checks the
 *          current state and starts a new measurement for the designated
 *          measurement module. This function is called directly from the
 *          xTaskMeasure main event loop.
 *
 * @param   None
 *
 * @returns -
 */
static void HandleMeasureStart(void)
{
    g_OutputSampleCount = 0;
    uint32_t AdcSamplesPerSec;
    uint32_t BlockTimeoutMillisecs;
    bool bOK;

    // N.B. All required sampling request parameters are in g_MeasurementRequest

    bOK = true;
    g_DspSettlingNumAdcSamples = 0;
    if (!g_MeasurementRequest.bRawAdcSampling)
    {
        // Normal signal chain sampling - perform pre-sampling setup
        // (includes analog hardware power, gain & filter setup), and get
        // required ADC samples/sec and # of DSP settling samples
        bOK = PassRailMeasure_PrepareSampling(g_MeasurementRequest.MeasId,
                                              &AdcSamplesPerSec,
                                              &g_DspSettlingNumAdcSamples);
//#define TESTSIGNALGEORGE
#ifdef TESTSIGNALGEORGE
        // testsignal injection init by george
        {
            void initSimulAmSignal(float sample_freq);
            printf("signal simulation sample_freq = %d\n",AdcSamplesPerSec);
            initSimulAmSignal(AdcSamplesPerSec);
        }
#endif
        if (!bOK)
        {
            Measure_SetError(MEASUREERROR_PRE_SAMPLING);
        }
    }
    else
    {
        // Raw ADC sampling required - no pre-sampling preparation
        AdcSamplesPerSec = g_MeasurementRequest.AdcSamplesPerSecIfRawAdc;
    }

    if (bOK)
    {
        // Start the ADC sampling
        if (AD7766_StartSampling(AdcSamplesPerSec))
        {
            // Set up an ADC block timeout to detect if sampling stalls, e.g. if
            // AD7766 hardware problem (N.B. Can simulate using
            // AD7766_PrepareMclkStoppageTest() or the corresponding CLI command).
            // Create a relaxed duration by multiplying the maximum calculated
            // duration by 2 and adding a further 100ms.
            BlockTimeoutMillisecs = ((AD7766_MaxBlockMillisecs(AdcSamplesPerSec) * 2) + 100);
            xTimerChangePeriod(TimerHandle_AdcBlockTimeout,
                               BlockTimeoutMillisecs / portTICK_PERIOD_MS,
                               portMAX_DELAY);

            // Start the ADC block timeout timer
            xTimerStart(TimerHandle_AdcBlockTimeout, portMAX_DELAY);

            g_bMeasureSamplingIsInProgress = true;
        }
        else
        {
            Measure_SetError(MEASUREERROR_STARTSAMPLING);
            bOK = false;
        }
    }

    if (!bOK)
    {
        // Call callback (higher-level task must then call Measure_GetErrorInfo())
        Measure_CallbackCall();
    }
}


/*
 * HandleMeasureAdcRealTimeBlock
 *
 * @desc    Process real-time ADC data.
 *          This function is called to process raw measurement data.
 *
 * @param
 *
 * @returns -
 */
static void HandleMeasureAdcRealTimeBlock(uint32_t *pAdcBlock)
{
    bool bStopSampling;

    if (g_bMeasureSamplingIsInProgress)
    {
        //***************************************
        // TODO: FOR TESTING ONLY
        //GPIO_DRV_SetPinOutput(TEST_IO2);
        //***************************************

#ifdef TESTSIGNALGEORGE
        {
            void calcSimulAmSignal(uint32_t samples, int32_t * out);
            calcSimulAmSignal(ADC_SAMPLES_PER_BLOCK, pAdcBlock);
        }
#else
        // Convert the raw uint32_t Adc words received over SPI into clean sample
        // values - can do in-place in the ping-pong buffers to save RAM - need to
        // typecast buffer to int32_t
        if (!Measure_ConvertAdcBlockToSampleBlock(pAdcBlock,
                                                  (int32_t *)pAdcBlock))
        {
            Measure_SetError(MEASUREERROR_ADC_TO_SAMPLE_BLOCK_CONVERT);
            // N.B. Do NOT stop sampling here if error, because want waveform for
            // error diagnostics
        }
#endif
        // Perform real-time DSP on the sample block (typecasting needed again)
        bStopSampling = Measure_DoRealTimeDSPToOutputBuf((int32_t *)pAdcBlock);

        //***************************************
        // TODO: FOR TESTING ONLY
        //GPIO_DRV_ClearPinOutput(TEST_IO2);
        //***************************************

        if (!bStopSampling)
        {
            // Restart the ADC block timeout timer
            xTimerReset(TimerHandle_AdcBlockTimeout, portMAX_DELAY);
        }
        else
        {
            if (!AD7766_FinishSampling())
            {
                Measure_SetError(MEASUREERROR_FINISHSAMPLING);
            }

            g_bMeasureSamplingIsInProgress = false;

            xTimerStop(TimerHandle_AdcBlockTimeout, portMAX_DELAY);

            if (!g_MeasurementRequest.bRawAdcSampling)
            {
                // Normal signal chain sampling

                // Do post-sampling stuff
                PassRailMeasure_PostSampling();
            }

            // Call callback (higher-level task must then call Measure_GetErrorInfo())
            Measure_CallbackCall();
        }
    }
}

/*
 * HandleMeasureAdcBlockTimeout
 *
 * @desc    Called when an ADC sample block isn't received within a timeout
 *          duration. Aborts sampling and calls the callback with the
 *          result codes.
 *
 * @param
 *
 * @returns -
 */
static void HandleMeasureAdcBlockTimeout(void)
{
    if (g_bMeasureSamplingIsInProgress)
    {
        // Stop/reset sampling - N.B. don't need to check return code here,
        // because any problems will be captured by AD7766 error checking
        AD7766_FinishSampling();

        g_bMeasureSamplingIsInProgress = false;

        // N.B. ADC block timeout timer has already fired, so don't need to
        // explicitly stop it here

        Measure_SetError(MEASUREERROR_BLOCKTIMEOUT);

        // Call callback (higher-level task must then call Measure_GetErrorInfo())
        Measure_CallbackCall();
    }
}

/*
 * Measure_CallbackCall
 *
 * @desc
 *
 * @param   -
 *
 * @returns -
 */
static void Measure_CallbackCall(void)
{
    if (g_MeasurementRequest.pMeasureCallback != NULL)
    {
        g_MeasurementRequest.pMeasureCallback();
    }
}

/*
 * Measure_GetErrorInfo
 *
 * @desc    Indicates whether sampling was successful, and provides detailed
 *          error information if not. After Measure_Start() has been called
 *          and the measurement callback has been called to indicate sampling
 *          completion, call this function to check whether there were any
 *          sampling errors.
 *
 * @param   pMeasureErrorInfo: Pointer to a MeasureErrorInfoType structure to
 *          return the error information in.
 *
 * @returns false if no errors (sampling was successful), or true if errors
 */
bool Measure_GetErrorInfo(MeasureErrorInfoType *pMeasureErrorInfo)
{
    AD7766ErrorEnum AD7766Error;
    uint16_t ErrorBlockNum = 0;

    // Get AD7766 driver error info
    AD7766Error = AD7766_GetFirstError(&ErrorBlockNum);

    // If AD7766 error then get the error block number from the AD7766
    // driver, otherwise if there's a measurement error then get the block
    // number from this measurement layer
    if ((AD7766Error == AD7766ERROR_NONE) &&
        (g_MeasureError != MEASUREERROR_NONE))
    {
        ErrorBlockNum = g_MeasureErrorBlockNum;
    }

    pMeasureErrorInfo->MeasureError = g_MeasureError;
    pMeasureErrorInfo->AD7766Error = AD7766Error;
    pMeasureErrorInfo->ErrorBlockNum = ErrorBlockNum;

    if ((g_MeasureError != MEASUREERROR_NONE) ||
        (AD7766Error != AD7766ERROR_NONE))
    {
        return true;
    }
    return false;
}



/*
 * Measure_ConvertAdcBlockToSampleBlock
 *
 * @desc    Converts a block of raw ADC SPI input words into signed sample
 *          values, and checks that the AD7766 SPI echo bytes in the block are
 *          all OK. Input and output blocks can be the same buffer if required,
 *          but would need typecasting.
 *
 * @param   pAdcBlockIn: Block of unsigned uint32_t raw ADC input words
 *          pSampleBlockOut: Block of signed int32_t clean sample value outputs
 *
 * @returns true if all OK, false if problem
 */
static bool Measure_ConvertAdcBlockToSampleBlock(uint32_t *pAdcBlockIn,
                                                 int32_t *pSampleBlockOut)
{
    uint32_t i;
    bool bOK = true;

#if 1
    for (i = 0; i < ADC_SAMPLES_PER_BLOCK; i++)
    {
        if (!AD7766_PreProcessRawSpiIntoSampleVal(pAdcBlockIn[i],
                                                  &(pSampleBlockOut[i])))
        {
            bOK = false;
            // N.B. Do NOT break here - still want waveform for error diagnosis
        }
    }
#else

    // TODO: george :when sample rates become higher, then consider this for speed
    // loop unrolling the adc format conversion
    uint32_t NumSamples = ADC_SAMPLES_PER_BLOCK;
    i=0;
    while (NumSamples >= 4 ) {
        // just an experiment by george
        union u16i32 { uint16_t u16[2];
              int32_t i32;
        } tmp;
        tmp.i32 = (int32_t) pAdcBlockIn[i];
        pSampleBlockOut[i++] = ((int32_t)  (tmp.i32<<16) | (int32_t) (tmp.u16[1] /* & 0xff00 */))>>8  ;
        tmp.i32 = (int32_t) pAdcBlockIn[i];
        pSampleBlockOut[i++] = ((int32_t)  (tmp.i32<<16) | (int32_t) (tmp.u16[1] /* & 0xff00 */))>>8  ;
        tmp.i32 = (int32_t) pAdcBlockIn[i];
        pSampleBlockOut[i++] = ((int32_t)  (tmp.i32<<16) | (int32_t) (tmp.u16[1] /* & 0xff00 */))>>8  ;
        tmp.i32 = (int32_t) pAdcBlockIn[i];
        pSampleBlockOut[i++] = ((int32_t)  (tmp.i32<<16) | (int32_t) (tmp.u16[1] /* & 0xff00 */))>>8  ;
        NumSamples -=4 ;
    }
    while (NumSamples) {
         // just an experiment by george
         union u16i32 { uint16_t u16[2];
               int32_t i32;
         } tmp;
         tmp.i32 = (int32_t) pAdcBlockIn[i];
         pSampleBlockOut[i++] = ((int32_t)  (tmp.i32<<16) | (int32_t) (tmp.u16[1] /* & 0xff00 */))>>8  ;
         NumSamples -= 1;
    }

#endif

    return bOK;
}

/*
 * Measure_DoRealTimeDSPToOutputBuf
 *
 * @desc    Performs the required real-time DSP on pSampleBlock of size
 *          ADC_SAMPLES_PER_BLOCK.
 *
 * @param   pSampleBlock: Block of samples to process
 *
 * @returns true if ADC_SAMPLES_PER_BLOCK samples have been successfully
 *          processed, false if overruns the sample output buffer
 */
static bool Measure_DoRealTimeDSPToOutputBuf(int32_t *pSampleBlock)
{
    uint32_t i;
    bool bRequestedOutputSamplesDone = false;
    uint32_t NumOutputSamples = 0;

    // Process input samples into DSP sample buffer
    if (!g_MeasurementRequest.bRawAdcSampling)
    {
        // Normal measurement-ID-based sampling, so do DSP
        PassRailDsp_ProcessBlock(pSampleBlock, g_DspOutSampleBuf,
                                 &NumOutputSamples);
    }
    else
    {
        // Raw AD7766 sampling - do NOT do DSP
        for (i = 0; i < ADC_SAMPLES_PER_BLOCK; i++)
        {
            g_DspOutSampleBuf[i] = pSampleBlock[i];
        }
        NumOutputSamples = ADC_SAMPLES_PER_BLOCK;
    }

    // If in DSP settling time then ignore sampling block, otherwise write
    // it to output buffer
    if (g_DspSettlingNumAdcSamples > 0)
    {
        if (g_DspSettlingNumAdcSamples >= ADC_SAMPLES_PER_BLOCK)
        {
            g_DspSettlingNumAdcSamples -= ADC_SAMPLES_PER_BLOCK;
        }
        else
        {
            // Completed required number of DSP settling samples
            g_DspSettlingNumAdcSamples = 0;
        }
    }
    else
    {
        // Transfer DSP sample buffer into output sample buffer
        for (i = 0; i < NumOutputSamples; i++)
        {
            if (g_OutputSampleCount < SAMPLE_BUFFER_SIZE_WORDS)  // Buffer overrun protection
            {
                g_pSampleBuffer[g_OutputSampleCount] = g_DspOutSampleBuf[i];

                g_OutputSampleCount++;
                if (g_OutputSampleCount >= g_MeasurementRequest.NumOutputSamples)
	                {
                    bRequestedOutputSamplesDone = true;
#ifdef SAMPLING_EXEC_TIME_EN
                    g_nStopSamplingTick = xTaskGetTickCount();
#endif
                    break;
                }
            }
            else
            {
                // ERROR: Sample buffer overrun
                // ******************** TODO: Indicate this error
                // NOTE: The following lines might not yet be correct - just
                // dumped here for now
                // bRequestedOutputSamplesDone = true;
                // g_SamplingResultCode = SAMPLINGRESULT_RAW_SAMPLE_BUFFER_OVERRUN;
                break;
            }
        }
    }

    return bRequestedOutputSamplesDone;
}

/*
 * Measure_AdcISRCallback
 *
 * @desc    Callback function for finished block transfers to the raw sample
 *          buffer.
 *
 * @param
 *
 * @returns -
 */
static void Measure_AdcISRCallback(tAdcBlockData AdcBlockData)
{
    tMeasureEvent event;
    bool rval;

    event.Descriptor = MeasureEvt_Undefined;
    switch (AdcBlockData.AdcBlockResult)
    {
    case ADCBLOCKRESULT_DATAREADY:
        // Create event
        event.Descriptor = MeasureEvt_RealTimeProcessData;
        event.Data.AdcBlockData = AdcBlockData;
        break;

    default:
        event.Descriptor = MeasureEvt_Undefined;
        break;
    }

    // Check if ADC can be stopped (prevent unwanted process event)
    if (event.Descriptor != MeasureEvt_Undefined)
    {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        rval = xQueueSendFromISR(_EventQueue_Measure, &event, &xHigherPriorityTaskWoken);
        // IMPORTANT: The following task yield code is required so that the
        // measurement task is triggered and HandleMeasureAdcRealTimeBlock() is
        // called IMMEDIATELY after the event is sent, rather than waiting for
        // the next 1ms FreeRTOS tick.
        if (xHigherPriorityTaskWoken == pdTRUE)
        {
            vPortYieldFromISR();
        }
        if (rval == false)
        {
            // TODO
            QueueErrorFlag = true;
        }
    }
}

/*
 * AdcBlockTimeoutCallback
 *
 * @desc    Called if the ADC block timeout timer fires.
 *
 * @param   pxTimer: Handle of the timer that expired
 *
 * @returns -
 */
static void AdcBlockTimeoutCallback(TimerHandle_t pxTimer)
{
    tMeasureEvent event;
    bool rval;

    // Check timer
    if (pxTimer == TimerHandle_AdcBlockTimeout)
    {
        // Send ADC block timeout event
        event.Descriptor = MeasureEvt_AdcBlockTimeout;
        rval = xQueueSend(_EventQueue_Measure, &event, 0);
        if (rval == false)
        {
            // LOG_EVENT( 0, LOG_NUM_APP, ERRLOGDEBUG,  "xQueueSend failed" );
        }
    }
}

/*
 * Measure_SetError
 *
 * @desc
 *          Only records the FIRST error after Measure_ClearError() was called,
 *          because this gives the most valuable error information.
 *
 * @param
 *
 * @returns
 */
static void Measure_SetError(MeasureErrorEnum MeasureError)
{
    if (g_MeasureError == MEASUREERROR_NONE)
    {
        g_MeasureError = MeasureError;
        g_MeasureErrorBlockNum = AD7766_GetCurrentBlockNum();
    }
}

/*
 * Measure_ClearError
 *
 * @desc
 *
 * @param
 *
 * @returns
 */
static void Measure_ClearError(void)
{
    g_MeasureError = MEASUREERROR_NONE;
    g_MeasureErrorBlockNum = 0;
}




#ifdef __cplusplus
}
#endif