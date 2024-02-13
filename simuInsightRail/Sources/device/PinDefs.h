#ifdef __cplusplus
extern "C" {
#endif

/*
 * PinDefs.h
 *
 *  Created on: Jan 12, 2016
 *      Author: Bart Willemse
 */

#include "fsl_gpio_driver.h"

#ifndef PINDEFS_H_
#define PINDEFS_H_

enum
{
    // TODO: ***** Some of the following are currently also defined in gpio.h,
    // and therefore are commented out below - need to tidy up eventually

    //..........................................................................
    // Rev1-board-specific pin definitions
    // TODO: Also Figure out DC_LEVEL - is on PTA13 and PTB7
    nPWR_ON =               GPIO_MAKE_PIN(GPIOB_IDX, 9U),
    WATCHDOG_RESET =        GPIO_MAKE_PIN(GPIOA_IDX, 10U),
    FLASH_WPn =             GPIO_MAKE_PIN(GPIOA_IDX, 29U),
    SYSTEM_STATUS_LED1 =    GPIO_MAKE_PIN(GPIOE_IDX, 6U),
    SYSTEM_STATUS_LED2 =    GPIO_MAKE_PIN(GPIOB_IDX, 8U),
    uP_POWER_TEST =         GPIO_MAKE_PIN(GPIOC_IDX, 7U),   // NOTE: On rev 2, PTC7 is WIRELESS_BURST
    TEMP_ALERTn =           GPIO_MAKE_PIN(GPIOC_IDX, 9U),
    GNSS_INTn =             GPIO_MAKE_PIN(GPIOD_IDX, 0U),
    ACCEL_INT2n =           GPIO_MAKE_PIN(GPIOD_IDX, 5U),
    ADC_DVDD_ON =           GPIO_MAKE_PIN(GPIOE_IDX, 4U),   // NOTE: On rev 2, PTE4 is PSU_ENABLE
    DC_CTL =                GPIO_MAKE_PIN(GPIOC_IDX, 3U),
    TEST_IO1 =              GPIO_MAKE_PIN(GPIOB_IDX, 18U),
    TEST_IO2 =              GPIO_MAKE_PIN(GPIOC_IDX, 0U),

    // HW board ID
    ID_1 =                  GPIO_MAKE_PIN(GPIOB_IDX, 19U),
    ID_2 =                  GPIO_MAKE_PIN(GPIOB_IDX, 20U),
    ID_3 =                  GPIO_MAKE_PIN(GPIOB_IDX, 21U),
    ID_4 =                  GPIO_MAKE_PIN(GPIOB_IDX, 22U),
    ID_5 =                  GPIO_MAKE_PIN(GPIOB_IDX, 23U),

    //..........................................................................
    // Rev2-board-specific pin definitions
    TMR_POWER_OFFn =        GPIO_MAKE_PIN(GPIOB_IDX, 0U),   // N.B. Also has useful test point on rev 1 board
    WIRELESS_BURST =        GPIO_MAKE_PIN(GPIOC_IDX, 7U),   // NOTE: On rev 1, PTC7 is uP_POWER_TEST
    BANDPASS_SELECT =       GPIO_MAKE_PIN(GPIOC_IDX, 12U),
    CLI_UART_RTS =          GPIO_MAKE_PIN(GPIOC_IDX, 18U),
    CLI_UART_CTS =          GPIO_MAKE_PIN(GPIOC_IDX, 19U),
    CLI_UART_TX  =          GPIO_MAKE_PIN(GPIOC_IDX, 17U),
    CLI_UART_RX  =          GPIO_MAKE_PIN(GPIOC_IDX, 16U),
    TEST_UART_TX =          GPIO_MAKE_PIN(GPIOC_IDX, 15U),
    TEST_UART_RX =          GPIO_MAKE_PIN(GPIOC_IDX, 14U),
	PMIC_UART_TX =          GPIO_MAKE_PIN(GPIOD_IDX, 8U),
	PMIC_UART_RX =          GPIO_MAKE_PIN(GPIOD_IDX, 9U),
    PSU_ENABLE =            GPIO_MAKE_PIN(GPIOE_IDX, 4U),   // NOTE: On rev 1, PTE4 is ADC_DVDD_ON
    POWER_SAVEn =           GPIO_MAKE_PIN(GPIOE_IDX, 26U),

    //..........................................................................
    // Cellular
    CELLULAR_TXD0 =         GPIO_MAKE_PIN(GPIOE_IDX, 0U),
    CELLULAR_RXD0 =         GPIO_MAKE_PIN(GPIOE_IDX, 1U),
    CELLULAR_CTS0 =         GPIO_MAKE_PIN(GPIOE_IDX, 2U),
    CELLULAR_RTS0 =         GPIO_MAKE_PIN(GPIOE_IDX, 3U),
    EHS5E_FST_SHDN =        GPIO_MAKE_PIN(GPIOB_IDX, 12U),
    EHS5E_DCD   =           GPIO_MAKE_PIN(GPIOC_IDX, 6U),
    EHS5E_EMERG_RST =       GPIO_MAKE_PIN(GPIOB_IDX, 13U),
    EHS5E_AUTO_ON =         GPIO_MAKE_PIN(GPIOB_IDX, 6U),
    CELLULAR_DTR =          GPIO_MAKE_PIN(GPIOD_IDX, 15U),
    EHS5E_POWER_IND =       GPIO_MAKE_PIN(GPIOB_IDX, 2U),
    WIRELESS_ON =           GPIO_MAKE_PIN(GPIOB_IDX, 3U),

    // mems 3d acc
    ACCEL_INT1n =           GPIO_MAKE_PIN(GPIOD_IDX, 10U),

	// PMIC reset
	PMIC_RESET =           	GPIO_MAKE_PIN(GPIOD_IDX, 10U),

	// harvester control
	HARVESTER_HALT =		GPIO_MAKE_PIN(GPIOB_IDX, 0U),

    // GNSS
    GNSS_RESET_HARV =       GPIO_MAKE_PIN(GPIOD_IDX, 7U),
	GNSS_RESET_BATT = 		GPIO_MAKE_PIN(GPIOD_IDX, 9U),

    GNSS_ON =               GPIO_MAKE_PIN(GPIOD_IDX, 1U),
    GNSS_TX =               GPIO_MAKE_PIN(GPIOD_IDX, 2U),
    GNSS_RX =               GPIO_MAKE_PIN(GPIOD_IDX, 3U),
//  GNSS_INTn =             GPIO_MAKE_PIN(GPIOD_IDX, 0U),

    // Analog
    VIBRATION_ON =          GPIO_MAKE_PIN(GPIOC_IDX, 4U),
    VIB_SELF_TEST_EN =      GPIO_MAKE_PIN(GPIOC_IDX, 1U),
    PREAMP_GAIN_OFF =       GPIO_MAKE_PIN(GPIOC_IDX, 5U),
    HP_SELECT =             GPIO_MAKE_PIN(GPIOC_IDX, 2U),
    SPI1_MOSI =             GPIO_MAKE_PIN(GPIOB_IDX, 16U),
    SPI1_MISO =             GPIO_MAKE_PIN(GPIOB_IDX, 17U),
    SPI1_SCK =              GPIO_MAKE_PIN(GPIOB_IDX, 11U),
    ADC_CSn =               GPIO_MAKE_PIN(GPIOB_IDX, 10U),
    ADC_PWRDNn_HARV =       GPIO_MAKE_PIN(GPIOD_IDX, 6U),
	ADC_PWRDNn_BATT =       GPIO_MAKE_PIN(GPIOD_IDX, 8U),
    ADC_MCLK =              GPIO_MAKE_PIN(GPIOD_IDX, 4U),
    ADC_IRQn =              GPIO_MAKE_PIN(GPIOE_IDX, 5U),
    //...........................
    // TODO: Not GPIO pins - figure out what to do with these
    //  SELF_TEST_PULSE =       GPIO_MAKE_PIN(GPIO _IDX, nnU),
    //  VIBRATION =             GPIO_MAKE_PIN(GPIO _IDX, nnU),
    //  VIBRATION_N =           GPIO_MAKE_PIN(GPIO _IDX, nnU),
    //...........................
    // ADC inputs
    DC_LEVEL =              GPIO_MAKE_PIN(GPIOB_IDX, 7U),

    // I2C interface signals
    I2C0_SCL =              GPIO_MAKE_PIN(GPIOE_IDX, 24U),
    I2C0_SDA =              GPIO_MAKE_PIN(GPIOE_IDX, 25U),
    I2C1_SCL =              GPIO_MAKE_PIN(GPIOC_IDX, 10U),
    I2C1_SDA =              GPIO_MAKE_PIN(GPIOC_IDX, 11U),
    I2C2_SCL =              GPIO_MAKE_PIN(GPIOA_IDX, 12U),
    I2C2_SDA =              GPIO_MAKE_PIN(GPIOA_IDX, 11U),

	// PMIC SWD interface
	MK24_SPI2_CLK =			GPIO_MAKE_PIN(GPIOD_IDX, 12U),
	MK24_SPI2_MOSI =		GPIO_MAKE_PIN(GPIOD_IDX, 13U),
	MK24_SPI2_MISO =		GPIO_MAKE_PIN(GPIOD_IDX, 14U),

    // Flash
    FLASH_CSn =             GPIO_MAKE_PIN(GPIOA_IDX, 14U),
    SPI0_MOSI =             GPIO_MAKE_PIN(GPIOA_IDX, 16U),
    SPI0_MISO =             GPIO_MAKE_PIN(GPIOA_IDX, 17U),
    SPI0_SCK =              GPIO_MAKE_PIN(GPIOA_IDX, 15U)
};

#endif // PINDEFS_H_



#ifdef __cplusplus
}
#endif