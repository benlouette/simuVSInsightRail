#ifdef __cplusplus
extern "C" {
#endif

/*
 * configPinNames.c
 *
 *  Created on: June 30, 2016
 *      Author: Bart Willemse (adapted from D. van der Velde's .h file approach)
 * Description:
 *
 */


#include "PinDefs.h"

//..............................................................................

static const struct
{
    uint32_t NameID;
    char *String;
} g_PinNameStrings[] =
{
    //..........................................................................
    // Rev1-board-specific
    {WATCHDOG_RESET, "WATCHDOG_RESET"},
    {FLASH_WPn, "FLASH_WPn"},
    {SYSTEM_STATUS_LED2, "SYSTEM_STATUS_LED2"},
    {TEMP_ALERTn, "TEMP_ALERTn"},
    {GNSS_INTn, "GNSS_INTn"},
    {ACCEL_INT2n, "ACCEL_INT2n"},
    {uP_POWER_TEST, "uP_POWER_TEST"},
    {ADC_DVDD_ON, "ADC_DVDD_ON"},

    //..........................................................................
    // Rev2-board-specific
    {TMR_POWER_OFFn, "TMR_POWER_OFFn"},
    {WIRELESS_BURST, "WIRELESS_BURST"},
    {BANDPASS_SELECT, "BANDPASS_SELECT"},
    {CLI_UART_RTS, "CLI_UART_RTS"},
    {CLI_UART_CTS, "CLI_UART_CTS"},
    {PSU_ENABLE, "PSU_ENABLE"},
    {POWER_SAVEn, "POWER_SAVEn"},

    //..........................................................................

    {ID_1, "ID_1"},
    {ID_2, "ID_2"},
    {ID_3, "ID_3"},
    {ID_4, "ID_4"},
    {ID_5, "ID_5"},
    {ADC_IRQn, "ADC_IRQn"},
    {ACCEL_INT1n, "ACCEL_INT1n"},
    {EHS5E_POWER_IND, "EHS5E_POWER_IND"},
    {SYSTEM_STATUS_LED1, "SYSTEM_STATUS_LED1"},
    {nPWR_ON, "nPWR_ON"},
    {WIRELESS_ON, "WIRELESS_ON"},
    {EHS5E_AUTO_ON, "EHS5E_AUTO_ON"},
    {EHS5E_FST_SHDN, "EHS5E_FST_SHDN"},
    {EHS5E_EMERG_RST, "EHS5E_EMERG_RST"},
    {TEST_IO1, "TEST_IO1"},
    {TEST_IO2, "TEST_IO2"},
    {EHS5E_DCD, "EHS5E_DCD"},
    {VIB_SELF_TEST_EN, "VIB_SELF_TEST_EN"},
    {HP_SELECT, "HP_SELECT"},
    {DC_CTL, "DC_CTL"},
    {VIBRATION_ON, "VIBRATION_ON"},
    {PREAMP_GAIN_OFF, "PREAMP_GAIN_OFF"},
    {ADC_PWRDNn_HARV, "ADC_PWRDNn_HARV"},
	{ADC_PWRDNn_BATT, "ADC_PWRDNn_BATT"},
    {CELLULAR_DTR, "CELLULAR_DTR"},
    {GNSS_RESET_HARV, "GNSS_RESET_HARV"},
	{GNSS_RESET_BATT, "GNSS_RESET_BATT"},
    {GNSS_ON, "GNSS_ON"},
};

static const char *g_UnknownStr = "<unknown>";

//..............................................................................

/*
 * ConfigPinNameToString
 *
 * @desc    Returns a string corresponding to the PinName pin ID
 *
 * @param   PinName: Pin ID defined by GPIO_MAKE_PIN()
 *
 * @returns Pointer to pin name string
 */
char *ConfigPinNameToString(uint32_t PinName)
{
    uint16_t i;
    uint16_t PinStrArrayLen = sizeof(g_PinNameStrings) / sizeof(g_PinNameStrings[0]);

    for (i = 0; i < PinStrArrayLen; i++)
    {
        if (g_PinNameStrings[i].NameID == PinName)
        {
            return g_PinNameStrings[i].String;
        }
    }

    return (char *)g_UnknownStr;
}

#if 0
static volatile char *pTestString;
/*
 * ConfigPinNameToString_TEST
 *
 * @desc
 *
 * @param
 *
 * @returns
 */
void ConfigPinNameToString_TEST(void)
{
    pTestString = ConfigPinNameToString(TEST_IO1);
    pTestString = ConfigPinNameToString(DC_CTL);
    pTestString = ConfigPinNameToString(PREAMP_GAIN_OFF);
    pTestString = ConfigPinNameToString(99999);  // Should return "<unknown>"
    pTestString = ConfigPinNameToString(GNSS_ON);
}
#endif // 0


#ifdef __cplusplus
}
#endif