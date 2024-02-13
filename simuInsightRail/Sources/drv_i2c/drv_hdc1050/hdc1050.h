#ifdef __cplusplus
extern "C" {
#endif

/*
 * hdc1050.c
 *
 *  Created on: 2nd May 2016
 *      Author: Rex Taylor BF1418 (Livingston)
 */

#ifndef HDC1050_H_
#define HDC1050_H_

//uint16_t HDC1050_ReadManufacturerId();
//uint16_t HDC1050_ReadDeviceId();
bool hdc1050_init();
bool HDC1050_ReadHumidityTemperature(float* humidity, float* temperature);

#endif	// HDC1050_H_


#ifdef __cplusplus
}
#endif