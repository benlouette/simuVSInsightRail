#ifdef __cplusplus
extern "C" {
#endif

/*
 * PassRailDSP_NEW.c
 *
 *  Created on: 30 May 2016 (MVP version), then substantially updated from
 *              22 Sept 2016
 *      Author: Bart Willemse
 * Description: Passenger-rail-specific DSP functionality.
 *
 * *****************************************************************************
 * NOTE: See the PassRailDsp_DeveloperTesting.docx document for full DSP
 * testing and characterisation results.
 * *****************************************************************************
 *
 * -----------------------------------------------------------------------------
 * DSP signal chain design notes
 * -----------------------------------------------------------------------------
 *
 * Overview:
 *     - Kinetis MCU system clock frequency is currently 24.576MHz
 *
 *     - ADC is AD7766-1, 16x oversampling ADC
 *
 *     - Vibration output sample rate requirements: 1280sps, 2560sps, 5120sps
 *
 *     - Wheel-flat output sample rate requirements: 256sps, 512sps, 1280sps
 *
 *     - This DSP implementation uses the CMSIS-DSP library functions
 *
 *     - The ADC block size into the DSP chain is currently 128 samples - see
 *       ADC_SAMPLES_PER_BLOCK in the AD7766 driver firmware module
 *
 * Overall signal chain approach:
 *
 * |----------|   |----------|   |------|   |-----------|   |----------|   |----------|
 * | Hardware |   | AD7766-1 |   |      |   | Smoothing |   | Decim    |   | Decim    |
 * | filters  |-->| 16x over-|-->| Rect |-->|  filter   |-->| filter 1 |-->| filter 2 |-->
 * |          |   | sampling |   |      |   |           |   |          |   |          |
 * |----------|   |----------|   |------|   |-----------|   |----------|   |----------|
 *
 *                                <----- Enveloper ----->   <------ Decimation ------->
 *
 *
 * AD7766-1 sampling rates selection:
 *     - AD7766-1 16x oversampling ADC requires an MCLK of 16x the required
 *       ADC sampling rate
 *
 *     - For 2-stage decimation principles, the lowest practical overall
 *       decimation factor is probably /8 (/4 followed by a /2)
 *
 *     - For vibration sampling, an ADC sampling rate of 51200sps was chosen -
 *       this is the lowest possible ADC sampling rate which satisfies the
 *       following criteria, particularly for the worst-case 5120sps requirement:
 *           - The required x16 MCLK frequency can be exactly created from the
 *             24.576MHz Kinetis clock: 24576000 / 51200 / 16 = a divisor of 30
 *           - 51200sps ADC rate and 5120sps vibration output sampling rate
 *             gives an overall decimation factor of /10, which is above the
 *             practical minimum of /8, which is good
 *
 *     - For wheel-flat sampling, an ADC sampling rate of 10240sps was chosen
 *
 * ADC value scaling design:
 *     - The AD7766 ADC generates 24-bit output sample words (+/-23 bits)
 *     - For the DSP processing, these 24-bit values are simply right-justified
 *       inside the int32_t sample processing words, for the following reasons:
 *         - The arm_fir_decimate_q31() function requires that "In order to
 *           avoid overflows completely the input signal must be scaled down
 *           by log2(numTaps) bits" (see CMSIS-DSP reference documentation).
 *           The biggest number of taps is about 130, which is about 2^7,
 *           meaning that the top 7 bits of the int32_t ADC samples must
 *           not be used. So the 24 bits need to be right-justified
 *         - Developer testing of the DSP chain using a tiny sine wave (see
 *           SinePlusMinus50_GetAdcBlock128() function and
 *           INJECT_SINE_PLUSMINUS50 macro) showed that the LSB bits
 *           of the ADC values are fully preserved through the DSP chain,
 *           without having to left-shift the ADC values before injection
 *           into the chain
 *         - The lower ~7 bits (and possibly more) of the 24-bit words are
 *           mainly noise anyway - see the AD7766-1 histogram in the ADC's
 *           datasheet. Also, the sensor crystal & electronics generate
 *           significant noise
 *
 */

#include <stdint.h>
#include <stdbool.h>
#include "Resources.h"
#include "arm_math.h"
#include "PassRailDSP.h"
#include "AdcApiDefs.h"
#include "PinConfig.h"


//******************************************************************************
#ifdef PASSRAIL_DSP_NEW
//******************************************************************************

//..............................................................................

#define ARRAY_NUM_ELEMENTS(ArrayName)  (sizeof(ArrayName) / sizeof(ArrayName[0]))

//..............................................................................

static EnveloperEnum g_DspEnveloperID = ENV_NONE;

//..............................................................................
// Enveloping filter
// Enveloping filter coefficients (from Colin's e-mail of 2/8/2016, and
// Bart's "Envelope smoother filter calculator" spreadsheet implementing the
// calculations).
// Important note: The first coefficient of each pair needed to be changed to a
// positive value, to make the filtering non-inverting when used with
// DspEnvSmootherFilt().

// Vibration: for 51200sps, 5kHz cutoff
int32_t VibEnvFiltCoeffs[2] = {504164117, -1139155413};

// Wheel-flats: for 10240sps, 200Hz cutoff
int32_t WflatEnvFiltCoeffs[2] = {124150186, -1899183275};

static int64_t g_EnvFilterState = 0; // 64-bit to maintain precision

static int32_t g_EnvOutBuf[ADC_SAMPLES_PER_BLOCK];

//..............................................................................
// Decimation filter chains
//
// --------------------------
// Vibration decimation chain
// --------------------------
// The vibration decimation chain is as follows. The values above the lines
// show the sampling rates at the various stages:
//
// From
// enveloper
//  |
//  | 51200 |----------|  51200  |=============|  5120   |=============|  1280
//  ------->| Block    |-------->| Decim VibA1 |-------->| Decim VibA2 |------->
//     128  | resizer  | 160 |   |    /10      |   16    |     /4      |   4
//          |----------|     |   |=============|         |=============|
//                           |
//                           |
//                           |   |=============|  5120   |=============|  2560
//                           |-->| Decim VibB1 |-------->| Decim VibB2 |------->
//                           |   |    /10      |   16    |      /2     |   8
//                           |   |=============|         |=============|
//                           |
//                           |
//                           |   |=============|  10240  |=============|  5120
//                           --->| Decim VibC1 |-------->| Decim VibC2 |------->
//                               |     /5      |   32    |      /2     |   16
//                               |=============|         |=============|
//
//
// IMPORTANT: Block sizes:
//     - The AD7766 driver firmware module can only generate block sizes which
//       are a power of 2 - currently, the sample block size from the ADC is
//       128 - see ADC_SAMPLES_PER_BLOCK in the AD7766 driver firmware module
//
//     - The CMSIS-DSP decimation functions (e.g. arm_fir_decimate_init_q31() /
//       arm_fir_decimate_q31() etc) require that the decimator input block
//       sizes are a multiple of the decimation factor
//
//     - The first-stage decimators require factors of /10 and /5. However,
//       because ADC_SAMPLES_PER_BLOCK cannot be evenly divided by 10 or 5,
//       then the CMSIS-DSP decimation functions (which require their input
//       block sizes to be a multiple of their decimation ratios) cannot
//       directly accept the ADC block size
//
//     - Therefore, the block resizer buffer is used at the beginning of the
//       chain. This generates block sizes of 160 (DECIMFILT1_BLOCKSIZE),
//       which means that all the following decimation filter block sizes are
//       multiples of their decimation factors
//

// Decimation filter VibA1
//     - 51200sps input, 5120sps output, decimation factor = 10
//     - TFilter settings:
//         - Sampling freq: 51200sps
//         - Passband: 0 - 600Hz, 0.1dB ripple (actual 0.06dB)  - ******** NOTE: 500Hz doesn't work in TFilter
//         - Stopband: 2560 - 25600Hz, -80dB attenuation (actual -81.24dB)
//         - 103 taps
static const q31_t VibDecimFiltA1_Coeffs[] =
{
       332433,      376598,      566749,      797057,     1061870,
      1350347,     1645980,     1926142,     2161868,     2318564,
      2356968,     2234546,     1907528,     1333777,      475586,
      -696795,    -2201454,    -4041593,    -6201540,    -8644292,
    -11308675,   -14108208,   -16930499,   -19638124,   -22070825,
    -24049019,   -25378871,   -25858364,   -25284974,   -23463710,
    -20216170,   -15389448,    -8864958,     -566761,     9531459,
     21400531,    34952085,    50036844,    66445374,    83911096,
    102115865,   120698036,   139262046,   157391413,   174660021,
    190648441,   204955626,   217214786,   227106144,   234367347,
    238803731,   240295905,   238803731,   234367347,   227106144,
    217214786,   204955626,   190648441,   174660021,   157391413,
    139262046,   120698036,   102115865,    83911096,    66445374,
     50036844,    34952085,    21400531,     9531459,     -566761,
     -8864958,   -15389448,   -20216170,   -23463710,   -25284974,
    -25858364,   -25378871,   -24049019,   -22070825,   -19638124,
    -16930499,   -14108208,   -11308675,    -8644292,    -6201540,
     -4041593,    -2201454,     -696795,      475586,     1333777,
      1907528,     2234546,     2356968,     2318564,     2161868,
      1926142,     1645980,     1350347,     1061870,      797057,
       566749,      376598,      332433
};

// Decimation filter VibA2
//     - 5120sps input, 1280sps output, decimation factor = 4
//     - TFilter settings:
//         - Sampling freq: 5120sps
//         - Passband: 0 - 500Hz, 0.1dB ripple (actual 0.07dB)
//         - Stopband: 640 - 2560Hz, -80dB attenuation (actual -80.20dB)
//         - 131 taps
static const q31_t VibDecimFiltA2_Coeffs[] =
{
      -450092,     -543947,     -499415,      -32302,      912956,
      2159807,     3289284,     3762446,     3153612,     1407326,
     -1015473,    -3215091,    -4172701,    -3222436,     -460467,
      3126632,     5951553,     6479096,     3993895,     -882288,
     -6234503,    -9551173,    -8851151,    -3757146,     4027934,
     11189483,    14147843,    10768677,     1650628,    -9716259,
    -18186375,   -19204596,   -11119985,     3557052,    18734171,
     27150119,    23722172,     8337454,   -13350342,   -31799364,
    -37614448,   -26104204,     -239645,    29638659,    49694035,
     48714103,    23886889,   -16560562,   -55569070,   -73975584,
    -59285166,   -12710717,    49035097,    98824031,   109840526,
     68051017,   -18771499,  -119841767,  -189581227,  -183135463,
    -73854051,   133912973,   402080628,   668036841,   863226693,
    934878867,   863226693,   668036841,   402080628,   133912973,
    -73854051,  -183135463,  -189581227,  -119841767,   -18771499,
     68051017,   109840526,    98824031,    49035097,   -12710717,
    -59285166,   -73975584,   -55569070,   -16560562,    23886889,
     48714103,    49694035,    29638659,     -239645,   -26104204,
    -37614448,   -31799364,   -13350342,     8337454,    23722172,
     27150119,    18734171,     3557052,   -11119985,   -19204596,
    -18186375,    -9716259,     1650628,    10768677,    14147843,
     11189483,     4027934,    -3757146,    -8851151,    -9551173,
     -6234503,     -882288,     3993895,     6479096,     5951553,
      3126632,     -460467,    -3222436,    -4172701,    -3215091,
     -1015473,     1407326,     3153612,     3762446,     3289284,
      2159807,      912956,      -32302,     -499415,     -543947,
      -450092
};

// Decimation filter VibB1
//     - 51200sps input, 5120sps output, decimation factor = 10
//     - TFilter settings:
//         - Sampling freq: 51200sps
//         - Passband: 0 - 1000Hz, 0.1dB ripple (actual 0.06dB)
//         - Stopband: 2560 - 25600Hz, -80dB attenuation (actual -81.36dB)
//         - 125 taps
static const q31_t VibDecimFiltB1_Coeffs[] =
{
      -320467,     -348685,     -516279,     -714982,     -938268,
     -1175222,    -1410362,    -1623658,    -1790834,    -1884282,
     -1874394,    -1731316,    -1427090,     -937970,     -247083,
       652808,     1756770,     3045606,     4484267,     6020870,
      7586586,     9096810,    10453709,    11549615,    12271413,
     12506427,    12149462,    11109716,     9317778,     6733922,
      3354967,     -781497,    -5589867,   -10937066,   -16639636,
    -22467433,   -28147426,   -33371295,   -37805121,   -41101206,
    -42911960,   -42905006,   -40778829,   -36278516,   -29210603,
    -19456392,    -6982973,     8148804,    25779867,    45652240,
     67412626,    90620297,   114758858,   139251670,   163480546,
    186806860,   208593953,   228230370,   245152740,   258866869,
    268966866,   275151115,   277233355,   275151115,   268966866,
    258866869,   245152740,   228230370,   208593953,   186806860,
    163480546,   139251670,   114758858,    90620297,    67412626,
     45652240,    25779867,     8148804,    -6982973,   -19456392,
    -29210603,   -36278516,   -40778829,   -42905006,   -42911960,
    -41101206,   -37805121,   -33371295,   -28147426,   -22467433,
    -16639636,   -10937066,    -5589867,     -781497,     3354967,
      6733922,     9317778,    11109716,    12149462,    12506427,
     12271413,    11549615,    10453709,     9096810,     7586586,
      6020870,     4484267,     3045606,     1756770,      652808,
      -247083,     -937970,    -1427090,    -1731316,    -1874394,
     -1884282,    -1790834,    -1623658,    -1410362,    -1175222,
      -938268,     -714982,     -516279,     -348685,     -320467
};

// Decimation filter VibB2
//     - 5120sps input, 2560sps output, decimation factor = 2
//     - TFilter settings:
//         - Sampling freq: 5120sps
//         - Passband: 0 - 1000Hz, 0.1dB ripple (actual 0.06dB)
//         - Stopband: 1280 - 2560Hz, -80dB attenuation (actual -82.42dB)
//         - 67 taps
static const q31_t VibDecimFiltB2_Coeffs[] =
{
     -1445717,    -4220415,    -4384268,      598878,     5800534,
      2313240,    -7095579,    -7422172,     6161311,    13879246,
     -1251550,   -19944914,    -8481466,    22817622,    22791232,
    -19320911,   -39661017,     6410424,    55264305,    17838033,
    -64016288,   -53569137,    59043460,    98646768,   -32298653,
   -148729434,   -26604737,   197792582,   137318828,  -239177217,
   -367173277,   266835750,  1336406042,  1870924501,  1336406042,
    266835750,  -367173277,  -239177217,   137318828,   197792582,
    -26604737,  -148729434,   -32298653,    98646768,    59043460,
    -53569137,   -64016288,    17838033,    55264305,     6410424,
    -39661017,   -19320911,    22791232,    22817622,    -8481466,
    -19944914,    -1251550,    13879246,     6161311,    -7422172,
     -7095579,     2313240,     5800534,      598878,    -4384268,
     -4220415,    -1445717
};

// Decimation filter VibC1
//     - 51200sps input, 10240sps output, decimation factor = 5
//     - TFilter settings:
//         - Sampling freq: 51200sps
//         - Passband: 0 - 2000Hz, 0.1dB ripple (actual 0.06dB)
//         - Stopband: 5120 - 25600Hz, -80dB attenuation (actual -81.83dB)
//         - 63 taps
static const q31_t VibDecimFiltC1_Coeffs[] =
{
      -539444,    -1119229,    -1999481,    -2976154,    -3757552,
     -3925825,    -3008126,     -599260,     3479135,     9018055,
     15306368,    21110616,    24788560,    24551388,    18854923,
      6861362,   -11119991,   -33320184,   -56428641,   -75814109,
    -86065115,   -81799811,   -58630916,   -14118647,    51477130,
    134813996,   229569328,   327061618,   417322129,   490459527,
    538097790,   554633382,   538097790,   490459527,   417322129,
    327061618,   229569328,   134813996,    51477130,   -14118647,
    -58630916,   -81799811,   -86065115,   -75814109,   -56428641,
    -33320184,   -11119991,     6861362,    18854923,    24551388,
     24788560,    21110616,    15306368,     9018055,     3479135,
      -599260,    -3008126,    -3925825,    -3757552,    -2976154,
     -1999481,    -1119229,     -539444
};

// Decimation filter VibC2
//     - 10240sps input, 5120sps output, decimation factor = 2
//     - TFilter settings:
//         - Sampling freq: 10240sps
//         - Passband: 0 - 2000Hz, 0.1dB ripple (actual 0.06dB)
//         - Stopband: 2560 - 5120Hz, -80dB attenuation (actual -82.42dB)
//         - 67 taps
static const q31_t VibDecimFiltC2_Coeffs[] =
{
     -1445717,    -4220415,    -4384268,      598878,     5800534,
      2313240,    -7095579,    -7422172,     6161311,    13879246,
     -1251550,   -19944914,    -8481466,    22817622,    22791232,
    -19320911,   -39661017,     6410424,    55264305,    17838033,
    -64016288,   -53569137,    59043460,    98646768,   -32298653,
   -148729434,   -26604737,   197792582,   137318828,  -239177217,
   -367173277,   266835750,  1336406042,  1870924501,  1336406042,
    266835750,  -367173277,  -239177217,   137318828,   197792582,
    -26604737,  -148729434,   -32298653,    98646768,    59043460,
    -53569137,   -64016288,    17838033,    55264305,     6410424,
    -39661017,   -19320911,    22791232,    22817622,    -8481466,
    -19944914,    -1251550,    13879246,     6161311,    -7422172,
     -7095579,     2313240,     5800534,      598878,    -4384268,
     -4220415,    -1445717
};

//
// ---------------------------
// Wheel-flat decimation chain
// ---------------------------
//
// The wheel-flat decimation chain is as follows. The values above the lines
// show the sampling rates at the various stages:
//
// From
// enveloper
//  |
//  | 10240 |----------|  10240  |===============|  1024   |===============|  256
//  ------->| Block    |-------->| Decim WflatA1 |-------->| Decim WflatA2 |------->
//     128  | resizer  | 160 |   |     /10       |   16    |       /4      |   4
//          |----------|     |   |===============|         |===============|
//                           |
//                           |
//                           |   |===============|  1024   |===============|  512
//                           |-->| Decim WflatB1 |-------->| Decim WflatB2 |------->
//                           |   |      /10      |   16    |       /2      |   8
//                           |   |===============|         |===============|
//                           |
//                           |
//                           |   |===============|  2560   |===============|  1280
//                           --->| Decim WflatC1 |-------->| Decim WflatC2 |------->
//                               |       /4      |   40    |       /2      |   20
//                               |===============|         |===============|
//
//
// Decimation filter WflatA1
//     - 10240sps input, 1024sps output, decimation factor = 10
//     - TFilter settings:
//         - Sampling freq: 10240sps
//         - Passband: 0 - 110Hz, 0.1dB ripple (actual 0.07dB)  - ******** NOTE: 100Hz doesn't work in TFilter
//         - Stopband: 512 - 5120Hz, -80dB attenuation (actual -80.27dB)
//         - 99 taps
static const q31_t WflatDecimFiltA1_Coeffs[] =
{
       339357,      322144,      448893,      582857,      710688,
       814355,      871012,      853534,      731116,      470830,
        39179,     -595672,    -1461223,    -2577912,    -3955844,
     -5591757,    -7466317,    -9541401,   -11758051,   -14035455,
    -16270631,   -18339063,   -20096770,   -21383696,   -22027944,
    -21851277,   -20676083,   -18332361,   -14665538,    -9544899,
     -2870813,     5417394,    15335578,    26849674,    39872084,
     54261286,    69820530,    86303566,   103416740,   120828037,
    138176230,   155080436,   171152629,   186010506,   199289943,
    210657373,   219821909,   226545826,   230652713,   232033905,
    230652713,   226545826,   219821909,   210657373,   199289943,
    186010506,   171152629,   155080436,   138176230,   120828037,
    103416740,    86303566,    69820530,    54261286,    39872084,
     26849674,    15335578,     5417394,    -2870813,    -9544899,
    -14665538,   -18332361,   -20676083,   -21851277,   -22027944,
    -21383696,   -20096770,   -18339063,   -16270631,   -14035455,
    -11758051,    -9541401,    -7466317,    -5591757,    -3955844,
     -2577912,    -1461223,     -595672,       39179,      470830,
       731116,      853534,      871012,      814355,      710688,
       582857,      448893,      322144,      339357
};

// Decimation filter WflatA2
//     - 1024sps input, 256sps output, decimation factor = 4
//     - TFilter settings:
//         - Sampling freq: 1024sps
//         - Passband: 0 - 100Hz, 0.1dB ripple (actual 0.07dB)
//         - Stopband: 128 - 512Hz, -80dB attenuation (actual -80.20dB)
//         - 131 taps
static const q31_t WflatDecimFiltA2_Coeffs[] =
{
      -450092,     -543947,     -499415,      -32302,        912956,
      2159807,     3289284,     3762446,     3153612,       1407326,
     -1015473,    -3215091,    -4172701,    -3222436,       -460467,
      3126632,     5951553,     6479096,     3993895,       -882288,
     -6234503,    -9551173,    -8851151,    -3757146,       4027934,
     11189483,    14147843,    10768677,     1650628,      -9716259,
    -18186375,   -19204596,   -11119985,     3557052,      18734171,
     27150119,    23722172,     8337454,   -13350342,     -31799364,
    -37614448,   -26104204,     -239645,    29638659,      49694035,
     48714103,    23886889,   -16560562,   -55569070,     -73975584,
    -59285166,   -12710717,    49035097,    98824031,     109840526,
     68051017,   -18771499,  -119841767,  -189581227,    -183135463,
    -73854051,   133912973,   402080628,   668036841,     863226693,
    934878867,   863226693,   668036841,   402080628,     133912973,
    -73854051,  -183135463,  -189581227,  -119841767,     -18771499,
     68051017,   109840526,    98824031,    49035097,     -12710717,
    -59285166,   -73975584,   -55569070,   -16560562,      23886889,
     48714103,    49694035,    29638659,     -239645,     -26104204,
    -37614448,   -31799364,   -13350342,     8337454,      23722172,
     27150119,    18734171,     3557052,   -11119985,     -19204596,
    -18186375,    -9716259,     1650628,    10768677,      14147843,
     11189483,     4027934,    -3757146,    -8851151,      -9551173,
     -6234503,     -882288,     3993895,     6479096,       5951553,
      3126632,     -460467,    -3222436,    -4172701,      -3215091,
     -1015473,     1407326,     3153612,     3762446,       3289284,
      2159807,      912956,      -32302,     -499415,       -543947,
      -450092
};

// Decimation filter WflatB1
//     - 10240sps input, 1024sps output, decimation factor = 10
//     - TFilter settings:
//         - Sampling freq: 10240sps
//         - Passband: 0 - 200Hz, 0.1dB ripple (actual 0.06dB)
//         - Stopband: 512 - 5120Hz, -80dB attenuation (actual -81.36dB)
//         - 125 taps
static const q31_t WflatDecimFiltB1_Coeffs[] =
{
      -320467,     -348685,     -516279,     -714982,     -938268,
     -1175222,    -1410362,    -1623658,    -1790834,    -1884282,
     -1874394,    -1731316,    -1427090,     -937970,     -247083,
       652808,     1756770,     3045606,     4484267,     6020870,
      7586586,     9096810,    10453709,    11549615,    12271413,
     12506427,    12149462,    11109716,     9317778,     6733922,
      3354967,     -781497,    -5589867,   -10937066,   -16639636,
    -22467433,   -28147426,   -33371295,   -37805121,   -41101206,
    -42911960,   -42905006,   -40778829,   -36278516,   -29210603,
    -19456392,    -6982973,     8148804,    25779867,    45652240,
     67412626,    90620297,   114758858,   139251670,   163480546,
    186806860,   208593953,   228230370,   245152740,   258866869,
    268966866,   275151115,   277233355,   275151115,   268966866,
    258866869,   245152740,   228230370,   208593953,   186806860,
    163480546,   139251670,   114758858,    90620297,    67412626,
     45652240,    25779867,     8148804,    -6982973,   -19456392,
    -29210603,   -36278516,   -40778829,   -42905006,   -42911960,
    -41101206,   -37805121,   -33371295,   -28147426,   -22467433,
    -16639636,   -10937066,    -5589867,     -781497,     3354967,
      6733922,     9317778,    11109716,    12149462,    12506427,
     12271413,    11549615,    10453709,     9096810,     7586586,
      6020870,     4484267,     3045606,     1756770,      652808,
      -247083,     -937970,    -1427090,    -1731316,    -1874394,
     -1884282,    -1790834,    -1623658,    -1410362,    -1175222,
      -938268,     -714982,     -516279,     -348685,     -320467
};

// Decimation filter WflatB2
//     - 1024sps input, 512sps output, decimation factor = 2
//     - TFilter settings:
//         - Sampling freq: 1024sps
//         - Passband: 0 - 200Hz, 0.1dB ripple (actual 0.06dB)
//         - Stopband: 256 - 512Hz, -80dB attenuation (actual -82.42dB)
//         - 67 taps
static const q31_t WflatDecimFiltB2_Coeffs[] =
{
     -1445717,    -4220415,    -4384268,      598878,     5800534,
      2313240,    -7095579,    -7422172,     6161311,    13879246,
     -1251550,   -19944914,    -8481466,    22817622,    22791232,
    -19320911,   -39661017,     6410424,    55264305,    17838033,
    -64016288,   -53569137,    59043460,    98646768,   -32298653,
   -148729434,   -26604737,   197792582,   137318828,  -239177217,
   -367173277,   266835750,  1336406042,  1870924501,  1336406042,
    266835750,  -367173277,  -239177217,   137318828,   197792582,
    -26604737,  -148729434,   -32298653,    98646768,    59043460,
    -53569137,   -64016288,    17838033,    55264305,     6410424,
    -39661017,   -19320911,    22791232,    22817622,    -8481466,
    -19944914,    -1251550,    13879246,     6161311,    -7422172,
     -7095579,     2313240,     5800534,      598878,    -4384268,
     -4220415,    -1445717
};

// Decimation filter WflatC1
//     - 10240sps input, 2560sps output, decimation factor = 4
//     - TFilter settings:
//         - Sampling freq: 10240sps
//         - Passband: 0 - 500Hz, 0.1dB ripple (actual 0.05dB)
//         - Stopband: 1280 - 5120Hz, -80dB attenuation (actual -83.01dB)
//         - 51 taps
static const q31_t WflatDecimFiltC1_Coeffs[] =
{
      -660398,    -1699764,    -3243129,    -4803200,    -5498250,
     -4181050,      150100,     7762125,    17615359,    27081298,
     32213536,    28700709,    13393853,   -13966386,   -49397839,
    -84309127,  -106535473,  -102809195,   -62214424,    20244214,
    141025949,   287327264,   438724303,   571001723,   661316294,
    693386377,   661316294,   571001723,   438724303,   287327264,
    141025949,    20244214,   -62214424,  -102809195,  -106535473,
    -84309127,   -49397839,   -13966386,    13393853,    28700709,
     32213536,    27081298,    17615359,     7762125,      150100,
     -4181050,    -5498250,    -4803200,    -3243129,    -1699764,
      -660398
};

// Decimation filter WflatC2
//     - 2560sps input, 1280sps output, decimation factor = 2
//     - TFilter settings:
//         - Sampling freq: 2560sps
//         - Passband: 0 - 500Hz, 0.1dB ripple (actual 0.06dB)
//         - Stopband: 640 - 1280Hz, -80dB attenuation (actual -82.42dB)
//         - 67 taps
static const q31_t WflatDecimFiltC2_Coeffs[] =
{
     -1445717,    -4220415,    -4384268,      598878,     5800534,
      2313240,    -7095579,    -7422172,     6161311,    13879246,
     -1251550,   -19944914,    -8481466,    22817622,    22791232,
    -19320911,   -39661017,     6410424,    55264305,    17838033,
    -64016288,   -53569137,    59043460,    98646768,   -32298653,
   -148729434,   -26604737,   197792582,   137318828,  -239177217,
   -367173277,   266835750,  1336406042,  1870924501,  1336406042,
    266835750,  -367173277,  -239177217,   137318828,   197792582,
    -26604737,  -148729434,   -32298653,    98646768,    59043460,
    -53569137,   -64016288,    17838033,    55264305,     6410424,
    -39661017,   -19320911,    22791232,    22817622,    -8481466,
    -19944914,    -1251550,    13879246,     6161311,    -7422172,
     -7095579,     2313240,     5800534,      598878,    -4384268,
     -4220415,    -1445717
};

//..............................................................................

typedef enum
{
    DECIMFILT_VIB_A1,
    DECIMFILT_VIB_A2,
    DECIMFILT_VIB_B1,
    DECIMFILT_VIB_B2,
    DECIMFILT_VIB_C1,
    DECIMFILT_VIB_C2,

    DECIMFILT_WFLATS_A1,
    DECIMFILT_WFLATS_A2,
    DECIMFILT_WFLATS_B1,
    DECIMFILT_WFLATS_B2,
    DECIMFILT_WFLATS_C1,
    DECIMFILT_WFLATS_C2
} DecimFiltEnum;

typedef struct
{
    uint8_t Stage;      // 1 or 2
    DecimFiltEnum ID;
    uint8_t NumTaps;
    uint8_t Factor;
    const q31_t *pCoeffs;
    uint32_t BlockSize;
} DecimFiltConfigType;

static struct
{
    DecimChainEnum ID;
    DecimFiltEnum Stage1FiltID;
    DecimFiltEnum Stage2FiltID;
} DecimChains[] =
{
//   ID                      Stage1FiltID         Stage2FiltID
    {DECIMCHAIN_VIB_1280,    DECIMFILT_VIB_A1,    DECIMFILT_VIB_A2},
    {DECIMCHAIN_VIB_2560,    DECIMFILT_VIB_B1,    DECIMFILT_VIB_B2},
    {DECIMCHAIN_VIB_5120,    DECIMFILT_VIB_C1,    DECIMFILT_VIB_C2},

    {DECIMCHAIN_WFLATS_256,  DECIMFILT_WFLATS_A1, DECIMFILT_WFLATS_A2},
    {DECIMCHAIN_WFLATS_512,  DECIMFILT_WFLATS_B1, DECIMFILT_WFLATS_B2},
    {DECIMCHAIN_WFLATS_1280, DECIMFILT_WFLATS_C1, DECIMFILT_WFLATS_C2}
};

static DecimChainEnum g_DecimChainID = DECIMCHAIN_NONE;

//**********************************************************
// IMPORTANT: The following macros ** MUST ** be set to the
// largest corresponding field values in the
// DecimFiltConfigs[] list below
// N.B. Run-time checking of this is implemented in PassRailDsp_Init().
// TODO: Can this be done at build time somehow?
//**********************************************************
#define DECIMFILT1_BLOCKSIZE          (160)
#define DECIMFILT1_LARGEST_NUM_TAPS   (150)
#if (DECIMFILT1_BLOCKSIZE < ADC_SAMPLES_PER_BLOCK)
#error "BUILD ERROR: DECIMFILT1_BLOCKSIZE is less than ADC_SAMPLES_PER_BLOCK"
#endif

#define DECIMFILT2_LARGEST_BLOCKSIZE  (40)
#define DECIMFILT2_LARGEST_NUM_TAPS   (150)

// Decimation filter instances - only 2 needed because will only be 1 filter
// chain used at a time
static arm_fir_decimate_instance_q31 DecimFilt1;
static arm_fir_decimate_instance_q31 DecimFilt2;

// Decimation filter configurations
static DecimFiltConfigType DecimFiltConfigs[] =
{
    //**********************************************************
    // IMPORTANT: The DECIMFILT1_XXX and DECIMFILT2_XXX defines
    // above must stay updated with the corresponding fields in
    // the following table
    //**********************************************************

    // Stage  ID                   NumTaps                                      Factor  pCoeffs                  BlockSize
    // Vibration
    {  1,     DECIMFILT_VIB_A1,    ARRAY_NUM_ELEMENTS(VibDecimFiltA1_Coeffs),   10,     VibDecimFiltA1_Coeffs,   DECIMFILT1_BLOCKSIZE},
    {  2,     DECIMFILT_VIB_A2,    ARRAY_NUM_ELEMENTS(VibDecimFiltA2_Coeffs),   4,      VibDecimFiltA2_Coeffs,   16},

    {  1,     DECIMFILT_VIB_B1,    ARRAY_NUM_ELEMENTS(VibDecimFiltB1_Coeffs),   10,     VibDecimFiltB1_Coeffs,   DECIMFILT1_BLOCKSIZE},
    {  2,     DECIMFILT_VIB_B2,    ARRAY_NUM_ELEMENTS(VibDecimFiltB2_Coeffs),   2,      VibDecimFiltB2_Coeffs,   16},

    {  1,     DECIMFILT_VIB_C1,    ARRAY_NUM_ELEMENTS(VibDecimFiltC1_Coeffs),   5,      VibDecimFiltC1_Coeffs,   DECIMFILT1_BLOCKSIZE},
    {  2,     DECIMFILT_VIB_C2,    ARRAY_NUM_ELEMENTS(VibDecimFiltC2_Coeffs),   2,      VibDecimFiltC2_Coeffs,   32},

    // Wheel-flats
    {  1,     DECIMFILT_WFLATS_A1, ARRAY_NUM_ELEMENTS(WflatDecimFiltA1_Coeffs), 10,     WflatDecimFiltA1_Coeffs, DECIMFILT1_BLOCKSIZE},
    {  2,     DECIMFILT_WFLATS_A2, ARRAY_NUM_ELEMENTS(WflatDecimFiltA2_Coeffs), 4,      WflatDecimFiltA2_Coeffs, 16},

    {  1,     DECIMFILT_WFLATS_B1, ARRAY_NUM_ELEMENTS(WflatDecimFiltB1_Coeffs), 10,     WflatDecimFiltB1_Coeffs, DECIMFILT1_BLOCKSIZE},
    {  2,     DECIMFILT_WFLATS_B2, ARRAY_NUM_ELEMENTS(WflatDecimFiltB2_Coeffs), 2,      WflatDecimFiltB2_Coeffs, 16},

    {  1,     DECIMFILT_WFLATS_C1, ARRAY_NUM_ELEMENTS(WflatDecimFiltC1_Coeffs), 4,      WflatDecimFiltC1_Coeffs, DECIMFILT1_BLOCKSIZE},
    {  2,     DECIMFILT_WFLATS_C2, ARRAY_NUM_ELEMENTS(WflatDecimFiltC2_Coeffs), 2,      WflatDecimFiltC2_Coeffs, 40},
};

// Decimation filter configurations - these hold the configurations for the
// two filters of the current decimation filter chain, copied from the
// DecimFiltConfigs[] list above
static DecimFiltConfigType g_DecimFilt1Config;
static DecimFiltConfigType g_DecimFilt2Config;

// Block resizer buffer - see the BlockResizer_XXX() functions
#define BLOCK_RESIZER_BUF_SIZE  (2 * ADC_SAMPLES_PER_BLOCK)
static struct
{
    int32_t Buf[BLOCK_RESIZER_BUF_SIZE];
    uint16_t SamplesInBuf;
} g_BlockResizer =
{
    .SamplesInBuf = 0
};

// Define the state buffers. Their sizes need to be (numTaps + blockSize - 1)
// word - see arm_fir_decimate_init_q31() documentation. These sizes need to be
// the ** MAXIMUM ** size required across all 6 filter chains.
static q31_t DecimFilt1StateBuf[DECIMFILT1_BLOCKSIZE + DECIMFILT1_LARGEST_NUM_TAPS - 1];
static q31_t DecimFilt2StateBuf[DECIMFILT2_LARGEST_BLOCKSIZE + DECIMFILT2_LARGEST_NUM_TAPS - 1];

// Define the decimation inter-stage buffer
static int32_t g_DecimInterstageBuf[DECIMFILT2_LARGEST_BLOCKSIZE];

static int32_t g_nNumSettlingSamples;
static int32_t g_nMean;
static int32_t g_nMeanCount;
bool g_bDisableDcFilter = 0;

//..............................................................................

static void DspEnvSmootherFilt(int32_t *pCoeffs, int64_t *pState,
                               int32_t *pSampleBlock, uint32_t BlockSize);
static void BlockResizer_Reset(void);
static bool BlockResizer_Put(int32_t *pSampleBlock);
static bool BlockResizer_OutputGetPtr(int32_t **ppOutputBlockPtr);
static void BlockResizer_OutputFinish(void);

int32_t *SinePlusMinus50_GetAdcBlock128(void);
static void CalcMean(const int32_t* pnSampleBlock, int32_t nNumSamples);
static void RemoveDC(int32_t *pSampleBlock, int32_t nNumSamples);



//..............................................................................

/*
 * PassRailDsp_Init
 *
 * @desc    Initialises the passenger-rail-specific DSP chain. Needs to be
 *          called before every sampling burst.
 *
 * @param   EnveloperID: Specifies enveloper type required
 * @param   DecimChainID: Specifies decimation filter chain required
 *
 * @returns true if initialised OK, false otherwise
 */
bool PassRailDsp_Init(EnveloperEnum EnveloperID, DecimChainEnum DecimChainID)
{
    g_DspEnveloperID = EnveloperID;
    uint8_t i;
    uint8_t DecimChainArrayIndex = 0;
    arm_status DspStatus;
    bool bOK;

    g_nMean = 0;
    g_nMeanCount = 0;
    //We extract the mean (DC) at start for two blocks.
    g_nNumSettlingSamples = 2 * ADC_SAMPLES_PER_BLOCK;

    // Perform run-time checks on the DecimFiltConfigs[] array that none of
    // the block sizes or numbers of taps specified are larger than
    // DECIMFILT1_BLOCKSIZE, DECIMFILT1_LARGEST_NUM_TAPS,
    // DECIMFILT2_LARGEST_BLOCKSIZE and DECIMFILT2_LARGEST_NUM_TAPS, and
    // trigger an error if anything wrong.
    // Any problems which slip through development & code reviews should then
    // be caught here during developer testing or system testing.
    // TODO: Would be better done at build time somehow, but couldn't yet 
    // figure out how to do this
    for (i = 0; i < ARRAY_NUM_ELEMENTS(DecimFiltConfigs); i++)
    {
        if (DecimFiltConfigs[i].Stage == 1)
        {
            if ((DecimFiltConfigs[i].BlockSize > DECIMFILT1_BLOCKSIZE) ||
                (DecimFiltConfigs[i].NumTaps > DECIMFILT1_LARGEST_NUM_TAPS))
            {
                // Error
                return false;
            }
        }
        else if (DecimFiltConfigs[i].Stage == 2)
        {
            if ((DecimFiltConfigs[i].BlockSize > DECIMFILT2_LARGEST_BLOCKSIZE) ||
                (DecimFiltConfigs[i].NumTaps > DECIMFILT2_LARGEST_NUM_TAPS))
            {
                // Error
                return false;
            }
        }
        else
        {
            // Error
            return false;
        }
    }

    // Initialise the envelope smoother filter
    g_EnvFilterState = 0;

    g_DecimChainID = DECIMCHAIN_NONE;

    if (DecimChainID == DECIMCHAIN_NONE)
    {
        bOK = true;
    }
    else
    {
        // Reset the block resizer buffer
        BlockResizer_Reset();

        // Identify the decimation filter chain required
        bOK = false;
        for (i = 0; i < ARRAY_NUM_ELEMENTS(DecimChains); i++)
        {
            if (DecimChains[i].ID == DecimChainID)
            {
                DecimChainArrayIndex = i;
                g_DecimChainID = DecimChainID;
                bOK = true;
                break;
            }
        }

        // Get the stage 1 & stage 2 decimation filter configurations
        if (bOK)
        {
            bOK = false;
            bool bStage1Found = false;
            bool bStage2Found = false;
            for (i = 0; i < ARRAY_NUM_ELEMENTS(DecimFiltConfigs); i++)
            {
                if (DecimFiltConfigs[i].ID ==
                                    DecimChains[DecimChainArrayIndex].Stage1FiltID)
                {
                    g_DecimFilt1Config = DecimFiltConfigs[i];
                    bStage1Found = true;
                }

                if (DecimFiltConfigs[i].ID ==
                                    DecimChains[DecimChainArrayIndex].Stage2FiltID)
                {
                    g_DecimFilt2Config = DecimFiltConfigs[i];
                    bStage2Found = true;
                }
            }

            if (bStage1Found && bStage2Found)
            {
                bOK = true;
            }
        }

        // Initialise the decimation filters
        if (bOK)
        {
            bOK = false;
            DspStatus = arm_fir_decimate_init_q31(&DecimFilt1,
                                                  g_DecimFilt1Config.NumTaps,
                                                  g_DecimFilt1Config.Factor,
                                                  (q31_t *)g_DecimFilt1Config.pCoeffs,
                                                  DecimFilt1StateBuf,
                                                  g_DecimFilt1Config.BlockSize);
            if (DspStatus == ARM_MATH_SUCCESS)
            {
                DspStatus = arm_fir_decimate_init_q31(&DecimFilt2,
                                                      g_DecimFilt2Config.NumTaps,
                                                      g_DecimFilt2Config.Factor,
                                                      (q31_t *)g_DecimFilt2Config.pCoeffs,
                                                      DecimFilt2StateBuf,
                                                      g_DecimFilt2Config.BlockSize);
                if (DspStatus == ARM_MATH_SUCCESS)
                {
                    bOK = true;
                }
            }
        }
    }

    return bOK;
}

//------------------------------------------------------------------------------
/// Calculate the running mean of the raw input.
///		This function implements a basic HP IIR of the form
///				y[n] = βx[n] + (1 - β)y[n-1]
///			which we re-write as
///				y[n] = y[n-1] + β(x - y[n-1])
///			so we can use __QSUB throughout
///				y[n] = y[n-1] - α(x - y[n-1]) where α = -β
///		Note that we here we implement a variable coefficient filter because
///		'β' is set to be the reciprocal of the running mean count. This means
///		the filter starts off as an infinite low-pass (β = 1) but then quickly
///		narrows the cut-off as the mean count increases.
///		Note that the filter should not be run for any significant length of
///		time as the coefficient will overrun at 0x7fffffff.
/// @param  pnSampleBlock (const int32_t*) -- Pointer to raw ADC data block.
/// @param  nNumSamples (int32_t) -- Number of samples in the ADC data block.
//------------------------------------------------------------------------------
static void CalcMean(const int32_t* pnSampleBlock, int32_t nNumSamples)
{
   int32_t i;
   q31_t qAlpha, qDiff, qTemp;

   for(i = 0; i < nNumSamples; i++)
   {
	   //g_nMeanCount is initialised to zero so pre-increment. This also
	   //ensures that we never divide by zero.
	   g_nMeanCount++;
	   //From function documentation header above, α = -1/Count. -1 in Q31
	   //format would be -2147483648. However, we cannot represent this in
	   //32 bits, so we use the closest approximation of -2147483648.
	   //This means that for the very first sample the filter will not have
	   //an infinite pass-band, although 'close' to it.
	   qAlpha = (q31_t)(-2147483647 / g_nMeanCount);

	   //x - y[n-1]
	   qDiff = __QSUB(pnSampleBlock[i], g_nMean);
	   //α(x - y[n-1]) using Q64 fixed point and then back to 32-bit
	   qTemp = (((q63_t)qAlpha * (q63_t)qDiff) >> 32);
	   //Saturate to 31 bits (Q31)
	   qTemp = __SSAT(qTemp, 31);
	   //We performed Q63 arithmetic above but we're working with Q31
	   //so need to shift back
	   qTemp <<= 1u;
	   //y[n] = y[n-1] - α(x - y[n-1])
	   g_nMean = __QSUB(g_nMean, (q31_t)qTemp);
   }
}

//------------------------------------------------------------------------------
/// Remove the DC from the raw input using the calculated running mean.
/// @param  pnSampleBlock (int32_t*) -- Pointer to raw ADC data block to
///			remove DC from.
/// @param  nNumSamples (int32_t) -- Number of samples in the ADC data block.
//------------------------------------------------------------------------------
static void RemoveDC(int32_t *pSampleBlock, int32_t nNumSamples)
{
	int32_t i;
	for (i = 0; i < nNumSamples; i++)
	{
		pSampleBlock[i] -= g_nMean;
	}
}

//------------------------------------------------------------------------------
/// get the determined mean values, to display in the cli
/// @param  mean
/// @param  meanCount
//------------------------------------------------------------------------------
void getMeanValues( int32_t * meanp, uint32_t * meanCountp)
{
    if (meanp) *meanp = g_bDisableDcFilter ? 0 : g_nMean;
    if (meanCountp) *meanCountp = g_bDisableDcFilter ? 0 : g_nMeanCount;
}

/*
 * PassRailDsp_ProcessBlock
 *
 * @desc    Performs the passenger-rail-specific DSP processing of a single
 *          block of incoming ADC_SAMPLES_PER_BLOCK samples.
 *          Notes:
 *            - The number of output samples will generally be less than the
 *              number of input samples, due to decimation. However, it will
 *              never be greater
 *            - This function internally converts the input blocks of size
 *              ADC_SAMPLES_PER_BLOCK into blocks of size DECIMFILT1_BLOCKSIZE
 *              (in the block resizer buffer), to help cater for the CMSIS-DSP
 *              requirement for each decimation filter's input block size to
 *              a multiple of its decimation factor
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
    int32_t *pDecim1InputBlock;
    bool bOK;

    bOK = false;

    // NOTE: For ADC input sample value scaling considerations, see the comments
    // at the top of this file.

//#define INJECT_SINE_PLUSMINUS50
#ifdef INJECT_SINE_PLUSMINUS50
    pSampleBlockIn = SinePlusMinus50_GetAdcBlock128();
#endif // INJECT_SINE_PLUSMINUS50

    //Have we settled?
    if (g_nNumSettlingSamples > 0)
	{
    	//While settling, we calculate the running mean
		CalcMean(pSampleBlockIn, ADC_SAMPLES_PER_BLOCK);

		//Are we done settling?
		if(g_nNumSettlingSamples > ADC_SAMPLES_PER_BLOCK)
		{
			g_nNumSettlingSamples -= ADC_SAMPLES_PER_BLOCK;
		}
		else
		{
			g_nNumSettlingSamples = 0;
		}
	}
	else //We've settled so commence with DSP processing
	{
		//Always remove DC component
		if(!g_bDisableDcFilter) RemoveDC(pSampleBlockIn, ADC_SAMPLES_PER_BLOCK);

		//..........................................................................
		// Enveloper (rectifier + first-order lowpass smoothing filter)

//#define BYPASS_ENVELOPER
#ifdef BYPASS_ENVELOPER
		g_DspEnveloperID = ENV_NONE;
#endif

		if (g_DspEnveloperID != ENV_NONE)
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
			if (g_DspEnveloperID == ENV_VIB)
			{
			DspEnvSmootherFilt(VibEnvFiltCoeffs, &g_EnvFilterState,
							   g_EnvOutBuf, ADC_SAMPLES_PER_BLOCK);
			}
			else if (g_DspEnveloperID == ENV_WFLATS)
			{
			DspEnvSmootherFilt(WflatEnvFiltCoeffs, &g_EnvFilterState,
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

//#define BYPASS_DECIMATION
#ifdef BYPASS_DECIMATION
		g_DecimChainID = DECIMCHAIN_NONE;
#endif

		if (g_DecimChainID == DECIMCHAIN_NONE)
		{
			// No decimation chain - just pass straight through
			for (i = 0; i < ADC_SAMPLES_PER_BLOCK; i++)
			{
				pSampleBlockOut[i] = g_EnvOutBuf[i];
			}
			*pNumOutputSamples = ADC_SAMPLES_PER_BLOCK;

			bOK = true;
		}
		else
		{
			// Perform block resizing for upcoming decimation - resizes from
			// ADC_SAMPLES_PER_BLOCK (128) to DECIMFILT1_BLOCKSIZE (160)
			// N.B. Not every pass will result in an output block, because the output
			// block size is larger than the input block size
			bOK = BlockResizer_Put(g_EnvOutBuf);
			if (bOK)
			{
				// If sufficient samples available in resizer buffer, then process a
				// block of them through the decimation chain
				if (BlockResizer_OutputGetPtr(&pDecim1InputBlock))
				{
					// Stage 1 decimation filter
					arm_fir_decimate_q31(&DecimFilt1, pDecim1InputBlock,
										 g_DecimInterstageBuf, g_DecimFilt1Config.BlockSize);

						// Stage 2 decimation filter
//#define DECIMCHAIN_BYPASS_STAGE2
#ifdef DECIMCHAIN_BYPASS_STAGE2
					uint16_t DecimFilt1OutputBlockSize = g_DecimFilt1Config.BlockSize / g_DecimFilt1Config.Factor;
					for (i = 0; i < DecimFilt1OutputBlockSize; i++)
					{
						//******** TODO: Add buffer overrun protection

						pSampleBlockOut[i] = g_DecimInterstageBuf[i];
					}
					*pNumOutputSamples = DecimFilt1OutputBlockSize;
#else

					arm_fir_decimate_q31(&DecimFilt2, g_DecimInterstageBuf,
										 pSampleBlockOut, g_DecimFilt2Config.BlockSize);

					// Indicate the number of output samples this time around
					*pNumOutputSamples = g_DecimFilt2Config.BlockSize / g_DecimFilt2Config.Factor;
#endif

					// Finish the current block resizer output cycle
					BlockResizer_OutputFinish();

					// TODO: Set better size for pSampleBlockOut buffer - it's g_DspOutSampleBuf[]
				}
			}
			else
			{
				// ERROR: BlockResizer_Put() failed
			}
		}
	}
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
static void DspEnvSmootherFilt(int32_t *pCoeffs, int64_t *pState,
                               int32_t *pSampleBlock, uint32_t BlockSize)
{
    uint32_t i;
    int64_t SampleInTimesCoeff0;

    for (i = 0; i < BlockSize; i++)
    {
        SampleInTimesCoeff0 = (int64_t)pSampleBlock[i] * (int64_t)pCoeffs[0];

        // N.B. The right-shift below is OK because it is an "arithmetic" shift
        // rather than a "logical" shift. GCC documentation comment (when
        // Google for "gcc arithmetic shift"): "For right shifts, if the type
        // is signed, then we use an arithmetic shift"
        pSampleBlock[i] = (int32_t)((SampleInTimesCoeff0 + *pState) >> 31);

        *pState = SampleInTimesCoeff0 - ((int64_t)pSampleBlock[i] * (int64_t)pCoeffs[1]);
    }
}

/*
 * BlockResizer_Reset
 *
 * @desc
 *          - Input block size = 128 (ADC_SAMPLES_PER_BLOCK)
 *          - Output block size = 160 (DECIMFILT1_BLOCKSIZE)
 *          - Buffer size needs to be 2 x input block size so that can accept
 *            worst-case of 2 x full back-to-back input blocks
 *
 * @param
 *
 * @returns -
 */
static void BlockResizer_Reset(void)
{
    g_BlockResizer.SamplesInBuf = 0;
}

/*
 * BlockResizer_Put
 *
 * @desc    Puts a sample block of size ADC_SAMPLES_PER_BLOCK into the block
 *          resizer
 *
 * @param   pSampleBlock: Input sample block of size ADC_SAMPLES_PER_BLOCK
 *
 * @returns true if OK, false if insufficient room - THIS IS AN ERROR because
 *          should never overflow if used properly
 */
static bool BlockResizer_Put(int32_t *pSampleBlock)
{
    uint16_t i;

    // If room in buffer (N.B. buffer has size of 2 x ADC_SAMPLES_PER_BLOCK),
    // then append new block into buffer
    if (g_BlockResizer.SamplesInBuf <= ADC_SAMPLES_PER_BLOCK) // Also provides buffer overrun protection
    {
        for (i = 0; i < ADC_SAMPLES_PER_BLOCK; i++)
        {
            g_BlockResizer.Buf[g_BlockResizer.SamplesInBuf + i] = pSampleBlock[i];
        }
        g_BlockResizer.SamplesInBuf += ADC_SAMPLES_PER_BLOCK;
        return true;
    }

    return false;
}

/*
 * BlockResizer_OutputGetPtr
 *
 * @desc    Checks if DECIMFILT1_BLOCKSIZE samples are available in the block
 *          resizer, and if so then provides a pointer to the block of samples.
 *
 * @param   **ppOutputBlockPtr: Written with address of block resizer's
 *          internal buffer where the output block is available
 *
 * @returns true if enough samples are available, false if not
 */
static bool BlockResizer_OutputGetPtr(int32_t **ppOutputBlockPtr)
{
    if (g_BlockResizer.SamplesInBuf >= DECIMFILT1_BLOCKSIZE)
    {
        *ppOutputBlockPtr = g_BlockResizer.Buf;
        return true;
    }
    return false;
}

/*
 * BlockResizer_OutputFinish
 *
 * @desc    Must be called once BlockResizer_OutputGetPtr() has been successful
 *          and the output block usage has completed. Adjusts the internal
 *          buffering.
 *
 * @param   -
 *
 * @returns -
 */
static void BlockResizer_OutputFinish(void)
{
    uint16_t i;

    if (g_BlockResizer.SamplesInBuf <= BLOCK_RESIZER_BUF_SIZE)  // Buffer overrun protection
    {
        // If extra samples remaining in the buffer after use, then copy them
        // to beginning of buffer and adjust control variable
        if (g_BlockResizer.SamplesInBuf >= DECIMFILT1_BLOCKSIZE)
        {
            for (i = 0; i < (g_BlockResizer.SamplesInBuf - DECIMFILT1_BLOCKSIZE); i++)
            {
                g_BlockResizer.Buf[i] = g_BlockResizer.Buf[DECIMFILT1_BLOCKSIZE + i];
            }

            g_BlockResizer.SamplesInBuf -= DECIMFILT1_BLOCKSIZE;
        }
    }
}

//******************************************************************************
//******************************************************************************
// Test functionality follows

static const int32_t SinePlusMinus50[ADC_SAMPLES_PER_BLOCK * 2] =
{
    0,    1,    2,    3,    4,    6,    7,    8,    9,    10,
    12,   13,   14,   15,   16,   17,   19,   20,   21,   22,
    23,   24,   25,   26,   27,   28,   29,   30,   31,   32,
    33,   34,   35,   36,   37,   37,   38,   39,   40,   40,
    41,   42,   42,   43,   44,   44,   45,   45,   46,   46,
    47,   47,   47,   48,   48,   48,   49,   49,   49,   49,
    49,   49,   49,   49,   50,   49,   49,   49,   49,   49,
    49,   49,   49,   48,   48,   48,   47,   47,   47,   46,
    46,   45,   45,   44,   44,   43,   42,   42,   41,   40,
    40,   39,   38,   37,   37,   36,   35,   34,   33,   32,
    31,   30,   29,   28,   27,   26,   25,   24,   23,   22,
    21,   20,   19,   17,   16,   15,   14,   13,   12,   10,
    9,    8,    7,    6,    4,    3,    2,    1,    0,    -2,
    -3,   -4,   -5,   -7,  -8,    -9,   -10,  -11,  -13,  -14,
    -15,  -16,  -17,  -18, -20,  -21,   -22,  -23,  -24,  -25,
    -26,  -27,  -28,  -29, -30,  -31,   -32,  -33,  -34,  -35,
    -36,  -37,  -38,  -38, -39,  -40,   -41,  -41,  -42,  -43,
    -43,  -44,  -45,  -45, -46,  -46,   -47,  -47,  -48,  -48,
    -48,  -49,  -49,  -49, -50,  -50,   -50,  -50,  -50,  -50,
    -50,  -50,  -50,  -50, -50,  -50,   -50,  -50,  -50,  -50,
    -50,  -49,  -49,  -49, -48,  -48,   -48,  -47,  -47,  -46,
    -46,  -45,  -45,  -44, -43,  -43,   -42,  -41,  -41,  -40,
    -39,  -38,  -38,  -37, -36,  -35,   -34,  -33,  -32,  -31,
    -30,  -29,  -28,  -27, -26,  -25,   -24,  -23,  -22,  -21,
    -20,  -18,  -17,  -16, -15,  -14,   -13,  -11,  -10,  -9,
    -8,   -7,   -5,   -4,  -3,   -2
};

/*
 * SinePlusMinus50_GetAdcBlock128
 *
 * @desc    Creates blocks of 128 samples of overall cyclic 256-sample sine
 *          wave of amplitude +/-50, to use for DSP resolution testing.
 *          Generates the following equivalent sine wave frequencies:
 *            - 100Hz for 25600sps (raw ADC sampling rate)
 *            - 200Hz for 51200sps (vibration ADC sampling rate)
 *            - 40Hz for 10240sps (wheel-flat ADC sampling rate)
 *
 * @param   -
 *
 * @returns Pointer to next 128-sample block
 */
int32_t *SinePlusMinus50_GetAdcBlock128(void)
{
    int32_t *pSampleBlock;
    static bool g_bSinePlusMinus50Bottom = true;

    if (g_bSinePlusMinus50Bottom)
    {
        pSampleBlock = (int32_t *)&SinePlusMinus50[0];
        g_bSinePlusMinus50Bottom = false;
    }
    else
    {
        pSampleBlock = (int32_t *)&SinePlusMinus50[ADC_SAMPLES_PER_BLOCK];
        g_bSinePlusMinus50Bottom = true;
    }

    return pSampleBlock;
}



#if 0

static int32_t g_TestBlock[ADC_SAMPLES_PER_BLOCK];
static volatile bool g_bResultOK;
static volatile int32_t *g_pOutputBlock;
static volatile int32_t g_InputVal = 0;
static volatile int32_t g_OutputVal = 0;
static volatile bool g_bBlockResizerError = false;

// Test functions
void BlockResizer_TEST(void);


/*
 * PassRailDsp_TEST
 *
 * @desc
 *
 * @param
 *
 * @returns
 */
void PassRailDsp_TEST(void)
{
    //BlockResizer_TEST();

}



/*
 * BlockResizer_TEST
 *
 * @desc    Tests the block resizer by putting input blocks (of size
 *          ADC_SAMPLES_PER_BLOCK = 128) with incrementing sample values into
 *          the block resizer, and checking that the output blocks (of size
 *          DECIMFILT1_BLOCKSIZE = 160) match this.
 *
 *          To use it, set up a breakpoint where indicated near the end of the
 *          function - if it fires then something has gone wrong.
 *
 * @param   -
 *
 * @returns -
 */
static void BlockResizer_TEST(void)
{
    uint16_t i;

    //..........................................................................
    // Test the block resizer
    BlockResizer_Reset();

    g_InputVal = 0;
    g_OutputVal = 0;
    g_bBlockResizerError = false;

    while(1)
    {
        //......................................................................
        // Put an input block - number the values so can track
        for (i = 0; i < ADC_SAMPLES_PER_BLOCK; i++)
        {
            g_TestBlock[i] = g_InputVal;
            g_InputVal++;
        }
        g_bResultOK = BlockResizer_Put(g_TestBlock);
        if (!g_bResultOK)
        {
            // ERROR: The "put" operation failed - should never happen if block
            // resizer is used correctly
            g_bBlockResizerError = true;
        }

        //......................................................................
        // Get an output block, if available
        g_bResultOK = BlockResizer_OutputGetPtr(&g_pOutputBlock);
        if (g_bResultOK)
        {
            // Output block is now available - check it
            for (i = 0; i < DECIMFILT1_BLOCKSIZE; i++)
            {
                if (g_pOutputBlock[i] == g_OutputVal)
                {
                    g_OutputVal++;
                }
                else
                {
                    // ERROR: Output sample sequence didn't match input sample sequence
                    g_bBlockResizerError = true;
                }
            }

            // Finish the current output block's handling
            BlockResizer_OutputFinish();
        }

        //......................................................................

        if (g_bBlockResizerError)
        {
            g_InputVal = 999;   // Breakpointable
            break;
        }
    }
}


#endif // 0

//******************************************************************************
//******************************************************************************



//******************************************************************************
#endif // PASSRAIL_DSP_NEW
//******************************************************************************



#ifdef __cplusplus
}
#endif