#ifdef __cplusplus
extern "C" {
#endif

/*
 * DrvTmp431.c
 *
 *  Created on: 17 Dec 2015
 *      Author: Rex Taylor BF1418 (Livingston)
 */

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#include "i2c.h"

#include "PowerControl.h"
#include "PinConfig.h"
#include "PinDefs.h"

#include "DrvTmp431.h"

#include "fsl_os_abstraction.h"
#include "xTaskDevice.h"

#define TMP431_BUSY		0x80
#define TMP431_OPEN		0x04

#define MUTEX_MAXWAIT_MS (300)

#define EXTENDED_TEMP_RANGE_OFFSET			64

#define TMP431_MAX_TEMP_REG                 (5)

#define TMP431_RUN		0x24	// bit[7] 0 = ALERT Enabled, 1 = ALERT masked
#define TMP431_SHUTDOWN	0x64	// bit[6] 0 = Run, 1 = Shutdown
								// bit[5] 0 = ALERT mode, 1 = THERM mode
								// bit[4-3]	reserved
								// bit[2] 0 = 0°C to 127°C, 1 = 55°C to 150°C
								// bit[1-0] reserved


// used bit masks inside registers
#define TMP431_CFG1_SD_MASK                 (0x40)

#define MAX_SAMPLES		(3)
#define MAX_SENSORS		(2)
#define TMP431_LIMIT	(2)

static i2c_device_t tmp431Dev =
{
    .address = 0x4c,
    .baudRate_kbps = 400
};

// global variables
static mutex_t gTmp431Mutex = NULL;
static uint32_t I2cbus = I2C0_IDX;

/*
 * @function tmp431_init()
 *
 * @desc    Allocates OS resources
 *
 * @param
 *
 * @return Bool variable - Returns true if function completed correctly
 */
bool tmp431_init()
{
    bool rc_ok = true;
    if(gTmp431Mutex == NULL)
    {
        rc_ok =  (kStatus_OSA_Success == OSA_MutexCreate(&gTmp431Mutex));
    }
    if (Device_GetHardwareVersion() == HW_PASSRAIL_REV1)
    {
    	I2cbus = I2C2_IDX;
    }
    return rc_ok;
}

/*
 * @function tmp431TakeMutex()
 *
 * @desc    Function to take control of the temperature chip mutex
 *
 * @param
 *
 * @return Bool variable - Returns true if function completed correctly
 */
static bool tmp431TakeMutex()
{
    bool rc_ok = false;

    if (gTmp431Mutex)
    {
        rc_ok = (kStatus_OSA_Success == OSA_MutexLock(&gTmp431Mutex, MUTEX_MAXWAIT_MS));
    }
	return rc_ok;
}

/*
 * @function tmp431GiveMutex()
 *
 * @desc    Function to release the temperature chip mutex
 *
 * @param
 *
 * @return Bool variable - Returns true if function completed correctly
 */
static bool tmp431GiveMutex()
{
    return  kStatus_OSA_Success == OSA_MutexUnlock(&gTmp431Mutex);
}


/*
 * @function ReadTmp431Register()
 *
 * @desc    Function to read a TMP431 register
 *
 * @param register to be read
 *
 * @param pointer to store the register value
 *
 * @return Bool variable - Returns true if function completed correctly
 */
bool ReadTmp431Register(uint32_t regname, uint8_t* value)
{
	bool rc_ok = false;

	if (tmp431TakeMutex())
	{
        rc_ok = I2C_ReadRegister(I2cbus, tmp431Dev, regname, 1, value);

        // always give back when we took it
        if (tmp431GiveMutex()==false)
        {
            rc_ok = false;
        }
	}

	return rc_ok;
}

//
// Read local and remote temperatures
// Compensate for extended temperature mode
//

bool ReadTmp431Temperatures(float* pLocalTemp, float* pRemoteTemp)
{
    bool rc_ok = false;
    uint8_t temps[TMP431_MAX_TEMP_REG] = {0x00};
    uint8_t bufB;
    const uint8_t reg[TMP431_MAX_TEMP_REG] = {
    		STATUS_REGISTER_TEMP,
			MSB_LOCAL_TEMP_READ,
    		LSB_LOCAL_TEMP_READ,
    		MSB_REMOTE_TEMP_READ,
    		LSB_REMOTE_TEMP_READ,
    };

    if(Device_HasPMIC())
    {
    	*pLocalTemp = 0;
    	*pRemoteTemp = 0;
    	return true;
    }

	// tmp431 is may be disabled
	*pLocalTemp =
	*pRemoteTemp = NAN;

	if (tmp431TakeMutex())
	{
		do
		{
			// Set THERM mode, extended temperature range the shutdown
			if(!I2C_WriteRegister(I2cbus, tmp431Dev, CONFIG_REG_1_WRITE, TMP431_SHUTDOWN))
				break;

			// check it's in the correct mode
			if(!I2C_ReadRegister(I2cbus, tmp431Dev, CONFIG_REG_1_READ, 1, &bufB) || (bufB != TMP431_SHUTDOWN))
				break;

			// do a one shot reading
			if(!I2C_WriteRegister(I2cbus, tmp431Dev, ONE_SHOT_WRITE, 0))
				break;

			// wait till not busy, internal channel takes 17ms, external about 130ms, so set timeout
			int i = 200;
			do
			{
				rc_ok = I2C_ReadRegister(I2cbus, tmp431Dev, STATUS_REGISTER_TEMP, 1, &temps[0]);
				if(!rc_ok || (false == (temps[0] & TMP431_BUSY)))
					break;
				vTaskDelay(1/portTICK_PERIOD_MS);
			} while(i--);

			// have we had an error
			if(!rc_ok) break;

			// still busy so call it a day
			if(temps[0] & TMP431_BUSY)
			{
				rc_ok = false;
				break;
			}

			// read the required registers
			for(i = 0; i < TMP431_MAX_TEMP_REG; i++)
			{
				rc_ok = I2C_ReadRegister(I2cbus, tmp431Dev, reg[i], 1, &temps[i]);
				if(!rc_ok)
					break;
			}

			// check that we're good
			if (!rc_ok)
			{
				break;
			}

			// see section 8.3.1, and table 1 and 2 in the datasheet.
			*pLocalTemp = (float)(temps[1] - EXTENDED_TEMP_RANGE_OFFSET) + ((temps[2]>>4) * 0.0625);	// extended temperature range compensation
			if(temps[0] & TMP431_OPEN)
			{
				// At this point the internal temperature is good but the remote is broken and set to NaN
				break;
			}
			*pRemoteTemp = (float)(temps[3] - EXTENDED_TEMP_RANGE_OFFSET) + ((temps[4]>>4) * 0.0625);		// extended temperature range compensation
		} while(0);

		// always give back when we took it
		if (tmp431GiveMutex()==false)
		{
			rc_ok = false;
		}
	}

	return rc_ok;
}

/*
 * @function tmp431Valid()
 *
 * @desc    Function validate a reading
 *
 * @param	localOrRemote - temperature to validate local/remote
 *
 * @param	pTemperature - pointer to value to be validated or to be updated
 *
 * @return Bool variable - Returns true if temperature validated
 */
bool tmp431Valid(int localOrRemote, float *pTemperature)
{
	int nValidReadings = 0;
	float correctedTemp, temps[MAX_SAMPLES][MAX_SENSORS];
	const char msg[] = "Temperature read error was = %d corrected to %d";

	if(!isnan(*pTemperature))
	{
		// make three readings test for ± 2 degrees
		for(int i = 0; i < MAX_SAMPLES; i++)
		{
			// read the current temperature
			if(ReadTmp431Temperatures(&temps[i][TMP431_LOCAL], &temps[i][TMP431_REMOTE]))
			{
				// test original and new value for ± 2 degrees
				if((temps[i][localOrRemote] >= *pTemperature - TMP431_LIMIT) &&
				   (temps[i][localOrRemote] <= *pTemperature + TMP431_LIMIT))
				{
					nValidReadings++;
				}
			}
		}

		// if we have three valid readings we are good to go
		if(MAX_SAMPLES == nValidReadings)
		{
			return true;
		}

		// well the original reading is out of band so what about the three we just took
		nValidReadings = 0;
		correctedTemp = temps[0][localOrRemote];
		for(int i = 1; i < MAX_SAMPLES; i++)
		{
			if((temps[i][localOrRemote] >= (correctedTemp - TMP431_LIMIT)) &&
			   (temps[i][localOrRemote] <= (correctedTemp + TMP431_LIMIT)))
			{
				nValidReadings++;
			}
		}

		// if we don't have good readings, make things obvious
		if((MAX_SAMPLES - 1) != nValidReadings)
		{
			correctedTemp = NAN;
		}
	}
	else
	{
		correctedTemp = NAN;
	}

	// NOTE LOG_EVENT uses vsnprintf to format the string. Using the %f format does
	// not work so round up the temperature to an integer and use that for formatting

	// log the error and set a known value
	LOG_EVENT(9990, LOG_NUM_APP, ERRLOGWARN, msg, (int)*pTemperature, (int)correctedTemp);
	*pTemperature = correctedTemp;
	return false;
}


#ifdef __cplusplus
}
#endif