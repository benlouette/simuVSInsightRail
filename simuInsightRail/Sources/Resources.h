#ifdef __cplusplus
extern "C" {
#endif

/*
 * Resources.h
 *
 *  Created on: 9 mei 2014
 *      Author: frijnsewijn
 */

#ifndef RESOURCES_H_
#define RESOURCES_H_

/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>
#include "Insight.h"
#include "FreeRTOS.h"
#include "task.h"

#include "fsl_device_registers.h"



/*
 * Macros & Types
 */

/*--------------------------------------------------------------------------*
 |   System Configuration Macros                                            |
 *--------------------------------------------------------------------------*/

#ifdef CPU_PARTNUM_MK20DX128VLH5
/****************************************************************************
 * FRDM-K20D50 controller
 ****************************************************************************/

// Memory
#define SYS_FLASH_SIZE              (131072)      // 128kB
#define SYS_RAM_SIZE                (16384)       // 16kB

#define SYS_FLASH_SECTOR_SIZE       (0x400)       // 1kB
#define SYS_FLASH_CONFIG_ADDRESS    (0x1F800)     // 2Kb from end of flash !! TODO make sure this is reserved in the linker map !!!

#define SYS_FLASH_TOP                       (0x20000)
#define SYS_FLASH_HART_CONFIG_SIZE          (0x0400)     // 1Kb
#define SYS_FLASH_HART_CONFIG_ADDRESS       (SYS_FLASH_TOP - SYS_FLASH_HART_CONFIG_SIZE )

// ADCBUF (must be power of 2)
#define SYS_ADCBUF_SIZE             (2048)        // 2kB (=1024 16-bit ADC samples)

// PROBUF (must be power of 2)
#define SYS_PROBUF_SIZE             (2048)        // 2kB (=1024 16-bit processed samples)

// PDB
#define DRVPDB_PDBID_ADC            (0)
#define DRVPDB_CFG_NUM_DEVICES      (1)           // Only 1 PDB available
#define DRVPDB_INIT                 { { PDB0_BASE_PTR } }

// ADC
#define DRVADC_ADCID_A              (0)           // ADC 0 used for measurements
#define DRVADC_CFG_NUM_DEVICES      (1)           // One ADC instance available
#define DRVADC_INIT                 { { ADC0_BASE_PTR } }

#define DRVADC_ADC_VTEMP_Q16        (47120384L)   // V_TEMP from data sheet: 719[mV]
#define DRVADC_ADC_TEMPSLOPE_Q16    (112394L)     // TEMP SLOPE from data sheet: 1.715 [mV/deg C]
#define DRVADC_ADC_VTEMP            (719.0F)      // V_TEMP from data sheet: 719[mV]
#define DRVADC_ADC_TEMPSLOPE        (1.715F)      // TEMP SLOPE from data sheet: 1.715 [mV/deg C]

// DMA
#define DRVDMA_DMAID_A              (0)           // Single eDMA controller
#define DRVDMA_CFG_NUM_DEVICES      (1)           // One eDMA instance available
#define DRVDMA_CFG_NUM_CHANNELS     (4)           // 4 eDMA channels available
#define DRVDMA_INIT                 { { DMAMUX_BASE_PTR, DMA_BASE_PTR, {NULL, } } }

// PIT
#define DRVPIT_PITID_A              (0)           // Single PIT controller
#define DRVPIT_CFG_NUM_DEVICES      (1)           // One PIT instance available
#define DRVPIT_CFG_NUM_TIMERS       (4)           // 4 PIT timers available
#define DRVPIT_INIT                 { \
    { PIT_BASE_PTR, \
      {NULL, NULL, NULL, NULL }, \
      {false, false, false, false} } }

#define DRVPIT_TIMERID_RPMPULSE     (0)           // Timer used by PulseMeasure module
#define DRVPIT_TIMERID_RESV1        (1)           // Reserved (free...)
#define DRVPIT_TIMERID_RESV2        (2)           // Reserved (free...)
#define DRVPIT_TIMERID_RESV3        (3)           // Reserved (free...)

// UART
#define DRVUART_UARTID_0            (0)           // UART0, core clock + 8 fifo
#define DRVUART_UARTID_1            (1)           // UART1, core clock
#define DRVUART_UARTID_2            (2)           // UART2, bus clock
#define DRVUART_CFG_NUM_DEVICES     (3)           // 3 UART peripherals available
#define DRVUART_INIT                { \
    { UART0_BASE_PTR, 115200, true },   /* CLI */ \
    { UART1_BASE_PTR, 115200, false },  /* DUST */ \
    { UART2_BASE_PTR, 57600, false } }  /* RS485/Local port */

// TMP108
#define DRVTMP108_ID_0              (0)
#define DRVTMP108_CFG_NUM_DEVICES   (1)
#define DRVTMP108_INIT { \
    { { 0x49, NULL, {0,}, false, false, false } }

#elif defined(CPU_MK22FN512VLH12)
/*****************************************************************************
 * FRDM-K22F Controller
 *****************************************************************************/

// Memory
#define SYS_FLASH_SIZE              (524288)      // 512kB
#define SYS_RAM_SIZE                (131072)      // 128kB

#define SYS_FLASH_SECTOR_SIZE       (0x800)       // 2kB

// new definitions of flash configuration layout here, just make sure the linkermap keeps this space free.
/*
 *  sector size flash is 2kb, total memory is 512kbyte
    top of memory        = 0x80000
    bootloader config 1 sect = 0x7f800..0x7ffff ( 2kb = 0x0800)
    startup config    1 sect = 0x7f000..0xf77ff ( 2kb = 0x0800)
    event log         1 sect = 0x7e800..0x7efff ( 2kb = 0x0800)
    HARTconfig+shadow 2 sect = 0x7d800..0x7e7ff ( 4kb = 0x1000)
    revcounter+shadow 2 sect = 0x7c800..0x7d7ff ( 4kb = 0x1000)

    spare                = 0x78000..0x7c7ff (18kb = 0x4800)

    program flash            = 0x00410..0x77fff ( 478kb = 0x77bf0)
    with bootloader          = 0x08410..0x77fff ( 446kb = 0x6fbf0)
 *
 */
#define SYS_FLASH_TOP                       (0x80000)

#define SYS_FLASH_BOOT_CONFIG_SIZE          (0x00800)     // 2Kb
#define SYS_FLASH_BOOT_CONFIG_ADDRESS       (SYS_FLASH_TOP - SYS_FLASH_BOOT_CONFIG_SIZE)


#define SYS_FLASH_STARTUP_CONFIG_SIZE       (0x00800)     // 2Kb
#define SYS_FLASH_STARTUP_CONFIG_ADDRESS    (SYS_FLASH_BOOT_CONFIG_ADDRESS - SYS_FLASH_STARTUP_CONFIG_SIZE)

#define SYS_FLASH_EVENTLOG_SIZE             (0x00800)     // 2Kb
#define SYS_FLASH_EVENTLOG_ADDRESS          (SYS_FLASH_STARTUP_CONFIG_ADDRESS - SYS_FLASH_EVENTLOG_SIZE)

#define SYS_FLASH_HART_CONFIG_SIZE          (0x01000)     // 4Kb
#define SYS_FLASH_HART_CONFIG_ADDRESS       (SYS_FLASH_EVENTLOG_ADDRESS - SYS_FLASH_HART_CONFIG_SIZE )

#define SYS_FLASH_REVCOUNTER_SIZE           (0x01000)     // 4Kb
#define SYS_FLASH_REVCOUNTER_ADDRESS        (SYS_FLASH_HART_CONFIG_ADDRESS - SYS_FLASH_REVCOUNTER_SIZE)



// ADCBUF (must be power of 2)
//#define SYS_ADCBUF_SIZE             (4096)        // 4kB (2048 16-bit words, 1024 32-bit words)
#define SYS_ADCBUF_SIZE             (2048)        // 2kB (1024 16-bit words, 512 32-bit words)

// PROBUF (must be power of 2)
#define SYS_PROBUF_SIZE             (65536)       // 64kB (=32768 16-bit processed samples)
#define SYS_PROBUF_MAXSAMPLES       (SYS_PROBUF_SIZE>>1)  // 16-bits samples

// PDB
#define DRVPDB_PDBID_ADC            (0)           // PDB 0 used for ADC triggers
//#define DRVPDB_PDBID_DAC            (1)           // PDB 1 used for DAC triggers
#define DRVPDB_CFG_NUM_DEVICES      (1)           // Two PDB instances available
#define DRVPDB_INIT                 { { PDB0_BASE_PTR } }

// ADC
#define DRVADC_ADCID_A              (0)           // ADC 0 used for vibration
#define DRVADC_ADCID_B              (1)           // ADC 1 used for other measurements
#define DRVADC_CFG_NUM_DEVICES      (2)           // Two ADC instances available
#define DRVADC_INIT                 { { ADC0_BASE_PTR }, { ADC1_BASE_PTR } }

#define DRVADC_ADC_VTEMP_Q16        (46923776L)   // V_TEMP from data sheet: 716[mV]
#define DRVADC_ADC_TEMPSLOPE_Q16    (106168L)     // TEMP SLOPE from data sheet: 1.62 [mV/deg C]
#define DRVADC_ADC_VTEMP            (716.0F)      // V_TEMP from data sheet: 716[mV]
#define DRVADC_ADC_TEMPSLOPE        (1.62F)       // TEMP SLOPE from data sheet: 1.62 [mV/deg C]

// DMA
#define DRVDMA_DMAID_A              (0)           // Single eDMA controller
#define DRVDMA_CFG_NUM_DEVICES      (1)           // One eDMA instance available
#define DRVDMA_CFG_NUM_CHANNELS     (16)          // 16 eDMA channels available
#define DRVDMA_INIT                 { { DMAMUX_BASE_PTR, DMA_BASE_PTR, {NULL, } } }

// PIT
#define DRVPIT_PITID_A              (0)           // Single PIT controller
#define DRVPIT_CFG_NUM_DEVICES      (1)           // One PIT instance available
#define DRVPIT_CFG_NUM_TIMERS       (4)           // 4 PIT timers available
#define DRVPIT_INIT                 { { PIT_BASE_PTR, \
                                        {NULL, NULL, NULL, NULL }, \
                                        {false, false, false, false} } }

#define DRVPIT_TIMERID_RPMPULSE     (0)           // Timer used by PulseMeasure module
#define DRVPIT_TIMERID_RESV1        (1)           // Reserved (free...)
#define DRVPIT_TIMERID_RESV2        (2)           // Reserved (free...)
#define DRVPIT_TIMERID_RESV3        (3)           // Reserved (free...)

// UART
#define DRVUART_UARTID_0            (0)           // UART0, core clock + 8 fifo
#define DRVUART_UARTID_1            (1)           // UART1, core clock + 8 fifo
#define DRVUART_UARTID_2            (2)           // UART2, bus clock
//#define DRVUART_UARTID_3            (3)           // UART3, bus clock
//#define DRVUART_UARTID_4            (4)           // UART4, bus clock
//#define DRVUART_UARTID_5            (5)           // UART5, bus clock
#define DRVUART_CFG_NUM_DEVICES     (3)           // 6 UART peripherals available
#define DRVUART_INIT                { \
    { UART0_BASE_PTR, 57600, true },    /* RS485 / local port */ \
    { UART1_BASE_PTR, 115200, true },    /* CLI */ \
    { UART2_BASE_PTR, 115200, false } } /* not used */
#define RS485_UART_DEVICE           UART0_BASE_PTR
#define CLI_UART_DEVICE             UART1_BASE_PTR


// TMP108
#define DRVTMP108_ID_0              (0)
#define DRVTMP108_CFG_NUM_DEVICES   (1)
#define DRVTMP108_INIT { \
    { 0x49, NULL, {0,} } }


#elif defined(CPU_PARTNUM_MK10DN512VMC10) || defined(CPU_MK10DN512VMC10)
/*****************************************************************************
 * MK10DN512VMC10, Insight M&I prototype 1 board
 *****************************************************************************/

// Memory
#define SYS_FLASH_SIZE              (524288)      // 512kB
#define SYS_RAM_SIZE                (131072)      // 128kB

#define SYS_FLASH_SECTOR_SIZE       (0x800)       // 2kB

// new definitions of flash configuration layout here, just make sure the linkermap keeps this space free.
/*
 *  sector size flash is 2kb, total memory is 512kbyte
    top of memory        = 0x80000
    bootloader config 1 sect = 0x7f800..0x7ffff ( 2kb = 0x0800)
    startup config    1 sect = 0x7f000..0xf77ff ( 2kb = 0x0800)
    event log         1 sect = 0x7e800..0x7efff ( 2kb = 0x0800)
    HARTconfig+shadow 2 sect = 0x7d800..0x7e7ff ( 4kb = 0x1000)
    revcounter+shadow 2 sect = 0x7c800..0x7d7ff ( 4kb = 0x1000)

    spare                = 0x78000..0x7c7ff (18kb = 0x4800)

    program flash            = 0x00410..0x77fff ( 478kb = 0x77bf0)
    with bootloader          = 0x08410..0x77fff ( 446kb = 0x6fbf0)
 *
 */
#define SYS_FLASH_TOP                       (0x80000)

#define SYS_FLASH_BOOT_CONFIG_SIZE          (0x00800)     // 2Kb
#define SYS_FLASH_BOOT_CONFIG_ADDRESS       (SYS_FLASH_TOP - SYS_FLASH_BOOT_CONFIG_SIZE)


#define SYS_FLASH_STARTUP_CONFIG_SIZE       (0x00800)     // 2Kb
#define SYS_FLASH_STARTUP_CONFIG_ADDRESS    (SYS_FLASH_BOOT_CONFIG_ADDRESS - SYS_FLASH_STARTUP_CONFIG_SIZE)

#define SYS_FLASH_EVENTLOG_SIZE             (0x00800)     // 2Kb
#define SYS_FLASH_EVENTLOG_ADDRESS          (SYS_FLASH_STARTUP_CONFIG_ADDRESS - SYS_FLASH_EVENTLOG_SIZE)

#define SYS_FLASH_HART_CONFIG_SIZE          (0x01000)     // 4Kb
#define SYS_FLASH_HART_CONFIG_ADDRESS       (SYS_FLASH_EVENTLOG_ADDRESS - SYS_FLASH_HART_CONFIG_SIZE )

#define SYS_FLASH_REVCOUNTER_SIZE           (0x01000)     // 4Kb
#define SYS_FLASH_REVCOUNTER_ADDRESS        (SYS_FLASH_HART_CONFIG_ADDRESS - SYS_FLASH_REVCOUNTER_SIZE)



// ADCBUF (must be power of 2)
//#define SYS_ADCBUF_SIZE             (4096)        // 4kB (2048 16-bit words, 1024 32-bit words)
#define SYS_ADCBUF_SIZE             (2048)        // 2kB (1024 16-bit words, 512 32-bit words)

// PROBUF (must be power of 2)
#define SYS_PROBUF_SIZE             (65536)       // 64kB (=32768 16-bit processed samples)
#define SYS_PROBUF_MAXSAMPLES       (SYS_PROBUF_SIZE>>1)  // 16-bits samples

// PDB
#define DRVPDB_PDBID_ADC            (0)           // PDB 0 used for ADC triggers
//#define DRVPDB_PDBID_DAC            (1)           // PDB 1 used for DAC triggers
#define DRVPDB_CFG_NUM_DEVICES      (1)           // Two PDB instances available
#define DRVPDB_INIT                 { { PDB0_BASE_PTR } }

// ADC
#define DRVADC_ADCID_A              (0)           // ADC 0 used for vibration
#define DRVADC_ADCID_B              (1)           // ADC 1 used for other measurements
#define DRVADC_CFG_NUM_DEVICES      (2)           // Two PDB instances available
#define DRVADC_INIT                 { { ADC0_BASE_PTR }, { ADC1_BASE_PTR } }

#define DRVADC_ADC_VTEMP_Q16        (46923776L)   // V_TEMP from data sheet: 716[mV]
#define DRVADC_ADC_TEMPSLOPE_Q16    (106168L)     // TEMP SLOPE from data sheet: 1.62 [mV/deg C]
#define DRVADC_ADC_VTEMP            (716.0F)      // V_TEMP from data sheet: 716[mV]
#define DRVADC_ADC_TEMPSLOPE        (1.62F)       // TEMP SLOPE from data sheet: 1.62 [mV/deg C]

// DMA
#define DRVDMA_DMAID_A              (0)           // Single eDMA controller
#define DRVDMA_CFG_NUM_DEVICES      (1)           // One eDMA instance available
#define DRVDMA_CFG_NUM_CHANNELS     (16)          // 16 eDMA channels available
#define DRVDMA_INIT                 { { DMAMUX_BASE_PTR, DMA_BASE_PTR, {NULL, } } }

// PIT
#define DRVPIT_PITID_A              (0)           // Single PIT controller
#define DRVPIT_CFG_NUM_DEVICES      (1)           // One PIT instance available
#define DRVPIT_CFG_NUM_TIMERS       (4)           // 4 PIT timers available
#define DRVPIT_INIT                 { { PIT_BASE_PTR, \
                                        {NULL, NULL, NULL, NULL }, \
                                        {false, false, false, false} } }

#define DRVPIT_TIMERID_RPMPULSE     (0)           // Timer used by PulseMeasure module
#define DRVPIT_TIMERID_RESV1        (1)           // Reserved (free...)
#define DRVPIT_TIMERID_RESV2        (2)           // Reserved (free...)
#define DRVPIT_TIMERID_RESV3        (3)           // Reserved (free...)

// UART
#define DRVUART_UARTID_0            (0)           // UART0, core clock + 8 fifo
#define DRVUART_UARTID_1            (1)           // UART1, core clock + 8 fifo
#define DRVUART_UARTID_2            (2)           // UART2, bus clock
#define DRVUART_UARTID_3            (3)           // UART3, bus clock
#define DRVUART_UARTID_4            (4)           // UART4, bus clock
#define DRVUART_UARTID_5            (5)           // UART5, bus clock
#define DRVUART_CFG_NUM_DEVICES     (6)           // 6 UART peripherals available
#define DRVUART_INIT                { \
    { UART0_BASE_PTR, 115200, true },    /* DUST */ \
    { UART1_BASE_PTR, 115200, true },    /* not used */ \
    { UART2_BASE_PTR, 115200, false },   /* not used */ \
    { UART3_BASE_PTR, 57600, false },    /* RS485 / Local Port */ \
    { UART4_BASE_PTR, 115200, false },   /* CLI */ \
    { UART5_BASE_PTR, 115200, false }, } /* not used */

#define RS485_UART_DEVICE           UART3_BASE_PTR
#define CLI_UART_DEVICE             UART4_BASE_PTR


// TMP108
#define DRVTMP108_ID_0              (0)
#define DRVTMP108_CFG_NUM_DEVICES   (1)
#define DRVTMP108_INIT { \
    { 0x49, NULL, {0,} } }

#elif defined(CPU_MK24FN1M0VDC12)


#define DRVUART_UARTID_0            (0)           // UART0, core clock + 8 fifo
#define DRVUART_UARTID_1            (1)           // UART1, core clock + 8 fifo
#define DRVUART_UARTID_2            (2)           // UART2, bus clock
#define DRVUART_UARTID_3            (3)           // UART3, bus clock
#define DRVUART_UARTID_4            (4)           // UART4, bus clock
#define DRVUART_UARTID_5            (5)           // UART5, bus clock
#define DRVUART_CFG_NUM_DEVICES     (6)           // 6 UART peripherals available
#define DRVUART_INIT                { \
    { UART0_BASE_PTR, 115200, true },    /* not used */ \
    { UART1_BASE_PTR, 115200, true },    /* Cellular */ \
    { UART2_BASE_PTR, 115200, false },   /* GNSS */ \
    { UART3_BASE_PTR, 115200, false },   /* CLI  */ \
    { UART4_BASE_PTR, 115200, false },   /* Test UART */ \
    { UART5_BASE_PTR, 115200, false }, } /* not used */

#define CPU_MK24FN1M0VDC12_MIN_OPERATING_TEMP_DEG_C		(-40.0f)
#define CPU_MK24FN1M0VDC12_MAX_OPERATING_TEMP_DEG_C		(105.0f)

#else
// Compile error
#error "CPU not supported"
#endif

//..............................................................................
// Sample buffer definitions
// *** CRITICALLY IMPORTANT ***: SAMPLE_BUFFER_SIZE_BYTES below **MUST**
// correspond exactly to the buffer size defined for m_sample_buffer in the
// linker file.
// TODO: See if can also define a buffer SIZE symbol in linker file - is there
// some kind of LENGTH()-type construct?
#define SAMPLE_BUFFER_SIZE_BYTES  (0x00020000U)
#define SAMPLE_BUFFER_SIZE_WORDS  (SAMPLE_BUFFER_SIZE_BYTES / sizeof(int32_t))
extern int32_t *g_pSampleBuffer;    // NOTE: Signed

//..............................................................................
// EDMA channels and priorities
// TODO: Have currently set the channels to match the priorities required,
// because these are the power-on default priorities - see further explanation
// in the InitAppEdma() function. Need to improve eventually.
#define EDMACHANNEL_AD7766_TX   (14)
#define EDMACHANNEL_AD7766_RX   (15)

//..............................................................................
// FlexTimer allocations
#define FLEXTIMER_ALLOC_SAMPLING            (0)
#define FLEXTIMER_ALLOC_POWER_OFF_FAILSAFE  (1)
// NOTE: FlexTimer 2 can be optionally used for power-off failsafe testing - 
// see POWEROFFFAILSAFE_TEST_IO1_AND_FTM2

//..............................................................................

//*****************************************
//*****************************************
// Define a macro to get the number of rows of an array
#define ROW_COUNT(a)    (sizeof(a) / sizeof(a[0]))

//*****************************************
//*****************************************


//*****************************************
//*****************************************

#define PASSRAIL_DSP_NEW
//#define SAMPLING_EXEC_TIME_EN
#ifdef SAMPLING_EXEC_TIME_EN
extern TickType_t g_nStopSamplingTick;
extern TickType_t g_nStartSamplingTick;
#endif

// List the task info types.
typedef enum
{
	TASK_INFO_PRINT				= 0,
	TASK_INFO_GET_TASK_INDX		= 1,
	TASK_INFO_GET_TASK_NAME		= 2
} taskInfoTypes_t;

//*****************************************
//*****************************************
// These vars are primarily used for Timing Analysis.
extern uint32_t g_nStartTick;

/*
 * Functions
 */

void Resources_InitLowLevel( void );
void Resources_InitTasks( void );
void Resources_Reboot(bool toloader, const char *pMsg);
void Resources_PrintTaskInfo();
char* Resources_GetTaskName(uint8_t taskIndx);
uint8_t Resources_GetTaskIndex(TaskHandle_t handle);

#endif /* RESOURCES_H_ */


#ifdef __cplusplus
}
#endif