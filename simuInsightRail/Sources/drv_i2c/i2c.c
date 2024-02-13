#ifdef __cplusplus
extern "C" {
#endif

/*
 * i2c.c
 *
 *  Created on: 17 Dec 2015
 *      Author: BF1418
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "CLIio.h"
#include "CLIcmd.h"
#include "printgdf.h"

#include "i2c.h"
#include "fsl_i2c_master_driver.h"
#include "hdc1050_fsl_i2c_master_driver.h"		// TODO - added to allow delay between address write and address read

#include "PowerControl.h"
#include "PinConfig.h"
#include "fsl_gpio_driver.h"
#include "fsl_os_abstraction.h"

#include "log.h"


#define MUTEX_MAXWAIT_MS (300)

//
// each interface its own master sdk data and mutex
//
static struct i2c_driverState_str {
    i2c_master_state_t masterState;
    uint32_t sda_pin;
    uint32_t scl_pin;
    mutex_t mutex;// will block when in use
} i2c_driverState[I2C_INSTANCE_COUNT] = {{ .mutex = NULL}}; // should set all mutex to NULL initially



// find a better way for these interrupt handlers, now just copied from PE generated code.
/*
** ===================================================================
**     Interrupt handler : I2C0_IRQHandler
**
**     Description :
**         User interrupt service routine.
**     Parameters  : None
**     Returns     : Nothing
** ===================================================================
*/
void I2C0_IRQHandler(void)
{
  I2C_DRV_IRQHandler(I2C0_IDX);
  /* Write your code here ... */
}
/*
** ===================================================================
**     Interrupt handler : I2C0_IRQHandler
**
**     Description :
**         User interrupt service routine.
**     Parameters  : None
**     Returns     : Nothing
** ===================================================================
*/
void I2C1_IRQHandler(void)
{
  I2C_DRV_IRQHandler(I2C1_IDX);
  /* Write your code here ... */
}
/*
** ===================================================================
**     Interrupt handler : I2C0_IRQHandler
**
**     Description :
**         User interrupt service routine.
**     Parameters  : None
**     Returns     : Nothing
** ===================================================================
*/
void I2C2_IRQHandler(void)
{
  I2C_DRV_IRQHandler(I2C2_IDX);
  /* Write your code here ... */
}


/*
 * @function    i2c_init
 * @desc        init i2c driver/hardware
 *              to replace Processor Expert initialisation etc.
 *              will set the pins/init sdk driver structures etc.
 * @param interface instance number to init (I2C0_IDX, I2C1_IDX, I2C2_IDX etc.)
 * @param sda_pin   pin number of the sda pin (constructed with GPIO_MAKE_PIN() )
 * @param scl_pin   pin number of the sda pin (constructed with GPIO_MAKE_PIN() )
 */
bool i2c_init(uint32_t instance, uint32_t sda_pin, uint32_t scl_pin)
{
    bool rc_ok = false;

    if (instance < I2C_INSTANCE_COUNT) {
        rc_ok = true;

        if ( i2c_driverState[instance].mutex == NULL) {
            // create mutex
            rc_ok =  (kStatus_OSA_Success == OSA_MutexCreate(&i2c_driverState[instance].mutex));
        }
        if (rc_ok) {
            // configure the pins
            /* SCL */
            PORT_HAL_SetMuxMode(g_portBase[GPIO_EXTRACT_PORT(scl_pin)], GPIO_EXTRACT_PIN(scl_pin), kPortMuxAlt5);
            PORT_HAL_SetOpenDrainCmd(g_portBase[GPIO_EXTRACT_PORT(scl_pin)], GPIO_EXTRACT_PIN(scl_pin),true);
            /* SDA */
            PORT_HAL_SetMuxMode(g_portBase[GPIO_EXTRACT_PORT(sda_pin)], GPIO_EXTRACT_PIN(sda_pin),kPortMuxAlt5);
            PORT_HAL_SetOpenDrainCmd(g_portBase[GPIO_EXTRACT_PORT(sda_pin)], GPIO_EXTRACT_PIN(sda_pin),true);

            i2c_driverState[instance].sda_pin = sda_pin;
            i2c_driverState[instance].scl_pin = scl_pin;

            // init the sdk driver structure
            rc_ok = (kStatus_I2C_Success == I2C_DRV_MasterInit( instance, &i2c_driverState[instance].masterState));
        }
    }

    return rc_ok;
}

/*
 * @function i2c_terminate()
 *
 * @desc    free resources if possible
 *
 * @param   instance of the i2c interface
 *
 * @returns Bool variable - Returns true if function completed correctly
 */

bool i2c_terminate(uint32_t instance)
{
    bool rc_ok = false;

    if (instance < I2C_INSTANCE_COUNT)
    {
        rc_ok = (kStatus_I2C_Success == I2C_DRV_MasterDeinit(instance));

        // continue even when previous went wrong
        if(&i2c_driverState[instance].mutex && *(&i2c_driverState[instance].mutex))
        {
        	rc_ok &= (kStatus_OSA_Success == OSA_MutexDestroy(&i2c_driverState[instance].mutex));

        	i2c_driverState[instance].mutex = NULL;
        }

        PORT_HAL_SetMuxMode(g_portBase[GPIO_EXTRACT_PORT(i2c_driverState[instance].sda_pin)],
                GPIO_EXTRACT_PIN(i2c_driverState[instance].sda_pin),
                kPortPinDisabled);
        PORT_HAL_SetMuxMode(g_portBase[GPIO_EXTRACT_PORT(i2c_driverState[instance].scl_pin)],
                GPIO_EXTRACT_PIN(i2c_driverState[instance].scl_pin),
                kPortPinDisabled);
    }
    return rc_ok;
}


/*
 * @function i2cTakeMutex()
 *
 * @desc    Function to take control of the i2c mutex
 *
 * @param   instance    i2c interface instance
 * @param   maxWaitMs   wait no longer...
 *
 * @returns Bool variable - Returns true if function completed correctly
 */
bool i2cTakeMutex(uint32_t instance, uint32_t maxWaitMs)
{
    bool rc_ok = false;
    if (instance < I2C_INSTANCE_COUNT) {
        if ( i2c_driverState[instance].mutex)  {
            rc_ok = (kStatus_OSA_Success == OSA_MutexLock(&i2c_driverState[instance].mutex, maxWaitMs));
        }
    }
    return rc_ok;
}
/*
 * @function i2cGiveMutex()
 *
 * @desc    Function to give back control of the i2c mutex
 *
  * @param   instance i2c interface instance
 *
 * @returns Bool variable - Returns true if function completed correctly
 */
bool i2cGiveMutex(uint32_t instance)
{
    bool rc_ok = false;

    if (instance < I2C_INSTANCE_COUNT) {
        if ( i2c_driverState[instance].mutex)  {
            rc_ok = (kStatus_OSA_Success == OSA_MutexUnlock(&i2c_driverState[instance].mutex));
        }
    }
    return rc_ok;
}


/*
 * @function I2C_ReadRegister(uint32_t instance,
 * 							i2c_device_t device,
 * 							uint8_t cmd,
 * 							uint8_t len,
 * 							uint8_t *bufRecv)
 *
 * @desc	Function to read i2c register for the device
 *
 * @param	instance 	- instance of the peripheral calling this routine
 * 			device		- device details
 * 			cmd			- command to be sent to peripheral
 * 			len			- length of returned data
 * 			bufRecv		- pointer to variable to store returned data
 *
 * @returns Bool variable - Returns true if function completed correctly
 */
bool I2C_ReadRegister(uint32_t instance,
							i2c_device_t device,
							uint8_t cmd,
							uint8_t len,
							uint8_t *bufRecv)
{
	bool rc_ok = true;
	i2c_status_t i2c_ret;
	uint8_t reg[2] = {0};

	// estimate the maximum transfer time
	uint32_t maxTimeMs;

	maxTimeMs = (1 + 1 + len)*8 / device.baudRate_kbps; // 1000*(address + command + length data) / (kilobauds * 1000), the 1000 for kilo and milli cancel out
	// take some margin of 1.5
	maxTimeMs += maxTimeMs/2;
	// some sanity checking
	if (maxTimeMs < 30) maxTimeMs = 30;

	rc_ok = i2cTakeMutex(instance, MUTEX_MAXWAIT_MS );		// take control of the i2c mutex
	if(true == rc_ok)
	{
		reg[0] = cmd;
		i2c_ret = I2C_DRV_MasterReceiveDataBlocking(instance,			// instance	The I2C peripheral instance number.
										&device,						// device	The pointer to the I2C device information structure.
										reg,							// cmdBuff	The pointer to the commands to be transferred, could be NULL.
										1,								// cmdSize	The length in bytes of the commands to be transferred, could be 0.
										(uint8_t *)bufRecv,				// rxBuff	The pointer to the data to be transferred, cannot be NULL.
										len,							// rxSize	The length in bytes of the data to be transferred, cannot be 0.
										maxTimeMs);							// timeout_ms	A timeout for the transfer in milliseconds.
		if (i2c_ret != kStatus_I2C_Success)
		{
			LOG_EVENT( 0, LOG_LEVEL_I2C, ERRLOGFATAL, "I2C_ReadRegister() failed, Addr=0x%02x, Cmd:0x%02x, error = %d",
					  device.address, cmd, i2c_ret );
			rc_ok = false;
		}

		/* return control of the i2c mutex */
		if(false == i2cGiveMutex(instance))
		{
			rc_ok = false;
		}
	}

	return rc_ok;
}

/*
 * @function HDC1050_I2C_ReadRegister(uint32_t instance,
 * 							i2c_device_t device,
 * 							uint8_t cmd,
 * 							uint8_t len,
 * 							uint8_t *bufRecv)
 *
 * @desc	Function to read register specifically from HDC1050 device
 *
 * @param	instance 	- instance of the peripheral calling this routine
 * 			device		- device details
 * 			cmd			- command to be sent to peripheral
 * 			len			- length of returned data
 * 			bufRecv		- pointer to variable to store returned data
 *
 * @returns Bool variable - Returns true if function completed correctly
 */
bool HDC1050_I2C_ReadRegister(uint32_t instance,
							i2c_device_t device,
							uint8_t cmd,
							uint8_t len,
							uint8_t *bufRecv)
{
	bool rc_ok = true;
	i2c_status_t i2c_ret;
	uint8_t reg[2] = {0};

	rc_ok = i2cTakeMutex(instance, MUTEX_MAXWAIT_MS );		// take control of the i2c mutex
	if(true == rc_ok)
	{
		reg[0] = cmd;
		i2c_ret = HDC1050_I2C_DRV_MasterReceiveDataBlocking(instance,	// instance	The I2C peripheral instance number.
										&device,						// device	The pointer to the I2C device information structure.
										reg,							// cmdBuff	The pointer to the commands to be transferred, could be NULL.
										1,								// cmdSize	The length in bytes of the commands to be transferred, could be 0.
										(uint8_t *)bufRecv,				// rxBuff	The pointer to the data to be transferred, cannot be NULL.
										len,							// rxSize	The length in bytes of the data to be transferred, cannot be 0.
										200);							// timeout_ms	A timeout for the transfer in microseconds.
		if (i2c_ret != kStatus_I2C_Success)
		{
			LOG_DBG(LOG_LEVEL_I2C,"HDC1050_I2C_ReadRegister() failed, error = %d\n", i2c_ret );
			rc_ok = false;
			bufRecv[0] = 0xff;
			bufRecv[1] = 0xff;
		}

		/* return control of the i2c mutex */
		if(false == i2cGiveMutex(instance))
		{
			rc_ok = false;
		}
	}

	return rc_ok;
}

/*
 * @function I2C_WriteRegister(uint32_t instance,
 * 						i2c_device_t device,
 * 						uint8_t cmd,
 * 						uint8_t data)
 *
 * @desc	Function to write data to the specified register
 *
 * @param	instance 	- instance of the peripheral calling this routine
 * 			device		- device details
 * 			cmd			- the peripheral register
 * 			data		- data to be transmitted
 *
 * @returns Bool variable - Returns true if function completed correctly
 */
bool I2C_WriteRegister(uint32_t instance,
						i2c_device_t device,
						uint8_t cmd,
						uint8_t data)
{
    return I2C_WriteRegisters(instance,device, cmd, 1, &data );
}

/*
 * @function I2C_WriteRegisters(uint32_t instance,
 *                      i2c_device_t device,
 *                      uint8_t cmd,
 *                      int16_t len,
 *                      uint8_t *data)
 *
 * @desc    Function to write data to the specified register
 *
 * @param   instance    - instance of the peripheral calling this routine
 *          device      - device details
 *          cmd         - the peripheral register
 *          len         - length of the data array
 *          data        - data array  to be transmitted
 *
 * @returns Bool variable - Returns true if function completed correctly
 */
bool I2C_WriteRegisters(uint32_t instance,
                        i2c_device_t device,
                        uint8_t cmd,
                        uint16_t len,
                        uint8_t *data)
{
    bool rc_ok = true;
    i2c_status_t i2c_ret;
    uint8_t reg[2] = {0};

    // estimate the maximum transfer time
    uint32_t maxTimeMs;

    maxTimeMs = (1 + 1 + len)*8 / device.baudRate_kbps; // 1000*(address + command + length data) / (kilobauds * 1000), the 1000 for kilo and milli cancel out
    // take some margin of 1.5
    maxTimeMs += maxTimeMs/2;
    // some sanity checking
    if (maxTimeMs < 30) maxTimeMs = 30;


    rc_ok = i2cTakeMutex(instance, MUTEX_MAXWAIT_MS);     // take control of the i2c mutex
    if(true == rc_ok)
    {
        reg[0] = cmd;

        i2c_ret = I2C_DRV_MasterSendDataBlocking(instance,          // instance The I2C peripheral instance number.
                                                &device,            // device   The pointer to the I2C device information structure.
                                                reg,                // cmdBuff  The pointer to the commands to be transferred, could be NULL.
                                                1,                  // cmdSize  The length in bytes of the commands to be transferred, could be 0.
                                                data,               // txBuff   The pointer to the data to be transferred, cannot be NULL.
                                                len,                // txSize   The length in bytes of the data to be transferred, cannot be 0.
                                                maxTimeMs);         // timeout_ms   A timeout for the transfer in microseconds.
        if (i2c_ret != kStatus_I2C_Success)
        {
            LOG_EVENT( 0, LOG_LEVEL_I2C, ERRLOGFATAL, "I2C_WriteRegisters() failed, Addr=0x%02x, Cmd:0x%02x, error = %d",
            					  device.address, cmd, i2c_ret );
            rc_ok = false;
        }

        /* return control of the i2c mutex */
		if(false == i2cGiveMutex(instance))
		{
			rc_ok = false;
		}
    }

    return rc_ok;
}

/*
 * @function HDC1050_I2C_WriteRegister(uint32_t instance,
 * 						i2c_device_t device,
 * 						uint8_t cmd,
 * 						uint16_t data,
 * 						uint8_t dataLen)
 *
 * @desc	Function to write data to the specified register.
 * 			Specifically the HDC1050
 *
 * @param	instance 	- instance of the peripheral calling this routine
 * 			device		- device details
 * 			cmd			- the peripheral register
 * 			data		- data to be transmitted
 *
 * @returns Bool variable - Returns true if function completed correctly
 */
bool HDC1050_I2C_WriteRegister(uint32_t instance,
						i2c_device_t device,
						uint8_t cmd,
						uint16_t data,
						uint8_t dataLen)
{
	bool rc_ok = true;
	i2c_status_t i2c_ret;
	uint8_t reg[2] = {0};
	uint8_t bufSend[10] = {0};

	rc_ok = i2cTakeMutex(instance, MUTEX_MAXWAIT_MS);		// take control of the i2c mutex
	if(true == rc_ok)
	{
		reg[0] = cmd;

		bufSend[0] = (data & 0xFF00) >> 8;
		bufSend[1] = (data & 0x00FF);

		i2c_ret = HDC1050_I2C_DRV_MasterSendDataBlocking(instance,		// instance	The I2C peripheral instance number.
												&device,				// device	The pointer to the I2C device information structure.
												reg,					// cmdBuff	The pointer to the commands to be transferred, could be NULL.
												1,						// cmdSize	The length in bytes of the commands to be transferred, could be 0.
												bufSend,				// txBuff	The pointer to the data to be transferred, cannot be NULL.
												dataLen,				// txSize	The length in bytes of the data to be transferred, cannot be 0.
												200);					// timeout_ms	A timeout for the transfer in microseconds.
		if (i2c_ret != kStatus_I2C_Success)
		{
			LOG_DBG(LOG_LEVEL_I2C,"I2C_WriteRegister() failed, error = %d\n", i2c_ret);
			rc_ok = false;
		}

        /* return control of the i2c mutex */
		if(false == i2cGiveMutex(instance))
		{
			rc_ok = false;
		}
	}

	return rc_ok;
}


#ifdef __cplusplus
}
#endif