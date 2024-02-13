#ifdef __cplusplus
extern "C" {
#endif

/*
 * ds1371.c
 *
 *  Created on: 19 Jan 2016
 *      Author: Rex Taylor BF1418 (Livingston)
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "i2c.h"

#include "PowerControl.h"
#include "PinConfig.h"
#include "Device.h"
#include "ds137n.h"

/**
 * 	Implementation of DS137n, I2C, 32-Bit Binary Counter Watchdog chip
 *
 *	The chip provides a secondary wake up source to the node,
 *	in the event that the primary wakeup source (Internal RTC) fails.
 *
 *	Each and every wakeup of the node, this chip should be programmed
 *	with the configured wake up.
 *
 *	Should this chip be the source of the current wake up
 *	(indicating that the Internal RTC has failed)
 *	then the boot strap loader should implement the necessary action .
 *
 *	NOTE 1:- Rev 1 hardware uses DS1371. Rev 2 hardware uses DS1374
 *	This is due to not requiring the oscillator output which operates
 *	by default on power on of the hardware.
 *
 *  NOTE 2: A +Vdig settling time of about 250 msec is required before the
 * 			chip could be operated, after a power on.
 */

/*! \brief Address map
 *
 */

// private function prototypes
static bool ds137nTakeMutex();
static bool ds137nGiveMutex();

#define DS137N_TOD_COUNTER_BYTE_0			0x00	// Time of day counter byte 0
#define DS137N_TOD_COUNTER_BYTE_1			0x01	// Time of day counter byte 1
#define DS137N_TOD_COUNTER_BYTE_2			0x02	// Time of day counter byte 2
#define DS137N_TOD_COUNTER_BYTE_3			0x03	// Time of day counter byte 3
#define DS137N_WD_ALARM_COUNTER_BYTE_0		0x04	// Watchdog/Alarm counter
#define DS137N_WD_ALARM_COUNTER_BYTE_1		0x05	// Watchdog/Alarm counter
#define DS137N_WD_ALARM_COUNTER_BYTE_2		0x06	// Watchdog/Alarm counter
#define DS137N_CONTROL_REG					0x07	// Control register
#define DS137N_STATUS_REG					0x08	// Status register

#define MUTEX_MAXWAIT_MS (300)

// global variables
static mutex_t gDs137nMutex = NULL;

static i2c_device_t counter =
{
    .address = 0x68,								// chip address
    .baudRate_kbps = 400							// baud rate
};

/*
 * @function DS137n_init()
 *
 * @desc    Allocates OS resources
 *
 * @param
 *
 * @return Bool variable - Returns true if function completed correctly
 */
bool DS137n_init()
{
    bool rc_ok = true;
    if(gDs137nMutex == NULL)
    {
        rc_ok =  (kStatus_OSA_Success == OSA_MutexCreate(&gDs137nMutex));
    }
    return rc_ok;
}

/*
 * @function ds137nTakeMutex()
 *
 * @desc	Function to take control of the ds1371 mutex
 *
 * @param
 *
 * @return Bool variable - Returns true if function completed correctly
 */
static bool ds137nTakeMutex()
{
	bool rc_ok = true;

    if (gDs137nMutex)
    {
        rc_ok = (kStatus_OSA_Success == OSA_MutexLock(&gDs137nMutex, MUTEX_MAXWAIT_MS));
    }

	return rc_ok;
}

/*
 * @function ds137nGiveMutex()
 *
 * @desc	Function to release the ds1371 mutex
 *
 * @param
 *
 * @return Bool variable - Returns true if function completed correctly
 */
static bool ds137nGiveMutex()
{
    return  kStatus_OSA_Success == OSA_MutexUnlock(&gDs137nMutex);
}

/*
 * @function DS137n_ReadStatus(uint8_t* pStatus)
 *
 * @desc	Function to return the DS1371 status
 *
 * @param   Pointer to status byte to be returned
 *
 * @return Bool variable - Returns true if function completed correctly
 */
bool DS137n_ReadStatus(uint8_t* pStatus)
{
    bool rc_ok = true;				// return result

	if(!Device_HasPMIC())
	{
		if(true == (rc_ok = ds137nTakeMutex()))
		{
			// read the status register
			rc_ok = I2C_ReadRegister(I2C2_IDX, counter, DS137N_STATUS_REG, 1, pStatus);

			// clear the OSF and AF bits to complete the read status register operation by writing 0x00
			if(false == I2C_WriteRegister(I2C2_IDX, counter, DS137N_STATUS_REG, 0x00))
			{
				rc_ok = false;
			}
		}

		// give back ownership of the ds137n mutex
		if(false == ds137nGiveMutex())
		{
			rc_ok = false;
		}
	}
	else
	{
		// TODO
		*pStatus = 0;
		rc_ok = true;
	}

	return rc_ok;
}

/*
 * @function DS137n_SetStatus(uint8_t value)
 *
 * @desc	Function to set the DS1371 status register: DS137N_STATUS_REG
 *
 * @param   Value to write to the register
 *
 * @return Bool variable - Returns true if function completed correctly
 */
bool DS137n_SetStatus(uint8_t value)
{
	bool rc_ok = false;

	if(!Device_HasPMIC())
	{
		if(ds137nTakeMutex())// take ownership of the ds1371 mutex
		{
			rc_ok = I2C_WriteRegister(I2C2_IDX, counter, DS137N_STATUS_REG, value);

			// give back ownership of the ds137n mutex
			if (false==ds137nGiveMutex())
			{
				rc_ok = false;
			}
		}
	}
	else
	{
		// TODO
		rc_ok = true;
	}
	return rc_ok;
}

/*
 * @function DS137n_ReadControl(uint8_t* pControl)
 *
 * @desc	Function to return the DS1371 status
 *
 * @param   Pointer to control byte to be returned
 *
 * @return Bool variable - Returns true if function completed correctly
 */
bool DS137n_ReadControl(uint8_t* pControl)
{
    bool rc_ok = false;				// return result

	if(!Device_HasPMIC())
	{
		if(ds137nTakeMutex())	// take ownership of the ds1371 mutex
		{
			// read the status register
			rc_ok = I2C_ReadRegister(I2C2_IDX, counter, DS137N_CONTROL_REG, 1, pControl);

			// give back ownership of the ds137n mutex
			if(false == ds137nGiveMutex())
			{
				rc_ok = false;
			}
		}
	}
	else
	{
		*pControl = 0;
		rc_ok = true;
	}
	return rc_ok;
}

/*
 * @function DS137n_SetControl(uint8_t value)
 *
 * @desc	Function to write to the DS137n_CONTROL_REG register
 *
 * @param   Value to write to the register
 *
 * @return Returns true if function completed correctly
 */
bool DS137n_SetControl(uint8_t value)
{
	bool rc_ok = false;

	if(!Device_HasPMIC())
	{
		if(ds137nTakeMutex())// take ownership of the ds1371 mutex
		{
			rc_ok = I2C_WriteRegister(I2C2_IDX, counter, DS137N_CONTROL_REG, value);

			// give back ownership of the ds137n mutex
			if (false==ds137nGiveMutex())
			{
				rc_ok = false;
			}
		}
	}
	else
	{
		// TODO
		rc_ok = true;
	}

	return rc_ok;
}

/*
 * @function DS137n_SetAlarm(uint32_t seconds)
 *
 * @desc	Function to set up the next DS137n alarm wakeup.
 * 			If zero is passed, disable the alarm
 *
 * @param   seconds - Number of seconds until wakeup
 *
 * @return Returns true if function completed correctly
 */
bool DS137n_SetAlarm(uint32_t seconds)
{
   bool rc_ok = false;
   uint8_t bufSend[4];

	if(!Device_HasPMIC())
	{
		if(true == (rc_ok = ds137nTakeMutex()))
		{
		  // clear the control and status register AF bit
		  // in order to start the ALARM counting down the WACE bit must toggle from 0 -> 1
		  // so doing the following takes care of this
		  bufSend[0] = 0;
		  bufSend[1] = 0;
		  if(true == (rc_ok = I2C_WriteRegisters(I2C2_IDX, counter, DS137N_CONTROL_REG, 2, bufSend)))
		  {
			  bufSend[0] = (seconds & 0x0000FF);
			  bufSend[1] = ((seconds & 0x00FF00) >> 8);
			  bufSend[2] = ((seconds & 0xFF0000) >> 16);
			  if(0 == seconds)
			  {
				  // Disable alarm
				  bufSend[3] = 0;
			  }
			  else
			  {
				  // 0x49;
				  // WACE [6] = 1 - WD/ALM counter is enabled
				  // WDSTR [3] - permits the alarm flag bit in the status register to assert SQW/INT (provided that the alarm is also enabled)
				  // AIE [0] - permits the AF bit in the status register to assert SQW/INT (when INTCN = 1).
				  bufSend[3] = (DS_CONTROL_WACE + DS_CONTROL_WDSTR + DS_CONTROL_AIE);
			  }
			  rc_ok = I2C_WriteRegisters(I2C2_IDX, counter, DS137N_WD_ALARM_COUNTER_BYTE_0, 4, bufSend);
		  }
		  if(false == ds137nGiveMutex())
		  {
			  rc_ok = false;
		  }
		}
	}
	else
	{
		// TODO
		rc_ok = true;
	}

	return rc_ok;
}

/*
 * @function DS137n_ReadAlarm(uint32_t* count)
 *
 * @desc	Function to read the next DS137n wakeup in seconds
 *
 * @param   pointer to the number of seconds variable to be returned
 *
 * @return Returns true if function completed correctly
 */
bool DS137n_ReadAlarm(uint32_t* count)
{
	bool rc_ok = false;
	uint8_t bufRecv[3] = {0};	// data receive buffer

	if(!Device_HasPMIC())
	{
		if (ds137nTakeMutex())		// take ownership of the ds1371 mutex
		{
			rc_ok = I2C_ReadRegister(I2C2_IDX, counter, DS137N_WD_ALARM_COUNTER_BYTE_0, 3, bufRecv);

			// give back ownership of the ds137n mutex
			if (ds137nGiveMutex()==false)
			{
				rc_ok = false;
			}

			// calculate counts since last power on
			*count = (bufRecv[2]<<16) | (bufRecv[1]<<8) | (bufRecv[0]);
		}
	}
	else
	{
		// TODO
		rc_ok = true;
	}

	return rc_ok;
}

/*
 * @function DS137n_ReadCounter(uint32_t* count)
 *
 * @desc	Function to read the current counter value
 *
 * @param   pointer to the count in seconds to be returned
 *
 * @return Returns true if function completed correctly
 */
bool DS137n_ReadCounter(uint32_t* count)
{
	bool rc_ok = false;
	uint8_t bufRecv[4] = {0};		// data receive buffer

	if(!Device_HasPMIC())
	{
		if(ds137nTakeMutex())// take ownership of the ds1371 mutex
		{
			rc_ok = I2C_ReadRegister(I2C2_IDX, counter, DS137N_TOD_COUNTER_BYTE_0, 4, bufRecv);

			// give back ownership of the ds137n mutex
			if(ds137nGiveMutex()==false)
			{
				rc_ok = false;
			}

			// calculate counts since last power on
			*count = (bufRecv[3]<<24) | (bufRecv[2]<<16) | (bufRecv[1]<<8) | (bufRecv[0]);
		}
	}
	else
	{
		// TODO
		rc_ok = true;
	}

	return rc_ok;
}

// reset DS137n counter
bool DS137n_ResetCounter()
{
	bool rc_ok = false;
	const uint8_t bufSend[] = {0, 0, 0, 0};

	if(!Device_HasPMIC())
	{
		if(ds137nTakeMutex())		// take ownership of the ds1371 mutex
		{
			rc_ok = I2C_WriteRegisters(I2C2_IDX, counter, DS137N_TOD_COUNTER_BYTE_0, sizeof(bufSend), (uint8_t*)bufSend);

			// give back ownership of the ds137n mutex
			if(ds137nGiveMutex()==false)
			{
				rc_ok = false;
			}
		}
	}
	else
	{
		// TODO
		rc_ok = true;
	}

	return rc_ok;
}


/*
 *  CLI functions
 */

static bool dsInfo(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	uint8_t regs[9] = { 0 };
	bool rc_ok = false;

	if(!Device_HasPMIC())
	{
		if(ds137nTakeMutex())// take ownership of the ds1371 mutex
		{
			if(true == (rc_ok = I2C_ReadRegister(I2C2_IDX, counter, DS137N_TOD_COUNTER_BYTE_0, 4, &regs[0])))
			{
				rc_ok = I2C_ReadRegister(I2C2_IDX, counter, DS137N_WD_ALARM_COUNTER_BYTE_0, 5, &regs[4]);
			}
			// give back ownership of the ds137n mutex
			if (false == ds137nGiveMutex())
			{
				rc_ok = false;
			}
		}
	}
	else
	{
		// TODO
		rc_ok = true;
	}

   if(rc_ok)
   {
		  printf("DS1374\n"
				"  count   = %d\n"
				"  alarm   = %d\n"
				"  control = 0x%02X\n"
				"  status  = 0x%02X\n",
				(regs[3] << 24) + (regs[2] << 16) + (regs[1] << 8) + regs[0],
				(regs[6] << 16) + (regs[5] << 8) + regs[4],
				regs[7],
				regs[8]);
   }

   return rc_ok;
}

static bool dsSetalarm(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	if(argc < 1)
	{
		return false;
	}
	return DS137n_SetAlarm(argi[0]);
}

static bool dsWritecontrol(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	if(argc < 1 || argi[0] > 255)
	{
		return false;
	}
	return DS137n_SetControl(argi[0]);
}

static bool dsWritestatus(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	if(argc < 1 || argi[0] > 255)
	{
		return false;
	}
	return DS137n_SetStatus(argi[0]);
}

typedef const struct subfunction
{
	const char* command;
	const char* help;
	bool (*function)(uint32_t argc, uint8_t * argv[], uint32_t * argi);
} CLI_Command;

CLI_Command subfunctions[] =
{
	{"info",			"show DS1374 settings",						dsInfo},
	{"setalarm",		"set DS1374 alarm register (in seconds)",	dsSetalarm},
	{"writecontrol",	"write <n> to control register",			dsWritecontrol},
	{"writestatus",		"write <n> to status register",				dsWritestatus},
	{0, 0}
};

bool cliDS1374(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	if(argc == 0)
	{
		cliDS1374LongHelp(0, 0, 0);
		return true;
	}

	CLI_Command *pCLIcmd = subfunctions;
	while(pCLIcmd->command)
	{
		if(strcmp(pCLIcmd->command, (char*)argv[0]) == 0)
		{
			// 'shift' parameters
			return (pCLIcmd->function)((argc)?argc-1:0, &argv[1], &argi[1]);
		}
		pCLIcmd++;
	}

	return false;
}

bool cliDS1374LongHelp(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	int max_length = 0;

	CLI_Command *pCLIcmd = subfunctions;
	while(pCLIcmd->command)
	{
		int this_length = strlen(pCLIcmd->command);
		if(this_length > max_length)
		{
			max_length = this_length;
		}
		pCLIcmd++;
	}

	pCLIcmd = subfunctions;
	while(pCLIcmd->command)
	{
#if 0
		printf("%s  %*s%s\n", pCLIcmd->command, max_length - strlen(pCLIcmd->command), " ", pCLIcmd->help);
#endif
		printf("%s ", pCLIcmd->command);
		for(int i=0; i < max_length - strlen(pCLIcmd->command); i++)
		{
			printf("%s", " ");
		}
		printf("%s\n", pCLIcmd++->help);
	}

	return true;
}



#ifdef __cplusplus
}
#endif