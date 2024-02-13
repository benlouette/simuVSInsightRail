#ifdef __cplusplus
extern "C" {
#endif

/*
 * PassRailMeasure.c
 *
 *  Created on: 11 July 2016
 *      Author: Bart Willemse
 * Description: Passenger-rail-specific measurement definitions and
 *              functionality.
 */

#include <stdint.h>
#include <stdbool.h>
#include "freeRTOS.h"
#include "task.h"

#include "Resources.h"
#include "PassRailMeasure.h"
#include "PassRailAnalogCtrl.h"

#ifdef PASSRAIL_DSP_NEW
#include "PassRailDSP.h"
#else
#include "PassRailDSP_MVP.h"
#endif

#include "PowerControl.h"
#include "Device.h"

//..............................................................................

#ifdef PASSRAIL_DSP_NEW

// Define the ADC sampling rates (i.e. the input sampling rates) required for
// each signal chain type
#define ADC_SPS_RAWACCEL    (25600)
#define ADC_SPS_VIB         (51200)
#define ADC_SPS_WFLATS      (10240)

#else  // NOT PASSRAIL_DSP_NEW

// Define the ADC sampling rates (i.e. the input sampling rates) required for
// each signal chain type
// TODO: PLACEHOLDER VALUES ONLY FOR NOW - still to be finalised
#define ADC_SPS_RAWACCEL    (25600)
#define ADC_SPS_VIB         (40960)
// Wheel-flat raw ADC sampling rate can be much lower, because analog
// low-pass wheel-flat filtering rolls off at 400Hz
#define ADC_SPS_WFLATS      (10240)

#endif  // NOT PASSRAIL_DSP_NEW

//..............................................................................

// Note on DSP settling times: The DspAdcSettlingSamples values are the number
// of raw ADC samples required for each DSP chain settling time. They were
// determined by injecting a low-frequency square wave to trigger each filter
// chain's impulse response on each square-wave edge. The number of OUTPUT
// samples required for DSP settling was noted, and then multiplied by the
// decimation factor and adding a safety margin. See the test documentation
// and impulse response graphs in the test spreadsheets.

struct
{
    tMeasId MeasId;
    AnalogFiltEnum AnalogFiltID;
    uint32_t ADCSamplesPerSec;
    EnveloperEnum EnveloperID;
    DecimChainEnum DecimChainID;
    uint32_t DspAdcSettlingSamples;
} MeasTable[] =
{
    // For EMC testing
    // MeasId                   AnalogFiltID       ADCSamplesPerSec  EnveloperID   DecimChainID            DspAdcSettlingSamples
	{MEASID_EMC_RAWVIB_1024SPS, ANALOGFILT_RAW,    1024,             ENV_NONE,     DECIMCHAIN_NONE,        0},

#ifdef PASSRAIL_DSP_NEW
	// Raw acceleration
    // MeasId                  AnalogFiltID       ADCSamplesPerSec  EnveloperID   DecimChainID            DspAdcSettlingSamples
    {MEASID_RAWACCEL_25600SPS, ANALOGFILT_RAW,    ADC_SPS_RAWACCEL, ENV_NONE,     DECIMCHAIN_NONE,        0},

    //********* TODO: Select suitable settling time values below (using placeholder values of 1000 ADC samples for now)

    // Vibration
    {MEASID_VIB_1280SPS,       ANALOGFILT_VIB,    ADC_SPS_VIB,      ENV_VIB,      DECIMCHAIN_VIB_1280,    2000},
    {MEASID_VIB_2560SPS,       ANALOGFILT_VIB,    ADC_SPS_VIB,      ENV_VIB,      DECIMCHAIN_VIB_2560,    1000},
    {MEASID_VIB_5120SPS,       ANALOGFILT_VIB,    ADC_SPS_VIB,      ENV_VIB,      DECIMCHAIN_VIB_5120,    1000},

    // Wheel-flats (N.B. not available on rev 1 board)
    {MEASID_WFLATS_256SPS,     ANALOGFILT_WFLATS, ADC_SPS_WFLATS,   ENV_WFLATS,   DECIMCHAIN_WFLATS_256,  2000},
    {MEASID_WFLATS_512SPS,     ANALOGFILT_WFLATS, ADC_SPS_WFLATS,   ENV_WFLATS,   DECIMCHAIN_WFLATS_512,  1000},
    {MEASID_WFLATS_1280SPS,    ANALOGFILT_WFLATS, ADC_SPS_WFLATS,   ENV_WFLATS,   DECIMCHAIN_WFLATS_1280, 1000},

#else // NOT PASSRAIL_DSP_NEW

	// Minimum Viable Product measurement IDs
    // MeasId                  AnalogFilt         ADCSamplesPerSec  Enveloper     Decimation                DspAdcSettlingSamples
    {MEASID_RAWACCEL_25600SPS, ANALOGFILT_RAW,    ADC_SPS_RAWACCEL, ENV_NONE,     DECIMCHAIN_NONE,               0},
    {MEASID_VIB_2560SPS,       ANALOGFILT_VIB,    ADC_SPS_VIB,      ENV_VIB,      DECIMCHAIN_VIB_FACTOR16_MVP,   1000},
    {MEASID_WFLATS_1280SPS,    ANALOGFILT_WFLATS, ADC_SPS_WFLATS,   ENV_WFLATS,   DECIMCHAIN_WFLATS_FACTOR8_MVP, 1000},

#endif // NOT PASSRAIL_DSP_NEW
};

//..............................................................................


/*
 * PassRailMeasure_GetAdcSpsForMeasId
 *
 * @desc    Gets the ADC samples/sec value corresponding to the measurement ID.
 *
 * @param   MeasId: Measurement ID
 *
 * @returns Non-zero ADC samples/sec if measurement ID exists in table,
 *          zero otherwise
 */
uint32_t PassRailMeasure_GetAdcSpsForMeasId(tMeasId MeasId)
{
    uint8_t i;

    for (i = 0; i < (sizeof(MeasTable) / sizeof(MeasTable[0])); i++)
    {
        if (MeasTable[i].MeasId == MeasId)
        {
            return MeasTable[i].ADCSamplesPerSec;
        }
    }

    return 0;
}

/*
 * PassRailMeasure_GetAnalogFiltForMeasId
 *
 * @desc    Gets the analog filter setting corresponding to the measurement ID.
 *
 * @param   MeasId: Measurement ID
 *
 * @returns One of AnalogFiltEnum (or ANALOGFILT_UNDEFINED if couldn't find
 *          measurement ID)
 */
AnalogFiltEnum PassRailMeasure_GetAnalogFiltForMeasId(tMeasId MeasId)
{
    uint8_t i;

    for (i = 0; i < (sizeof(MeasTable) / sizeof(MeasTable[0])); i++)
    {
        if (MeasTable[i].MeasId == MeasId)
        {
            return MeasTable[i].AnalogFiltID;
        }
    }

    return ANALOGFILT_UNDEFINED;
}

/*
 * PassRailMeasure_PrepareSampling
 *
 * @desc    Prepares for sampling for specified measurement ID - powers on
 *          analog subsystem, sets up required analog hardware gain and
 *          filtering, and initialises DSP chain. Returns the ADC sampling
 *          rate and number of ADC DSP settling samples required.
 *
 * @param   MeasID: Measurement ID
 * @param   *pAdcSamplesPerSecRet: Filled with return value of ADC samples/sec
 *          required
 * @param   *pDspSettlingSamplesRet: Filled with return value of # of DSP
 *          settling samples required
 *
 * @returns true if successful, false otherwise
 */
bool PassRailMeasure_PrepareSampling(tMeasId MeasId,
                                     uint32_t *pAdcSamplesPerSecRet,
                                     uint32_t *pDspAdcSettlingSamplesRet)
{
    uint8_t i;
    uint8_t MeasTableIndex;
    bool bOK;

    bOK = false;
    for (i = 0; i < (sizeof(MeasTable) / sizeof(MeasTable[0])); i++)
    {
        if (MeasTable[i].MeasId == MeasId)
        {
            bOK = true;
            MeasTableIndex = i;
            break;
        }
    }

    *pAdcSamplesPerSecRet = 0;
    *pDspAdcSettlingSamplesRet = 0;
    if (bOK)
    {
        bOK = false;

        // Switch analog power on
        powerAnalogOn();
    	// REV4 hardware requires double the gain
    	AnalogGainSelect(Device_GetHardwareVersion() >= HW_PASSRAIL_REV4);
        if (AnalogFiltSelect(MeasTable[MeasTableIndex].AnalogFiltID))
        {
            // Op-amp signal chain settling delay - allow 500ms (according to HW engineers)
            vTaskDelay(500 / portTICK_PERIOD_MS);

            *pAdcSamplesPerSecRet = MeasTable[MeasTableIndex].ADCSamplesPerSec;
            *pDspAdcSettlingSamplesRet = MeasTable[MeasTableIndex].DspAdcSettlingSamples;

            bOK = PassRailDsp_Init(MeasTable[MeasTableIndex].EnveloperID,
                                   MeasTable[MeasTableIndex].DecimChainID);
        }
    }

    return bOK;
}

/*
 * PassRailMeasure_PostSampling
 *
 * @desc    Performs all required post-sampling stuff for passenger rail board -
 *          powers off analog subsystem etc.
 *          TODO: Will DSP post-processing eventually be called from this
 *          function?
 *
 * @param   -
 *
 * @returns -
 */
void PassRailMeasure_PostSampling(void)
{
    powerAnalogOff();
}



#ifdef __cplusplus
}
#endif