#ifdef __cplusplus
extern "C" {
#endif

/*
 * Vbat.h
 *
 *  Created on: July 10, 2017
 *      Author: Jayant Rampuria
 *      to store and retrieve most frequently updating parameters.
 */

#ifndef SOURCES_APP_VBAT_H_
#define SOURCES_APP_VBAT_H_

#include <stdint.h>
#include <stdbool.h>

// *****************************************************************************************
// Byte array index for the VBAT RF. Any new data must be < VBAT_DATA_INDX_CHKSUM
// and ***MUST*** be added at VBAT_DATA_DUMMY i.e. appended to preserve values across a FW change
// *****************************************************************************************
typedef enum
{
	VBAT_DATA_INDX_NODE_TASK				= 0,	// 1 byte used for node task
	VBAT_DATA_INDX_FLAGS           			= 1,	// 2 bytes used for storing flags
	VBAT_DATA_INDX_ALARM           			= 3,	// 1 byte used for alarm state handling (Harvester only)
	VBAT_DATA_INDX_UPTIME           		= 4,	// 4 bytes used for storing Up_Time
	VBAT_DATA_INDX_TOTAL_ENERGY_USED        = 8,	// 4 bytes used for storing Total energy consumed by the sensor
	VBAT_DATA_INDX_COMMS_ENERGY_USED       	= 12,	// 4 bytes used for storing Total Comms energy consumed by the sensor
	VBAT_STACK_OVERFLOW_TASK_INDX           = 16,	// 1 byte used for storing task Index.
	VBAT_DATA_UPLOAD_DAY_OFFSET				= 17,	// 1 byte used for storing upload day offset
	VBAT_DATA_INDX_PCB_SIM_TEMP				= 18,	// 2 bytes used for storing simulated local temperature (including self-check)
	VBAT_DATA_INDX_EXT_SIM_TEMP				= 20,	// 2 bytes used for storing simulated remote temperature (including self-check)
	VBAT_DATA_INDX_ALARM_ATTEMPTS			= 22,	// 1 byte used for storing 'quick' alarm retry count and state
	VBAT_DATA_INDX_LIMIT_ATTEMPTS			= 23,	// 1 byte used for storing 'quick' limit alarm retry count and state
	VBAT_DATA_INDX_24_HOUR_COUNTER_ALARMS	= 24,	// 1 byte Down counter, decremented every 10 min wake. Used for daily (Amber/Red) Alarm send retry and masking
	VBAT_DATA_INDX_24_HOUR_COUNTER_LIMITS	= 25,	// 1 byte Down counter, decremented every 10 min wake. Used for daily Limit alarm retry and masking

	//VBAT_DATA_DUMMY           			= 26,	// Just a place holder.
	/* Add new index here */
	VBAT_DATA_INDX_WATCHDOG_L				= 29,	// watchdog low flag
	VBAT_DATA_INDX_WATCHDOG_H				= 30,	// watchdog high flag
	VBAT_DATA_INDX_CHKSUM					= 31,	// 1 byte used for the checksum
}VBAT_DATA_INDEX_t;

// Enum defines various flags and their unique bit mask values out of 16 bits.
typedef enum
{
	VBATRF_FLAG_UNEXPECTED_LAST_SHUTDOWN 		= 1<<0,		// When set on power on indicates an unexpected shutdown.
	VBATRF_FLAG_LAST_COMMS_OK					= 1<<1,		// When set indicates THE LAST COMMS was successful.
	VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG 	= 1<<2,		// When set indicates AMBER temperature alarm.
	VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG 		= 1<<3,		// When set indicates RED temperature alarm.
	VBATRF_FLAG_HARD_FAULT						= 1<<4,		// When set indicates a HARD FAULT exception detected.
	VBATRF_FLAG_MODEM							= 1<<5,		// When set indicates that the modem is active
	VBATRF_FLAG_GNSS							= 1<<6,		// When set indicates that the GNSS is active
	VBATRF_FLAG_STACK_OVERFLOW					= 1<<7,		// When set indicates that the stack overflow exception detected.
	VBATRF_FLAG_HEAP_OVERFLOW					= 1<<8,		// When set indicates that the FreeRTOS Heap is running out of space.
	VBATRF_FLAG_LOW_TEMPERATURE_LIMIT_TRIG		= 1<<9,		// When set indicates LOW temperature alarm (limit).
	VBATRF_FLAG_HIGH_TEMPERATURE_LIMIT_TRIG		= 1<<10,	// When set indicates HIGH temperature alarm (limit).
	VBATRF_FLAG_AMBER_MASKED					= 1<<11,	// When set, Amber alarm is masked (i,e will not set Amber Triggered flag)
	VBATRF_FLAG_RED_MASKED						= 1<<12,	// When set, Red alarm is masked (i,e will not set Red Triggered flag)
	VBATRF_FLAG_LOW_TEMPERATURE_LIMIT_MASKED	= 1<<13,	// When set, Low temp alarm is masked (i,e will not set Low Temp Triggered flag)
	VBATRF_FLAG_HIGH_TEMPERATURE_LIMIT_MASKED	= 1<<14,	// When set, High temp alarm is masked (i,e will not set High Temp Triggered flag)

	VBATRF_FLAG_UNUSED_1						= 1<<15,	// Unused flag
} VBATRF_FLAGS_BITMASK_t;

typedef enum
{
	VBATRF_remote_is_simulated				= 1,
	VBATRF_local_is_simulated				= 2
} VBATRF_TEMPERATURE_FLAGS_t;

#ifdef _MSC_VER // Check if using Microsoft Visual Studio compiler

// Disable warning about nonstandard extension used: nameless struct/union
#pragma warning(disable: 4201)

// Define the structure with #pragma pack to ensure it's packed
#pragma pack(push, 1)
typedef struct
{
	int8_t remote;
	int8_t local;
	uint8_t flags;
} SimulatedTemperature_t;
#pragma pack(pop)

#else // Assume GCC-based compiler

// Define the structure with __attribute__((packed))
typedef struct __attribute__((packed))
{
	int8_t remote;
	int8_t local;
	uint8_t flags;
} SimulatedTemperature_t;

#endif


void Vbat_Init();

// Node wakeup task storage and retrieval.
void Vbat_SetNodeWakeupTask(uint8_t nodeTask);
bool Vbat_GetNodeWakeupTask(uint8_t *nodeTask);

// Node alarm state store/retrieve
void Vbat_SetAlarmState(uint8_t state);
bool Vbat_GetAlarmState(uint8_t *pState);

bool Vbat_IsFlagSet(uint16_t flag);
bool Vbat_SetFlag(uint16_t flag);
bool Vbat_ClearFlag(uint16_t flag);
bool Vbat_GetFlags(uint16_t* pFlagVar);
bool Vbat_GetUpTime(uint32_t *pUpTime);
bool Vbat_SetUpTime(uint32_t UpTime);
bool Vbat_GetEnergyUsedInCommsCycles(float *pfEnergyConsumed);
bool Vbat_SetEnergyUsedInCommsCycles(float fEnergyUsed_Joules);
bool Vbat_AddEnergyUsedInCommsCycles(float fEnergyUsed_Joules);
void Vbat_FlagHardFault(void);
void Vbat_CheckForWatchdog();
void Vbat_FlagStackOverflow(uint8_t taskIndx);
bool Vbat_GetStackOverflowTaskIndx(uint8_t* pTaskIndx);
void Vbat_FlagHeapOverflow();

bool Vbat_SetByte(VBAT_DATA_INDEX_t index, uint8_t val);
bool Vbat_GetByte(VBAT_DATA_INDEX_t index, uint8_t* pVal);

uint8_t Vbat_GetUploadDayOffset();
bool Vbat_SetUploadDayOffset(uint8_t UploadDayOffset);

SimulatedTemperature_t* Vbat_CheckForSimulatedTemperatures();
void Vbat_ClearSimulatedTemperatures();

// CLI related Api's
bool cliHelpVbatFlags(uint32_t argc, uint8_t * argv[], uint32_t * argi);
bool cliVbat( uint32_t args, uint8_t * argv[], uint32_t * argi);
bool cliHelpVbat(uint32_t argc, uint8_t * argv[], uint32_t * argi);

bool cliSetLocalSimulatedTemp(uint32_t argc, uint8_t * argv[], uint32_t * argi);
bool cliSetRemoteSimulatedTemp(uint32_t argc, uint8_t * argv[], uint32_t * argi);
bool cliClearSimulatedTemps(uint32_t argc, uint8_t * argv[], uint32_t * argi);

#endif /* SOURCES_APP_VBAT_H_ */


#ifdef __cplusplus
}
#endif