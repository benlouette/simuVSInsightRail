#ifdef __cplusplus
extern "C" {
#endif

/*
 * Device.h
 *
 *  Created on: 7 jul. 2014
 *      Author: g100797
 */

#ifndef DEVICE_H_
#define DEVICE_H_

/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>

/*
 * Macros
 */

/*
 * Types
 */

/* For Rev3 boards, it is proposed that the revision is reported as a decimal
 * value 3 (see ontime 9650), though the HW revision read from the pins is 1.
 * This is because, no change has been made in the HW ID pins from Rev2 to Rev3
 * boards. However, for future HW revisions (Rev 4 onwards) the HW version value
 * would be reported based on the HW ID pin settings, i.e, the HW_ID pins
 * for Rev4 would be set to binary 4.
 */

/*
 * Comment above has been superseded
 *
 * Harvester variant support:
 * Resistors are read as expected
 *
 * Battery variant support:
 * Rev 1 boards had no resistors fitted i.e they read as Rev 0
 * Rev 2 and 3 boards had one resistor fitted so they are considered to be Rev 3
 * Rev 4 and above have the correct resistors fitted
 */
typedef enum
{
    HW_PASSRAIL_REV1 = 0x01,
	HW_PASSRAIL_REV2 = 0x02,
	HW_PASSRAIL_REV3 = 0x03,
	HW_PASSRAIL_REV4 = 0x04,
	HW_PASSRAIL_REV5 = 0x05,
	HW_PASSRAIL_REV6 = 0x06,
	HW_PASSRAIL_REV10 = 0x0A,
	HW_PASSRAIL_REV12 = 0x0C,
	HW_PASSRAIL_REV13 = 0x0D,
	HW_PASSRAIL_REV14 = 0x0E,
    HW_VERSION_UNDEFINED = 0xFF
} HwVerEnum;

/*
 * Data
 */

/*
 * Functions
 */


void Device_PowerDown(); // TODO PowerDown

void Device_InitHardwareVersion();
HwVerEnum Device_GetHardwareVersion();
char* Device_GetHardwareVersionString(HwVerEnum ver);
bool Device_GetEngineeringMode();

bool Device_IsHarvester(void);
bool Device_HasPMIC(void);
bool Device_PMICcontrolGPS(void);

uint32_t Device_ADC_PowerdownPin();
uint32_t Device_GNSS_ResetPin();

void Device_SetVrefOn();
void Device_SetVrefOff();

#endif /* DEVICE_H_ */


#ifdef __cplusplus
}
#endif