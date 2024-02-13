#ifdef __cplusplus
extern "C" {
#endif

/*
 * PowerOffFailsafe.c
 *
 *  Created on: 5 September 2016
 *      Author: Bart Willemse
 * Description: Implements the node power-off failsafe timer mechanism.
 *
 * Overview
 * --------
 * Uses a Kinetis FlexTimer to trigger the TMR-POWER-OFF# line after the
 * power-off failsafe timeout period (up to about 8.7 minutes max), which
 * in turn drives the ADM6713 reset supervisor chip's ~MR input:
 *
 *     FlexTimer -> TMR-POWER-OFF# line -> ADM6713 ~MR input
 *
 * The design intent of this FlexTimer-based power-off mechanism is to provide
 * a completely separate, unconditional (under normal field operation) node
 * power-off trigger mechanism, to ensure that the node powers off after some
 * time if the firmware hangs, to prevent running down the battery.
 *
 * NOTE: This TMR-POWER-OFF#-based power-off circuitry is NOT IMPLEMENTED ON
 * THE PASSENGER RAIL REV 1 BOARD. However, the signal will still appear on
 * TP21 / PTB0.
 *
 * *****************************************************************************
 * IMPORTANT: The MCGFFCLK must be configured for the slow 32kHz internal
 * reference clock.
 * *****************************************************************************
 *
 * Implementation details
 * ----------------------
 * The FlexTimer counting is clocked at 250Hz. This is derived from the
 * Kinetis FlexTimer's "fixed frequency clock" option which is MCGFFCLK
 * (see reference manual 3.8.2.3 Fixed frequency clock), which is the
 * Kinetis internal 32kHz clock source. The FlexTimer's 250Hz counter
 * clocking is generated via its divide-by-128 prescaler.
 *
 * When the FlexTimer expires, it starts to produce a short low pulse of about
 * 15-20ms (controlled by POWEROFFFAILSAFE_FLEXTIMER_MATCH_VAL), which is
 * ample for triggering the ADM6713 reset chip's ~MR input (which has an
 * input pulse width requirement of 1 microsecond). However, the resultant
 * Kinetis reset then interrupts this. N.B. If it did not reset the Kinetis,
 * the FlexTimer would actually be free-running (producing the low pulses
 * every timeout period).
 *
 * The timing diagram is as follows:
 *
 *                                          __
 *  Modulo value -->                      __  __
 *      |                               __      __
 *      |                             __          __
 *      |                           __              __
 *  FlexTimer                     __                  __
 *  counter values              __                      __                  __
 *      |                     __      250Hz count rate    __              __
 *      |                   __                              __          __
 *  Match value --->      __                                  __      __
 *      |               __                                      __  __
 *      0 --------->  __                                          __
 *
 *                 ____________________________________________        ...........
 *                        .                                   |        .
 *  TMR-POWER-OFF# /      .                                   | Low    .
 *  FlexTimer             .                                   | pulse  .
 *  channel               .    Failsafe timeout period        | period .
 *  output                .                                   |        .
 *                        .                                   |.........
 *
 *                        ^                                   ^
 *                        |                                   |
 *                 Change from GPIO                     Triggers Kinetis
 *                output to FlexTimer                   reset via ADM6713
 *                      output                           supervisor chip
 *
 *
 * N.B. The KSDK FlexTimer HAL-level functions are mostly used. The higher-
 * level FTM_DRV_XXXX() functions are mostly not suitable for use here, because
 * they only allow specifying FlexTimer periods of down to 1 second (because
 * ftm_pwm_param_t.uFrequencyHZ is an integer), whereas we need a number of
 * minutes.
 *
 */

#include <stdint.h>
#include <stdbool.h>
#include "PowerOffFailsafe.h"
#include "Resources.h"
#include "fsl_ftm_driver.h"
#include "fsl_ftm_hal.h"
#include "PinDefs.h"
#include "PinConfig.h"
#include "Timer.h"

// ***** NOTE: Can test using TEST-IO1 and FlexTimer 2 if required, so can
// more easily see what's going on (instead of normal TMR-POWER-OFF# and
// FlexTimer 1). BUT 13/10/2016: NOTE that TEST-IO1 is now used for engineering
// mode, so need to be careful.
//#define POWEROFFFAILSAFE_TEST_IO1_AND_FTM2
#ifndef POWEROFFFAILSAFE_TEST_IO1_AND_FTM2 // NOT POWEROFFFAILSAFE_TEST_IO1_AND_FTM2
// Use TMR-POWER-OFF# and FlexTimer 1 for normal power-off triggering
#define POWEROFFFAILSAFE_PIN_NAME       TMR_POWER_OFFn
#define POWEROFFFAILSAFE_PIN_MUX_MODE   kPortMuxAlt3
#define POWEROFFFAILSAFE_FTM_INSTANCE   (FLEXTIMER_ALLOC_POWER_OFF_FAILSAFE)
#define POWEROFFFAILSAFE_FTM_CHANNEL    (0)
#else
// Use TEST-IO1 and FlexTimer 2 for testing
#define POWEROFFFAILSAFE_PIN_NAME       TEST_IO1
#define POWEROFFFAILSAFE_PIN_MUX_MODE   kPortMuxAlt3
#define POWEROFFFAILSAFE_FTM_INSTANCE   (2)
#define POWEROFFFAILSAFE_FTM_CHANNEL    (0)
#endif

// POWEROFFFAILSAFE_FLEXTIMER_MATCH_VAL specifies the FlexTimer counter value
// at which the channel output fires - needs to be near 1 because needs to
// trigger at end of up-down counter cycle.
#define POWEROFFFAILSAFE_FLEXTIMER_MATCH_VAL   (2)

//..............................................................................

static bool g_bPowerOffFailsafeIsRunning = false;

//..............................................................................

static bool PowerOffFailsafe_FlexTimerStart(uint16_t Secs);
static void PowerOffFailsafe_FlexTimerStop(void);

//..............................................................................

/*
 * PowerOffFailsafe_Start
 *
 * @desc    Starts the power-off failsafe, with a period specified by Secs.
 *          // *****************************************************************
 *                                     IMPORTANT:
 *            - Before this function is called, the TMR-POWER-OFF# pin must
 *              already have been set as GPIO output high (preferably shortly
 *              after firmware start-up)
 *            - Secs can only be up to POWEROFFFAILSAFE_MAX_SECS
 *            - Uses the Kinetis 32kHz slow internal reference clock via
 *              MCGFFCLK (see reference manual 3.8.2.3 Fixed frequency clock) -
 *              this must have been set up before this function is called
 *            - Must be called AFTER the system clocks have been configured
 *              and the 32kHz oscillator has stabilised
 *          // *****************************************************************
 *
 * @param   Secs: Duration before the FlexTimer fires. Must be > 0 and
 *          <= POWEROFFFAILSAFE_MAX_SECS.
 *
 * @returns true if OK, false if problem.
 */
bool PowerOffFailsafe_Start(uint16_t Secs)
{
    bool bOK;

    if (g_bPowerOffFailsafeIsRunning)
    {
        return false;
    }

    // Ensure that the control line starts off as GPIO output high
    // N.B. Should have already been done before this, but good precaution
    // to prevent pin glitching
    pinConfigDigitalOut(POWEROFFFAILSAFE_PIN_NAME, kPortMuxAsGpio, 1, false);
    DelayMicrosecs(10000);

    // Start the FlexTimer
    bOK = PowerOffFailsafe_FlexTimerStart(Secs);

    // For safety, delay to allow any initial FlexTimer output pulse activity
    // to complete beyond its initial POWEROFFFAILSAFE_FLEXTIMER_MATCH_VAL
    // period (if any). This period must be substantially longer than the
    // number of 250Hz clocks required to go down past the match value to zero,
    // and then back up to the match value (which is about 16ms for
    // POWEROFFFAILSAFE_FLEXTIMER_MATCH_VAL = 2).
    // IMPORTANT: Do NOT use a FreeRTOS task delay, because this function will
    // be called before FreeRTOS starts up
    DelayMicrosecs(50000);  // Delay 50ms to be safe

    // Now configure power-off control line as FlexTimer output - at this time
    // it should be high
    // NOTE: Should be no voltage glitch here.
    pinConfigDigitalOut(POWEROFFFAILSAFE_PIN_NAME,
                        POWEROFFFAILSAFE_PIN_MUX_MODE, 0, false);

    g_bPowerOffFailsafeIsRunning = true;

    return bOK;
}

/*
 * PowerOffFailsafe_Stop
 *
 * @desc    Stops and resets the power-off failsafe.
 *
 * @param   -
 *
 * @returns -
 */
void PowerOffFailsafe_Stop(void)
{
    // Configure power-off pin as GPIO output high before anything else,
    // to ensure that stays stable
    pinConfigDigitalOut(POWEROFFFAILSAFE_PIN_NAME, kPortMuxAsGpio, 1, false);

    PowerOffFailsafe_FlexTimerStop();

    g_bPowerOffFailsafeIsRunning = false;
}

/*
 * PowerOffFailsafe_IsRunning
 *
 * @desc    Indicates whether the power-off failsafe is currently running.
 *
 * @param   -
 *
 * @returns true if failsafe is currently running, false otherwise.
 */
bool PowerOffFailsafe_IsRunning(void)
{
    return g_bPowerOffFailsafeIsRunning;
}

/*
 * PowerOffFailsafe_FlexTimerStart
 *
 * @desc    Starts the FlexTimer of the power-off failsafe, with a period
 *          specified by Secs. The FlexTimer clocking is sourced from the
 *          32kHz internal Kinetis clock (see kClock_source_FTM_FixedClk),
 *          and uses a /128 prescaler to give a 250Hz counter clocking rate.
 *
 *          *** IMPORTANT: Secs can only be up to POWEROFFFAILSAFE_MAX_SECS.
 *
 *          N.B. This function was heavily adapted from FTM_DRV_PwmStart() and
 *          FTM_HAL_EnablePwmMode().
 *
 * @param   Secs: Duration before the FlexTimer fires. Must be > 0 and
 *          <= POWEROFFFAILSAFE_MAX_SECS.
 *
 * @returns true if OK, false if problem.
 */
static bool PowerOffFailsafe_FlexTimerStart(uint16_t Secs)
{
    uint16_t ModuloVal, MatchVal;
    FTM_Type *pFtmBase = g_ftmBase[POWEROFFFAILSAFE_FTM_INSTANCE];
    uint8_t chnlPairnum;
    const ftm_user_config_t FtmUserConfig =
    {
        .tofFrequency = 0,          // FTMx_CONF -> NUMTOF field
        .BDMMode = kFtmBdmMode_11,
        .isWriteProtection = false,
        .syncMethod = kFtmUseSoftwareTrig
    };

    if ((Secs == 0) || (Secs > POWEROFFFAILSAFE_MAX_SECS))
    {
        return false;
    }

    // Always returns kStatusFtmSuccess, so don't check return value
    // N.B. FTM_DRV_Init() enables an interrupt, but it doesn't fire before
    // failsafe timeout fires, so no problem
    FTM_DRV_Init(POWEROFFFAILSAFE_FTM_INSTANCE, &FtmUserConfig);

    // Configure prescaler for /128, so gives 250Hz clock from 32KHz input clock
    FTM_HAL_SetClockPs(pFtmBase, kFtmDividedBy128);

    // Clear the overflow flag
    FTM_HAL_ClearTimerOverflow(pFtmBase);

    //..........................................................................
    // The following was adapted from FTM_HAL_EnablePwmMode()

    chnlPairnum = FTM_HAL_GetChnPairIndex(POWEROFFFAILSAFE_FTM_CHANNEL);

    // Disable dual-edge capture mode
    FTM_HAL_SetDualEdgeCaptureCmd(pFtmBase, chnlPairnum, false);

    // Configure for low-true pulses
    FTM_HAL_SetChnEdgeLevel(pFtmBase, POWEROFFFAILSAFE_FTM_CHANNEL, 1);

    // Disable channel pairing
    FTM_HAL_SetDualChnCombineCmd(pFtmBase, chnlPairnum, false);

    // Set up/down counting mode - doubles the achievable period
    FTM_HAL_SetCpwms(pFtmBase, 1);

    // Set to edge-aligned PWM mode
    FTM_HAL_SetChnMSnBAMode(pFtmBase, POWEROFFFAILSAFE_FTM_CHANNEL, 2);

    //..........................................................................

    // Based on Ref manual, in PWM mode CNTIN is to be set 0
    FTM_HAL_SetCounterInitVal(pFtmBase, 0);

    // Calculate modulo (top count value) for the required seconds - each
    // second requires HALF the number of 250Hz clocks, because using an
    // up-then-down counting approach (so that can achieve longer periods)
    ModuloVal = Secs * (250 / 2);
    MatchVal = POWEROFFFAILSAFE_FLEXTIMER_MATCH_VAL;
    FTM_HAL_SetMod(pFtmBase, ModuloVal);
    FTM_HAL_SetChnCountVal(pFtmBase, POWEROFFFAILSAFE_FTM_CHANNEL, MatchVal);

    // Set clock source to start the counter (from Kinetis internal 32kHz clock)
    FTM_HAL_SetClockSource(pFtmBase, kClock_source_FTM_FixedClk);

    return true;
}

/*
 * PowerOffFailsafe_FlexTimerStop
 *
 * @desc    Stops the power-off failsafe FlexTimer.
 *
 * @param   -
 *
 * @returns -
 */
static void PowerOffFailsafe_FlexTimerStop(void)
{
    FTM_DRV_Deinit(POWEROFFFAILSAFE_FTM_INSTANCE);
}




#ifdef __cplusplus
}
#endif