#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "configMQTT.h"
#include <stdio.h>
#include "arm_math.h"


uint32_t    m_loader_version = 0x00000410u;

uint32_t __app_version_size = 0x7f0;

char   __app_version[0x7f0];

uint32_t __sample_buffer_size = 0x20000;
int32_t __sample_buffer [0x20000 / sizeof(char) ];

uint32_t __device_calib_size = 0x1000;
uint32_t __device_calib[0x1000 / sizeof(char)];


uint32_t  __app_nvdata_size = 0x000f7000;
uint32_t __app_nvdata[0x000f7000 / sizeof(char)];

uint32_t   __app_cfg_size = 0x2000;
uint32_t __app_cfg_shadow [0x2000 / sizeof(char)];

uint32_t  __eventlog_size = 0x3000;
uint32_t __event_log[0x3000 / sizeof(char)];

uint32_t __bootcfg_size = 0x1000;
uint32_t __bootcfg[0x1000 / sizeof(char)];

uint32_t __app_text_size = 0x76400;
int32_t __app_text[0x76400 / sizeof(char)];

uint32_t __ota_mgmnt_size = 0x1000;
uint32_t __ota_mgmnt_data[0x1000 / sizeof(char)];


uint32_t __loader_version_size = 0x7f0;
char __loader_version[0x000007f0];

uint32_t __loader_image_size = 0x7b000;

uint32_t __loader_image[0x00000400 / sizeof(char)];

uint32_t  __app_origin[0x00000400 / sizeof(char)];
uint32_t __app_image_size = 0x77000;

uint32_t __app_cfg [0x00002000 / sizeof(char)];
uint32_t __loader_origin[0x00000400 / sizeof(char)];

uint32_t __app_cfgsh[0x00000400 / sizeof(char)];












const char kstrOTACompleteSubTopic[] = "/OTAComplete";
const char kstrImagesManifestUpdateSubTopic[] = "/ImagesManifestUpdate";

#include "configIDEF.h"

SKF_ProtoBufFileVersion_t verifyRightVersionFile = SKF_ProtoBufFileVersion_t_PROTOBUF_FILE_VERSION_GPB_2_1;

#include "imageTypes.h"
const char sMK24_Loader[] = "MK24_Loader";
const char sMK24_Application[] = "MK24_Application";
const char sPMIC_Application[] = "PMIC_Application";
const char sPackage[] = "Package";

#include "ephemeris.h"
const char* const EpoErrorString[] = { EPO_STATUS };

#include "fsl_llwu_hal.h"

void LLWU_HAL_SetExternalInputPinMode(LLWU_Type* base,
    llwu_external_pin_modes_t pinMode,
    llwu_wakeup_pin_t pinNumber) {

}

/*!
 * @brief Enables/disables the internal module source.
 *
 * This function enables/disables the internal module source mode that is used
 * as a wake up source.
 *
 * @param base      Register base address of LLWU
 * @param moduleNumber  module number specified
 * @param enable        enable or disable setting
 */
void LLWU_HAL_SetInternalModuleCmd(LLWU_Type* base, llwu_wakeup_module_t moduleNumber, bool enable) {

}

/*!
 * @brief Gets the external wakeup source flag.
 *
 * This function checks the external pin flag to detect whether the MCU is
 * woke up by the specific pin.
 *
 * @param base      Register base address of LLWU
 * @param pinNumber     pin number specified
 * @return true if the specific pin is wake up source.
 */
bool LLWU_HAL_GetExternalPinWakeupFlag(LLWU_Type* base, llwu_wakeup_pin_t pinNumber) {
    return 0;

}

/*!
 * @brief Clears the external wakeup source flag.
 *
 * This function clears the external wakeup source flag for a specific pin.
 *
 * @param base      Register base address of LLWU
 * @param pinNumber     pin number specified
 */
void LLWU_HAL_ClearExternalPinWakeupFlag(LLWU_Type* base, llwu_wakeup_pin_t pinNumber) {
}

/*!
 * @brief Gets the internal module wakeup source flag.
 *
 * This function checks the internal module wake up flag to detect whether the MCU is
 * woke up by the specific internal module.
 *
 * @param base      Register base address of LLWU
 * @param moduleNumber  module number specified
 * @return true if the specific module is wake up source.
 */
bool LLWU_HAL_GetInternalModuleWakeupFlag(LLWU_Type* base, llwu_wakeup_module_t moduleNumber) {
    return 0;
}



/*!
 * @brief Sets the pin filter configuration.
 *
 * This function sets the pin filter configuration.
 *
 * @param base      Register base address of LLWU
 * @param filterNumber  filter number specified
 * @param pinFilterMode filter mode configuration
 */
void LLWU_HAL_SetPinFilterMode(LLWU_Type* base, uint32_t filterNumber,
    llwu_external_pin_filter_mode_t pinFilterMode) {

}

/*!
 * @brief Gets the filter detection flag.
 *
 * This function checks the filter detection flag to detect whether the external
 * pin selected by the specific filter is the wake up source.
 *
 * @param base      Register base address of LLWU
 * @param filterNumber  filter number specified
 * @return true if the the pin is wakeup source
 */
bool LLWU_HAL_GetFilterDetectFlag(LLWU_Type* base, uint32_t filterNumber) {

}

/*!
 * @brief Clears the filter detection flag.
 *
 * This function will clear the filter detection flag.
 *
 * @param base      Register base address of LLWU
 * @param filterNumber  filter number specified
 */
void LLWU_HAL_ClearFilterDetectFlag(LLWU_Type* base, uint32_t filterNumber) {

}


/*!
 * @brief Sets the reset pin mode.
 *
 * This function sets how the reset pin is used as a low leakage mode exit source.
 *
 * @param base         Register base address of LLWU
 * @param resetPinMode RESET pin mode defined in llwu_reset_pin_mode_t
 */
void LLWU_HAL_SetResetPinMode(LLWU_Type* base, llwu_reset_pin_mode_t resetPinMode) {

}

#include "fsl_smc_hal.h"
/*!
 * @brief Configures the power mode.
 *
 * This function configures the power mode base on configuration structure. If
 * it not possible to switch to the target mode directly, this function checks
 * internally and chooses the right path.
 *
 * @param base  Base address for current SMC instance.
 * @param powerModeConfig Power mode configuration structure smc_power_mode_config_t
 * @return SMC error code.
 */
smc_hal_error_code_t SMC_HAL_SetMode(SMC_Type* base,
    const smc_power_mode_config_t* powerModeConfig) {
    return 0;
}

/*!
 * @brief Gets the current power mode status.
 *
 * This function  returns the current power mode stat. Once application
 * switches the power mode, it should always check the stat to check whether it
 * runs into the specified mode or not. An application  should  check
 * this mode before switching to a different mode. The system  requires that
 * only certain modes can switch to other specific modes. See the
 * reference manual for details and the _power_mode_stat for information about
 * the power stat.
 *
 * @param base  Base address for current SMC instance.
 * @return Current power mode status.
 */
power_mode_stat_t SMC_HAL_GetStat(SMC_Type* base) {
    return 0;
}

#include "fsl_rcm_hal.h"
/*!
 * @brief Gets the reset source status.
 *
 * This function gets the current reset source status for a specified source.
 *
 * Example:
   @code
   uint32_t resetStatus;

   // To get all reset source statuses.
   resetStatus = RCM_HAL_GetSrcStatus(RCM, kRcmSrcAll);

   // To test whether MCU is reset by watchdog.
   resetStatus = RCM_HAL_GetSrcStatus(RCM, kRcmWatchDog);

   // To test multiple reset source.
   resetStatus = RCM_HAL_GetSrcStatus(RCM, kRcmWatchDog | kRcmSoftware);
   @endcode
 *
 * @param base     Register base address of RCM
 * @param statusMask Bit mask for the reset sources to get.
 * @return The reset source status.
 */
uint32_t RCM_HAL_GetSrcStatus(RCM_Type* base, uint32_t statusMask) {

    return 0;
}

int PMICprogram(const uint8_t* image, uint32_t size){
int rc = 0;
//int error_phase = 0;
//
//do
//{
//	rc = flash_decoder_open();
//	if (rc != ERROR_SUCCESS)
//	{
//		break;
//	}
//
//	error_phase += ERROR_COUNT;
//	rc = flash_decoder_write(0, image, size);
//	if (rc != ERROR_SUCCESS)
//	{
//		flash_decoder_close();
//		break;
//	}
//
//	error_phase += ERROR_COUNT;
//	rc = flash_decoder_close();
//	if (rc != ERROR_SUCCESS)
//	{
//		break;
//	}
//
//} while (0);
//
///*
// * We should reset the PMIC whether we succeeded or not
// */
//PIN_SWRST_PORT->PCR[PIN_SWRST_BIT] = PORT_PCR_MUX(1) |   /* GPIO */
//PORT_PCR_DSE_MASK; /* High drive strength */
//PIN_SWRST_GPIO->PSOR = 1 << PIN_SWRST_BIT;                      /* High level */
//PIN_SWRST_GPIO->PDDR |= 1 << PIN_SWRST_BIT;                      /* Output */
//
//vTaskDelay(10);
//PIN_SWRST_GPIO->PCOR = 1 << PIN_SWRST_BIT;                      /* High level */
//
//if (0 != rc)
//{
//	rc += error_phase;
//}
return rc;
}

#define U64   unsigned long long
typedef struct {
    U64(*pfGetTime)      (void);
    void (*pfSendTaskList) (void);
} SEGGER_SYSVIEW_OS_API;
static U64 _cbGetTime(void) {
    U64 Time;

    Time = xTaskGetTickCount();
    Time *= 1000;
    return Time;
}
static void _cbSendTaskList(void) {
}
// Callbacks provided to SYSTEMVIEW by FreeRTOS
const SEGGER_SYSVIEW_OS_API SYSVIEW_X_OS_TraceAPI = {
  _cbGetTime,
  _cbSendTaskList,
};


/**
 * @brief Q31 vector absolute value.
 * @param[in]       *pSrc points to the input buffer
 * @param[out]      *pDst points to the output buffer
 * @param[in]       blockSize number of samples in each vector
 * @return none.
 *
 * <b>Scaling and Overflow Behavior:</b>
 * \par
 * The function uses saturating arithmetic.
 * The Q31 value -1 (0x80000000) will be saturated to the maximum allowable positive value 0x7FFFFFFF.
 */

void arm_abs_q31(
    q31_t* pSrc,
    q31_t* pDst,
    uint32_t blockSize)
{
    uint32_t blkCnt;                               /* loop counter */
    q31_t in;                                      /* Input value */


    /* Run the below code for Cortex-M4 and Cortex-M3 */
    q31_t in1, in2, in3, in4;

    /*loop Unrolling */
    blkCnt = blockSize >> 2u;

    /* First part of the processing with loop unrolling.  Compute 4 outputs at a time.
     ** a second loop below computes the remaining 1 to 3 samples. */
    while (blkCnt > 0u)
    {
        /* C = |A| */
        /* Calculate absolute of input (if -1 then saturated to 0x7fffffff) and then store the results in the destination buffer. */
        in1 = *pSrc++;
        in2 = *pSrc++;
        in3 = *pSrc++;
        in4 = *pSrc++;

        *pDst++ = (in1 > 0) ? in1 : (q31_t)__QSUB(0, in1);
        *pDst++ = (in2 > 0) ? in2 : (q31_t)__QSUB(0, in2);
        *pDst++ = (in3 > 0) ? in3 : (q31_t)__QSUB(0, in3);
        *pDst++ = (in4 > 0) ? in4 : (q31_t)__QSUB(0, in4);

        /* Decrement the loop counter */
        blkCnt--;
    }

    /* If the blockSize is not a multiple of 4, compute any remaining output samples here.
     ** No loop unrolling is used. */
    blkCnt = blockSize % 0x4u;


    while (blkCnt > 0u)
    {
        /* C = |A| */
        /* Calculate absolute value of the input (if -1 then saturated to 0x7fffffff) and then store the results in the destination buffer. */
        in = *pSrc++;
        *pDst++ = (in > 0) ? in : ((in == INT32_MIN) ? INT32_MAX : -in);

        /* Decrement the loop counter */
        blkCnt--;
    }

}


void arm_fir_decimate_q31(
    const arm_fir_decimate_instance_q31* S,
    q31_t* pSrc,
    q31_t* pDst,
    uint32_t blockSize)
{
    q31_t* pState = S->pState;                     /* State pointer */
    q31_t* pCoeffs = S->pCoeffs;                   /* Coefficient pointer */
    q31_t* pStateCurnt;                            /* Points to the current sample of the state */
    q31_t x0, c0;                                  /* Temporary variables to hold state and coefficient values */
    q31_t* px;                                     /* Temporary pointers for state buffer */
    q31_t* pb;                                     /* Temporary pointers for coefficient buffer */
    q63_t sum0;                                    /* Accumulator */
    uint32_t numTaps = S->numTaps;                 /* Number of taps */
    uint32_t i, tapCnt, blkCnt, outBlockSize = blockSize / S->M;  /* Loop counters */



    /* Run the below code for Cortex-M4 and Cortex-M3 */

    /* S->pState buffer contains previous frame (numTaps - 1) samples */
    /* pStateCurnt points to the location where the new input data should be written */
    pStateCurnt = S->pState + (numTaps - 1u);

    /* Total number of output samples to be computed */
    blkCnt = outBlockSize;

    while (blkCnt > 0u)
    {
        /* Copy decimation factor number of new input samples into the state buffer */
        i = S->M;

        do
        {
            *pStateCurnt++ = *pSrc++;

        } while (--i);

        /* Set accumulator to zero */
        sum0 = 0;

        /* Initialize state pointer */
        px = pState;

        /* Initialize coeff pointer */
        pb = pCoeffs;

        /* Loop unrolling.  Process 4 taps at a time. */
        tapCnt = numTaps >> 2;

        /* Loop over the number of taps.  Unroll by a factor of 4.
         ** Repeat until we've computed numTaps-4 coefficients. */
        while (tapCnt > 0u)
        {
            /* Read the b[numTaps-1] coefficient */
            c0 = *(pb++);

            /* Read x[n-numTaps-1] sample */
            x0 = *(px++);

            /* Perform the multiply-accumulate */
            sum0 += (q63_t)x0 * c0;

            /* Read the b[numTaps-2] coefficient */
            c0 = *(pb++);

            /* Read x[n-numTaps-2] sample */
            x0 = *(px++);

            /* Perform the multiply-accumulate */
            sum0 += (q63_t)x0 * c0;

            /* Read the b[numTaps-3] coefficient */
            c0 = *(pb++);

            /* Read x[n-numTaps-3] sample */
            x0 = *(px++);

            /* Perform the multiply-accumulate */
            sum0 += (q63_t)x0 * c0;

            /* Read the b[numTaps-4] coefficient */
            c0 = *(pb++);

            /* Read x[n-numTaps-4] sample */
            x0 = *(px++);

            /* Perform the multiply-accumulate */
            sum0 += (q63_t)x0 * c0;

            /* Decrement the loop counter */
            tapCnt--;
        }

        /* If the filter length is not a multiple of 4, compute the remaining filter taps */
        tapCnt = numTaps % 0x4u;

        while (tapCnt > 0u)
        {
            /* Read coefficients */
            c0 = *(pb++);

            /* Fetch 1 state variable */
            x0 = *(px++);

            /* Perform the multiply-accumulate */
            sum0 += (q63_t)x0 * c0;

            /* Decrement the loop counter */
            tapCnt--;
        }

        /* Advance the state pointer by the decimation factor
         * to process the next group of decimation factor number samples */
        pState = pState + S->M;

        /* The result is in the accumulator, store in the destination buffer. */
        *pDst++ = (q31_t)(sum0 >> 31);

        /* Decrement the loop counter */
        blkCnt--;
    }

    /* Processing is complete.
     ** Now copy the last numTaps - 1 samples to the satrt of the state buffer.
     ** This prepares the state buffer for the next function call. */

     /* Points to the start of the state buffer */
    pStateCurnt = S->pState;

    i = (numTaps - 1u) >> 2u;

    /* copy data */
    while (i > 0u)
    {
        *pStateCurnt++ = *pState++;
        *pStateCurnt++ = *pState++;
        *pStateCurnt++ = *pState++;
        *pStateCurnt++ = *pState++;

        /* Decrement the loop counter */
        i--;
    }

    i = (numTaps - 1u) % 0x04u;

    /* copy data */
    while (i > 0u)
    {
        *pStateCurnt++ = *pState++;

        /* Decrement the loop counter */
        i--;
    }


}


/**
 * @brief  Initialization function for the Q31 FIR decimator.
 * @param[in,out] *S points to an instance of the Q31 FIR decimator structure.
 * @param[in] numTaps  number of coefficients in the filter.
 * @param[in] M  decimation factor.
 * @param[in] *pCoeffs points to the filter coefficients.
 * @param[in] *pState points to the state buffer.
 * @param[in] blockSize number of input samples to process per call.
 * @return    The function returns ARM_MATH_SUCCESS if initialization was successful or ARM_MATH_LENGTH_ERROR if
 * <code>blockSize</code> is not a multiple of <code>M</code>.
 *
 * <b>Description:</b>
 * \par
 * <code>pCoeffs</code> points to the array of filter coefficients stored in time reversed order:
 * <pre>
 *    {b[numTaps-1], b[numTaps-2], b[N-2], ..., b[1], b[0]}
 * </pre>
 * \par
 * <code>pState</code> points to the array of state variables.
 * <code>pState</code> is of length <code>numTaps+blockSize-1</code> words where <code>blockSize</code> is the number of input samples passed to <code>arm_fir_decimate_q31()</code>.
 * <code>M</code> is the decimation factor.
 */

arm_status arm_fir_decimate_init_q31(
    arm_fir_decimate_instance_q31* S,
    uint16_t numTaps,
    uint8_t M,
    q31_t* pCoeffs,
    q31_t* pState,
    uint32_t blockSize)
{
    arm_status status;

    /* The size of the input block must be a multiple of the decimation factor */
    if ((blockSize % M) != 0u)
    {
        /* Set status as ARM_MATH_LENGTH_ERROR */
        status = ARM_MATH_LENGTH_ERROR;
    }
    else
    {
        /* Assign filter taps */
        S->numTaps = numTaps;

        /* Assign coefficient pointer */
        S->pCoeffs = pCoeffs;

        /* Clear the state buffer.  The size is always (blockSize + numTaps - 1) */
        memset(pState, 0, (numTaps + (blockSize - 1)) * sizeof(q31_t));

        /* Assign state pointer */
        S->pState = pState;

        /* Assign Decimation factor */
        S->M = M;

        status = ARM_MATH_SUCCESS;
    }

    return (status);

}



#ifdef __cplusplus
}
#endif