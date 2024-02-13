#ifdef __cplusplus
extern "C" {
#endif

/*
 * PassRailAnalogCtrl.c
 *
 *  Created on: July 1, 2016
 *      Author: Bart Willemse
 * Description: Passenger-rail-specific analog signal chain hardware
 *              configuration functions.
 *
 */

#include "PassRailAnalogCtrl.h"
#include "PinDefs.h"
#include "PinConfig.h"
#include "Device.h"
#include "Resources.h"

//..............................................................................

/*
 * AnalogGainSelect
 *
 * @desc    Switches the analog hardware gain stage on or off.
 *          NOTE: The analog subsystem must be switched on before this
 *          function is called.
 *
 * @param   bGainOn: true to switch gain on, false to switch off.
 *
 * @returns -
 */
void AnalogGainSelect(bool bGainOn)
{
    if (bGainOn)
    {
        GPIO_DRV_ClearPinOutput(PREAMP_GAIN_OFF);
    }
    else
    {
        GPIO_DRV_SetPinOutput(PREAMP_GAIN_OFF);
    }
}

/*
 * AnalogFiltSelect
 *
 * @desc    Selects the analog hardware filtering.
 *          NOTE: The analog subsystem must be switched on before this
 *          function is called.
 *
 * @param   FiltSelect: One of AnalogFiltEnum. NOTE: Available options depend
 *          upon the board version.
 *
 * @returns true if FiltSelect was recognised, false otherwise
 */
bool AnalogFiltSelect(AnalogFiltEnum FiltSelect)
{
    bool bFiltSelectRecognised = true;

    //..........................................................................
    // Rev 2/3 board
    if ((Device_GetHardwareVersion() >= HW_PASSRAIL_REV3)  || Device_HasPMIC())
    {
        switch (FiltSelect)
        {
        case ANALOGFILT_RAW:
            GPIO_DRV_SetPinOutput(HP_SELECT);
            GPIO_DRV_ClearPinOutput(BANDPASS_SELECT);
            break;

        case ANALOGFILT_VIB:
            GPIO_DRV_ClearPinOutput(HP_SELECT);
            GPIO_DRV_ClearPinOutput(BANDPASS_SELECT);
            break;

        case ANALOGFILT_WFLATS:
            GPIO_DRV_SetPinOutput(BANDPASS_SELECT);
            break;

        default:
            bFiltSelectRecognised = false;
            break;
        }
    }
    //..........................................................................
    // Rev 1 board
    else if (Device_GetHardwareVersion() == HW_PASSRAIL_REV1)
    {
        switch (FiltSelect)
        {
        case ANALOGFILT_RAW:
            GPIO_DRV_SetPinOutput(HP_SELECT);
            break;

        case ANALOGFILT_VIB:
            GPIO_DRV_ClearPinOutput(HP_SELECT);
            break;

        default:
            bFiltSelectRecognised = false;
            break;
        }
    }

    //..........................................................................
    else
    {
        bFiltSelectRecognised = false;
    }

    return bFiltSelectRecognised;
}


/*
 * AnalogSelfTestSelect
 *
 *          Switches the analog self-test function on or off.
 *          NOTE: The analog subsystem must be switched on before this
 *          function is called.
 *
 * @param   bSelfTestOn: true to switch self-test on, false to switch off.
 *
 * @returns -
 */
void AnalogSelfTestSelect(bool bSelfTestOn)
{
    if (bSelfTestOn)
    {
        GPIO_DRV_SetPinOutput(VIB_SELF_TEST_EN);
    }
    else
    {
        GPIO_DRV_ClearPinOutput(VIB_SELF_TEST_EN);
    }
}




#ifdef __cplusplus
}
#endif