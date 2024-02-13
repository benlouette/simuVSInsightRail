#ifdef __cplusplus
extern "C" {
#endif

/*
 * PowerControl.c
 *
 *  Created on: Dec 22, 2015
 *      Author: Bart Willemse
 * Description: Power control functions. Notes:
 *                - IMPORTANT: The subsystem pin configurations in the power
 *                  switching functions are specific to each allocated pin,
 *                  because the pins' alternate function indexes might change
 *                  on different pins
 */

#include <freeRTOS.h>
#include <task.h>
#include "PowerControl.h"
#include "PinConfig.h"
#include "PinDefs.h"
#include "stdbool.h"
#include "Device.h"
#include "Resources.h"
#include "DacWaveform.h"
#include "vbat.h"
#include "EventLog.h"

#include "Log.h"

// it appears freeRTOS is already included, but where ?
// #include <FreeRTOS.h>
// #include <task.h>

//uint32_t vib_self_test_en = 0;// temporary test value


//..............................................................................

static bool bModemPowerIsOn = false;
static bool bGNSSPowerIsOn = false;
static bool bAnalogPowerIsOn = false;

//..............................................................................
// Local function prototypes

//..............................................................................


// Some basic check that MCU core frequency has not been changed which would compromise soft start pulse timing.
#if DEFAULT_SYSTEM_CLOCK != 24576000
#error The defined clock frequency has been changed. The softstart pulse timing may have been compromised.
#endif

// RAMP_CYCLES defines the number of cycles WIRELESS_ON will be pulsed for the soft starts
#define RAMP_CYCLES 500

extern uint32_t SystemCoreClock;
/*
 * modemPowerSoftStart
 *
 * @desc    Performs a pulsed power up of modem power to limit the maximum current draw from the battery
 * 			to avoid deep voltage dip which could cause a reboot
 *
 *			in practice the ON pulse length is set to a small value to limit the energy transfer per pulse (peak current from battery)
 *			the pulse cycle time is much larger in order to keep the duty cycle low and that way limit the average current
 *
 *			pulse length approx 0.8 us
 *			approx 85 us pulse cycle time
 *			start up time approx 20 ms
 *			Pulse train length approx: 500*85 us = 43 ms
 *
 *			Note: All interrupts are disabled during the 0.8 us pulse duration but
 *				  are again enabled during the 85 us delay between the pulses.
 *				  The interrupts are disabled to avoid taking an interrupt when the
 *				  pulse is active because even a small increase in pulse length
 *				  increases the inrush current alot.
 *
 *			There is a simple check that MCU core clock frequency is not changed.
 *			It is not fail safe as it checks the default system clock frequency.
 *			The core clock could be changed in firmware to something else and the
 *			pulse timing would be off.
 *			It would be difficult to ensure proper pulse length through FW control.
 *			It is possible to catch - but still hard to handle - also not much
 *			benefit to throw an error message other than to catch it during FW-test.
 *
 * @param
 *
 * @returns -
 */

static inline void modemPowerSoftStart( void )
{
#ifdef _MSC_VER

#else
	// setting up pointers to have direct access to the HW-registers
	const uint32_t port = GPIO_EXTRACT_PORT( WIRELESS_ON );
	const uint32_t pin = GPIO_EXTRACT_PIN( WIRELESS_ON );
	const uint32_t pinMask = 1<<pin;

	GPIO_Type * pGpioPort = g_gpioBase[ port ];
	PORT_Type * pPort = g_portBase[ port ];

	// run time check of clock frequency
	if( SystemCoreClock != 24576000 )
	{
		LOG_EVENT(0, LOG_NUM_APP, ERRLOGMAJOR, "Modem SoftStart: Unexpected MCU clock speed"); // Todo: Assign specific errCode
	}

	pPort->PCR[ pin ] |= 0x40; // enable high drive strength

	pGpioPort->PSOR = pinMask;	// set WIRELESS_ON high
	pGpioPort->PDDR |= pinMask;	// set data direction register

	for(uint32_t cyc=0; cyc<RAMP_CYCLES; cyc++)
	{
		// All interrupts are disabled during the time critical low-pulse
		// Only very small clock frequency deviation allowable - A pulse 25% longer than the intended 0.8 us
		// will almost double the peak current.
		// 20 nops will be close to 0.8 us when clock frequency 24.576 MHz - peak current about 200 mA
		// 25 nops will be close to 1 us and the peak current will be close to 300 mA
		// 30 nops: 1.2 us - 500 mA

		__disable_irq(); // disable interrupts during the duration of the low-pulse

		pGpioPort->PCOR = pinMask;	// set WIRELESS_ON low
        // GCC or other compilers

		__asm("   nop;\n");
        __asm("   nop;\n");
        __asm("   nop;\n");
        __asm("   nop;\n");
        __asm("   nop;\n");
		__asm("   nop;\n");
        __asm("   nop;\n");
        __asm("   nop;\n");
        __asm("   nop;\n");
        __asm("   nop;\n");
		__asm("   nop;\n");
        __asm("   nop;\n");
        __asm("   nop;\n");
        __asm("   nop;\n");
        __asm("   nop;\n");
		__asm("   nop;\n");
        __asm("   nop;\n");
        __asm("   nop;\n");
        __asm("   nop;\n");
        __asm("   nop;\n");

		pGpioPort->PSOR = pinMask;	// set WIRELESS_ON high

		__enable_irq(); // enable interrupts again - no harm if interrupts occur during the delay between pulses
		for(uint32_t i=0;i< 400; i++)
		{
			__asm("   nop;\n");
        }
	}
	// finally WIRELESS_ON continous on
	pGpioPort->PCOR = pinMask;
#endif
}



/*
 * powerControlInit
 *
 * @desc    Initialises the power control module:
 *            - Initialises the cellular, GNSS and analog subsystems to
 *              their powered-off state
 *            - Other stuff (TBD)
 *
 * @param
 *
 * @returns -
 */
void powerControlInit(void)
{
    powerModemOff();
    powerGNSSOff();
    powerAnalogOff();
}


// TODO FOR CELLULAR MODEM POWER CONTROL:
//   - IMPORTANT: The modem might also require further pin configuration
//     control if powering down using the ESH5E_FST_SHDN line, and possibly
//     also other power-down scenarios - for example, see the EHS5E Hardware
//     Interface Description document section 2.1.14.4, "Fast Shutdown"
//
//   - The following cellular pin configurations, and possibly the ordering
//     of them, need to be properly checked and changed if necessary


/*
 * powerModemOn
 *
 * @desc    Powers on the cellular modem subsystem, and re-configures the
 *          required Kinetis pins for this.
 *          NOTE: The cellular modem has a high power-on inrush current of
 *          100's of mA.
 * @param
 *
 * @returns false when power did not work according the EHS5E_POWER_IND line
 */
bool powerModemOn(void)
{
    //TODO: Figure out whether to switch power before or after configuring the
    // pins - OR, might need to first use set the output lines to pulling
    // inputs before switching the power on, then switch the power on, then
    // change the output lines to outputs?
    // Switch on subsystem power

	Vbat_SetFlag(VBATRF_FLAG_MODEM);

	// call only after freeRTOS is running !!!
    // only if it was not powered, we do not want toggling IO lines when the power was already applied.
	// TODO : keep state information about what was powered or not, for the moment assume signal EHS5E_POWER_IND will do the job.
    if (GPIO_DRV_ReadPinInput(EHS5E_POWER_IND))
    {
    	// pin is High so modem is off
		bModemPowerIsOn = false;

		// lets first set the pins correctly before switching on the power
		if(Device_HasPMIC())
		{
			pinConfigDigitalOut(WIRELESS_ON, kPortMuxAsGpio, 1, false);
		}
		else
		{
			pinConfigDigitalOut(WIRELESS_ON, kPortMuxAsGpio, 0, false);
		}

		// Input pins:     Pin name          Mux mode        Pull   Pull direction  Interrupt config
		pinConfigDigitalIn(CELLULAR_RXD0,    kPortMuxAlt3,   false, kPortPullDown,  kPortIntDisabled);// controlled by uart
		pinConfigDigitalIn(CELLULAR_CTS0,    kPortMuxAlt3,   true,  kPortPullDown,  kPortIntDisabled);// controlled by uart flow control
		pinConfigDigitalIn(EHS5E_POWER_IND,  kPortMuxAsGpio, false, kPortPullDown,  kPortIntDisabled);
		pinConfigDigitalIn(EHS5E_DCD,        kPortMuxAsGpio, false, kPortPullDown,  kPortIntFallingEdge);// DCD is inverted by the level shifter

		// Output pins:     Pin name         Mux mode        Level  Open drain
		pinConfigDigitalOut(CELLULAR_TXD0,   kPortMuxAlt3,   0,     false);// controlled by uart
		pinConfigDigitalOut(CELLULAR_RTS0,   kPortMuxAlt3,   0,     false);// controlled by uart flow control
		pinConfigDigitalOut(EHS5E_FST_SHDN,  kPortMuxAsGpio, 1,     false);// inverted before entering modem where it is high active
		pinConfigDigitalOut(EHS5E_EMERG_RST, kPortMuxAsGpio, 0,     false);// inverted before entering modem where it is LOW active
		pinConfigDigitalOut(CELLULAR_DTR,    kPortMuxAsGpio, 1,     false);// inverted before entering modem

		bool bELS61_Modem = Device_GetHardwareVersion() >= HW_PASSRAIL_REV10;

		// modem OFF initially
		if(bELS61_Modem)
		{
			pinConfigDigitalOut(EHS5E_AUTO_ON, kPortMuxAsGpio, 1, false);// inverted before entering modem where it is high active
		}
		else
		{
			pinConfigDigitalOut(EHS5E_AUTO_ON, kPortMuxAsGpio, 0, false);// inverted before entering modem where it is low active
		}

		if (Device_GetHardwareVersion() >= HW_PASSRAIL_REV3 || Device_HasPMIC())
		{
			// Ensure that modem regulator's burst mode is disabled so that it
			// can deliver full current whenever required
			pinConfigDigitalOut(WIRELESS_BURST, kPortMuxAsGpio, 0, false);
		}

		// now all pins should be configured OK, lets now turn on modem power
		if(Device_HasPMIC())
		{
			modemPowerSoftStart();
		}
		else
		{
			GPIO_DRV_WritePinOutput(WIRELESS_ON, 1);
		}

		// turn modem on
		if(bELS61_Modem)
		{
			vTaskDelay(1000/portTICK_PERIOD_MS) ;// 1000 ms according to ELS61-E Hardware Interface Description - Section 3.2.1.2
			GPIO_DRV_WritePinOutput(EHS5E_AUTO_ON, 0);
		}
		else
		{
			vTaskDelay(10/portTICK_PERIOD_MS) ;// 10 ms according to EHS5-E Hardware Interface description Section 3.2.1.3
			GPIO_DRV_WritePinOutput(EHS5E_AUTO_ON, 1);
		}

		// signal EHS5E_POWER_IND goes zero when modem has powered up
		for (int i = 0; i < 20; ++i)
		{
			if(0 == GPIO_DRV_ReadPinInput(EHS5E_POWER_IND))
			{
				bModemPowerIsOn = true;
				break;
			}
			vTaskDelay(10/portTICK_PERIOD_MS) ;// wild guess 10 ms ?
		}

		// set the modem ON or AUTO_ON to inactive
		if(bELS61_Modem)
		{
			GPIO_DRV_WritePinOutput(EHS5E_AUTO_ON, 1);
		}
		else
		{
			GPIO_DRV_WritePinOutput(EHS5E_AUTO_ON, 0);
		}
     }

	return bModemPowerIsOn;
}

/*
 * powerModemOff
 *
 * @desc
 *
 * @param
 *
 * @returns -
 */
void powerModemOff(void)
{
    // TODO: Need any special power-off sequence for modem here? Or is the
    // following OK?

    // TODO: With the following power-off pin configuration of input pulldown,
    // the cellular subsystem power takes MANY SECONDS to decay (probably
    // because it has big internal PSU capacitance), so need to consider
    // whether need to drain it much more quickly but setting the Kinetis
    // pins to output low rather than input pulldown - BUT could this cause
    // high parasitic currents through these pins until the cellular
    // subsystem voltage has decayed?

    // TODO: Consider whether need a power-OFF delay time, to allow the
    // power voltage to fully decay to zero so that guarantees power-cycling
    // reset of external parts

    pinConfigGPIOForPowerOff(CELLULAR_RXD0);
    pinConfigGPIOForPowerOff(CELLULAR_CTS0);
    pinConfigGPIOForPowerOff(EHS5E_POWER_IND);
    pinConfigGPIOForPowerOff(CELLULAR_TXD0);
    pinConfigGPIOForPowerOff(CELLULAR_RTS0);
    pinConfigGPIOForPowerOff(EHS5E_FST_SHDN);
    pinConfigGPIOForPowerOff(EHS5E_EMERG_RST);
    pinConfigGPIOForPowerOff(EHS5E_AUTO_ON);
    pinConfigGPIOForPowerOff(CELLULAR_DTR);
    pinConfigGPIOForPowerOff(EHS5E_DCD);

    // Switch off subsystem power last of all
    if(Device_HasPMIC())
    {
    	pinConfigDigitalOut(WIRELESS_ON, kPortMuxAsGpio, 1, false);
    }
    else
    {
    	pinConfigDigitalOut(WIRELESS_ON, kPortMuxAsGpio, 0, false);
    }

    if(Device_GetHardwareVersion() >= HW_PASSRAIL_REV3 || Device_HasPMIC())
    {
    	if(Device_HasPMIC())
    	{
    		// set Burst mode ON for max efficiency
    		pinConfigDigitalOut(WIRELESS_BURST, kPortMuxAsGpio, 1, false);
    	}
    	else
    	{
    		// Ensure that modem regulator's burst mode is disabled so that it
    		// can deliver full current whenever required
    		pinConfigDigitalOut(WIRELESS_BURST, kPortMuxAsGpio, 0, false);
    	}
    }

    bModemPowerIsOn = false;

    // check that FreeRTOS is actually running
    if(taskSCHEDULER_NOT_STARTED != xTaskGetSchedulerState())
    {
    	Vbat_ClearFlag(VBATRF_FLAG_MODEM);
    }
}

/*
 * powerModemIsOn
 *
 * @desc
 *
 * @param
 *
 * @returns -
 */
bool powerModemIsOn(void)
{
    return bModemPowerIsOn;
}

/*
 * powerGNSSOn
 *
 * @desc
 *
 * @param
 *
 * @returns -
 */
void powerGNSSOn(void)
{
    Vbat_SetFlag(VBATRF_FLAG_GNSS);
    if (Device_PMICcontrolGPS()) //#163840
    {
        LOG_DBG(LOG_LEVEL_GNSS, "%s: HW REV >= 13\n", __func__ );
        // Rev 13 Keep it high
        pinConfigDigitalOut(Device_GNSS_ResetPin(), kPortMuxAsGpio, 1, false);
        // Switch on subsystem power first of all
        pinConfigDigitalOut(GNSS_ON, kPortMuxAsGpio, 1, false);

        // jj_f_163840 pmic gps
        // disable pin pullup in UART RX mode when GPS powered on
        // Input pins:     Pin name     Mux mode        Pull   Pull direction  Interrupt config
        pinConfigDigitalIn(GNSS_TX,     kPortMuxAlt3,   false,  kPortPullDown, kPortIntDisabled);
        pinConfigDigitalIn(GNSS_INTn, kPortMuxAsGpio, false, kPortPullDown, kPortIntDisabled);

        // The GNSS power supply has a slow rise time so delay bringing the GNSS out of reset
        vTaskDelay(pdMS_TO_TICKS(5));

        // Output pins:     Pin name    			Mux mode        Level  	Open drain
        // GNSS reset high - Rev 13 - already set high before power on
        pinConfigDigitalOut(GNSS_RX,  kPortMuxAlt3,   0, false);
    }
    else
    {
        LOG_DBG(LOG_LEVEL_GNSS, "%s: HW REV<13\n", __func__ );

	    // Rev 12 hold GNSS reset low?
        pinConfigDigitalOut(Device_GNSS_ResetPin(), kPortMuxAsGpio, 0, false);
        // Switch on subsystem power first of all
        pinConfigDigitalOut(GNSS_ON, kPortMuxAsGpio, 1, false);
        // Input pins:     Pin name     Mux mode        Pull   Pull direction  Interrupt config
        pinConfigDigitalIn(GNSS_TX,     kPortMuxAlt3,   true,  kPortPullUp,    kPortIntDisabled);

        // The GNSS power supply has a slow rise time so delay bringing the GNSS out of reset
        vTaskDelay(pdMS_TO_TICKS(5));

        // Output pins:     Pin name    			Mux mode        Level  	Open drain
        // Rev 12 set GNSS reset high again, Rev 13 - already set high before power on
        pinConfigDigitalOut(Device_GNSS_ResetPin(), kPortMuxAsGpio, 1, false);
        pinConfigDigitalOut(GNSS_RX, kPortMuxAlt3,   0,     	false);
    }

    bGNSSPowerIsOn = true;
}

/*
 * powerGNSSOff
 *
 * @desc
 *
 * @param
 *
 * @returns -
 */
void powerGNSSOff(void)
{
    if (Device_PMICcontrolGPS())
    {
        pinConfigDigitalIn(Device_GNSS_ResetPin(),  kPortMuxAsGpio,   true,  kPortPullUp,    kPortIntDisabled); //#163840
    }
    else
    {
        pinConfigGPIOForPowerOff(Device_GNSS_ResetPin());
        pinConfigGPIOForPowerOff(GNSS_RX);
    }
    pinConfigGPIOForPowerOff(GNSS_TX);

    if (Device_GetHardwareVersion() == HW_PASSRAIL_REV1)
    {
        pinConfigGPIOForPowerOff(GNSS_INTn);
    }

    // Switch off subsystem power last of all
    if (Device_PMICcontrolGPS())
    {
        pinConfigDigitalIn(GNSS_ON,  kPortMuxAsGpio,   true,  kPortPullDown,    kPortIntDisabled);
    }
    else
    {
        pinConfigDigitalOut(GNSS_ON, kPortMuxAsGpio, 0, false);
    }

    // TODO: Consider whether need a power-OFF delay time, to allow the
    // power voltage to fully decay to zero so that guarantees power-cycling
    // reset of external parts

    bGNSSPowerIsOn = false;

    // check that FreeRTOS is actually running
    if(taskSCHEDULER_NOT_STARTED != xTaskGetSchedulerState())
    {
    	Vbat_ClearFlag(VBATRF_FLAG_GNSS);
    }
}

/*
 * powerGNSSIsOn
 *
 * @desc
 *
 * @param
 *
 * @returns -
 */
bool powerGNSSIsOn(void)
{
    return bGNSSPowerIsOn;
}

/*
 * powerAnalogOn
 *
 * @desc
 *          ******* IMPORTANT: MUST BE CALLED FROM A FREERTOS TASK, because
 *          ******* calls vTaskDelay()
 *
 * @param
 *
 * @returns -
 */
void powerAnalogOn(void)
{
    // Switch on subsystem and AD7766 power first of all
    pinConfigDigitalOut(VIBRATION_ON, kPortMuxAsGpio, 1, false);  // PTC4
    if (Device_GetHardwareVersion() == HW_PASSRAIL_REV1 && !Device_HasPMIC())
    {
        pinConfigDigitalOut(ADC_DVDD_ON, kPortMuxAsGpio, 1, false);   // PTE4
    }

    // Delay to let the analog subsystem PSU voltage ramp up and stabilise
    // before configuring the Kinetis pins (so don't drive Kinetis pins into
    // unpowered subsystem)
    vTaskDelay(200 / portTICK_PERIOD_MS);

    // Bring ADC chip-select line high first to prevent any spurious signals
    // on the other SPI lines from causing ADC chip confusion
    pinConfigDigitalOut(ADC_CSn, kPortMuxAsGpio, 1, false);     // PTB10

    // Wake up the ADC
    pinConfigDigitalOut(Device_ADC_PowerdownPin(), kPortMuxAsGpio, 1, false);  // PTD8 or PTD6

    // SPI1_MISO from AD7766: Needs pulling resistor because AD7766 SDO pin is
    // mostly floating because its ~CS will be mostly high
    // TODO: Figure out the required pulling direction - depends upon line
    // idle state of SPI mode used
    pinConfigDigitalIn(SPI1_MISO, kPortMuxAlt2, true,  kPortPullDown, kPortIntDisabled);   // PTB17

    pinConfigDigitalIn(ADC_IRQn, kPortMuxAsGpio, false, kPortPullDown, kPortIntDisabled);  // PTE5

    pinConfigDigitalOut(SPI1_MOSI, kPortMuxAlt2, 0, false);  // PTB16
    pinConfigDigitalOut(SPI1_SCK, kPortMuxAlt2, 0, false);   // PTB11
    pinConfigDigitalOut(ADC_MCLK, kPortMuxAlt4, 0, false);   // PTD4

    // Configure analog circuitry control pins
    pinConfigDigitalOut(VIB_SELF_TEST_EN, kPortMuxAsGpio, /* vib_self_test_en */ 0, false); // PTC1 - disable self-test
    pinConfigDigitalOut(PREAMP_GAIN_OFF, kPortMuxAsGpio, 1, false);  // PTC5 - default to gain off
    pinConfigDigitalOut(HP_SELECT, kPortMuxAsGpio, 1, false);        // PTC2 - default to HP filter off

    if (Device_GetHardwareVersion() >= HW_PASSRAIL_REV3 || Device_HasPMIC())
    {
        pinConfigDigitalOut(BANDPASS_SELECT, kPortMuxAsGpio, 0, false);  // PTC12
    }

    // TODO: Sort the following pins
    //VIBRATION     // Not GPIO pins - TODO - figure out what to do with these
    //VIBRATION_N   // They currently have no-fit series resistors, so not a problem for now

    // TODO: Put further delay in here to ensure that all signals have fully settled

    // Analog power & control signal settling time - allow 200ms (according
    // to HW engineers)
    vTaskDelay(pdMS_TO_TICKS(200));

    bAnalogPowerIsOn = true;

}

/*
 * powerAnalogOff
 *
 * @desc
 *
 * @param
 *
 * @returns -
 */
void powerAnalogOff(void)
{
    // TODO: Consider whether need a power-OFF delay time, to allow the
    // power voltage to fully decay to zero so that guarantees power-cycling
    // reset of external parts

    // Disable the DAC output.
	Dac_Disable();

    // TODO: SELF_TEST_PULSE is not a GPIO pin.
    // However, it can be used as DAC0_OUT, so can control the DAC to produce
    // high and low voltages just like a logic pin - BUT it has a worst-case
    // settling time of 30us. OR, remove the SELF-TEST-PULSE series resistor
    // R80 and feed the U18 MUX with a separate Kinetis GPIO output via TP55.
    // TODO: Check what this pin is set up as - if it's DAC output then make sure it doesn't drive when the subsystem is powered off
    //pinConfigGPIOForPowerOff(SELF_TEST_PULSE);

    pinConfigGPIOForPowerOff(VIB_SELF_TEST_EN);
    pinConfigGPIOForPowerOff(PREAMP_GAIN_OFF);
    pinConfigGPIOForPowerOff(HP_SELECT);
    if (Device_GetHardwareVersion() >= HW_PASSRAIL_REV3 || Device_HasPMIC())
    {
        pinConfigGPIOForPowerOff(BANDPASS_SELECT);
    }

    //VIBRATION     // Not GPIO pins - TODO - figure out what to do with these
    //VIBRATION_N   // They currently have no-fit series resistors, so not a problem for now
    pinConfigGPIOForPowerOff(SPI1_MOSI);
    pinConfigGPIOForPowerOff(SPI1_MISO);
    pinConfigGPIOForPowerOff(SPI1_SCK);
    pinConfigGPIOForPowerOff(Device_ADC_PowerdownPin());
    pinConfigGPIOForPowerOff(ADC_MCLK);
    pinConfigGPIOForPowerOff(ADC_IRQn);

    // Leave ~CS pin high until here to guarantee that that any spurious
    // activity on the other SPI lines has been ignored
    pinConfigGPIOForPowerOff(ADC_CSn);

    if (Device_GetHardwareVersion() == HW_PASSRAIL_REV1 && !Device_HasPMIC())
    {
        // Switch off ADC power
        pinConfigGPIOForPowerOff(ADC_DVDD_ON);  // ************ TODO: Should be OUTPUT low?? Because controlling regulator
    }

    // Switch off subsystem power last of all
    pinConfigDigitalOut(VIBRATION_ON, kPortMuxAsGpio, 0, false);

    bAnalogPowerIsOn = false;
}

/*
 * powerAnalogIsOn
 *
 * @desc
 *
 * @param
 *
 * @returns -
 */
bool powerAnalogIsOn(void)
{
    return bAnalogPowerIsOn;
}

/*
 * pinConfigGPIOForPowerOff
 *
 * @desc
 *
 * @param
 *
 * @returns -
 */
void pinConfigGPIOForPowerOff(uint32_t pinName)
{
    // TODO: Consider whether better to configure pins as output low for
    // subsystem power off, rather than input pulldown? Was output low for
    // MSP430 platform, at request of hardware engineers

    pinConfigDigitalIn(pinName, kPortMuxAsGpio, true, kPortPullDown, kPortIntDisabled);
}


#if 0
// TODO: These VDIG power control functions are not yet complete and should not
// currently be used. This is because the power-switched VDIG supply (controlled
// by the PSU-ENABLE line) supplies various peripheral chips, and the
// corresponding Kinetis I/O pins would need to be re-configured for VDIG-on
// versus VDIG-off.

/*
 * powerPSU_ENABLE_ON
 *
 * @desc
 * Power ON for VDIG supply (N.B. Not available on rev 1 board)
 *
 * @param
 *
 * @returns -
 */
void powerPSU_ENABLE_ON(void)
{

    if (HardwareVersion3or4())
    {
    	pinConfigDigitalOut(PSU_ENABLE, kPortMuxAsGpio, 1, false);
    }
}

/*
 * powerPSU_ENABLE_OFF
 *
 * @desc
 * Power OFF for VDIG supply (N.B. Not available on rev 1 board)
 *
 * @param
 *
 * @returns -
 */
void powerPSU_ENABLE_OFF(void)
{
    if (HardwareVersion3or4())
    {
        pinConfigDigitalOut(PSU_ENABLE, kPortMuxAsGpio, 0, false);
    }
}

#endif // 0


#ifdef __cplusplus
}
#endif