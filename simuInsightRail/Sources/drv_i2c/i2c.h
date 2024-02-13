#ifdef __cplusplus
extern "C" {
#endif

/*
 * i2c.h
 *
 *  Created on: 17 Dec 2015
 *      Author: BF1418
 */

#ifndef I2C_H_
#define I2C_H_

/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>
#include "fsl_i2c_hal.h"
#include "fsl_i2c_master_driver.h"
#include "fsl_i2c_slave_driver.h"
#include "fsl_i2c_shared_function.h"

#include "log.h"

bool i2c_init(uint32_t instance, uint32_t sda_pin, uint32_t scl_pin);
bool i2c_terminate(uint32_t instance);
bool i2cGiveMutex(uint32_t instance);
bool i2cTakeMutex(uint32_t instance, uint32_t maxWaitMs);

bool I2C_ReadRegister(uint32_t instance,
							i2c_device_t device,
							uint8_t cmd,
							uint8_t len,
							uint8_t *bufRecv);

bool I2C_WriteRegister(uint32_t instance,
						i2c_device_t device,
						uint8_t cmd,
						uint8_t data);

bool I2C_WriteRegisters(uint32_t instance,
                        i2c_device_t device,
                        uint8_t cmd,
                        uint16_t len,
                        uint8_t *data);

bool HDC1050_I2C_ReadRegister(uint32_t instance,
							i2c_device_t device,
							uint8_t cmd,
							uint8_t len,
							uint8_t *bufRecv);

bool HDC1050_I2C_WriteRegister(uint32_t instance,
						i2c_device_t device,
						uint8_t cmd,
						uint16_t data,
						uint8_t dataLen);

#endif /* I2C_H_ */



#ifdef __cplusplus
}
#endif