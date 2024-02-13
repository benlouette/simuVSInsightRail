#ifdef __cplusplus
extern "C" {
#endif


/*
 * Resources.c
 *
 *  Created on: 9 mei 2014
 *      Author: g100797
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "configFeatures.h"

#include "Resources.h"
#include "i2c.h"

#ifdef CONFIG_PLATFORM_CLI
#include "xTaskCLI.h"
#include "binaryCLI.h"
#include "binaryCLI_Task.h"
#endif

#ifdef CONFIG_PLATFORM_MODEM
#include "xTaskModem.h"
#endif

#ifdef CONFIG_PLATFORM_COMM
#include "TaskComm.h"
#endif

#ifdef CONFIG_PLATFORM_OTA
#include "xTaskAppOta.h"
#endif

#ifdef CONFIG_PLATFORM_MEASURE
#include "xTaskMeasure.h"
#endif

#ifdef CONFIG_PLATFORM_EVENT_LOG
#include "EventLog.h"
#endif

#include "xTaskApp.h"

#include "taskGnss.h"

#ifdef PLATFORM_IS25
#include "ExtFlash.h"
#endif


#include "xTaskAppGNSS.h"


#include "xTaskDevice.h"

#include "DataStore.h"
#include "boardSpecificCLI.h"
#include "configBootloader.h"
#include "configData.h"

#include "CS1.h"

#include "pin_mux.h"
#include "PinDefs.h"
#include "PowerControl.h"


#include "fsl_rtc_driver.h"
#include "rtc.h"


#include "NvmConfig.h"
#include "PinConfig.h"
#include "TestUART.h"
#include "PMIC_UART.h"
#include "fsl_sim_hal.h"
#include "fsl_edma_driver.h"
#include "AD7766_DMA.h"
#include "Device.h"
#include "hdc1050.h"
#include "DrvTmp431.h"
#include "ds137n.h"

#include "NvmData.h"
#include "NvmCalib.h"

#include "Vbat.h"
#include "json.h"
#include "EnergyMonitor.h"
#include "drv_is25.h"
#include "crc.h"
#include "linker.h"
#include "pmic.h"
#include "harvester.h"
#include "flash.h"
#include "Modem.h"

#define TASK_ARRAY_LEN		(sizeof(tasks) / sizeof(tasks[0]))
extern void checkLoaderGE_1_4(void);

/*================================================================================*
 |                              GLOBAL VARIABLE PROTOTYPES                        |
 *================================================================================*/
/*
 * simplistic task list, just the ones we are interested in, and currently only stack info is printed
 */
extern TaskHandle_t _TaskHandle_CLI;
extern TaskHandle_t _TaskHandle_Modem;
extern TaskHandle_t   _TaskHandle_Device;
extern TaskHandle_t _TaskHandle_APPLICATION;
extern TaskHandle_t   _TaskHandle_APPLICATION_OTA;
#ifdef CONFIG_PLATFORM_COMM
extern TaskHandle_t  sCommTaskHandle;
extern TaskHandle_t MQTT_Rx_TaskHandle;
#endif
extern TaskHandle_t _TaskHandle_Measure;
extern TaskHandle_t   _TaskHandle_Gnss;
extern TaskHandle_t   _TaskHandle_extFlash;
extern TaskHandle_t   _TaskHandle_PMIC;

extern BootCfg_t gBootCfg;

#ifdef SAMPLING_EXEC_TIME_EN
TickType_t g_nStopSamplingTick = 0;
TickType_t g_nStartSamplingTick = 0;
#endif
//..............................................................................
// Define sample output buffer. Allocated in linker file as the __sample_buffer
// symbol (rather than as a global array in C), to avoid zero-initialisation
// problems and excessive flash initialisation const usage? (ask George).
// The buffer size is defined by the SAMPLE_BUFFER_SIZE_BYTES and
// SAMPLE_BUFFER_SIZE_WORDS macros - see Resources.h.
//
// TODO: See if any way to set up this buffer as a simple global array without
// problems here in the C file instead.

// "extern" below causes linkage to __sample_buffer symbol defined in linker file
extern int32_t __sample_buffer[];
// Make it signed and give it a nicer name
int32_t *g_pSampleBuffer = (int32_t *)__sample_buffer;  // NOTE: Signed

// These vars are primarily used for Timing Analysis.
uint32_t g_nStartTick = 0;

//..............................................................................

static edma_state_t EdmaModuleState;

static const TaskHandle_t *tasks[] = {
		&_TaskHandle_CLI,
        &_TaskHandle_Modem,
        &_TaskHandle_Device,
#ifdef CONFIG_PLATFORM_COMM
        &sCommTaskHandle,
        &MQTT_Rx_TaskHandle,
#endif
        &_TaskHandle_APPLICATION,
        &_TaskHandle_APPLICATION_OTA,
        &_TaskHandle_Measure,
        &_TaskHandle_Gnss,
        &_TaskHandle_extFlash,
		&_TaskHandle_PMIC
};

/*================================================================================*
 |                          PRIVATE FUNCTION PROTOTYPES                           |
 *================================================================================*/


/*================================================================================*
 |                                 IMPLEMENTATION                                 |
 *================================================================================*/

/*
 * InitAppNVIC
 *
 * @brief Initialization application specific NVIC Priorities
 *
 */
static void InitAppNVIC( void )
{
    /*
     * Determine start-up condition
     */
    // TODO Determine start condition

#if 1 // TODO Decide if needed
    IRQn_Type irqNum;

    // Set all priorities to 8 by default
    for ( irqNum = 0; irqNum < NUMBER_OF_INT_VECTORS; irqNum++ ) {
        NVIC_SetPriority( irqNum, 8U );
    }
#endif

    // GPIO Priorities
    NVIC_SetPriority( PORTA_IRQn, 8U );
    NVIC_SetPriority( PORTB_IRQn, 8U );
    NVIC_SetPriority( PORTC_IRQn, 7U );         // Higher priority DCD pin
    NVIC_SetPriority( PORTD_IRQn, 8U );			// ACCEL-INT
    NVIC_SetPriority( PORTE_IRQn, 8U );			// AD7766 irq

    // DMA Priorities
    NVIC_SetPriority( DMA0_IRQn, 5U );          // ADC0/1 conversions to buffer
    NVIC_SetPriority( DMA1_IRQn, 6U );          // DAC self-test signal
    NVIC_SetPriority( DMA_Error_IRQn, 5U );

    // I2C
    NVIC_SetPriority( I2C0_IRQn, 8U );          // accellerometer ??
    NVIC_SetPriority( I2C2_IRQn, 8U );          // counter DS1372, Temperature sensor TMP431, power chip ??

    NVIC_SetPriority( SPI0_IRQn, 8U );          // External flash
    NVIC_SetPriority( SPI1_IRQn, 8U );          // AD7766

    // UART
    NVIC_SetPriority( UART0_RX_TX_IRQn, 8U );   //
    NVIC_SetPriority( UART0_ERR_IRQn, 7U );

    NVIC_SetPriority( UART1_RX_TX_IRQn, 8U );   // Modem
    NVIC_SetPriority( UART1_ERR_IRQn, 7U );

    NVIC_SetPriority( UART2_RX_TX_IRQn, 8U );   // GNSS
    NVIC_SetPriority( UART2_ERR_IRQn, 7U );

    NVIC_SetPriority( UART3_RX_TX_IRQn, 8U );   // CLI
    NVIC_SetPriority( UART3_ERR_IRQn, 7U );

    NVIC_SetPriority( UART4_RX_TX_IRQn, 8U );   // Test Uart
    NVIC_SetPriority( UART4_ERR_IRQn, 7U );

    NVIC_SetPriority( UART5_RX_TX_IRQn, 5U );   // PMIC Uart
    NVIC_SetPriority( UART5_ERR_IRQn, 6U );

    // ADC
    NVIC_SetPriority( ADC0_IRQn, 8U );          // Vibration ADC test interrupt

    // CMP0
    NVIC_SetPriority( CMP0_IRQn, 3U );          // RPM-PULSE comparator interrupt

    // RTC
    NVIC_SetPriority( RTC_IRQn, 8U );
    NVIC_SetPriority( RTC_Seconds_IRQn, 8U );

    // PIT
    NVIC_SetPriority( PIT0_IRQn, 3U );          // Counter for RPM calculations

    // PDB
    NVIC_SetPriority( PDB0_IRQn, 8U );          // PDB used for ADC sample clock/timing

    // MCG
    NVIC_SetPriority( MCG_IRQn, 8U );           // Loss of (PLL) lock interrupt

    // LPTMR
    NVIC_SetPriority( LPTMR0_IRQn, 3U );        // Used for RPM pulse counting
}

/*
 * InitAppEdma
 *
 * @desc    Initialises the EDMA driver and the required channel priorities
 *          and preemption modes etc
 *
 * @param   None
 *
 * @returns -
 */
void InitAppEdma(void)
{
    edma_user_config_t EdmaUserConfig;
    DMA_Type *pEdmaRegBase;

    EdmaUserConfig.chnArbitration = kEDMAChnArbitrationFixedPriority;
    EdmaUserConfig.notHaltOnError = false;
    EDMA_DRV_Init(&EdmaModuleState, &EdmaUserConfig);

    pEdmaRegBase = g_edmaBase[0];

    // Set up the channel priorities and preemption.
    // IMPORTANT NOTE: All channel priorities must be different

    // AD7766 EDMA channel preemption and priorities - this sampling stuff
    // MUST be the highest priority, and all other channels MUST allow
    // preemption, to ensure that sampling is never disrupted. Also, Rx must
    // be higher priority than Tx??
    // TODO: CHECK THIS, and also use the debugger to check that the actual
    // EDMA channel registers are set up correspondingly
    // TODO: Also compare to the SRB code
    // TODO: Convert the following to a const array to enforce separate priority for
    // each channel, and allow clearer specifying of the preemption stuff?
    // TODO: The EDMA_HAL_SetChannelPriority() calls below cause hanging (in
    // the default ISR) when the AD7766 channel numbers are set, if the
    // priorities do not equal the numerical channel numbers. This is
    // presumably due to EDMA configuration errors, because would have two
    // channels with the same priority (because the power-on defaults are
    // for each channel to have its priority set to its channel number).
    // Therefore, just use the channel numbers which correspond to the required
    // priorities for now.
    EDMA_HAL_SetChannelPreemptMode(pEdmaRegBase, EDMACHANNEL_AD7766_RX, false, true);
    //EDMA_HAL_SetChannelPriority(pEdmaRegBase, EDMACHANNEL_AD7766_RX, kEDMAChnPriority15);
    EDMA_HAL_SetChannelPreemptMode(pEdmaRegBase, EDMACHANNEL_AD7766_TX, false, true);
    //EDMA_HAL_SetChannelPriority(pEdmaRegBase, EDMACHANNEL_AD7766_TX, kEDMAChnPriority14);
}



/*
 * leftovers of the processor expert generated code, which I placed here
 * mainly port initialisations I do not want to change at the moment
 *
 */
static void pe_hardware_init(void)
{
  init_jtag_pins(JTAG_IDX);
  init_osc_pins(OSC_IDX);
  init_tpiu_pins(TPIU_IDX);
}


/*
 * Ports_initLowLevel
 *
 * @brief does the gpio port init, previous done by processor expert routines
 *
 */

static void Ports_initLowLevel()
{
    // all gpio pins now go using barts pinConfig routines
    // but the processor expert did also init the clock for the ports, lets now do it here
    // GPIO_DRV_Init(gpio1_InpConfig0,gpio1_OutConfig0);

    /* Enable clock for PORTs */
    SIM_HAL_EnableClock(SIM,kSimClockGatePortB);
    SIM_HAL_EnableClock(SIM,kSimClockGatePortC);
    SIM_HAL_EnableClock(SIM,kSimClockGatePortD);
    SIM_HAL_EnableClock(SIM,kSimClockGatePortE);
    SIM_HAL_EnableClock(SIM,kSimClockGatePortA);

    pe_hardware_init();

    if(!Device_HasPMIC())
    {
		// Latch the nPWR-ON signal, to hold power on. It may already be latched, but just to make sure
		pinConfigDigitalOut(nPWR_ON, kPortMuxAsGpio, 0, false);
		GPIO_DRV_ClearPinOutput(nPWR_ON);

		//Same pin used to drive harvester halt line on harvester variant
        pinConfigDigitalOut(TMR_POWER_OFFn, kPortMuxAsGpio, 1, false); // PTB0 is not named this on rev 1, but still has useful test point
    }
    else
    {
    	if(Device_IsHarvester())
    	{
        	pinConfigDigitalOut(HARVESTER_HALT, kPortMuxAsGpio, 0, false);
    	}
    	pinConfigDigitalOut(PMIC_UART_TX,   kPortMuxAlt3,   0,     false);// PTD8 controlled by uart
    	pinConfigDigitalIn(PMIC_UART_RX,    kPortMuxAlt3,   true, kPortPullUp,  kPortIntDisabled);// PTD9 controlled by uart
        // Rev 12?
        // REV 13
        if (Device_PMICcontrolGPS())
        {
            pinConfigDigitalIn(GNSS_ON,  kPortMuxAsGpio,   true,  kPortPullDown,    kPortIntDisabled);
            pinConfigDigitalIn(Device_GNSS_ResetPin(),  kPortMuxAsGpio,   true,  kPortPullUp,    kPortIntDisabled);
        }
    }

    // *********** TODO: Move many of the following individual pin
    // *********** configurations out to their corresponding individual
    // *********** modules if appropriate, as long as will be initialised
    // *********** shortly after firmware start-up

    // uart pin mux, was in PE, must move to driver component eventually
    pinConfigDigitalOut(CLI_UART_TX,   kPortMuxAlt3,   0,     false);// PTC17 controlled by uart
    pinConfigDigitalIn(CLI_UART_RX,    kPortMuxAlt3,   true, kPortPullUp,  kPortIntDisabled);// PTC16 controlled by uart
    pinConfigDigitalOut(TEST_UART_TX,   kPortMuxAlt3,   0,     false);// PTC15 controlled by uart
    pinConfigDigitalIn(TEST_UART_RX,    kPortMuxAlt3,   true, kPortPullUp,  kPortIntDisabled);// PTD14 controlled by uart

    if (Device_GetHardwareVersion() >= HW_PASSRAIL_REV3 || Device_HasPMIC())
    {
        // Rev2 or greater board-specific pin configurations
        pinConfigDigitalOut(CLI_UART_RTS, kPortMuxAlt3, 1, false);  // PTC18
        pinConfigDigitalIn(CLI_UART_CTS, kPortMuxAlt3, true, kPortPullUp, kPortIntDisabled);  // PTC19

        // +VDIG regulator: Enable it in non-power-saving mode for now
        pinConfigDigitalOut(PSU_ENABLE, kPortMuxAsGpio, 1, false);
        pinConfigDigitalOut(POWER_SAVEn, kPortMuxAsGpio, 1, false);

        if(Device_HasPMIC())
        {
        	// set Burst mode ON for best efficiency
        	pinConfigDigitalOut(WIRELESS_BURST, kPortMuxAsGpio, 1, false);
        }
    }
    else if (Device_GetHardwareVersion() == HW_PASSRAIL_REV1)
    {
        // Rev1-board-specific pin configurations
        // TODO: Also figure out DC_LEVEL - is on BOTH PTA13 and PTB7

        pinConfigDigitalOut(WATCHDOG_RESET, kPortMuxAsGpio, 0, false);
        pinConfigDigitalOut(FLASH_WPn, kPortMuxAsGpio, 1, true);
        pinConfigDigitalOut(SYSTEM_STATUS_LED2, kPortMuxAsGpio, 0, false);
        pinConfigDigitalOut(uP_POWER_TEST, kPortMuxAsGpio, 0, false);
        pinConfigDigitalIn(TEMP_ALERTn, kPortMuxAsGpio, true, kPortPullUp, kPortIntDisabled);
        pinConfigDigitalIn(ACCEL_INT2n, kPortMuxAsGpio, true, kPortPullUp, kPortIntDisabled);
    }
}

/*
 * Resources_InitLowLevel
 *
 * @brief Initialization of low level drivers and interrupt configuration
 *
 */
void Resources_InitLowLevel( void )
{
    // Initialise the hardware version info. IMPORTANT: Needs to be done as
    // early as practical after main(), and certainly before
    // GetHardwareVersion() is called for the first time.
    Device_InitHardwareVersion();

	memcpy((uint8_t*) &gBootCfg, (uint8_t*) __bootcfg, sizeof(BootCfg_t));

	// TODO check engineering jumper and if fitted (or signal is present), do CLI

	// check CRC of config
	uint32_t crc32 = crc32_hardware((uint8_t*)&gBootCfg.cfg, sizeof(gBootCfg.cfg));
	if(crc32 != gBootCfg.crc32)
	{
		memset(&gBootCfg, 0, sizeof(gBootCfg));
		gBootCfg.cfg.ImageInfoFromLoader.OTAmaxImageSize = 0x80000;
		gBootCfg.cfg.ImageInfoFromLoader.OTAstartAddrForApp = 0xF80000;
		gBootCfg.crc32 = crc32_hardware((uint8_t*)&gBootCfg.cfg, sizeof(gBootCfg.cfg));
	}

	// Load the OTA process management data from flash.
	OtaProcess_LoadFromFlash();

    /* Set Application NVIC Priorities */
    InitAppNVIC();

    // Initialise EDMA
    InitAppEdma();

    // Initialise the AD7766 driver - needs to be done here to ensure that
    // gets the required fixed EDMA channels - see its function comments
    // for more details
    AD7766_Init();
    Ports_initLowLevel();

    //check the loader is >= 1.4 .
    // if not flash the BOOTLOADER vectors to the APP copy
    // note this is a one time deal if you're using a JLINK
    // to program a new APP the vectors may no longer be valid
    // The APP can no longer be upgraded until the BOOTLOADER is >= 1.4
    checkLoaderGE_1_4();

    // Is this a good location to read the device config ?
    // for release2 probably yes, later replace with more generic datastore
    // shadow copy not yet used
    if (NvmConfigRead(&gNvmCfg) == false)
    {
    	// First time, or corrupted: initialise with defaults
    	NvmConfigDefaults(&gNvmCfg);
     	if(NvmConfigWrite(&gNvmCfg) && Device_HasPMIC())
     	{
     		PMIC_ScheduleConfigUpdate();
     	}
    }
    dbg_logging = gNvmCfg.dev.general.dbg_logging;

    // If the config is the default then we override RAT if LTE Modem (based on PCB HW version)
    // This will therefore configure RAT correctly on first power up
    if((gNvmCfg.dev.modem.radioAccessTechnology == eRAT_UMTS) && (Device_GetHardwareVersion() >= 10))
	{
		gNvmCfg.dev.modem.radioAccessTechnology = eRAT_UMTS_LTE;
		NvmConfigWrite(&gNvmCfg);
	}

    // calibration data has its own special area in flash, including routines for access etc.
    if (NvmCalibRead(&gNvmCalib) == false)
    {
        // First time, or corrupted: initialise with defaults
        NvmCalibDefaults(&gNvmCalib);
        NvmCalibWrite(&gNvmCalib);
    }

    // overwrite the config with the calib data
    gNvmCfg.dev.measureConf.Scaling_Bearing     = gNvmCalib.dat.scaling.bearingEnv3;
    gNvmCfg.dev.measureConf.Scaling_Wheel_Flat  = gNvmCalib.dat.scaling.wheelFlat;
    gNvmCfg.dev.measureConf.Scaling_Raw         = gNvmCalib.dat.scaling.raw;

    // this is here for now - see gNvmCfg above
    if(NvmDataRead(&gNvmData) == false)
    {
    	NvmDataDefaults(&gNvmData);
    	NvmDataWrite(&gNvmData);
    }

    // Initialise power control
    powerControlInit();

    // Initialise TEST_IO2 pin as digital output for testing
    pinConfigDigitalOut(TEST_IO2, kPortMuxAsGpio, 0, false);

    // Initialise test UART
    TestUARTInit();

    // Initialise PMIC UART
    if(Device_HasPMIC())
    {
    	PMIC_UART_Init();
    }

    // Turn ON the VREF.
    Device_SetVrefOn();
}

void Resources_InitTasks( void )
{

    // Update the RAM copy of the FW Version from the const. value.
     Firmware_Version = getFirmwareVersion();

    // Task specific initialization before tasks are started
	DataStore_Init();

	InitRtc(RTC_IDX);

#if 1
	i2c_init(I2C0_IDX, I2C0_SDA, I2C0_SCL);
	i2c_init(I2C2_IDX, I2C2_SDA, I2C2_SCL);

	// the init of the data structures of some i2c drivers.
	// they return a status, but at this point I do not know what to do if they fail.
	// if they fail, it is probably because of 'out of heap', and then the freeRTOS malloc will hang in an infinite loop...
	 hdc1050_init();
	 tmp431_init();
	 DS137n_init();

	 IS25_Init(IS25_TRANSFER_BAUDRATE, NULL);
#endif

#ifdef CONFIG_PLATFORM_EVENT_LOG
    EventLog_InitFlashData();
#endif


 #ifdef CONFIG_PLATFORM_CLI
     xTaskCLI_Init();
 #endif

     if(Device_HasPMIC())
     {
    	 PMIC_StartPMICtask();
     }

 #ifdef CONFIG_PLATFORM_MODEM
     xTaskModem_Init();
 #endif

#ifdef CONFIG_PLATFORM_COMM
     TaskComm_Init();
#endif

#ifdef CONFIG_PLATFORM_OTA
   	 xTaskAppOta_Init();
   	binaryCLI_InitCLI();
#endif

     // Board specific commands
     boardSpecificCliInit();

     if(Device_HasPMIC())
     {
    	 PMIC_InitCLI();
     }

     if(Device_IsHarvester())
     {
    	 Harvester_InitCLI();
     }

#ifdef CONFIG_PLATFORM_MEASURE
     // NOTE: AD7766_Init() is called separately before this
     xTaskMeasure_Init();
#endif

#ifdef PLATFORM_IS25
     taskExtFlash_Init();
#endif

#ifdef CONFIG_PLATFORM_GNSS
     taskGnss_Init(gNvmCfg.dev.general.gnssIgnoreSysMsg);
#endif

     Vbat_Init();

     // next one is supposed to be always needed
     xTaskApp_Init();

#if 0
     // test led task, very temporary
     void TaskLed_cliInit();
     void TaskLed_Init();
     TaskLed_Init();
     TaskLed_cliInit();
#endif

     xTaskDevice_Init();// this should become the first task !

	 // and as last, start the OS */

}


void Resources_Reboot(bool toloader, const char *pMsg)
{
	// here we do a a controlled reboot to start the application
	printf("Reboot within 1 second %s\n", pMsg);
	vTaskDelay(300 / portTICK_PERIOD_MS);

	// OK, shutdown is planned so clear flag
    Vbat_ClearFlag(VBATRF_FLAG_UNEXPECTED_LAST_SHUTDOWN);

	vPortStopTickTimer();
	INT_SYS_DisableIRQGlobal();

	// Disable all interrupts
	for(int i = 0; i < 8; i++)
	{
		NVIC->ICER[i] = NVIC->ICER[i];
	}

	// turn off all I/O clocks
	SIM_WR_SCGC1(SIM, 0);
	SIM_WR_SCGC2(SIM, 0);
	SIM_WR_SCGC3(SIM, 0);
	SIM_WR_SCGC4(SIM, 0xF0100030);
	SIM_WR_SCGC5(SIM, 0x00040182);
	SIM_WR_SCGC6(SIM, 0x40000001);
	SIM_WR_SCGC7(SIM, 0x00000006);

	uint32_t *start = (toloader) ? __loader_origin : __app_origin;

	// initialise stack pointers
	__set_PSP(start[0]);
	__set_MSP(start[0]);	// Initialise Loader's Stack Pointer
    __ISB();
    __DSB();
    ((void(*)(void))start[1])();
}

/*
 * Resources_PrintTaskInfo
 *
 * @brief	Prints task stack usage
 *
 * @return  void
 */
void Resources_PrintTaskInfo()
{
	printf("Remaining Heap bytes: %d\n",xPortGetFreeHeapSize());
	printf("Task name : remaining (%d bytes sized) stack entries\n", sizeof(StackType_t ));
	for(uint8_t i = 0; i < TASK_ARRAY_LEN; i++)
	{
		char * p;
		UBaseType_t stackhigh;

		// don't display information for tasks which haven't been created/started
		if(NULL == *tasks[i])
		{
			continue;
		}

		// display task name and stack high watermark
		p = pcTaskGetTaskName(*tasks[i]);
		stackhigh = uxTaskGetStackHighWaterMark(*tasks[i]);
		if (p)
		{
			printf(" %s : %d\n", p, stackhigh );
		}
	}
}

/*
 * Resources_GetTaskName
 *
 * @brief	Gets task name from the task index.

 * @param	taskIndx - task index to facilitate fetching the corresponding
 * 			task name, if found valid.
 *
 * @return  taskname if a matching task is found, else "Unknown".
 */
char* Resources_GetTaskName(uint8_t taskIndx)
{
	char *pTaskName = "Unknown";
	if((taskIndx < TASK_ARRAY_LEN) && (*tasks[taskIndx]))
	{
		pTaskName = pcTaskGetTaskName(*tasks[taskIndx]);
	}
	return pTaskName;
}

/*
 * Resources_GetTaskIndex
 *
 * @brief	Gets task index from the *tasks[] based on the task handle.
 *
 * @param	handle - Task handle.
 *
 * @return  task index, 0xFF returned when no matching task is found.
 */
uint8_t Resources_GetTaskIndex(TaskHandle_t handle)
{
	for(uint8_t i = 0; i < TASK_ARRAY_LEN; i++)
	{
		if(handle == *tasks[i])
		{
			return i;
		}
	}
	return 0xFF;
}



#ifdef __cplusplus
}
#endif