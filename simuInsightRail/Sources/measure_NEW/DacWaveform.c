#ifdef __cplusplus
extern "C" {
#endif

/*
 * DacWaveform.c
 *
 *  Created on: 17 Aug 2016
 *      Author: Jayant Rampuria
 * Description: DAC generation of sine waveforms for Analog self-test.
 */

#include <stdint.h>
#include <stdbool.h>
#include "DacWaveform.h"
#include "fsl_dac_driver.h"
#include "Resources.h"
#include "arm_math.h"				// Included for generating sin wave at DAC o/p.
#include "fsl_pdb_driver.h"			// PDB driver for the HW trigger.
#include "PassRailAnalogCtrl.h"		// For selecting the DAC O/P at XTAL+


#define DAC_MAX_COUNTS					(4096)
// Zero Dc at .6V.
#define ZERO_DC_OFFSET_COUNT			(2047)
// 12bit DAC, Vref = 1.2V
#define DAC_COUNTS_PER_VOLT				(DAC_MAX_COUNTS / 1.2f)
// Offset in Radians corresponding to 96 degrees to make the wave symmetric.
#define OFFSET_IN_RADIANS				(1.67552f)
#define NUM_OF_DAC_SAMPLES_PER_PERIOD	(30)
// Initial Vpeak DAC count corresponding to 100mV Peak.
#define INITIAL_VPEAK_OFFSET_DAC_COUNT	(342)
#define PDB_CLK_PRESCALER				(kPdbClkPreDivBy32)
#define DEFAULT_OFFSET_MILLI_VOLT		(0)
#define DEFAULT_SIN_WAVE_FREQ			(390)

static bool ConfigureDAC();
static void ConfigurePDBAsHwTriggerInpForDAC();
static void GetDACOutputBufferValues(uint16_t *pnBuf, uint8_t nLen, uint16_t nOffset_mV);
static bool g_bDacSystemEn = false;
//..............................................................................

/*
 * Dac_Enable
 *
 * @desc    This function is available for the App. code to Start the DAC system
 * 			It configures the DAC for the specified offset and frequency.
 *
 *          IMPORTANT:
 *            - The analog subsystem must be powered on before this function
 *              is called
 *            - Assumes that the Kinetis internal 1.2V voltage reference
 *              is already on before this function is called.
 *
 * @return	void
 */
bool Dac_Enable()
{
	Dac_On(DEFAULT_OFFSET_MILLI_VOLT);
	Dac_SetSwTrigger(DEFAULT_SIN_WAVE_FREQ);
	return g_bDacSystemEn;
}

/*
 * Dac_Disable
 *
 * @desc    This function is available for the App. code to Stop the DAC system
 * 			It configures the DAC for the specified offset and frequency.
 *
 *          IMPORTANT:
 *            - The analog subsystem must be powered on before this function
 *              is called
 *            - Assumes that the Kinetis internal 1.2V voltage reference
 *              is already on before this function is called.
 *
 * @return	void
 */
void Dac_Disable()
{
	Dac_Off();
}


/*
 * Dac_On
 *
 * @desc    Turns ON the DAC0 and sets its output to .7V. This function could be
 *			invoked via the cli "dac-on" command.
 *          IMPORTANT:
 *            - The analog subsystem must be powered on before this function
 *              is called
 *            - Assumes that the Kinetis internal 1.2V voltage reference
 *              is already on before this function is called.
 *
 * @param	nOffset_mV(uint16_t) - Specifies the offset value to add to the
 * 			DAC O/P voltage. Used for testing the saturation level.
 *
 * @return	void
 */
void Dac_On(uint16_t nOffset_mV)
{
    uint16_t nOutDataBuf[DAC_DATL_COUNT] = 	{0};

    if(ConfigureDAC())
    {
		// Get the values to write to the DAC o/p buffer.
		GetDACOutputBufferValues(&nOutDataBuf[0], ROW_COUNT(nOutDataBuf), nOffset_mV);

		// Setup the values corresponding to the requested Offset into the DAC o/p buffer.
		if(DAC_DRV_SetBuffValue(DAC0_IDX, 0, ROW_COUNT(nOutDataBuf), &nOutDataBuf[0]) == kStatus_DAC_Success)
		{
			g_bDacSystemEn = true;
		}
		else
		{
			// TODO: Log an error if the DAC init fails
			g_bDacSystemEn = false;
		}
    }
}

/*
 * Dac_Off
 *
 * @desc    Disables DAC0 and also switches off the PDB system. This function
 * 			could be invoked via the cli "dac-off" command.
 *          NOTE: Does NOT switch off the Kinetis internal voltage reference.
 * @return void
 */
void Dac_Off(void)
{
	if(g_bDacSystemEn)
	{
		DAC_DRV_Deinit(DAC0_IDX);
		PDB_DRV_Deinit(PDB0_IDX);
		g_bDacSystemEn = false;
	}
}

/*
 * IsDacSystemEn
 *
 * @desc	Query the status of the DAC system initialisation.
 *
 * @return true - If the Dac system is enabled,
 * 			false otherwise.
 */
bool IsDacSystemEn()
{
    return g_bDacSystemEn;
}

/*
 * IsRequestOffsetValid
 *
 * @desc	Checks if the requested offset / scaling is valid.
 *
 * @param	pnOffset_mV (uint16_t*) - pointer to the requested offset/scaling.
 *
 * @return  true - if the requested offset can be applied to the DAC o/p,
 * 			false - otherwise.
 */
bool IsRequestOffsetValid(uint16_t *pnOffset_mV)
{
	bool bRetVal = false;
	float nMaxDacCountsAboveZeroDCOffset = (DAC_MAX_COUNTS -
											ZERO_DC_OFFSET_COUNT -
											INITIAL_VPEAK_OFFSET_DAC_COUNT);
	float fMaxValidOffset_mV = (nMaxDacCountsAboveZeroDCOffset / DAC_COUNTS_PER_VOLT) * 1000.0f;

	if(fMaxValidOffset_mV >= *pnOffset_mV)
	{
		bRetVal = true;
	}
	else
	{
		*pnOffset_mV = (uint16_t)(fMaxValidOffset_mV);
	}

	return bRetVal;
}

/*
 * Dac_SetSwTrigger
 *
 * @desc	Trigger for the DAC system to start generating the Sine wave at the
 * 			requested frequency. This is used for the Self Test.
 *
 * @param	nExpOutFreq_Hz(uint16_t) - Expected O/P frequency(in Hz) of the sine
 * 			wave generated from the DAC O/P.
 *
 * @return  void.
 */
void Dac_SetSwTrigger(uint16_t nExpOutFreq_Hz)
{
	uint32_t nDivisor = (nExpOutFreq_Hz * NUM_OF_DAC_SAMPLES_PER_PERIOD);
	uint32_t nPdbCounts = DEFAULT_SYSTEM_CLOCK / (nDivisor * (1 << PDB_CLK_PRESCALER));

    if(nPdbCounts > 0)
    {
    	PDB_DRV_SetTimerModulusValue(PDB0_IDX, nPdbCounts);
    	PDB_DRV_SetDacIntervalValue(PDB0_IDX, 0, nPdbCounts);
    	PDB_DRV_LoadValuesCmd(PDB0_IDX);
    	PDB_DRV_SoftTriggerCmd(PDB0_IDX);
    }
}

/*
 * ConfigureDAC
 *
 * @desc	Configures the DAC to produce a Sine wave ot the DAC o/p. The
 * 			reference selected is at 1.2V. Swing mode is used to generate the
 * 			sine wave and a H/W trigger is used to produce the wave at the
 * 			specified frequency.
 *
 * @return  void.
 */
static bool ConfigureDAC()
{
	bool bRetVal = false;
    dac_status_t DacStatus = kStatus_DAC_Failed;
    dac_converter_config_t stcDacConfig;
    dac_buffer_config_t stcDacBufConfig;

    // Select internal 1.2V voltage reference - see K24F Ref Manual
	// section 3.7.3.4 12-bit DAC Reference
	stcDacConfig.dacRefVoltSrc = kDacRefVoltSrcOfVref1;
	// Select full-power mode to minimise chance of problems, e.g.
	// cross-interference between internal ADC and DAC
	stcDacConfig.lowPowerEnable = false;
	// Init. the DAC module.
	DacStatus = DAC_DRV_Init(DAC0_IDX, &stcDacConfig);

	if (DacStatus == kStatus_DAC_Success)
	{
		// Resets the read pointer index & the value.
		DAC_DRV_Output(DAC0_IDX, 0);
		//Select the Swing Mode, Buffer Mode for DAC operation.
		stcDacBufConfig.buffWorkMode = kDacBuffWorkAsSwingMode;
		stcDacBufConfig.bufferEnable = true;								// Enable buffer function for swing mode.
		stcDacBufConfig.dmaEnable = false;
		stcDacBufConfig.idxStartIntEnable = false;
		stcDacBufConfig.idxUpperIntEnable = false;
		stcDacBufConfig.idxWatermarkIntEnable = false;
		stcDacBufConfig.triggerMode = kDacTriggerByHardware;				// PDB is the only H/W trigger source.
		ConfigurePDBAsHwTriggerInpForDAC();
		stcDacBufConfig.upperIdx = 15;										// Set the upper limit.
		stcDacBufConfig.watermarkMode = kDacBuffWatermarkFromUpperAs1Word;	// Default.
		DacStatus = DAC_DRV_ConfigBuffer(DAC0_IDX, &stcDacBufConfig);
	}
	if(DacStatus == kStatus_DAC_Success)
	{
		bRetVal = true;
    }

	return bRetVal;
}

/*
 * GetDACOutputBufferValues
 *
 * @desc	Populates the buffer with the values used for generating a Sine
 * 			waveform at the specified offset above the initial Vpeak offset.
 *
 * @param	pnBuf(uint16_t*) - Pointer to the buffer where the data is to be stored.
 * @param	nLen(uint8_t) - Buffer length
 * @param	nOffset_mV(uint16_t) - Offset / Scaling of the o/p waveform.
 *
 * @return  void.
 */
static void GetDACOutputBufferValues(uint16_t *pnBuf, uint8_t nLen, uint16_t nOffset_mV)
{
	uint8_t i = 0;
	float fVpeakDacCount = INITIAL_VPEAK_OFFSET_DAC_COUNT;
	// Convert the Offset in mV to V.
	float fRequestedOffsetDacCount = ((float)nOffset_mV / 1000.0f) * (DAC_COUNTS_PER_VOLT);

    // Compute the DAC Counts corresponding to the requested offset.
    fVpeakDacCount += fRequestedOffsetDacCount;

    // Compute the DAC O/P buffer values for the Sine wave generation.
    for(i = 0; i < nLen; i++)
    {
		// Now determine the values to generate a sine wave at DAC o/p.
    	pnBuf[i] = (sin((((2 * PI) / NUM_OF_DAC_SAMPLES_PER_PERIOD) * i)
								+ OFFSET_IN_RADIANS) * fVpeakDacCount) +
				   ZERO_DC_OFFSET_COUNT;
    }
}

/*
 * ConfigurePDBAsHwTriggerInpForDAC
 *
 * @desc	Configures the PDB module as the HW trigger source for the DAC.
 *
 * @return 	void
 */
static void ConfigurePDBAsHwTriggerInpForDAC()
{
	pdb_timer_config_t stcPdbTimerConfig;
	pdb_dac_interval_config_t stcPdbDacPeriodConfig;

	stcPdbTimerConfig.clkPreDiv = PDB_CLK_PRESCALER;
	stcPdbTimerConfig.clkPreMultFactor = kPdbClkPreMultFactorAs1;
	stcPdbTimerConfig.continuousModeEnable = true;
	stcPdbTimerConfig.dmaEnable = false;
	stcPdbTimerConfig.intEnable = false;
	stcPdbTimerConfig.loadValueMode = kPdbLoadValueAtNextTrigger;
	stcPdbTimerConfig.seqErrIntEnable = false;
	stcPdbTimerConfig.triggerInput = kPdbSoftTrigger;

	if(PDB_DRV_Init(PDB0_IDX, &stcPdbTimerConfig) == kStatus_PDB_Success)
	{
		stcPdbDacPeriodConfig.extTriggerInputEnable = false;
		stcPdbDacPeriodConfig.intervalTriggerEnable = true;
		(void)PDB_DRV_ConfigDacInterval(PDB0_IDX, 0, &stcPdbDacPeriodConfig);
	}
	else
	{
		// TODO: Add an error log here.
	}
}


#ifdef PDB_DEBUG_EN
// These function were useful during testing of the DAC init.
uint32_t GetPDBTimerValue()
{
	uint32_t nRetVal = 0;
	nRetVal = PDB_DRV_GetTimerValue(PDB0_IDX);
	return nRetVal;
}

uint32_t GetPDBSCValue()
{
	uint32_t nRetVal = 0;
	nRetVal = PDB_DRV_ReadSC(PDB0_IDX);
	return nRetVal;
}

uint32_t GetPDBMODValue()
{
	uint32_t nRetVal = 0;
	nRetVal = PDB_DRV_ReadMOD(PDB0_IDX);
	return nRetVal;
}
#endif


#ifdef __cplusplus
}
#endif