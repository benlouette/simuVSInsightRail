#ifdef __cplusplus
extern "C" {
#endif

/*
 * hdc1050.c
 *
 *  Created on: 2nd May 2016
 *      Author: Rex Taylor BF1418 (Livingston)
 */

#include "hdc1050_fsl_i2c_master_driver.h"
#include "log.h"

#include "i2c.h"

#include "hdc1050.h"

#define MUTEX_MAXWAIT_MS (300)

// Register definitions
// Note- Registers 0x03 - 0xFA are reserved and should not be written
#define HDC1050_TEMPERATURE					0x00	// Temperature measurement output - Reset value= 0x0000
#define HDC1050_HUMIDITY					0x01	// Relative humidity measurement output - Reset value= 0x0000
#define HDC1050_CONFIGURATION				0x02	// HDC1050 configuration and status - Reset value= 0x1000
#define HDC1050_SERIAL_ID_1					0xFB	// First 2 bytes of the serial ID part
#define HDC1050_SERIAL_ID_2					0xFC	// Mid 2 bytes of the serial ID part
#define HDC1050_SERIAL_ID_3					0xFD	// Last byte bit of the serial ID part
#define HDC1050_MANUFACTURER_ID				0xFE	// ID of Texas Instruments - Reset value= 0x5449
#define HDC1050_DEVICE_ID					0xFF	// ID of the HDC1050 device - Reset value= 0x1050

static bool ReadRegister(uint8_t reg, uint16_t* value);
static bool WriteRegister(uint8_t reg, uint16_t value);

static i2c_device_t humidity =
{
    .address = 0x40,
    .baudRate_kbps = 400
};

static mutex_t gHdc1050Mutex = NULL;

/*
 * @function hdc1050_init()
 *
 * @desc    Allocates OS resources
 *
 * @param
 *
 * @return Bool variable - Returns true if function completed correctly
 */
bool hdc1050_init()
{
    bool rc_ok = true;
    if(gHdc1050Mutex == NULL)
    {
        rc_ok =  (kStatus_OSA_Success == OSA_MutexCreate(&gHdc1050Mutex));
    }
    return rc_ok;
}

/*
 * @function hdc1050TakeMutex()
 *
 * @desc    Function to take control of the humidity chip mutex
 *
 * @param
 *
 * @return Bool variable - Returns true if function completed correctly
 */
static bool hdc1050TakeMutex()
{
    bool rc_ok = false;

	if (gHdc1050Mutex) {
	    rc_ok = (kStatus_OSA_Success == OSA_MutexLock(&gHdc1050Mutex, MUTEX_MAXWAIT_MS));
	}
	return rc_ok;
}

/*
 * @function hdc1050GiveMutex()
 *
 * @desc    Function to release the humidity chip mutex
 *
 * @param
 *
 * @return Bool variable - Returns true if function completed correctly
 */
static bool  hdc1050GiveMutex()
{
	return (kStatus_OSA_Success == OSA_MutexUnlock(&gHdc1050Mutex));

}
#if 0
// these functions are nowhere used !
uint16_t HDC1050_ReadManufacturerId()
{
	uint16_t value = 0x00;

	ReadRegister(HDC1050_MANUFACTURER_ID, &value);

	value = ((value & 0x00FF) << 8) | ((value & 0xff00) >> 8);
	return value;
}

uint16_t HDC1050_ReadDeviceId()
{
	uint16_t value = 0x00;

	ReadRegister(HDC1050_DEVICE_ID, &value);

	value = ((value & 0x00FF) << 8) | ((value & 0xff00) >> 8);
	return value;
}
#endif

bool HDC1050_ReadHumidityTemperature(float* humidityVal, float* temperature)
{
	bool rc_ok = false;
	uint16_t value = 0x00;

	if (hdc1050TakeMutex()) {
#if 0

	// not used, and the modified read driver call has a serious delay

	// Read device Id
	ReadRegister(HDC1050_DEVICE_ID, &value);
	value  = ((value & 0x000000FF) << 8) | ((value & 0x0000ff00) >> 8);
	//printf("HDC1050_DEVICE_ID = 0x%02x\r\n", value);

	ReadRegister(HDC1050_MANUFACTURER_ID, &value);
	value  = ((value & 0x000000FF) << 8) | ((value & 0x0000ff00) >> 8);
	//printf("HDC1050_MANUFACTURER_ID = 0x%02x\r\n", value);

	ReadRegister(HDC1050_SERIAL_ID_1, &value);
	value  = ((value & 0x000000FF) << 8) | ((value & 0x0000ff00) >> 8);
	//printf("HDC1050_SERIAL_ID_1 = 0x%02x\r\n", value);

	ReadRegister(HDC1050_SERIAL_ID_2, &value);
	value  = ((value & 0x000000FF) << 8) | ((value & 0x0000ff00) >> 8);
	//printf("HDC1050_SERIAL_ID_2 = 0x%02x\r\n", value);

	ReadRegister(HDC1050_SERIAL_ID_3, &value);
	value  = ((value & 0x000000FF) << 8) | ((value & 0x0000ff00) >> 8);
	//printf("HDC1050_SERIAL_ID_3 = 0x%02x\r\n", value);
#endif

        value= 0x00;
        rc_ok = WriteRegister(HDC1050_CONFIGURATION, value);
    //	ReadRegister(HDC1050_CONFIGURATION, &value);
    //	value  = ((value & 0x000000FF) << 8) | ((value & 0x0000ff00) >> 8);
        //printf("HDC1050_CONFIGURATION = 0x%02x\r\n", value);
        if (rc_ok) rc_ok =ReadRegister(HDC1050_HUMIDITY, &value);
        if (rc_ok) {
            value  = ((value & 0x000000FF) << 8) | ((value & 0x0000ff00) >> 8);
            //printf("Humidity raw = 0x%02x\r\n", value);
            *humidityVal = value;		//((value & 0x000000FF) << 8) | ((value & 0x0000ff00) >> 8);
            *humidityVal = (float)( (*humidityVal/65536) *100 );
        }
        if (rc_ok) rc_ok = ReadRegister(HDC1050_TEMPERATURE, &value);
        if (rc_ok) {
            value  = ((value & 0x000000FF) << 8) | ((value & 0x0000ff00) >> 8);
            //printf("Temp raw = 0x%02x\r\n", value);
            *temperature = value;       // ((value & 0x000000FF) << 8) | ((value & 0x0000ff00) >> 8);
            *temperature = (float)( (*temperature/65536) *165 )-40;
        }
        // always give back when we took it
        if (hdc1050GiveMutex() == false) {
            rc_ok = false;
        }
	}

	if (!rc_ok) {
	    LOG_DBG(LOG_LEVEL_I2C,"HDC1050_ReadHumidityTemperature() FAILED\n");
	}

	return rc_ok;
}



static bool ReadRegister(uint8_t reg, uint16_t* value)
{

	return HDC1050_I2C_ReadRegister(I2C0_IDX, humidity, reg, 2, (uint8_t *)value);
}

static bool WriteRegister(uint8_t reg, uint16_t value)
{
	return HDC1050_I2C_WriteRegister(I2C0_IDX, humidity, reg, value, 2);
}




#ifdef __cplusplus
}
#endif