#ifdef __cplusplus
extern "C" {
#endif

/*
 * PowerControl.h
 *
 *  Created on: Dec 22, 2015
 *      Author: Bart Willemse
 */

#ifndef POWERCONTROL_H_
#define POWERCONTROL_H_

#include <stdbool.h>
#include <stdint.h>

void powerControlInit(void);

bool powerModemOn(void);
void powerModemOff(void);
bool powerModemIsOn(void);
void powerGNSSOn(void);
void powerGNSSOff(void);
bool powerGNSSIsOn(void);
void powerAnalogOn(void);
void powerAnalogOff(void);
bool powerAnalogIsOn(void);

void powerPSU_ENABLE_ON(void);
void powerPSU_ENABLE_OFF(void);


void pinConfigGPIOForPowerOff(uint32_t pinName);

#endif // POWERCONTROL_H_



#ifdef __cplusplus
}
#endif