#ifdef __cplusplus
extern "C" {
#endif

#include "configLog.h"
#include "PinConfig.h"
#include "Log.h"
#include "errorcodes.h"
#include "pmic.h"

#include "EnergyMonitor.h"

/* --------------------------- MACRO FUNCTIONS ------------------------------ */
#define mJ_TO_J(a) ((a)/1000)
#define nWh_TO_J(a) (((a) * 3600) / 1000000000)

// Low showed that PMIC replied within 22 ms, so set a period to check close to 
// that (+ some margin)
#define	ENERGY_MONITOR_PERIOD_IN_MILLI_SECS	(25)

/* --------------------- LOCAL FUNCTIONS DECLARATION ------------------------ */
static Mk24EnergyResult_t* getEnergyData(int max_wait_ms);

/* ------------------------ FUNCTIONS DEFINITIONS --------------------------- */
/**
 * @brief Displays the 'live' energy consumption by the sensor
 * 
 * Data is fetched directly from PMIC. Only for CLI output, do not affect any of
 * the local energy data.
 * 
 * @retval True if fetching was successful, False otherwise
 */
bool EnergyMonitor_Report(void)
{
	int max_wait_ms = 1000;

	Mk24EnergyResult_t* pEnergy = getEnergyData(max_wait_ms);
	if(!pEnergy)
	{
		return false;
	}

	const float previous_total_J = mJ_TO_J((float)pEnergy->energy_Total_mJ);
	const float this_run_J = nWh_TO_J((float)pEnergy->energy_this_run_nWh);

	LOG_DBG(LOG_LEVEL_APP,
			"\n------Energy-(J)-----\n"
			" Previous Total: %.2f\n"
			" This run:       %.2f\n"
			" Total Energy:   %.2f\n"
			"----------------------\n",
			previous_total_J,
			this_run_J,
			previous_total_J + this_run_J);

	return true;
}


/**
 * @brief Fetch 'live' energy consumption data
 * 
 * @param[out] pfTotalEnergy Pointer to store total energy (J). Can be NULL.
 * @param[out] pfCurrentRun  Pointer to store energy this run (J). Can be NULL.
 *
 * @retval True if fetching was successful, False otherwise
 */
bool EnergyMonitor_GetEnergyConsumed_J(float* pfTotalEnergy, float* pfCurrentRun)
{
	int max_wait_ms = 1000;

	Mk24EnergyResult_t* pEnergy = getEnergyData(max_wait_ms);
	if(!pEnergy)
	{
		return false;
	}
	
	if (NULL != pfCurrentRun)
	{
		*pfCurrentRun = nWh_TO_J((float)pEnergy->energy_this_run_nWh);
	}

	if (NULL != pfTotalEnergy)
	{
		*pfTotalEnergy = mJ_TO_J((float)pEnergy->energy_Total_mJ) +
				         nWh_TO_J((float)pEnergy->energy_this_run_nWh);
	}

	return true;
}


/**
 * @brief 	Reads the DC level in volts.
 *
 * @param[out]	pVolts - pointer to variable to store the dc level in volts.
 *
 * @return 	True if the read was successful, False otherwise.
 */
bool EnergyMonitor_ReadDCLevel_v(float *pfVolts)
{
	int max_wait_ms = 1000;

	Mk24EnergyResult_t* pEnergy = getEnergyData(max_wait_ms);
	if(!pEnergy)
	{
		return false;
	}
	*pfVolts = ((float)(pEnergy->voltage_mV))/1000;
	return true;
}


/**
 * @brief 	returns duration in msecs of current ticks - start ticks, checking for errors
 *
 * @param[in] 	nStartTick start ticks.
 *
 * @return 	duration in msecs if no error, otherwise 0
 */
uint32_t EnergyMonitor_GetDurationMsecs(uint32_t nStartTicks)
{
	uint32_t nStopTicks = xTaskGetTickCount();
	if(nStopTicks > nStartTicks)
	{
		return nStopTicks - nStartTicks;
	}
	LOG_EVENT(eLOG_ENERGY_DATA, LOG_NUM_APP, ERRLOGWARN,  "nStopTicks(%d) <= nStartTicks(%d)", nStopTicks, nStartTicks);
	return 0;
}


/**
 * @brief Fetch energy data from PMIC
 * 
 * Send command to PMIC to request energy data, and check (in a loop, with
 * timeout) to see if the updated energy flag has been set.
 * 
 * @param[in] max_wait_ms    Timeout waiting for response from PMIC
 * @return Address of energy structure, or NULL if error occur!
 */
static Mk24EnergyResult_t* getEnergyData(int max_wait_ms)
{
	PMIC_SendEnergyReqMsg();
	if(PMIC_GetEnergyUpdatedFlag() != false)
	{
		LOG_EVENT(eLOG_ENERGY_DATA, LOG_NUM_APP, ERRLOGMAJOR, "%s(): Energy Updated flag already set!", __func__);
	}
	while(PMIC_GetEnergyUpdatedFlag() == false)
	{
		vTaskDelay(ENERGY_MONITOR_PERIOD_IN_MILLI_SECS/portTICK_PERIOD_MS);
		if((max_wait_ms -= ENERGY_MONITOR_PERIOD_IN_MILLI_SECS) <= 0)
		{
			LOG_EVENT(eLOG_ENERGY_DATA, LOG_NUM_APP, ERRLOGMAJOR, "%s(): Timed out waiting for PMIC Energy report!", __func__);
			return NULL;
		}
	}
	return PMIC_GetEnergyData();
}


#ifdef __cplusplus
}
#endif