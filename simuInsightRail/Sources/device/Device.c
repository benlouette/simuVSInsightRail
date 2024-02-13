#ifdef __cplusplus
extern "C" {
#endif

/*
 * Device.c
 *
 *  Created on: 7 jul. 2014
 *      Author: g100797
 */

/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>

#include "Resources.h"
#include "Device.h"
#include "fsl_gpio_driver.h"
#include "fsl_vref_hal.h"
#include "fsl_clock_manager.h"
#include "Timer.h"
#include "PinDefs.h"
#include "PinConfig.h"


/*
 * Macros
 */

/*
 * Types
 */

/*
 * Data
 */
static HwVerEnum m_HardwareVersion = HW_VERSION_UNDEFINED;
static bool m_HasPMIC = false;
static bool m_HasPMICcontrolGPS = false;

/*
 * Functions
 */
static HwVerEnum ReadHardwareVersionBits();
static bool ReadHasPMIC();


/*!
 * Device_InitHardwareVersion
 *
 * @brief      Initializes the hardware version information, based upon the
 *             hardware ID jumpers.
 *             ** VERY IMPORTANT **: MUST be called as early as possible after
 *             main(), to ensure that the hardware version info is available
 *             to use immediately.
 *
 * @param      ..
 *
 * @returns    -
 */
#ifdef _MSC_VER
#define NO_OPTIMIZE_START __pragma(optimize("", off))
#define NO_OPTIMIZE_END   __pragma(optimize("", on))
#else
#define NO_OPTIMIZE_START _Pragma("GCC push_options") \
                          _Pragma("GCC optimize(\"O0\")")
#define NO_OPTIMIZE_END   _Pragma("GCC pop_options")
#endif
NO_OPTIMIZE_START
void  Device_InitHardwareVersion()
{
	m_HardwareVersion = ReadHardwareVersionBits();

	if (m_HardwareVersion >= HW_PASSRAIL_REV13)
	{
		m_HasPMICcontrolGPS = true;
	}

	if(m_HardwareVersion == HW_PASSRAIL_REV6  ||
	   m_HardwareVersion == HW_PASSRAIL_REV10 ||
	   m_HardwareVersion >= HW_PASSRAIL_REV12)
	{
		//Revisions 6, 10, 12 and 13 always have a PMIC
		m_HasPMIC = true;
	}
	else
	{
		//For all other versions, check PMIC presence to decide if it's a harvester variant

		//TODO optimise this delay to match PMIC boot time
		for(volatile int i=0; i<200; i++)
		{
			DelayMicrosecs(1000);
		}

		m_HasPMIC = ReadHasPMIC();
	}

	// For battery variant: Report the version as 3 for Rev2 & Rev 3 boards ONLY.
	if(((m_HardwareVersion == HW_PASSRAIL_REV1) || (m_HardwareVersion == HW_PASSRAIL_REV2)) && !Device_HasPMIC())
	{
		m_HardwareVersion = HW_PASSRAIL_REV3;
	}
}
NO_OPTIMIZE_END

/*!
 * GetHardwareVersion
 *
 * @brief      Returns the board hardware version, based upon the hardware
 *             ID jumpers.
 *             IMPORTANT: Device_InitHardwareVersion() must be called
 *             before this function.
 *
 * @param      ..
 *
 * @returns    -
 */
HwVerEnum Device_GetHardwareVersion(void)
{
    return m_HardwareVersion;
}


/*!
 * GetHardwareVersionString
 *
 * @brief      Returns the board hardware version, as a string.
 *             IMPORTANT: Device_InitHardwareVersion() must be called
 *             before this function.
 *
 * @param      ..
 *
 * @returns    -
 */
char* Device_GetHardwareVersionString(HwVerEnum ver)
{
	return(
		((ver) == HW_PASSRAIL_REV1)  ? "HW_PASSRAIL_REV1" : \
		((ver) == HW_PASSRAIL_REV2)  ? "HW_PASSRAIL_REV2" : \
		((ver) == HW_PASSRAIL_REV3)  ? "HW_PASSRAIL_REV3" : \
		((ver) == HW_PASSRAIL_REV4)  ? "HW_PASSRAIL_REV4" : \
		((ver) == HW_PASSRAIL_REV5)  ? "HW_PASSRAIL_REV5" : \
		((ver) == HW_PASSRAIL_REV6)  ? "HW_PASSRAIL_REV6" : \
		((ver) == HW_PASSRAIL_REV10) ? "HW_PASSRAIL_REV10" : \
		((ver) == HW_PASSRAIL_REV12) ? "HW_PASSRAIL_REV12" : \
		((ver) == HW_PASSRAIL_REV13) ? "HW_PASSRAIL_REV13" : \
		((ver) == HW_PASSRAIL_REV14) ? "HW_PASSRAIL_REV14" : \
									  "unknown HW version"
	);
}


/*!
 * Device_IsHarvester
 *
 * @brief      Check if power source is battery or harvester
 *
 * @returns    - True if harvester variant, False if battery variant
 */
bool Device_IsHarvester(void)
{
	return (Device_GetHardwareVersion() == HW_PASSRAIL_REV6);
}


/*!
 * Device_HasPMIC
 *
 * @brief      Check if the hardware contains the power management (PMIC) processor
 *
 * @returns    - True if contains the power management (PMIC), False if it doesn't
 */
bool Device_HasPMIC(void)
{
	return m_HasPMIC;
}

/**
 * @brief      Check if the hardware contains PMIC controlled GPS
 *
 * @retval  True if contains the PMIC can control GPS, False otherwise
 */
bool Device_PMICcontrolGPS(void)
{
	return m_HasPMICcontrolGPS;
}


uint32_t Device_ADC_PowerdownPin()
{
	return (m_HasPMIC?ADC_PWRDNn_HARV:ADC_PWRDNn_BATT);
}


uint32_t Device_GNSS_ResetPin()
{
	return (m_HasPMIC?GNSS_RESET_HARV:GNSS_RESET_BATT);
}



/*!
 * Device_SetVrefOn
 *
 * @brief      Switches ON the Vref module, thereby making the reference
 * 			   voltage available to be used by the internal ADC & DAC peripherals.
 *
 * @param      ..
 *
 * @returns    -
 */
void Device_SetVrefOn()
{
	vref_user_config_t stcConfigVref;

    /* Enable clock for VREF. */
    CLOCK_SYS_EnableVrefClock(VREF_IDX);

    // Enables the VREF.
	VREF_HAL_Init((VREF_Type *)VREF_BASE);

	stcConfigVref.chopOscEnable = true;
	stcConfigVref.regulatorEnable = true;
	stcConfigVref.soccEnable = true;
	stcConfigVref.trimValue = 0x28;		// trim to 1.2 volts,
	stcConfigVref.bufferMode = kVrefModeHighPowerBuffer;
	VREF_HAL_Configure((VREF_Type *)VREF_BASE, &stcConfigVref);
	DelayMicrosecs(100); //Delay for allowing the buffer o/p to settle to the final value.
}

/*!
 * Device_SetVrefOff
 *
 * @brief      Switches OFF the Vref module, thereby disconnecting the reference
 * 			   voltage which may be used by the internal ADC & DAC peripherals.
 *
 * @param      ..
 *
 * @returns    -
 */
void Device_SetVrefOff()
{
	// Diable the VREF.
    VREF_HAL_Disable((VREF_Type *)VREF_BASE);
    // Disable clock for VREF.
    CLOCK_SYS_DisableVrefClock(VREF_IDX);
}

/*!
 * Device_GetEngineeringMode
 *
 * @brief      Returns if the device is in engineering mode (TESTPIN is held high)
 *             We don't cache this as the jumper state may change
 *
 * @param      ..
 *
 * @returns    true when in 'engineering mode'
 */
bool Device_GetEngineeringMode()
{
    uint32_t mode;

#ifndef FCC_TEST_BUILD
    pinConfigDigitalIn(TEST_IO1,    kPortMuxAsGpio, true, kPortPullDown,  kPortIntDisabled);
    DelayMicrosecs(250);// reaching a low level when nothing is attached takes some time
    mode = GPIO_DRV_ReadPinInput(TEST_IO1);
    pinConfigDigitalIn(TEST_IO1,    kPortMuxAsGpio, true, kPortPullUp,   kPortIntDisabled);
#else
    mode = 1;
#endif
    return mode != 0;
}

/*
 * Local Functions
 */

/*!
 * ReadHardwareVersionBits
 *
 * @brief      Read input pins which define the hardware version of the board.
 * @note	   battery variant: For Rev3 boards, if ID is read as 1 or 2 then reported as 3
 * @returns    hwId
 */
static HwVerEnum ReadHardwareVersionBits()
{
    uint32_t hwId;

    // Input pins:     Pin name Mux mode        Pull  Pull direction  Interrupt config
    pinConfigDigitalIn(ID_1,    kPortMuxAsGpio, true, kPortPullDown,  kPortIntDisabled);
    pinConfigDigitalIn(ID_2,    kPortMuxAsGpio, true, kPortPullDown,  kPortIntDisabled);
    pinConfigDigitalIn(ID_3,    kPortMuxAsGpio, true, kPortPullDown,  kPortIntDisabled);
    pinConfigDigitalIn(ID_4,    kPortMuxAsGpio, true, kPortPullDown,  kPortIntDisabled);
    pinConfigDigitalIn(ID_5,    kPortMuxAsGpio, true, kPortPullDown,  kPortIntDisabled);

    // Allow settling time before reading the pins:
    // - Worst-case (largest and therefore slowest) pulling resistor value for
    //   an ID input pin is its internal pull-down resistor, which datasheet
    //   says is 50k worst-case (see Rpd)
    // - Worst-case (largest) input pin capacitance is 7pF according to
    //   datasheet (see Cin_d) - round up to 10pF for the PCB pads/tracks
    // - So RC time constant would be 50,000 * 10pF, which is about
    //   500 nanoseconds
    // - So just use a delay of about 1 millisecond (nice and big for safety)
    DelayMicrosecs(1000);

    hwId =             GPIO_DRV_ReadPinInput(ID_5);
    hwId = (hwId<<1) | GPIO_DRV_ReadPinInput(ID_4);
    hwId = (hwId<<1) | GPIO_DRV_ReadPinInput(ID_3);
    hwId = (hwId<<1) | GPIO_DRV_ReadPinInput(ID_2);
    hwId = (hwId<<1) | GPIO_DRV_ReadPinInput(ID_1);

    //Input pins:      Pin name Mux mode        Pull  Pull direction Interrupt config
    pinConfigDigitalIn(ID_1,    kPortMuxAsGpio, true, kPortPullUp,   kPortIntDisabled);
    pinConfigDigitalIn(ID_2,    kPortMuxAsGpio, true, kPortPullUp,   kPortIntDisabled);
    pinConfigDigitalIn(ID_3,    kPortMuxAsGpio, true, kPortPullUp,   kPortIntDisabled);
    pinConfigDigitalIn(ID_4,    kPortMuxAsGpio, true, kPortPullUp,   kPortIntDisabled);
    pinConfigDigitalIn(ID_5,    kPortMuxAsGpio, true, kPortPullUp,   kPortIntDisabled);

    HwVerEnum hw = (HwVerEnum)hwId;

    return hw;
}

/*!
 * ReadHasPMIC
 *
 * @brief      Determine if a PMIC is present (at hardware level).
 * @note
 * @returns    bool
 */
static bool ReadHasPMIC()
{
	/*
	 * Have a look at PMIC TX pin..
	 */
	DelayMicrosecs(500);

	// ensure VIBRATION_ON is low or else pin will be pulled up on battery variant
	pinConfigDigitalOut(VIBRATION_ON, kPortMuxAsGpio, 0, false);

	pinConfigDigitalIn(PMIC_UART_TX, kPortMuxAsGpio, true, kPortPullDown, kPortIntDisabled);

	const uint8_t noReadings = 200;

	for(int i = 0; i < noReadings; i++)
	{
		if(GPIO_DRV_ReadPinInput(PMIC_UART_TX))
		{
			return true;
		}
		DelayMicrosecs(1000);
	}

	return false;
}





#ifdef __cplusplus
}
#endif