#ifdef __cplusplus
extern "C" {
#endif

/*
 * Vbat.c
 *
 *  Created on: July 10, 2017
 *      Author: Jayant Rampuria
 *      to retrieve and store most frequently updating parameters.
 */




/*
 * Includes
 */
#include "Vbat.h"
#include "Log.h"
#include "configLog.h"
#include "fsl_os_abstraction.h"
#include "MK24F12_extension.h"
#include "Resources.h"
#include "CLIcmd.h"
#include "alarms.h"


#define VBAT_MUTEX_WAIT_MS			(10)
#define VBAT_MAX_BYTES				(32)

// This device includes a 32-byte register file that is powered in all power
// modes and is powered by VBAT. Please insert new values from dummy.

//	VbatRf Contents:
//
//	ByteIndex |	BytesUsed 	| Usage
//  ------------------------------------
//	0         |	1		  	|	NodeTask
//	1		  |	2			|	Flags
//	3		  |	1			|	Alarm
//	4		  |	4 			|	Up_Time
//	8		  |	4 			|	[Was EnergyUsed, not required now]
//	12		  |	4			|	Comms Energy Total
//	16		  |	1			|	StackOvrflowIndx
//	17		  |	1			|	UploadDayOffset
//	18		  |	2			|	SimulatedLocalTemp
//	20		  |	2			|	SimulatedRemoteTemp
//	22		  |	1			|	AlarmAttempts
//  23        |	1			|	LimitAttempts
//  24		  | 1			|	Alarms24HourCounter
//	25		  |	1			|	Limits24HourCounter
//	26		  |				|	(Dummy)
//
//	29		  |	2			|	Watchdog
//	31		  |	1			|	Chksum
//  ----------------------------------
#ifdef _MSC_VER
#define PACKED_STRUCT_START __pragma(pack(push, 1))
#define PACKED_STRUCT_END   __pragma(pack(pop))
#else
#define PACKED_STRUCT_START
#define PACKED_STRUCT_END   __attribute__((packed))
#endif
	PACKED_STRUCT_START
typedef union
{
	uint8_t byteArray[32];
	struct
	{
		uint32_t reg0;
		uint32_t reg1;
		uint32_t reg2;
		uint32_t reg3;
		uint32_t reg4;
		uint32_t reg5;
		uint32_t reg6;
		uint32_t reg7;
	}reg;
}VBAT_t;
	PACKED_STRUCT_END

// No.of bytes to read / write.
typedef enum
{
	VBAT_RW_ONE_BYTE						= 1,	// 1 byte
	VBAT_RW_TWO_BYTES		          		= 2,	// 2 bytes
	VBAT_RW_FOUR_BYTES		           		= 4,	// 4 bytes
}VBAT_NUM_BYTES_READ_WRITE_t;

// Vbat data operations.
typedef enum
{
	VBAT_DATA_OP_CLEAR_FLAGS						= 0,	// Clear flag operation
	VBAT_DATA_OP_SET_FLAGS		          			= 1,	// Set flag operation
	VBAT_DATA_OP_UPDATE_COMMS_ENERGY_USED   		= 2,	// Update the Comms energy used by the sensor so far.
}VBAT_DATA_OPS_t;

VBAT_t *rfVbat = (VBAT_t*)RFVBAT_BASE;
static SimulatedTemperature_t m_SimulatedTemperatures;

static SemaphoreHandle_t rfVbatMutex = NULL;
static void ComputeChecksum(uint8_t *pChksum);
static void LogChecksumError(const char *funcName, uint8_t storedChksum, uint8_t compChksum);
static bool ProtectedRead(const char* funcName, uint8_t vbatArrayIndx, void* pDstBuf, VBAT_NUM_BYTES_READ_WRITE_t numBytesToRead);
static bool ProtectedWrite(const char* funcName, uint8_t vbatArrayIndx, void* pSrcData, VBAT_NUM_BYTES_READ_WRITE_t numBytestoWrite);
static bool ProtectedReadModifyWrite(const char* funcName, uint8_t vbatArrayIndx, uint8_t* pSrcBuf, VBAT_DATA_OPS_t dataOp);
static void SetSimulatedTemperature();

const char msgMutexFailed[] = "\nVBAT Mutex failed in %s().\n";


/*
 * Vbat_ClearSimulatedTemperatures
 *
 * @brief	Clear simulated temperatures (in VBAT)
 *
 * @param	none
 *
 * @return  none
 */
void Vbat_ClearSimulatedTemperatures()
{
	uint16_t init_val = 0;
	(void)ProtectedWrite(__func__, VBAT_DATA_INDX_PCB_SIM_TEMP, &init_val, VBAT_RW_TWO_BYTES);
	(void)ProtectedWrite(__func__, VBAT_DATA_INDX_EXT_SIM_TEMP, &init_val, VBAT_RW_TWO_BYTES);
}

/*
 * Vbat_GetStackOverflowTaskIndx
 *
 * @brief	Retrieves the task index, from VBAT register file, whose stack overflew
 *
 * @param	pTaskIndx - pointer to the task index.
 *
 * @return  true - if successful , false otherwise.
 */
bool Vbat_GetStackOverflowTaskIndx(uint8_t* pTaskIndx)
{
	bool retval = false;

	if(pTaskIndx != NULL)
	{
		retval = ProtectedRead(__func__, VBAT_STACK_OVERFLOW_TASK_INDX, (uint8_t*)pTaskIndx, VBAT_RW_ONE_BYTE);
	}

	return retval;
}

/*
 * Vbat_FlagStackOverflow
 *
 * @brief	Set the STACK_OVERFLOW flag in the VBAT register file.
 *
 * @param	taskIndx - Task index determined from the Resources_TaskInfo().
 * 					   used to identify the taskname post reset.
 *
 * @return  void
 */
void Vbat_FlagStackOverflow(uint8_t taskIndx)
{
	// don't take the semaphore cause who knows what state we're in
	*(uint16_t*)(&rfVbat->byteArray[VBAT_DATA_INDX_FLAGS]) &= ~VBATRF_FLAG_UNEXPECTED_LAST_SHUTDOWN;
	*(uint16_t*)(&rfVbat->byteArray[VBAT_DATA_INDX_FLAGS]) |= VBATRF_FLAG_STACK_OVERFLOW;
	*(uint8_t*)(&rfVbat->byteArray[VBAT_STACK_OVERFLOW_TASK_INDX]) = taskIndx;
	ComputeChecksum(&rfVbat->byteArray[VBAT_DATA_INDX_CHKSUM]);
}

/*
 * Vbat_FlagHeapOverflow
 *
 * @brief	Set the HEAP_OVERFLOW flag in the VBAT register file.
 *
 * @return  void
 */
void Vbat_FlagHeapOverflow()
{
	// don't take the semaphore cause who knows what state we're in
	*(uint16_t*)(&rfVbat->byteArray[VBAT_DATA_INDX_FLAGS]) &= ~VBATRF_FLAG_UNEXPECTED_LAST_SHUTDOWN;
	*(uint16_t*)(&rfVbat->byteArray[VBAT_DATA_INDX_FLAGS]) |= VBATRF_FLAG_HEAP_OVERFLOW;
	ComputeChecksum(&rfVbat->byteArray[VBAT_DATA_INDX_CHKSUM]);
}

/*
 * Vbat_SetEnergyUsedInCommsCycles
 *
 * @brief	Sets the total energy consumed by the sensor in comms cycle to the
 * 			specified value.
 *
 * @param	fEnergyUsed_Joules - Energy value to update the COMMS cycle energy
 * 								 consumed.
 *
 * \note
 * Energy is stored with 2 decimal places and has to be divided/multiplied by
 * 100 to get actual number of Joules.
 *
 * @return  true - if successful , false otherwise.
 */
bool Vbat_SetEnergyUsedInCommsCycles(float fEnergyUsed_Joules)
{
	uint32_t energyUsed = (uint32_t)(fEnergyUsed_Joules * 100);
	return ProtectedWrite(__func__, VBAT_DATA_INDX_COMMS_ENERGY_USED, (uint8_t*)&energyUsed, VBAT_RW_FOUR_BYTES);
}

/*
 * Vbat_AddEnergyUsedInCommsCycles
 *
 * @brief	Adds the specified energy to the energy used by the sensor in comms
 * 			cycles.
 *
 *
 * @param	fEnergyUsed_Joules - Energy used in the current COMMS cycle.
 *
 * \note
 * Energy is stored with 2 decimal places and has to be divided/multiplied by
 * 100 to get actual number of Joules.
 *
 * @return  true - if successful , false otherwise.
 */
bool Vbat_AddEnergyUsedInCommsCycles(float fEnergyUsed_Joules)
{
	uint32_t energyUsed = (uint32_t)(fEnergyUsed_Joules * 100);
	return ProtectedReadModifyWrite(__func__, VBAT_DATA_INDX_COMMS_ENERGY_USED, (uint8_t*)&energyUsed, VBAT_DATA_OP_UPDATE_COMMS_ENERGY_USED);
}

/*
 * Vbat_GetEnergyUsedInCommsCycles
 *
 * @brief	Retrieves the Total energy consumed by the sensor in comms cycles.
 * 			This data is retrieved from the VBAT_DATA_INDX_COMMS_ENERGY_USED area of
 * 			the VBAT register file.
 *
 * @param	*pfEnergyConsumed - pointer to the var where the retrieved comms
 * 			 energy is to be stored.
 *
 * \note
 * Energy is stored with 2 decimal places and has to be divided/multiplied by
 * 100 to get actual number of Joules.
 *
 * @return  true - if read was successful, false otherwise.
 */
bool Vbat_GetEnergyUsedInCommsCycles(float *pfEnergyConsumed)
{
	bool retval = false;
	uint32_t energyUsed = 0;

	if((pfEnergyConsumed != NULL) &&
		(ProtectedRead(__func__, VBAT_DATA_INDX_COMMS_ENERGY_USED, (uint8_t*)&energyUsed, VBAT_RW_FOUR_BYTES)))
	{
		*pfEnergyConsumed = (float)energyUsed / 100.0f;
		retval = true;
	}
	return retval;
}

/*
 * Vbat_GetUpTime
 *
 * @brief	Retrieves the Up_Time from the VBAT_DATA_INDX_UPTIME area of
 * 			the VBAT register file.
 *
 * @param	*pUpTime - pointer to the var where the retrieved UpTime is to be stored.
 *
 * @return  true - if read was successful, false otherwise or error.
 */
bool Vbat_GetUpTime(uint32_t *pUpTime)
{
	bool retval = false;

	if(pUpTime != NULL)
	{
		retval = ProtectedRead(__func__, VBAT_DATA_INDX_UPTIME, (uint8_t*)pUpTime, VBAT_RW_FOUR_BYTES);
	}

	return retval;
}

/*
 * Vbat_SetUpTime
 *
 * @brief	Sets the UpTime in the VBAT register file.
 *
 * @param	UpTime - UpTime value to be set.
 *
 * @return  true - if successful , false otherwise.
 */
bool Vbat_SetUpTime(uint32_t UpTime)
{
	return ProtectedWrite(__func__, VBAT_DATA_INDX_UPTIME, (uint8_t*)&UpTime, VBAT_RW_FOUR_BYTES);
}

/*
 * Vbat_GetUploadDayOffset
 *
 * @brief	Retrieves the pUploadDayOffset from the VBAT_DATA_UPLOAD_DAY_OFFSET area of
 * 			the VBAT register file.
 *
 * @return  Upload day offset value, on read fail default to 0
 */
uint8_t Vbat_GetUploadDayOffset()
{
	uint8_t nUploadDayOffset = 0;

	if(!ProtectedRead(__func__, VBAT_DATA_UPLOAD_DAY_OFFSET, (uint8_t*)&nUploadDayOffset, VBAT_RW_ONE_BYTE))
	{
		LOG_EVENT(0, LOG_NUM_APP, ERRLOGFATAL, "%s: Checksum Error", __func__);
	}

	return nUploadDayOffset;
}

/*
 * Vbat_SetUploadDayOffset
 *
 * @brief	Sets the Uploa dDay Offset in the VBAT register file.
 *
 * @param	UploadDayOffset - Upload Day Offset value to be set.
 *
 * @return  true - if successful , false otherwise.
 */
bool Vbat_SetUploadDayOffset(uint8_t UploadDayOffset)
{
	return ProtectedWrite(__func__, VBAT_DATA_UPLOAD_DAY_OFFSET, (uint8_t*)&UploadDayOffset, VBAT_RW_ONE_BYTE);
}

/*
 * Vbat_GetFlags
 *
 * @brief	Retrieves the flags from the VBAT_DATA_INDX_FLAGS area of
 * 			the VBAT register file.
 *
 * @param	*pFlagVar - pointer to the var where the retrieved flags are to be stored.
 *
 * @return  true - if read was successful, false otherwise or error.
 */
bool Vbat_GetFlags(uint16_t* pFlagVar)
{
	bool retval = false;

	if(pFlagVar != NULL)
	{
		retval = ProtectedRead(__func__, VBAT_DATA_INDX_FLAGS, (uint16_t*)pFlagVar, VBAT_RW_TWO_BYTES);
	}
	return retval;
}

/*
 * Vbat_SetFlag
 *
 * @brief	Sets the requested flag in the VBAT register file flags.
 *
 * @param	flag - bit mask of flag to be set.
 *
 * @return  true - if successful , false otherwise.
 */
bool Vbat_SetFlag(uint16_t flag)
{
	return ProtectedReadModifyWrite(__func__, VBAT_DATA_INDX_FLAGS, (uint8_t*)&flag, VBAT_DATA_OP_SET_FLAGS);
}

/*
 * Vbat_ClearFlag
 *
 * @brief	Sets the requested flag in the VBAT register file flags.
 *
 * @param	flag - bit mask of flag to be set.
 *
 * @return  true - if successful , false otherwise.
 */
bool Vbat_ClearFlag(uint16_t flag)
{
	return ProtectedReadModifyWrite(__func__, VBAT_DATA_INDX_FLAGS, (uint8_t*)&flag, VBAT_DATA_OP_CLEAR_FLAGS);
}

/*
 * Vbat_IsFlagSet
 *
 * @brief	Checks if the requested flag in the VBAT_DATA_INDX_FLAGS area of
 * 			the VBAT register file is set.
 *
 * @param	flag - the bit flag required to be checked.
 *
 * @return  true - if set , false otherwise or error.
 */
bool Vbat_IsFlagSet(uint16_t flag)
{
	bool retval = false;
	uint16_t readFlags = 0;

	if(ProtectedRead(__func__,VBAT_DATA_INDX_FLAGS, (uint8_t*)&readFlags, VBAT_RW_TWO_BYTES))
	{
		if(readFlags & flag)
		{
			retval = true;
		}
	}
	return retval;
}


/*
 * Vbat_Init
 *
 * @brief	Creates a mutex and does any necessary initialisation for accessing
 * 			the VBAT.
 *
 * @param	void
 *
 */
void Vbat_Init()
{
	uint8_t computedChksum = 0;
    // create the Mutex
    if (rfVbatMutex == NULL)
    {
    	rfVbatMutex = xSemaphoreCreateMutex();
    }

    ComputeChecksum(&computedChksum);
    if(computedChksum != rfVbat->byteArray[VBAT_DATA_INDX_CHKSUM])
    {
    	LOG_DBG( LOG_LEVEL_APP,"\nVBAT Checksum Mismatch\n");
    	// TODO some sort of recovery
    }
}


/*
 * Vbat_SetNodeWakeupTask
 *
 * @brief	Stores the node task into the VBAT register file for the next wakeup.
 *
 * @param	nodeTask - The node task to store.
 *
 */
void Vbat_SetNodeWakeupTask(uint8_t nodeTask)
{
	if(false == ProtectedWrite(__func__, VBAT_DATA_INDX_NODE_TASK, &nodeTask, VBAT_RW_ONE_BYTE))
	{
		LOG_EVENT( 0, LOG_NUM_APP, ERRLOGFATAL, "Unable to setup node task in VBAT");
	}
}

/*
 * Vbat_GetNodeWakeupTask
 *
 * @brief	Retrieves the node task from VBAT register file.
 *
 * @param	nodeTask - pointer to the node task.
 *
 * @return  true - if successful , false otherwise.
 */
bool Vbat_GetNodeWakeupTask(uint8_t *nodeTask)
{
	bool retval = false;

	if(nodeTask != NULL)
	{
		retval = ProtectedRead(__func__, VBAT_DATA_INDX_NODE_TASK, (uint8_t*)nodeTask, VBAT_RW_ONE_BYTE);
	}

	return retval;
}



/*
 * Vbat_SetAlarmState
 *
 * @brief	Stores the temperature alarm state into the VBAT register file for the next wakeup.
 *
 * @param	state - The state to store.
 *
 */
void Vbat_SetAlarmState(uint8_t state)
{
	if(false == ProtectedWrite(__func__, VBAT_DATA_INDX_ALARM, (uint8_t*)&state, VBAT_RW_ONE_BYTE))
	{
		LOG_EVENT(0, LOG_NUM_APP, ERRLOGFATAL, "Unable to setup alarm state in VBAT");
	}
}

/*
 * Vbat_GetAlarmState
 *
 * @brief	Retrieves the temperature alarm state from VBAT register file.
 *
 * @param	pState - pointer to the state.
 *
 * @return  true - if successful , false otherwise.
 */
bool Vbat_GetAlarmState(uint8_t *pState)
{
	bool retval = false;

	if(pState != NULL)
	{
		retval = ProtectedRead(__func__, VBAT_DATA_INDX_ALARM, (uint16_t*)pState, VBAT_RW_ONE_BYTE);
	}

	return retval;
}


/*
 * Vbat_FlagHardFault
 *
 * @brief	Set the HARD_FAULT flag in the VBAT register file.
 *
 * @param	void
 *
 * @return  void
 */
void Vbat_FlagHardFault(void)
{
	// don't take the semaphore cause who knows what state we're in
	*(uint16_t*)(&rfVbat->byteArray[VBAT_DATA_INDX_FLAGS]) &= ~VBATRF_FLAG_UNEXPECTED_LAST_SHUTDOWN;
	*(uint16_t*)(&rfVbat->byteArray[VBAT_DATA_INDX_FLAGS]) |= VBATRF_FLAG_HARD_FAULT;
	ComputeChecksum(&rfVbat->byteArray[VBAT_DATA_INDX_CHKSUM]);
}

/*
 * Vbat_CheckForWatchdog
 *
 * @brief	Check if a watchdog timer interrupt has been detected
 *          if there is one log it then clean up by clearing flags
 *
 * @param	void
 *
 * @return  void
 */
void Vbat_CheckForWatchdog()
{
	char *wdset = NULL;
	if(0xAAAA == *(uint16_t*)(RFVBAT_BASE+VBAT_DATA_INDX_WATCHDOG_L))
	{
		wdset = "application";
	}
	else if(0x5555 == *(uint16_t*)(RFVBAT_BASE+VBAT_DATA_INDX_WATCHDOG_L))
	{
		wdset = "loader";
	}
	if(wdset)
	{
		LOG_EVENT(0, LOG_NUM_APP, ERRLOGFATAL, "WATCHDOG timeout detected in %s", wdset);
		// clear flags
		Vbat_ClearFlag(VBATRF_FLAG_UNEXPECTED_LAST_SHUTDOWN);
		*(uint16_t*)(RFVBAT_BASE+VBAT_DATA_INDX_WATCHDOG_L) = 0;
	}
}

// set VBAT from local structure
static void SetSimulatedTemperature()
{
	uint8_t buf[2];

	if(m_SimulatedTemperatures.flags & VBATRF_local_is_simulated)
	{
		buf[0] = m_SimulatedTemperatures.local;
		buf[1] = ~buf[0];
		(void)ProtectedWrite(__func__, VBAT_DATA_INDX_PCB_SIM_TEMP, buf, VBAT_RW_TWO_BYTES);
	}

	if(m_SimulatedTemperatures.flags & VBATRF_remote_is_simulated)
	{
		buf[0] = m_SimulatedTemperatures.remote;
		buf[1] = ~buf[0];
		(void)ProtectedWrite(__func__, VBAT_DATA_INDX_EXT_SIM_TEMP, buf, VBAT_RW_TWO_BYTES);
	}
}

/*
 * Vbat_CheckForSimulatedTemperatures
 *
 * @brief	Check if simulated temperatures are present in VBAT
 *          and return pointer to a SimulatedTemperature_t structure
 *
 * @param	none
 *
 * @return  Pointer to Structure of type SimulatedTemperature_t
 */
SimulatedTemperature_t* Vbat_CheckForSimulatedTemperatures()
{
	m_SimulatedTemperatures.flags = 0;

	uint8_t buf[4];
	if(ProtectedRead(__func__, VBAT_DATA_INDX_PCB_SIM_TEMP, buf, VBAT_RW_FOUR_BYTES))
	{
		if(buf[0] == (~buf[1] & 0xFF))
		{
			m_SimulatedTemperatures.local = buf[0];
			m_SimulatedTemperatures.flags |= VBATRF_local_is_simulated;
		}
		if(buf[2] == (~buf[3] & 0xFF))
		{
			m_SimulatedTemperatures.remote = buf[2];
			m_SimulatedTemperatures.flags |= VBATRF_remote_is_simulated;
		}
	}

	return &m_SimulatedTemperatures;
}


/*
 * Vbat_SetByte
 *
 * @brief	Sets the value of byte at 'index' in VBAT register file.
 *
 * @param	val - value to write to VBAT
 * 			index - location in VBAT
 *
 * @return  true on success
 */
bool Vbat_SetByte(VBAT_DATA_INDEX_t index, uint8_t val)
{
	if(index >= VBAT_DATA_INDX_CHKSUM)
	{
		LOG_EVENT(0, LOG_NUM_APP, ERRLOGFATAL, "Illegal attempt to write byte to VBAT at %d", index);
		return false;
	}
	return ProtectedWrite(__func__, index, (uint8_t*)&val, VBAT_RW_ONE_BYTE);
}

/*
 * Vbat_GetByte
 *
 * @brief	Retrieves the value of byte at index from VBAT register file.
 *
 * @param	pVal - pointer to the value
 * 			index - location in VBAT
 *
 * @return  true - if successful , false otherwise.
 */
bool Vbat_GetByte(VBAT_DATA_INDEX_t index, uint8_t* pVal)
{
	if(index > VBAT_DATA_INDX_CHKSUM)
	{
		LOG_EVENT(0, LOG_NUM_APP, ERRLOGFATAL, "Illegal attempt to read byte from VBAT at %d", index);
		return false;
	}
	return ProtectedRead(__func__, index, pVal, VBAT_RW_ONE_BYTE);
}


/*
 * WDOG_EWM_IRQHandler
 *
 * @brief	FLag watchdog interrupt and wait for reset
 *
 * @param	void
 *
 * @return  true if set otherwise false
 */
void WDOG_EWM_IRQHandler(void)
{
	*(uint16_t*)(RFVBAT_BASE+VBAT_DATA_INDX_WATCHDOG_L) = 0xAAAA;
	while(1);		// wait for reset
}

static void ComputeChecksum(uint8_t *pChksum)
{
	uint8_t i = 0;
	uint8_t chksum = 0x00;

	for(i = 0; i < VBAT_DATA_INDX_CHKSUM; i++)
	{
		chksum ^= rfVbat->byteArray[i];
	}
	*pChksum = chksum;
}

static void LogChecksumError(const char *funcName, uint8_t storedChksum, uint8_t compChksum)
{
	LOG_DBG( LOG_LEVEL_APP,"\nIn %s() ***VBAT Checksum Error***, Stored:0x%x, Computed:0x%x\n",
			 funcName, storedChksum , compChksum);
}

static bool ProtectedRead(const char* funcName, uint8_t vbatArrayIndx, void* pDstBuf, VBAT_NUM_BYTES_READ_WRITE_t numBytesToRead)
{
	bool retval = true;
	uint8_t computedChksum = 0;

	if(xSemaphoreTake(rfVbatMutex, VBAT_MUTEX_WAIT_MS) == pdTRUE)
	{
		ComputeChecksum(&computedChksum);
		if(rfVbat->byteArray[VBAT_DATA_INDX_CHKSUM] != computedChksum)
		{
			LogChecksumError(funcName, rfVbat->byteArray[VBAT_DATA_INDX_CHKSUM], computedChksum);
			retval = false;
		}
		else
		{
			switch(numBytesToRead)
			{
				case VBAT_RW_ONE_BYTE: //ONE_BYTE:
					*(uint8_t*)pDstBuf = *(uint8_t*)(&rfVbat->byteArray[vbatArrayIndx]);
					break;
				case VBAT_RW_TWO_BYTES: //TWO_BYTES:
					*(uint16_t*)pDstBuf = *(uint16_t*)(&rfVbat->byteArray[vbatArrayIndx]);
					break;
				case VBAT_RW_FOUR_BYTES: //FOUR_BYTES:
					*(uint32_t*)pDstBuf = *(uint32_t*)(&rfVbat->byteArray[vbatArrayIndx]);
					break;
				default:
					LOG_EVENT(0, LOG_LEVEL_APP, ERRLOGFATAL, "%s: Bad number of bytes:%d", funcName , numBytesToRead);
					retval = false;
					break;
			}
		}
		xSemaphoreGive(rfVbatMutex);
	}
	else
	{
		LOG_DBG(LOG_LEVEL_APP, msgMutexFailed, funcName);
		retval = false;
	}
	return retval;
}

static bool ProtectedWrite(const char* funcName, uint8_t vbatArrayIndx, void* pSrcBuf, VBAT_NUM_BYTES_READ_WRITE_t numBytesToWrite)
{
	bool retval = true;

	if(xSemaphoreTake(rfVbatMutex, VBAT_MUTEX_WAIT_MS) == pdTRUE)
	{
		switch(numBytesToWrite)
		{
			case VBAT_RW_ONE_BYTE: //ONE_BYTE:
				*(uint8_t*)(&rfVbat->byteArray[vbatArrayIndx]) = *(uint8_t*)pSrcBuf;
				break;
			case VBAT_RW_TWO_BYTES: //TWO_BYTES:
				*(uint16_t*)(&rfVbat->byteArray[vbatArrayIndx]) = *(uint16_t*)pSrcBuf;
				break;
			case VBAT_RW_FOUR_BYTES: //FOUR_BYTES:
				*(uint32_t*)(&rfVbat->byteArray[vbatArrayIndx]) = *(uint32_t*)pSrcBuf;
				break;
			default:
				LOG_EVENT(0, LOG_LEVEL_APP, ERRLOGFATAL, "%s: Bad number of bytes:%d", funcName , numBytesToWrite);
				retval = false;
				break;
		}
		ComputeChecksum(&rfVbat->byteArray[VBAT_DATA_INDX_CHKSUM]);
		xSemaphoreGive(rfVbatMutex);
	}
	else
	{
		LOG_DBG(LOG_LEVEL_APP, msgMutexFailed, funcName);
		retval = false;
	}
	return retval;
}

static bool ProtectedReadModifyWrite(const char* funcName, uint8_t vbatArrayIndx, uint8_t* pSrcBuf, VBAT_DATA_OPS_t dataOp)
{
	bool retval = true;
	uint8_t computedChksum = 0;
	uint32_t readData = 0;

	if(xSemaphoreTake(rfVbatMutex, VBAT_MUTEX_WAIT_MS) == pdTRUE)
	{
		ComputeChecksum(&computedChksum);
		if(rfVbat->byteArray[VBAT_DATA_INDX_CHKSUM] != computedChksum)
		{
			LogChecksumError(funcName, rfVbat->byteArray[VBAT_DATA_INDX_CHKSUM], computedChksum);
			retval = false;
		}
		else
		{
			// Perform the intended operation.
			switch(dataOp)
			{
				case VBAT_DATA_OP_CLEAR_FLAGS:
					// Field "FLAGS" occupies 2 bytes, hence access by uint16_t
					readData = *(uint16_t*)(&rfVbat->byteArray[vbatArrayIndx]);
					readData &= ~*((uint16_t*)pSrcBuf);
					*(uint16_t*)(&rfVbat->byteArray[vbatArrayIndx]) = (uint16_t)readData;
					break;

				case VBAT_DATA_OP_SET_FLAGS:
					// Field "FLAGS" occupies 2 bytes, hence access by uint16_t
					readData = *(uint16_t*)(&rfVbat->byteArray[vbatArrayIndx]);
					readData |= *((uint16_t*)pSrcBuf);
					*(uint16_t*)(&rfVbat->byteArray[vbatArrayIndx]) = (uint16_t)readData;
					break;

				case VBAT_DATA_OP_UPDATE_COMMS_ENERGY_USED:
					// Field "COMMS ENERGY" occupies 4 bytes, hence access by uint32_t
					// Energy is stored with 2 decimal places and has to be multiplied by
					// 100 to get actual number of Joules.
					readData = *(uint32_t*)(&rfVbat->byteArray[vbatArrayIndx]);
					readData += *((uint32_t*)pSrcBuf);
					*(uint32_t*)(&rfVbat->byteArray[vbatArrayIndx]) = readData;
					break;

				default:
					LOG_DBG(LOG_LEVEL_APP,"\nUnsupported VBAT operation num:%d\n", dataOp);
					retval = false;
					break;
			}
			ComputeChecksum(&rfVbat->byteArray[VBAT_DATA_INDX_CHKSUM]);
		}

		// Write the data
		xSemaphoreGive(rfVbatMutex);
	}
	else
	{
		LOG_DBG(LOG_LEVEL_APP, msgMutexFailed, funcName);
		retval = false;
	}
	return retval;
}
//==============================================================================
// 		CLI / TEST RELATED
//==============================================================================
static bool cliSetNodeTask( uint32_t args, uint8_t * argv[], uint32_t * argi);
static bool cliGetNodeTask( uint32_t args, uint8_t * argv[], uint32_t * argi);
static bool cliClearFlags( uint32_t args, uint8_t * argv[], uint32_t * argi);
static bool cliSetFlags( uint32_t args, uint8_t * argv[], uint32_t * argi);
static bool cliReadFlags( uint32_t args, uint8_t * argv[], uint32_t * argi);
static bool cliGetCommsEnergy( uint32_t argc, uint8_t * argv[], uint32_t * argi);
static bool cliSetCommsEnergy( uint32_t argc, uint8_t * argv[], uint32_t * argi);
static bool cliGetUploadDayOffset( uint32_t argc, uint8_t * argv[], uint32_t * argi);
static bool cliSetUploadDayOffset( uint32_t argc, uint8_t * argv[], uint32_t * argi);
static bool cliSetValue(uint32_t argc, uint8_t * argv[], uint32_t * argi);
static bool cliGetValue(uint32_t argc, uint8_t * argv[], uint32_t * argi);
static bool cliResetTemperatureAlarms(uint32_t argc, uint8_t * argv[], uint32_t * argi);

static const char rfVBATFlagsHelp[] = {
		"1   	- Last unexpected shutdown flag\r\n"
		"2   	- Last comms status flag\r\n"
		"4   	- Amber Temperature alarm flag\r\n"
		"8   	- Red Temperature alarm flag\r\n"
		"512   	- Unused_1 flag in the upper byte\r\n"
		"65535 	- All 16 flags\r\n"
};

bool cliHelpVbatFlags(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	printf("16 bit mask flags, with bit positions as:\r\n");
	printf("%s",rfVBATFlagsHelp);
	return true;
}

static const char VbatHelp[] =
{
	"Vbat subcommands:\r\n"
	" Setnodetask <task> \t\tSets node task\r\n"
	" Getnodetask \t\t\tReads node wakeup task\r\n"
	" Clearflags <flagstoclear> \tClear the passed flags.\r\n"
	" Setflags <flagstoSet> \t\tSets the passed flags.\r\n"
	" Getflags \t\t\tReads the flags \r\n"
	" GetCommsEnergy \t\tReads the energy used in the Comms Cycle.\r\n"
	" SetCommsEnergy <SetEnergy> \tSets the passed energy as the Comms energy used by the node.\r\n"
	" GetUploadDayOffset \t\tReads the upload day offset.\r\n"
	" SetUploadDayOffset \t\tSets the upload day offset.\r\n"
	" SetLocalSimulatedTemp \t\tSets the local (PCB) simulated temperature (\"t\").\r\n"
	" SetRemoteSimulatedTemp \tSets the remote (external) simulated temperature (\"t\").\r\n"
	" ClearSimulatedTemps \t\t\Clears simulated temperatures.\r\n"
	" ResetTempAlarms \t\t\Resets temperature alarms and lockouts\r\n"
	" SetVBAT \t\t\tSet VBAT value.\r\n"
	" GetVBAT \t\t\tGet VBAT value.\r\n"
};


struct cliSubCmd VbatSubCmds[] =
{
	{"Setnodetask", 			cliSetNodeTask},
	{"Getnodetask",  			cliGetNodeTask},
	{"Clearflags", 				cliClearFlags},
	{"Setflags", 				cliSetFlags},
	{"Getflags", 				cliReadFlags},
	{"GetCommsEnergy", 			cliGetCommsEnergy},
	{"SetCommsEnergy", 			cliSetCommsEnergy},
	{"GetUploadDayOffset",  	cliGetUploadDayOffset},
	{"SetUploadDayOffset",  	cliSetUploadDayOffset},
	{"SetLocalSimulatedTemp",	cliSetLocalSimulatedTemp},
	{"SetRemoteSimulatedTemp",	cliSetRemoteSimulatedTemp},
	{"ClearSimulatedTemps",		cliClearSimulatedTemps},
	{"SetVBAT",					cliSetValue},
	{"GetVBAT",					cliGetValue},
	{"ResetTempAlarms",			cliResetTemperatureAlarms}
};

bool cliHelpVbat(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	printf("%s",VbatHelp);
	return true;
}

bool cliVbat( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    bool rc_ok = true;

    if (args)
    {
		rc_ok = cliSubcommand(args,argv,argi, VbatSubCmds, sizeof(VbatSubCmds)/sizeof(*VbatSubCmds));
		if (rc_ok == false)
		{
			printf("VBAT error");
		}
    }
    else
    {
        printf("%s",VbatHelp);
    }

    return rc_ok;
}

static bool cliSetNodeTask( uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	if((argc == 1) && (argi[0] <= 0xFF))
	{
		printf("\nWriting Node Task as:0x%x, Now do a reboot...\n", argi[0]);
		Vbat_SetNodeWakeupTask(argi[0]);
	}
	else
	{
		printf("\nIncorrect Input.\n");
	}
	return true;
}

static bool cliGetNodeTask( uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	uint8_t nodeTask = 0;

	if(Vbat_GetNodeWakeupTask(&nodeTask))
	{
		printf("\nNodeTask Read:0x%x\n", nodeTask);
	}
	else
	{
		printf("\nNodeTask Read ERROR\n");
	}
	return true;
}

static bool cliSetUploadDayOffset( uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	if((argc == 1) && (argi[0] <= 0xFF))
	{
		printf("\nWriting Upload Day Offset as: %d.\n", argi[0]);
		Vbat_SetUploadDayOffset(argi[0]);
	}
	else
	{
		printf("\nIncorrect Input.\n");
	}
	return true;
}

static bool cliGetUploadDayOffset( uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	uint8_t UploadDayOffset = Vbat_GetUploadDayOffset();

	printf("\nUploadDayOffset Read:%d\n", UploadDayOffset);

	return true;
}

static bool cliClearFlags( uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	if((argc == 1) && (argi[0] > 0) && (argi[0] <= 0xFFFF))
	{
		printf("\nFlag to be cleared: 0x%x, Now do a reboot...\n", argi[0]);
		Vbat_ClearFlag(argi[0]);
	}
	else
	{
		printf("Invalid Input, refer help \n");
	}
	return true;
}

static bool cliSetFlags( uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	if((argc == 1) && (argi[0] > 0) && (argi[0] <= 0xFFFF))
	{
		printf("\nWriting Flag as: 0x%x, Now do a reboot...\n", argi[0]);
		Vbat_SetFlag(argi[0]);
	}
	else
	{
		printf("Invalid Input, refer help \n");
	}

	return true;
}

static bool cliReadFlags( uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	uint16_t flags = 0;
	if(argc == 0)
	{
		Vbat_GetFlags(&flags);
		printf("\nFlag Read as: 0x%04x \n", flags);
	}
	else
	{
		printf("Invalid Input, No parameter required\n");
	}
	return true;
}

static bool cliGetCommsEnergy( uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	float energy = 0.0f;
	if(argc == 0)
	{
		Vbat_GetEnergyUsedInCommsCycles(&energy);
		printf("\nComms Energy Used: %.2fJ \n", energy);
	}
	else
	{
		printf("Invalid Input, No parameter required\n");
	}

	return true;
}

static bool cliSetCommsEnergy( uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	if((argc == 1) && (argi[0] >= 0) && (argi[0] <= 83000))
	{
		printf("\nSetting Comms Energy to: %.2fJ \n", atof((char*)argv[0]));
		Vbat_SetEnergyUsedInCommsCycles(atof((char*)argv[0]));
	}
	else
	{
		printf("Invalid Input, refer help \n");
	}

	return true;
}

static bool cliSetValue(uint32_t argc, uint8_t* argv[], uint32_t* argi)
{
	if(argi[0] < VBAT_DATA_INDX_ALARM_ATTEMPTS || (argc != 2))
	{
		return false;
	}
	Vbat_SetByte(argi[0], argi[1]);
	return true;
}

static bool cliGetValue(uint32_t argc, uint8_t* argv[], uint32_t* argi)
{
	if(argc != 1)
	{
		return false;
	}
	uint8_t val;
	Vbat_GetByte(argi[0], &val);
	printf("VBAT[%d] = %d\r\n",argi[0],val);
	return true;
}



#include <limits.h>
#include <ctype.h>

// helper function for string to it parsing
static bool string_to_int(int* out, const uint8_t* pnum)
{
	char *end;
    if (*pnum == '\0' || isspace(*pnum))
    {
    	return false;
    }

	long l = strtol((char*)pnum, &end, 10);

	if ((l > INT_MAX) || (l < INT_MIN) || (*end != '\0'))
	{
		return false;
	}

	if(out)
	{
		*out = l;
	}
	return true;
}

static bool temp_in_range(int new_temp)
{
	return (new_temp >= SCHAR_MIN) && (new_temp <= SCHAR_MAX);
}

bool cliSetLocalSimulatedTemp(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	if(argc == 1)
	{
		int new_temp;
		if(string_to_int(&new_temp, argv[0]) && temp_in_range(new_temp))
		{
			m_SimulatedTemperatures.flags |= VBATRF_local_is_simulated;
			m_SimulatedTemperatures.local = new_temp;
			SetSimulatedTemperature();
		}
		else
		{
			printf("\nIncorrect Input. Did you use quotes?\n");
		}
	}
	else
	{
		printf("Invalid Input, refer help \n");
	}
	return true;
}

bool cliSetRemoteSimulatedTemp(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	if(argc == 1)
	{
		int new_temp;
		if(string_to_int(&new_temp, argv[0]) && temp_in_range(new_temp))
		{
			m_SimulatedTemperatures.flags |= VBATRF_remote_is_simulated;
			m_SimulatedTemperatures.remote = new_temp;
			SetSimulatedTemperature();
		}
		else
		{
			printf("\nIncorrect Input. Did you use quotes?\n");
		}
	}
	else
	{
		printf("Invalid Input, refer help \n");
	}
	return true;
}

bool cliClearSimulatedTemps(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	Vbat_ClearSimulatedTemperatures();
	return true;
}

static bool cliResetTemperatureAlarms(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	alarms_ResetTemperatureAlarms();
	printf("Temperature Alarms have been reset\n");

	return true;
}


#ifdef __cplusplus
}
#endif