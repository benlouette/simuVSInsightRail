#ifdef __cplusplus
extern "C" {
#endif

/*
 * PinConfig.c
 *
 *  Created on: Dec 22, 2015
 *  Author: Bart Willemse
 *      Configures input and output pins. Provides simpler/cleaner pin
 *      configuration functionality than the SDK's GPIO_DRV_InputPinInit()
 *      and GPIO_DRV_OutputPinInit() functions:
 *        - Includes pin MUX mode configuration, so don't have to call the
 *          separate PORT_HAL_SetMuxMode() function (which also doesn't
 *          support the pin names)
 *        - Can specify the pin parameters using function parameters rather
 *          than structures
 *        - The digital pin configuration functions provide all of the relevant
 *          digital configuration parameters. Also, they default the digital
 *          input pin filtering and output pin drive strength and slew rate
 *          parameters, because these defaults can be used for all digital
 *          pins for the project
 *
 *      The other GPIO_DRV_XXXX() SDK functions can be used together with these
 *      functions.
 */

#include "fsl_gpio_driver.h"
#include "fsl_clock_manager.h"
#include "fsl_interrupt_manager.h"
#include "fsl_device_registers.h"
#include "PinConfig.h"

/*
 * pinConfigDigitalIn
 *
 * @desc    Configures a pin for one of its digital input modes. Adapted from
 *          GPIO_DRV_InputPinInit(), KSDK rev 1.3.0.
 *          Always sets the following:
 *            - Disables the pin's passive analog filter, because the datasheet
 *              section 2.3.2 "General switching specifications" implies that it
 *              has a time constant of 50ns or so, which is too long for
 *              high-speed SPI (for example)
 *            - Disables the pin's digital filter - keeps things simple, and
 *              also because the K24F part only supports digital glitch filtering
 *              on port D anyway - see table in ref manual section 10.2.2 "Port
 *              control and interrupt summary".
 *
 *          The Kinetis internal pulling resistors are 20k-50k - see the RPU
 *          and RPD parameters in the datasheet.
 *
 * @param   pinName: Pin name generated using GPIO_MAKE_PIN()
 * @param   muxMode: One of the pin's digital port_mux_t modes
 * @param   isPullEnable: Enable/disable pulling resistor
 * @param   pullSelect: One of port_pull_t
 * @param   interruptConfig: One of port_interrupt_config_t
 *
 * @returns -
 */
void pinConfigDigitalIn(uint32_t pinName,
                        port_mux_t muxMode,
                        bool isPullEnable,
	                    port_pull_t pullSelect,
	                    port_interrupt_config_t interruptConfig)
{
#ifndef _MSC_VER
    // Get actual port and pin number
    uint32_t port = GPIO_EXTRACT_PORT(pinName);
    uint32_t pin = GPIO_EXTRACT_PIN(pinName);
    GPIO_Type * gpioBase = g_gpioBase[port];
    PORT_Type * portBase = g_portBase[port];

    // Un-gate port clock
    CLOCK_SYS_EnablePortClock(port);

    // Configure MUX mode
    PORT_HAL_SetMuxMode(portBase, pin, muxMode);

    // If GPIO then configure as GPIO input
    if (muxMode == kPortMuxAsGpio)
    {
        GPIO_HAL_SetPinDir(gpioBase, pin, kGpioDigitalInput);
    }

    // Configure input features
#if FSL_FEATURE_PORT_HAS_PULL_ENABLE
    PORT_HAL_SetPullCmd(portBase, pin, isPullEnable);
#endif

#if FSL_FEATURE_PORT_HAS_PULL_SELECTION
    PORT_HAL_SetPullMode(portBase, pin, pullSelect);
#endif

#if FSL_FEATURE_PORT_HAS_PASSIVE_FILTER
    // Disable passive filter
    PORT_HAL_SetPassiveFilterCmd(portBase, pin, false);
#endif

#if FSL_FEATURE_PORT_HAS_DIGITAL_FILTER
    // Disable digital filter
    PORT_HAL_SetDigitalFilterCmd(portBase, pin, false);
#endif

#if FSL_FEATURE_GPIO_HAS_INTERRUPT_VECTOR
    PORT_HAL_SetPinIntMode(portBase, pin, interruptConfig);
    // Configure NVIC
    if (interruptConfig && g_portIrqId[port])
    {
        // Enable GPIO interrupt
        INT_SYS_EnableIRQ(g_portIrqId[port]);
    }
#endif
#endif
}

/*
 * pinConfigDigitalOut
 *
 * @desc    Configures a pin for one of its digital output modes.
 *          Adapted from GPIO_DRV_OutputPinInit(), KSDK rev 1.3.0.
 *          Always sets the following:
 *            - Low drive strength: Datasheet implies about 2mA (see VOH),
 *              which should be sufficient even for the LEDs
 *            - Fast slew rate: Datasheet implies 6ns with low drive strength
 *              (see the "Port rise and fall time (low drive strength) - 3 V"
 *              parameter) - this is quick enough for requirements such as
 *              high-speed SPI
 *
 * @param   pinName: Pin name generated using GPIO_MAKE_PIN()
 * @param   muxMode: One of the pin's digital port_mux_t modes
 * @param   outputLogic: If mux mode is GPIO, then specifies output level (0 or 1)
 * @param   isOpenDrainEnabled: Enable/disable open drain
 *
 * @returns -
 */
void pinConfigDigitalOut(uint32_t pinName,
                         port_mux_t muxMode,
                         uint32_t outputLogic,
                         bool isOpenDrainEnabled)
{
#ifndef _MSC_VER
    // Get actual port and pin number
    uint32_t port = GPIO_EXTRACT_PORT(pinName);
    uint32_t pin = GPIO_EXTRACT_PIN(pinName);
    GPIO_Type * gpioBase = g_gpioBase[port];
    PORT_Type * portBase = g_portBase[port];

    // Un-gate port clock
    CLOCK_SYS_EnablePortClock(port);

    // TODO: The ordering of the following pin configuration operations might
    // produce glitches or other undesirable transient behaviours for certain
    // pin configuration changes. If any problems then need to investigate the
    // detail and possibly change the order of the operations.

    // Configure MUX mode
    PORT_HAL_SetMuxMode(portBase, pin, muxMode);

    // Configure GPIO-specific stuff
    if (muxMode == kPortMuxAsGpio)
    {
        // Set output state value - do this BEFORE changing direction to
        // avoid potential glitch if pin is currently input, and previous
        // output state was different
        GPIO_HAL_WritePinOutput(gpioBase, pin, outputLogic);
        // Set direction to output
        GPIO_HAL_SetPinDir(gpioBase, pin, kGpioDigitalOutput);

#if FSL_FEATURE_PORT_HAS_SLEW_RATE
        // Set fast slew rate
        PORT_HAL_SetSlewRateMode(portBase, pin, kPortFastSlewRate);
#endif

#if FSL_FEATURE_PORT_HAS_DRIVE_STRENGTH
        // Set low drive strength
        PORT_HAL_SetDriveStrengthMode(portBase, pin, kPortLowDriveStrength);
#endif

#if FSL_FEATURE_PORT_HAS_OPEN_DRAIN
        // Configure open-drain
        PORT_HAL_SetOpenDrainCmd(portBase, pin, isOpenDrainEnabled);
#endif
    }
#endif
}

/*
 * pinConfigNonDigital
 *
 * @desc    Configures a pin for one of its non-digital modes.
 *
 * @param   pinName: Pin name generated using GPIO_MAKE_PIN()
 * @param   muxMode: One of the pin's non-digital port_mux_t modes
 *
 * @returns -
 */
void pinConfigNonDigital(uint32_t pinName,
                         port_mux_t muxMode)
{
#ifndef _MSC_VER
    // Get actual port and pin number
    uint32_t port = GPIO_EXTRACT_PORT(pinName);
    uint32_t pin = GPIO_EXTRACT_PIN(pinName);
    PORT_Type * portBase = g_portBase[port];

    // Un-gate port clock
    CLOCK_SYS_EnablePortClock(port);

    // Configure MUX mode
    PORT_HAL_SetMuxMode(portBase, pin, muxMode);

    // Reference manual 11.6.1 Pin control: "When the Pin Muxing mode is
    // configured for analog or is disabled, all the digital functions on that
    // pin are disabled". So don't need to do this here.
#endif
}

/*
 * pinClearIntFlag
 *
 * @desc    Clears a pin's ISF interrupt/DMA flag.
 *
 * @param   pinName: Pin name generated using GPIO_MAKE_PIN()
 *
 * @returns -
 */
void pinClearIntFlag(uint32_t pinName)
{
#ifndef _MSC_VER
    // Get actual port and pin number
    uint32_t port = GPIO_EXTRACT_PORT(pinName);
    uint32_t pin = GPIO_EXTRACT_PIN(pinName);
    PORT_Type * portBase = g_portBase[port];

    PORT_HAL_ClearPinIntFlag(portBase, pin);
#endif
}

#if 0
// Disabled function because should never be needed. Originally created for
// AD7766 driver, but replaced by using pinClearIntFlag().
/*
 * portClearAllIntFlags
 *
 * @desc    Clears all of a port's ISF interrupt/DMA flags.
 *
 * @param   PortIndex: One of GPIOA_IDX, GPIOB_IDX etc
 *
 * @returns -
 */
void portClearAllIntFlags(uint32_t PortIndex)
{
    PORT_Type *portBase = g_portBase[PortIndex];

    PORT_HAL_ClearPortIntFlag(portBase);
}
#endif // 0

//******************************************************************************
// Test code follows
//******************************************************************************

/*
 * pinConfigTest
 *
 * @desc    Ad-hoc pin configuration test functionality. Use by single-stepping
 *          through each line and checking the expected PORTB registers in the
 *          IDE, and the electrical line states using a scope and basic
 *          electrical tests.
 *
 * @param   -
 *
 * @returns -
 */
void pinConfigTest(void)
{
#ifndef _MSC_VER
    // Define pins here locally so not dependent upon gpio1.h
    const uint32_t PinTestLED1 = GPIO_MAKE_PIN(GPIOE_IDX, 6U);
    const uint32_t PinTestLED2 = GPIO_MAKE_PIN(GPIOB_IDX, 8U);
    const uint32_t PinTestIO1 = GPIO_MAKE_PIN(GPIOB_IDX, 18U);

    //..........................................................................
    // Test LEDs to check for sufficient drive strength with low strength
    // drive
    pinConfigDigitalOut(PinTestLED1, kPortMuxAsGpio, 1, false);
    pinConfigDigitalOut(PinTestLED1, kPortMuxAsGpio, 0, false);

    pinConfigDigitalOut(PinTestLED2, kPortMuxAsGpio, 1, false);
    pinConfigDigitalOut(PinTestLED2, kPortMuxAsGpio, 0, false);

    //..........................................................................
    // Test detailed pin configuration using TEST-IO1 pin

    // Start with low totem-pole output
    pinConfigDigitalOut(PinTestIO1, kPortMuxAsGpio, 0, false);

    // Test as digital totem-pole output
    pinConfigDigitalOut(PinTestIO1, kPortMuxAsGpio, 1, false);
    pinConfigDigitalOut(PinTestIO1, kPortMuxAsGpio, 0, false);

    // Test as digital open-drain output
    pinConfigDigitalOut(PinTestIO1, kPortMuxAsGpio, 1, true);
    pinConfigDigitalOut(PinTestIO1, kPortMuxAsGpio, 0, true);

    // Test as digital input, no pulling resistor
    pinConfigDigitalIn(PinTestIO1, kPortMuxAsGpio, false, kPortPullDown,
                       kPortIntDisabled);

    // Test as digital input, pull up
    pinConfigDigitalIn(PinTestIO1, kPortMuxAsGpio, true, kPortPullUp,
                       kPortIntDisabled);

    // Test as digital input, pull down
    pinConfigDigitalIn(PinTestIO1, kPortMuxAsGpio, true, kPortPullDown,
                       kPortIntDisabled);

    // Test some sequences which change between input and output
    pinConfigDigitalOut(PinTestIO1, kPortMuxAsGpio, 1, false);
    pinConfigDigitalIn(PinTestIO1, kPortMuxAsGpio, true, kPortPullDown,
                       kPortIntDisabled);
    pinConfigDigitalOut(PinTestIO1, kPortMuxAsGpio, 0, false);

    // Generate output pulse train so can check slew rates on scope
    while(1)
    {
        pinConfigDigitalOut(PinTestIO1, kPortMuxAsGpio, 0, false);
        pinConfigDigitalOut(PinTestIO1, kPortMuxAsGpio, 1, false);
    }
#endif
    //..........................................................................
}





#ifdef __cplusplus
}
#endif