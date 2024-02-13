#ifdef __cplusplus
extern "C" {
#endif

/*
 * Copyright (c) 2013 - 2014, Freescale Semiconductor, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * o Redistributions of source code must retain the above copyright notice, this list
 *   of conditions and the following disclaimer.
 *
 * o Redistributions in binary form must reproduce the above copyright notice, this
 *   list of conditions and the following disclaimer in the documentation and/or
 *   other materials provided with the distribution.
 *
 * o Neither the name of Freescale Semiconductor, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include "fsl_adc16_driver.h"
#include "fsl_adc16_hal.h"
#include "fsl_clock_manager.h"
#include "fsl_interrupt_manager.h"
#if FSL_FEATURE_SOC_ADC16_COUNT

#if FSL_FEATURE_ADC16_HAS_CALIBRATION

/*FUNCTION*********************************************************************
 *
 * Function Name : ADC16_DRV_GetAutoCalibrationParam
 * Description   : Execute the the process of auto calibration and fetch
 * the calibration parameters that would be kept in "adc16_calibration_param_t"
 * type variable. When executing the process of auto calibration, the ADC
 * has been configured internally to work in the situation with highest
 * precision. Since this API may be called before the initialization, it enables
 * the ADC clock internally.
 *
 *END*************************************************************************/
adc16_status_t ADC16_DRV_GetAutoCalibrationParam(uint32_t instance, adc16_calibration_param_t *paramPtr)
{
    assert(instance < ADC_INSTANCE_COUNT);

    ADC_Type * base = g_adcBase[instance];
    volatile uint16_t dummy;

    /* Launch the auto-calibration. */
    ADC16_HAL_SetAutoCalibrationCmd(base, true); /* Launch auto calibration. */
    while ( !ADC16_HAL_GetChnConvCompletedFlag(base, 0U) )
    {
        if ( ADC16_HAL_GetAutoCalibrationFailedFlag(base) )
        {
            ADC16_HAL_SetAutoCalibrationCmd(base, false);
            return kStatus_ADC16_Failed;
        }
    }
    /* Read parameters that generated by auto calibration. */
#if FSL_FEATURE_ADC16_HAS_OFFSET_CORRECTION
    paramPtr->offsetValue = ADC16_HAL_GetOffsetValue(base);
#endif /* FSL_FEATURE_ADC16_HAS_OFFSET_CORRECTION */
    paramPtr->plusSideGainValue = ADC16_HAL_GetAutoPlusSideGainValue(base);
#if FSL_FEATURE_ADC16_HAS_DIFF_MODE
    paramPtr->minusSideGainValue = ADC16_HAL_GetAutoMinusSideGainValue(base);
#endif /* FSL_FEATURE_ADC16_HAS_DIFF_MODE */
    dummy = ADC16_HAL_GetChnConvValue(base, 0U); /* Clear the flags. */
    dummy = dummy; /* Avoid the warning. */

    /* Terminal the auot-calibration. */
    ADC16_HAL_SetAutoCalibrationCmd(base, false);

    return kStatus_ADC16_Success;
}

/*FUNCTION*********************************************************************
 *
 * Function Name : ADC16_DRV_SetCalibrationParam
 * Description   : Set the calibration parameters for ADC module. These
 * parameters can be fetched by launching the process of auto calibration
 * or to use some predefined values that filled in the structure of
 * "adc16_calibration_param_t". For higher precision,  it is recommended to
 * execute the process of calibration before initializing the ADC module.
 * Since this API may be called before the initialization, it enables the ADC
 * clock internally.
 *
 *END*************************************************************************/
adc16_status_t ADC16_DRV_SetCalibrationParam(uint32_t instance, const adc16_calibration_param_t *paramPtr)
{
    assert(instance < ADC_INSTANCE_COUNT);
    ADC_Type * base = g_adcBase[instance];

    if (!paramPtr)
    {
        return kStatus_ADC16_InvalidArgument;
    }

#if FSL_FEATURE_ADC16_HAS_OFFSET_CORRECTION
    ADC16_HAL_SetOffsetValue(base, paramPtr->offsetValue);
#endif /* FSL_FEATURE_ADC16_HAS_OFFSET_CORRECTION */
    ADC16_HAL_SetPlusSideGainValue(base, paramPtr->plusSideGainValue);
#if FSL_FEATURE_ADC16_HAS_DIFF_MODE
    ADC16_HAL_SetMinusSideGainValue(base, paramPtr->minusSideGainValue);
#endif /* FSL_FEATURE_ADC16_HAS_DIFF_MODE */

    return kStatus_ADC16_Success;
}

#endif /* FSL_FEATURE_ADC16_HAS_CALIBRATION */

/*FUNCTION*********************************************************************
 *
 * Function Name : ADC16_DRV_StructInitUserConfigDefault
 * Description   : Fills the initial user configuration defaultly for a one-time
 * trigger mode. Calling the initialization function with the filled parameter
 * configures the ADC module work as one-time trigger mode.
 *
 *END*************************************************************************/
adc16_status_t ADC16_DRV_StructInitUserConfigDefault(adc16_converter_config_t *userConfigPtr)
{
    if ( !userConfigPtr )
    {
        return kStatus_ADC16_InvalidArgument;
    }

    /* Special configuration for highest accuracy. */
    userConfigPtr->lowPowerEnable = true;
    userConfigPtr->clkDividerMode = kAdc16ClkDividerOf8;
    userConfigPtr->longSampleTimeEnable = true;
    userConfigPtr->resolution = kAdc16ResolutionBitOfSingleEndAs12;
    userConfigPtr->clkSrc = kAdc16ClkSrcOfAsynClk;
    userConfigPtr->asyncClkEnable = true;
    userConfigPtr->highSpeedEnable = false;
    userConfigPtr->longSampleCycleMode = kAdc16LongSampleCycleOf24;
    userConfigPtr->hwTriggerEnable = false;
    userConfigPtr->refVoltSrc = kAdc16RefVoltSrcOfVref;
    userConfigPtr->continuousConvEnable = false;
#if FSL_FEATURE_ADC16_HAS_DMA
    userConfigPtr->dmaEnable = false;
#endif /* FSL_FEATURE_ADC16_HAS_DMA */

    return kStatus_ADC16_Success;
}

/*FUNCTION*********************************************************************
 *
 * Function Name : ADC16_DRV_Init
 * Description   : Initialize the comparator in ADC module. No mater if the
 * calibration has been done for the device, this API with initial configuration
 * should be called before any other operations to the ADC module. In fact,
 * these initial configure are mainly for the comparator itself. For advanced
 * feature, responding APIs would be called after this function.
 *
 *END*************************************************************************/
adc16_status_t ADC16_DRV_Init(uint32_t instance, const adc16_converter_config_t *userConfigPtr)
{
    assert(instance < ADC_INSTANCE_COUNT);
    ADC_Type * base = g_adcBase[instance];

    if (!userConfigPtr)
    {
        return kStatus_ADC16_InvalidArgument;
    }
    /* Enable clock for ADC. */
    CLOCK_SYS_EnableAdcClock(instance);

    /* Reset all the register to a known state. */
    ADC16_HAL_Init(base);
    ADC16_HAL_ConfigConverter(base, userConfigPtr);

    /* Enable ADC interrupt in NVIC level.*/
    INT_SYS_EnableIRQ(g_adcIrqId[instance] );

    return kStatus_ADC16_Success;
}

/*FUNCTION*********************************************************************
 *
 * Function Name : ADC16_DRV_Deinit
 * Description   : De-initialize the comparator in ADC module. It will gate
 * the clock to ADC module. When ADC is no long used in application, calling
 * this API will shut down the device to reduce power consumption.
 *
 *END*************************************************************************/
adc16_status_t ADC16_DRV_Deinit(uint32_t instance)
{
    assert(instance < ADC_INSTANCE_COUNT);
    ADC_Type * base = g_adcBase[instance];

    /* Disable ADC interrupt in NVIC level. */
    INT_SYS_DisableIRQ( g_adcIrqId[instance] );

    ADC16_HAL_Init(base);

    /* Disable clock for ADC. */
    CLOCK_SYS_DisableAdcClock(instance);

    return kStatus_ADC16_Success;
}

/*FUNCTION*********************************************************************
 *
 * Function Name : ADC16_DRV_ConfigHwCompare
 * Description   : Initialize the feature of long sample mode in ADC
 * module. This API would enable the feature of hardware compare with
 * indicated configuration. Launch the hardware compare would make the
 * conversion result in predefined range can be only accepted. Values out of
 * range would be ignored during conversion.
 *
 *END*************************************************************************/
adc16_status_t ADC16_DRV_ConfigHwCompare(uint32_t instance, const adc16_hw_cmp_config_t *configPtr)
{
    assert(instance < ADC_INSTANCE_COUNT);
    ADC_Type * base = g_adcBase[instance];

    if (!configPtr)
    {
        return kStatus_ADC16_InvalidArgument;
    }

    ADC16_HAL_ConfigHwCompare(base, configPtr);

    return kStatus_ADC16_Success;
}

#if FSL_FEATURE_ADC16_HAS_HW_AVERAGE

/*FUNCTION*********************************************************************
 *
 * Function Name : ADC16_DRV_ConfigHwAverage
 * Description   : Configure the feature of hardware average in ADC module.
 *
 *END*************************************************************************/
adc16_status_t ADC16_DRV_ConfigHwAverage(uint32_t instance, const adc16_hw_average_config_t *configPtr)
{
    assert(instance < ADC_INSTANCE_COUNT);
    ADC_Type * base = g_adcBase[instance];

    if (!configPtr)
    {
        return kStatus_ADC16_InvalidArgument;
    }

    ADC16_HAL_ConfigHwAverage(base, configPtr);

    return kStatus_ADC16_Success;
}

#endif /* FSL_FEATURE_ADC16_HAS_HW_AVERAGE */

#if FSL_FEATURE_ADC16_HAS_PGA

/*FUNCTION*********************************************************************
 *
 * Function Name : ADC16_DRV_ConfigPga
 * Description   : Configure the feature of Programmable Gain Amplifier
 * (PGA) in ADC module.
 *
 *END*************************************************************************/
adc16_status_t ADC16_DRV_ConfigPga(uint32_t instance, const adc16_pga_config_t *configPtr)
{

    assert(instance < ADC_INSTANCE_COUNT);
    ADC_Type * base = g_adcBase[instance];

    if ( !configPtr)
    {
        return kStatus_ADC16_InvalidArgument;
    }

    ADC16_HAL_ConfigPga(base, configPtr);

    return kStatus_ADC16_Success;
}

#endif /* FSL_FEATURE_ADC16_HAS_PGA */

#if FSL_FEATURE_ADC16_HAS_MUX_SELECT
/*FUNCTION*********************************************************************
 *
 * Function Name : ADC16_DRV_SetChnMux
 * Description   : Switch the channel mux.
 *
 *END*************************************************************************/
void ADC16_DRV_SetChnMux(uint32_t instance, adc16_chn_mux_mode_t chnMuxMode)
{
    assert(instance < ADC_INSTANCE_COUNT);
    ADC_Type * base = g_adcBase[instance];

    ADC16_HAL_SetChnMuxMode(base, chnMuxMode);
}
#endif /* FSL_FEATURE_ADC16_HAS_MUX_SELECT */

/*FUNCTION*********************************************************************
 *
 * Function Name : ADC16_DRV_ConfigConvChn
 * Description   : Configure the conversion channel. When ADC module has
 * been initialized by enabling software trigger (disable hardware trigger),
 * calling this API will trigger the conversion.
 *
 *END*************************************************************************/
adc16_status_t ADC16_DRV_ConfigConvChn(uint32_t instance,
    uint32_t chnGroup, const adc16_chn_config_t *configPtr)
{
    assert(instance < ADC_INSTANCE_COUNT);
    ADC_Type * base = g_adcBase[instance];

    if (!configPtr)
    {
        return kStatus_ADC16_InvalidArgument;
    }

    ADC16_HAL_ConfigChn(base, chnGroup, configPtr);

    return kStatus_ADC16_Success;
}

/*FUNCTION*********************************************************************
 *
 * Function Name : ADC16_DRV_WaitConvDone
 * Description   : Wait the latest conversion for its complete. When
 * trigger the conversion by configuring the available channel, the converter
 * would launch to work, this API should be called to wait for the conversion's
 * complete when no interrupt or DMA mode is used for ADC module. After the
 * waiting, the available data of conversion result could be fetched then.
 * The complete flag would not be cleared until the result data would be read.
 *
 *END*************************************************************************/
void ADC16_DRV_WaitConvDone(uint32_t instance, uint32_t chnGroup)
{
    assert(instance < ADC_INSTANCE_COUNT);
    ADC_Type * base = g_adcBase[instance];

    while ( !ADC16_HAL_GetChnConvCompletedFlag(base, chnGroup) )
    {}
}

/*FUNCTION*********************************************************************
 *
 * Function Name : ADC16_DRV_PauseConv
 * Description   : Pause current conversion setting by software.
 *
 *END*************************************************************************/
void ADC16_DRV_PauseConv(uint32_t instance, uint32_t chnGroup)
{
    assert(instance < ADC_INSTANCE_COUNT);
    adc16_chn_config_t configStruct;

    configStruct.chnIdx = kAdc16Chn31;
    configStruct.convCompletedIntEnable = false;
#if FSL_FEATURE_ADC16_HAS_DIFF_MODE
    configStruct.diffConvEnable = false;
#endif
    ADC16_DRV_ConfigConvChn(instance, chnGroup, &configStruct);
}

/*FUNCTION*********************************************************************
 *
 * Function Name : ADC16_DRV_GetConvValueRAW
 * Description   : Get the conversion value from the ADC module.
 *
 *END*************************************************************************/
uint16_t ADC16_DRV_GetConvValueRAW(uint32_t instance, uint32_t chnGroup)
{

    assert(instance < ADC_INSTANCE_COUNT);
    ADC_Type * base = g_adcBase[instance];

    return ADC16_HAL_GetChnConvValue(base, chnGroup);
}

/*FUNCTION*********************************************************************
 *
 * Function Name : ADC16_DRV_GetConvValueSigned
 * Description   : Get the latest conversion value with signed.
 *
 *END*************************************************************************/
int16_t ADC16_DRV_GetConvValueSigned(uint32_t instance, uint32_t chnGroup)
{
    return (int16_t)ADC16_DRV_GetConvValueRAW(instance, chnGroup);
}

/*FUNCTION*********************************************************************
 *
 * Function Name : ADC16_DRV_GetConvFlag
 * Description   : Get the status of event of ADC converter.
 * If the event is asserted, it will return "true", otherwise will be "false".
 *
 *END*************************************************************************/
bool ADC16_DRV_GetConvFlag(uint32_t instance, adc16_flag_t flag)
{

    assert(instance < ADC_INSTANCE_COUNT);
    ADC_Type * base = g_adcBase[instance];

    bool bRet = false;
    switch (flag)
    {
    case kAdcConvActiveFlag:
        bRet = ADC16_HAL_GetConvActiveFlag(base);
        break;
#if FSL_FEATURE_ADC16_HAS_CALIBRATION
    case kAdcCalibrationFailedFlag:
        bRet = ADC16_HAL_GetAutoCalibrationFailedFlag(base);
        break;
    case kAdcCalibrationActiveFlag:
        bRet = ADC16_HAL_GetAutoCalibrationActiveFlag(base);
        break;
#endif /* FSL_FEATURE_ADC16_HAS_CALIBRATION */
    default:
        break;
    }
    return bRet;

}

/*FUNCTION*********************************************************************
 *
 * Function Name : ADC16_DRV_GetChnFlag
 * Description   : Get the status of event of each ADC channel group.
 * If the event is asserted, it will return "true", otherwise will be "false".
 *
 *END*************************************************************************/
bool ADC16_DRV_GetChnFlag(uint32_t instance, uint32_t chnGroup, adc16_flag_t flag)
{
    assert(instance < ADC_INSTANCE_COUNT);
    ADC_Type * base = g_adcBase[instance];

    bool bRet = false;
    switch (flag)
    {
    case kAdcChnConvCompleteFlag:
        bRet = ADC16_HAL_GetChnConvCompletedFlag(base, chnGroup);
        break;
    default:
        break;
    }
    return bRet;

}
#endif

/******************************************************************************
 * EOF
 *****************************************************************************/



#ifdef __cplusplus
}
#endif