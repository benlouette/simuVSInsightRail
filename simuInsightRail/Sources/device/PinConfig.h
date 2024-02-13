#ifdef __cplusplus
extern "C" {
#endif

/*
 * PinConfig.h
 *
 *  Created on: Dec 22, 2015
 *      Author: Bart Willemse
 */

#ifndef PINCONFIG_H_
#define PINCONFIG_H_

#include <stdbool.h>
#include "fsl_port_hal.h"

void pinConfigDigitalIn(uint32_t pinName,
                        port_mux_t muxMode,
                        bool isPullEnable,
	                    port_pull_t pullSelect,
	                    port_interrupt_config_t interruptConfig);
void pinConfigDigitalOut(uint32_t pinName,
                         port_mux_t muxMode,
                         uint32_t outputLogic,
                         bool isOpenDrainEnabled);
void pinConfigNonDigital(uint32_t pinName,
                         port_mux_t muxMode);
void pinClearIntFlag(uint32_t pinName);
//void portClearAllIntFlags(uint32_t PortIndex);

// Test functionality
void pinConfigTest(void);

#endif // PINCONFIG_H_



#ifdef __cplusplus
}
#endif