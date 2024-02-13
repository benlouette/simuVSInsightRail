#ifdef __cplusplus
extern "C" {
#endif

/*
 * PassRailDSP_MVP.c
 *
 *  Created on: 30 May 2016
 *      Author: Bart Willemse
 * Description: Passenger-rail-specific DSP functionality.
 *              ********** INITIAL MINIMUM VIABLE PRODUCT VERSION **************
 *
 */

#include <stdint.h>
#include <stdbool.h>
#include "Resources.h"
#include "arm_math.h"
#include "PassRailDSP_MVP.h"
#include "AdcApiDefs.h"
#include "PinConfig.h"

//******************************************************************************
#ifndef PASSRAIL_DSP_NEW  // N.B. NOT PASSRAIL_DSP_NEW
//******************************************************************************

//..............................................................................

static EnveloperEnum g_DspEnveloper = ENV_NONE;
static DecimChainEnum g_DspDecimation = DECIMCHAIN_NONE;

//..............................................................................
// Enveloping filters for MVP
// Enveloping filter coefficients (from Colin's e-mail of 2/8/2016, and
// Bart's "Envelope smoother filter calculator" spreadsheet implementing the
// calculations).
// Important note: The first coefficient of each pair needed to be changed to a
// positive value, to make the filtering non-inverting when used with
// DspEnvSmootherFilt().
// Vibration: for 40960sps, 5kHz cutoff
int32_t VibEnvFiltCoeffs[2] = {595267455, -956948738};
// Wheel-flats: for 10240sps, 200Hz cutoff
int32_t WflatEnvFiltCoeffs[2] = {124150186, -1899183275};
static int64_t EnvFilterState = 0; // 64-bit to maintain precision

//..............................................................................
// Decimation filters for MVP
// Quick design for MVP for now - but should work well. The filter coefficients
// were calculated using the TFilter on-line tool at
// http://t-filter.engineerjs.com/.
//
// TODO: Temporary for MVP only, because the raw ADC 40960sps sampling rate for
// vibration will need to change eventually (probably to 51200sps). This is
// because it has a ~1.5% frequency error - to generate the required 16 x MCLK's
// per sample, this equates to 655360Hz, which doesn't divide exactly from our
// Kinetis 24.576MHz core clock. But I didn't want to do this for MVP yet
// because it would require /10 decimation (instead of /8) and this is a
// non-power-of-2 decimation ratio, which is a bit more fiddly to do.
//
// ------------------------------------------
// Vibration decimation filter chain for MVP
// ------------------------------------------
//
// Requires 2560sps output sample rate (fmax of 1000Hz).
//
//                |---------------|            |---------------|
//     40960sps   |               |  5120sps   |               |   2560sps
//    ----------->| /8 decimation |----------->| /2 decimation |------------>
//                |               |            |               |
//                |---------------|            |---------------|
//
// /8 decimation filter (69 taps):
//    - 40960sps input, 5120sps output
//    - Passband: 0 - 1000Hz, target 0.2dB ripple (actual 0.14dB)
//    - Stopband: 3120 - 20480Hz, target -80dB attenuation (actual 80.4dB)
//    - NOTE: The 3120Hz stopband is ABOVE the downsampled Nyquist frequency of
//      2560sps, which will result in aliasing "folding back" below the Nyquist
//      frequency down to 2000Hz. However, this is deliberate because it is a
//      standard means of reducing # of taps / CPU requirements in this first
//      stage (I think). The aliasing is then removed by the second stage
static q31_t VibDecimFilt1Coeffs[] =
{
         528980,       959535,     1706311,     2689021,     3858065,
        5102370,      6243631,     7037845,     7189952,     6383122,
        4316700,       758670,    -4400154,   -11090007,   -19017568,
      -27636446,    -36142727,   -43499721,   -48494767,   -49827381,
      -46222193,    -36559049,   -20006413,     3854715,    34932598,
       72564695,    115506740,   161976143,   209749922,   256311365,
      299033523,    335383467,   363127598,   380518231,   386442450,
      380518231,    363127598,   335383467,   299033523,   256311365,
      209749922,    161976143,   115506740,    72564695,    34932598,
        3854715,    -20006413,   -36559049,   -46222193,   -49827381,
      -48494767,    -43499721,   -36142727,   -27636446,   -19017568,
      -11090007,     -4400154,      758670,     4316700,     6383122,
        7189952,      7037845,     6243631,     5102370,     3858065,
        2689021,      1706311,      959535,      528980
};
#define VIBDECIMFILT1_NUMCOEFFS     (sizeof(VibDecimFilt1Coeffs) / sizeof(VibDecimFilt1Coeffs[0]))
#define VIBDECIMFILT1_FACTOR        (8)
#define VIBDECIMFILT1_BLOCKSIZE     (ADC_SAMPLES_PER_BLOCK)

// /2 decimation filter (83 taps): (NOTE: ALSO USED FOR WHEEL-FLATS BELOW)
//    - 5120sps input, 2560sps output
//    - Passband: 0 - 1000Hz, target 0.2dB ripple (actual 0.06dB)
//    - Stopband: 1280 - 2560Hz, target -100dB attenuation (actual -108.5dB)
static q31_t VibDecimFilt2Coeffs[] =
{
         133803,       294935,     -236118,    -2088723,    -4044598,
       -3219258,       999649,     4038216,      949703,    -5085555,
       -4523843,      4295228,     8830664,     -694318,   -12414444,
       -6110170,     13195979,    15362026,    -9068869,   -24967416,
       -1399024,     31597039,    18186053,   -31220641,   -39216779,
       19998665,     60146844,     4698563,   -74538705,   -43310235,
       74424220,     93531537,   -50703760,  -150318846,    -7802737,
      206569979,    121063080,  -254346412,  -356155035,   286412006,
     1332507118,   1849777122,  1332507118,   286412006,  -356155035,
     -254346412,    121063080,   206569979,    -7802737,  -150318846,
      -50703760,     93531537,    74424220,   -43310235,   -74538705,
        4698563,     60146844,    19998665,   -39216779,   -31220641,
       18186053,     31597039,    -1399024,   -24967416,    -9068869,
       15362026,     13195979,    -6110170,   -12414444,     -694318,
        8830664,      4295228,    -4523843,    -5085555,      949703,
        4038216,       999649,    -3219258,    -4044598,    -2088723,
        -236118,       294935,      133803
};
#define VIBDECIMFILT2_NUMCOEFFS     (sizeof(VibDecimFilt2Coeffs) / sizeof(VibDecimFilt2Coeffs[0]))
#define VIBDECIMFILT2_FACTOR        (2)
#define VIBDECIMFILT2_BLOCKSIZE     (VIBDECIMFILT1_BLOCKSIZE / VIBDECIMFILT1_FACTOR)

// -------------------------------------------
// Wheel-flats decimation filter chain for MVP
// -------------------------------------------
//
// Requires 1280sps output sample rate (fmax of 500Hz).
//
//                |---------------|            |---------------|
//     10240sps   |               |  2560sps   |               |   1280sps
//    ----------->| /4 decimation |----------->| /2 decimation |------------>
//                |               |            |               |
//                |---------------|            |---------------|
//
// /4 decimation filter (75 taps):
//    - 10240sps input, 2560sps output
//    - Passband: 0 - 500Hz, target 0.2dB ripple (actual 0.11dB)
//    - Stopband: 1000 - 5120Hz, target -80dB attenuation (actual -82.2dB)
static q31_t WflatDecimFilt1Coeffs[] =
{
         453414,       831013,     1367658,     1858046,     2072245,
        1730204,       567846,    -1569528,    -4626463,    -8260024,
      -11814543,    -14376290,   -14921536,   -12545371,    -6736134,
        2361989,     13771684,    25659298,    35512428,    40529194,
       38180516,     26856455,     6470557,   -21119692,   -52003938,
      -80503296,    -99859517,  -103256600,   -85022929,   -41806357,
       26504874,    116317477,   220456681,   328908003,   430092398,
      512483989,    566305778,   585013341,   566305778,   512483989,
      430092398,    328908003,   220456681,   116317477,    26504874,
      -41806357,    -85022929,  -103256600,   -99859517,   -80503296,
      -52003938,    -21119692,     6470557,    26856455,    38180516,
       40529194,     35512428,    25659298,    13771684,     2361989,
       -6736134,    -12545371,   -14921536,   -14376290,   -11814543,
       -8260024,     -4626463,    -1569528,      567846,     1730204,
        2072245,      1858046,     1367658,      831013,      453414
};
#define WFLATDECIMFILT1_NUMCOEFFS   (sizeof(WflatDecimFilt1Coeffs) / sizeof(WflatDecimFilt1Coeffs[0]))
#define WFLATDECIMFILT1_FACTOR      (4)
#define WFLATDECIMFILT1_BLOCKSIZE   (ADC_SAMPLES_PER_BLOCK)

// /2 decimation filter: USES THE /2 *VIBRATION* DECIMATION FILTER, but with
// everything 1/2 of the SPS & frequencies - so results in the following:
//    - 2560sps input, 1280sps output
//    - Passband: 0 - 500Hz, target 0.2dB ripple (actual 0.06dB)
//    - Stopband: 640 - 1280Hz, target -100dB attenuation (actual -108.5dB)
static q31_t *WflatDecimFilt2Coeffs = VibDecimFilt2Coeffs;
#define WFLATDECIMFILT2_NUMCOEFFS   (VIBDECIMFILT2_NUMCOEFFS)
#define WFLATDECIMFILT2_FACTOR      (VIBDECIMFILT2_FACTOR)
#define WFLATDECIMFILT2_BLOCKSIZE   (WFLATDECIMFILT1_BLOCKSIZE / WFLATDECIMFILT1_FACTOR)

// (TODO: Is the above decimation filter sharing useful / advantageous, or
// confusing? N.B. Idea is eventually for vibration and wheel-flats to use
// exactly the same filter chains, but with different ADC input sampling rates).

//..............................................................................

static arm_fir_decimate_instance_q31 VibDecimFilt1;
static arm_fir_decimate_instance_q31 VibDecimFilt2;
static arm_fir_decimate_instance_q31 WflatDecimFilt1;
static arm_fir_decimate_instance_q31 WflatDecimFilt2;

// Define the state buffers - length needs to be (numTaps + blockSize - 1) 
// words - see arm_fir_decimate_init_q31() documentation
static q31_t VibDecimFilt1StateBuf[VIBDECIMFILT1_NUMCOEFFS + VIBDECIMFILT1_BLOCKSIZE - 1];
static q31_t VibDecimFilt2StateBuf[VIBDECIMFILT2_NUMCOEFFS + VIBDECIMFILT2_BLOCKSIZE - 1];
static q31_t WflatDecimFilt1StateBuf[WFLATDECIMFILT1_NUMCOEFFS + WFLATDECIMFILT1_BLOCKSIZE - 1];
static q31_t WflatDecimFilt2StateBuf[WFLATDECIMFILT2_NUMCOEFFS + WFLATDECIMFILT2_BLOCKSIZE - 1];

static int32_t g_EnvOutBuf[ADC_SAMPLES_PER_BLOCK];
static int32_t g_DecimInterstageBuf[ADC_SAMPLES_PER_BLOCK];

//..............................................................................

void DspEnvSmootherFilt(int32_t *pCoeffs, int64_t *pState,
                        int32_t *pSampleBlock, uint32_t BlockSize);

//..............................................................................


/*
 * PassRailDsp_Init
 *
 * @desc    Initialises the passenger-rail-specific DSP chain. Needs to be
 *          called before every sampling burst.
 *
 * @param   Enveloper: Specifies enveloper type required
 * @param   Decimation: Specifies decimation filter chain required
 *
 * @returns true if initialised OK, false otherwise
 */
bool PassRailDsp_Init(EnveloperEnum Enveloper, DecimChainEnum Decimation)
{
    g_DspEnveloper = Enveloper;
    g_DspDecimation = Decimation;
    arm_status DspStatus;
    bool bOK;

    bOK = false;

    // Initialise the envelope smoother filter
    EnvFilterState = 0;

    // Initialise the decimation filters
    if (Decimation != DECIMCHAIN_NONE)
    {
        //......................................................................
        // Vibration
        if (Decimation == DECIMCHAIN_VIB_FACTOR16_MVP)
        {
            DspStatus = arm_fir_decimate_init_q31(&VibDecimFilt1,
                                                  VIBDECIMFILT1_NUMCOEFFS,
                                                  VIBDECIMFILT1_FACTOR,
                                                  VibDecimFilt1Coeffs,
                                                  VibDecimFilt1StateBuf,
                                                  VIBDECIMFILT1_BLOCKSIZE);
            if (DspStatus == ARM_MATH_SUCCESS)
            {
                DspStatus = arm_fir_decimate_init_q31(&VibDecimFilt2,
                                                      VIBDECIMFILT2_NUMCOEFFS,
                                                      VIBDECIMFILT2_FACTOR,
                                                      VibDecimFilt2Coeffs,
                                                      VibDecimFilt2StateBuf,
                                                      VIBDECIMFILT2_BLOCKSIZE);
                if (DspStatus == ARM_MATH_SUCCESS)
                {
                    bOK = true;
                }
            }
        }

        //......................................................................
        // Wheel-flats
        else if (Decimation == DECIMCHAIN_WFLATS_FACTOR8_MVP)
        {
            DspStatus = arm_fir_decimate_init_q31(&WflatDecimFilt1,
                                                  WFLATDECIMFILT1_NUMCOEFFS,
                                                  WFLATDECIMFILT1_FACTOR,
                                                  WflatDecimFilt1Coeffs,
                                                  WflatDecimFilt1StateBuf,
                                                  WFLATDECIMFILT1_BLOCKSIZE);
            if (DspStatus == ARM_MATH_SUCCESS)
            {
                DspStatus = arm_fir_decimate_init_q31(&WflatDecimFilt2,
                                                      WFLATDECIMFILT2_NUMCOEFFS,
                                                      WFLATDECIMFILT2_FACTOR,
                                                      WflatDecimFilt2Coeffs,
                                                      WflatDecimFilt2StateBuf,
                                                      WFLATDECIMFILT2_BLOCKSIZE);
                if (DspStatus == ARM_MATH_SUCCESS)
                {
                    bOK = true;
                }
            }
        }

        //......................................................................
    }
    else
    {
        // DECIM_NONE
        // TODO: Nothing to do here?
        bOK = true;
    }

    return bOK;
}

/*
 * PassRailDsp_ProcessBlock
 *
 * @desc    Performs the passenger-rail-specific DSP processing of a single
 *          block of incoming ADC_SAMPLES_PER_BLOCK samples.
 *          NOTE: The number of output samples can often be less than the
 *          number of input samples, due to decimation etc. However, it will
 *          never be greater.
 *
 * @param   pSampleBlockIn: Sample input buffer of size ADC_SAMPLES_PER_BLOCK
 * @param   pSampleBlockOut: Sample output buffer
 * @param   pNumOutputSamples: RETURNS the number of output samples produced
 *
 * @returns -
 */
void PassRailDsp_ProcessBlock(int32_t *pSampleBlockIn,
                              int32_t *pSampleBlockOut,
                              uint32_t *pNumOutputSamples)
{
    uint32_t i;

    //..........................................................................
    // Enveloper (rectifier + first-order lowpass smoothing filter)

//#define BYPASS_ENVELOPER
#ifdef BYPASS_ENVELOPER
    g_DspEnveloper = ENV_NONE;
#endif

    if (g_DspEnveloper != ENV_NONE)
    {
        // Rectification
#define DO_RECTIFICATION
#ifdef DO_RECTIFICATION
        // Rectify
        arm_abs_q31(pSampleBlockIn, g_EnvOutBuf, ADC_SAMPLES_PER_BLOCK);
#else
        // Bypass rectifier
        for (i = 0; i < ADC_SAMPLES_PER_BLOCK; i++)
        {
            g_EnvOutBuf[i] = pSampleBlockIn[i];
        }
#endif

        // Smoothing filter
        if (g_DspEnveloper == ENV_VIB)
        {
            DspEnvSmootherFilt(VibEnvFiltCoeffs, &EnvFilterState,
                               g_EnvOutBuf, ADC_SAMPLES_PER_BLOCK);
        }
        else if (g_DspEnveloper == ENV_WFLATS)
        {
            DspEnvSmootherFilt(WflatEnvFiltCoeffs, &EnvFilterState,
                               g_EnvOutBuf, ADC_SAMPLES_PER_BLOCK);
        }
        else
        {
            // TODO: ERROR
        }
    }
    else
    {
        // No enveloper - pass directly through
        for (i = 0; i < ADC_SAMPLES_PER_BLOCK; i++)
        {
            g_EnvOutBuf[i] = pSampleBlockIn[i];
        }
    }

    //..........................................................................
    // Decimation filter chain
    *pNumOutputSamples = 0;

    if (g_DspDecimation == DECIMCHAIN_VIB_FACTOR16_MVP)
    {
        //......................................................................
        // Vibration decimation filter chain

        //*************
        //pinConfigDigitalOut(TEST_IO2, kPortMuxAsGpio, 1, false);

        // Filter 1
        arm_fir_decimate_q31(&VibDecimFilt1, g_EnvOutBuf,
                             g_DecimInterstageBuf, VIBDECIMFILT1_BLOCKSIZE);

        // Filter 2
//#define VIB_BYPASS_FILTER2
#ifdef VIB_BYPASS_FILTER2
        // Bypass filter 2
        for (i = 0; i < VIBDECIMFILT2_BLOCKSIZE; i++)
        {
            pSampleBlockOut[i] = g_DecimInterstageBuf[i];
        }
        *pNumOutputSamples = VIBDECIMFILT2_BLOCKSIZE;
#else
        arm_fir_decimate_q31(&VibDecimFilt2, g_DecimInterstageBuf,
                             pSampleBlockOut, VIBDECIMFILT2_BLOCKSIZE);

        //*************
        //pinConfigDigitalOut(TEST_IO2, kPortMuxAsGpio, 0, false);


        *pNumOutputSamples = VIBDECIMFILT2_BLOCKSIZE / VIBDECIMFILT2_FACTOR;
#endif
    }
    else if (g_DspDecimation == DECIMCHAIN_WFLATS_FACTOR8_MVP)
    {
        //......................................................................
        // Wheel-flats decimation filter chain
        // Filter 1
        arm_fir_decimate_q31(&WflatDecimFilt1, g_EnvOutBuf,
                             g_DecimInterstageBuf, WFLATDECIMFILT1_BLOCKSIZE);
        // Filter 2
//#define WFLATS_BYPASS_FILTER2
#ifdef WFLATS_BYPASS_FILTER2
        // Bypass filter 2
        for (i = 0; i < WFLATDECIMFILT2_BLOCKSIZE; i++)
        {
            pSampleBlockOut[i] = g_DecimInterstageBuf[i];
        }
        *pNumOutputSamples = WFLATDECIMFILT2_BLOCKSIZE;
#else
        arm_fir_decimate_q31(&WflatDecimFilt2, g_DecimInterstageBuf,
                             pSampleBlockOut, WFLATDECIMFILT2_BLOCKSIZE);

        *pNumOutputSamples = WFLATDECIMFILT2_BLOCKSIZE / WFLATDECIMFILT2_FACTOR;
#endif

        //......................................................................
    }
    else if (g_DspDecimation == DECIMCHAIN_NONE)
    {
        // Just pass through
        for (i = 0; i < ADC_SAMPLES_PER_BLOCK; i++)
        {
            pSampleBlockOut[i] = g_EnvOutBuf[i];
        }
        *pNumOutputSamples = ADC_SAMPLES_PER_BLOCK;

        //......................................................................

    }
    else
    {
        // TODO: Error
    }

    //..........................................................................
}

/*
 * DspEnvSmootherFilt
 *
 * @desc    Implements a simple IIR envelope smoother lowpass RC filter
 *          algorithm. Requires 2 x coefficients. Performs in-place in
 *          pSampleBlock.
 *          IMPORTANT: *pState must be persistent across calls, and must be
 *          initialised to 0 upon each new sampling commencement.
 *
 *          TODO: Experimental to begin with. This code was adapted from the
 *          Microlog's Enveloper() function - however, this code only
 *          implements the smoothing filter (not the rectification) - this
 *          is probably less efficient overall, but allows this filter
 *          to be tested separately.
 *
 * @param
 *
 * @returns -
 */
void DspEnvSmootherFilt(int32_t *pCoeffs, int64_t *pState,
                        int32_t *pSampleBlock, uint32_t BlockSize)
{
    uint32_t i;
    int64_t SampleInTimesCoeff0;

    for (i = 0; i < BlockSize; i++)
    {
        SampleInTimesCoeff0 = (int64_t)pSampleBlock[i] * (int64_t)pCoeffs[0];
        pSampleBlock[i] = (int32_t)((SampleInTimesCoeff0 + *pState) / INT32_MAX);
        *pState = SampleInTimesCoeff0 - ((int64_t)pSampleBlock[i] * (int64_t)pCoeffs[1]);
    }
}

//******************************************************************************
#endif // NOT PASSRAIL_DSP_NEW
//******************************************************************************




#ifdef __cplusplus
}
#endif