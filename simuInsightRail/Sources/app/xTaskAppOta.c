#ifdef __cplusplus
extern "C" {
#endif

/*
 * xTaskAppOta.c
 *
 *  Created on: Oct 12, 2016
 *      Author: B. Peters
 *
 *      Task that orchestrates Over The Air (OTA) Firmware Update
 */

/*
 * Includes
 */
#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <stdbool.h>
#include <stdio.h>

#include "Log.h"

#include "dataDef.h"
#include "Device.h"
#include "xTaskAppOta.h"
#include "xTaskDefs.h"
#include "xTaskDevice.h"
#include "xTaskDeviceEvent.h"
#include "TaskCommEvent.h"
#include "configData.h"

#include "configBootloader.h"

#include "SvcMqttFirmware.h"
#include "xTaskAppCommsTest.h"
#include "Vbat.h"
#include "drv_is25.h"
#include "imageTypes.h"
#include "image.h"
#include "NvmConfig.h"
#include "NvmData.h"
#include "EnergyMonitor.h"
#include "crc.h"
#include "linker.h"
#include "flash.h"
#include "resources.h"
#include "CLIcmd.h"
#include "ExtFlash.h"
#include "pmic.h"
#include "binaryCLI.h"
#include "TaskComm.h"
#include "configMQTT.h"

#define MAX_NUM_OF_BLOCK_REQ_RETRIES			(3)
#define RECEPTION_TIMEOUT_IN_MS					(30000)

#define BLOCK_SIZE 								(0x8000)

#define EVENTQUEUE_NR_ELEMENTS_APP_OTA  		(4)     	//! Event Queue can contain this number of elements

#define LOG_EVENTCODE_OTA_BASE					(10)
#define LOG_EVENTCODE_OTA_UNKNOWN_EVENT			(20)
#define LOG_EVENTCODE_OTA_RETRIES 				(21)
#define LOG_EVENTCODE_OTA_UPDATE_DONE			(30)
#define LOG_EVENTCODE_OTA_MGMNT_CRC_FAIL		(31)
#define LOG_EVENTCODE_OTA_MGMNT_WRITE_FAIL		(32)
#define LOG_EVENTCODE_OTA_TERMINATED			(33)
#define LOG_EVENTCODE_OTA_DISCARDS				(34)

#define DIV_CEIL(dividend, divisor)		((dividend + (divisor - 1))/divisor)
#define ABORT_TIMEOUT	2000		// make it 2 seconds

typedef enum {
    OTAFIRMWAREEXCEP_NONE = 0,
	OTAFIRMWAREEXCEP_LOST_CONNECTION,
	OTAFIRMWAREEXCEP_CRC_FAILED,
	OTAFIRMWAREEXCEP_FLASH_WRITE_FAILED,
	OTAFIRMWAREEXCEP_FLASH_ERASE_FAILED,
	OTAFIRMWAREEXCEP_BOOTCFG_FLASH_FAILED,
	OTAFIRMWAREEXCEP_INVALID_FLASH_ERASE_SIZE,
	OTAFIRMWAREEXCEP_NO_BLOCK_RECEIVED,
	OTAFIRMWAREEXCEP_CANNOT_SETUP_CONNECTION,
	OTAFIRMWAREEXCEP_MISSING_LOADER,
	OTAFIRMWAREEXCEP_ABORTED,
	OTAFIRMWAREEXCEP_UNKNOWN_TYPE,
} OtaFirmwareException_t;

typedef struct {
	OtaFirmwareException_t const eventType;
	char const *debugType;
	char const *debugMsg;
} debugInfo_t;

const debugInfo_t debugInfo[] =
{
	{ ERRLOGFATAL, NULL, NULL },
	{ ERRLOGWARN,	"OTAFIRMWAREEXCEP_LOST_CONNECTION",				"Lost connection" },
	{ ERRLOGFATAL,	"OTAFIRMWAREEXCEP_CRC_FAILED",					"CRC Failed" },
	{ ERRLOGFATAL,	"OTAFIRMWAREEXCEP_FLASH_WRITE_FAILED",			"Flash Write FAILED" },
	{ ERRLOGFATAL,	"OTAFIRMWAREEXCEP_FLASH_ERASE_FAILED",			"Flash Erase FAILED" },
	{ ERRLOGFATAL,	"OTAFIRMWAREEXCEP_BOOTCFG_FLASH_FAILED",		"bootcfg write failed" },
	{ ERRLOGFATAL,	"OTAFIRMWAREEXCEP_INVALID_FLASH_ERASE_SIZE",	"Flash Erase Size is 0" },
	{ ERRLOGWARN,	"OTAFIRMWAREEXCEP_NO_BLOCK_RECEIVED", 			"Block not received" },
	{ ERRLOGWARN,	"OTAFIRMWAREEXCEP_CANNOT_SETUP_CONNECTION",		"Could not setup connection" },
	{ ERRLOGWARN,	"OTAFIRMWAREEXCEP_MISSING_LOADER", 				"Boot loader missing" },
	{ ERRLOGWARN,	"OTAFIRMWAREEXCEP_ABORTED",						"OTA terminated" },
	{ ERRLOGWARN,	"OTAFIRMWAREEXCEP_UNKNOWN", 					"Unknown exception" },
};

// Ota Process management data structure,
// NVM copy resident at __ota_mgmnt_data[] location.
typedef struct OtaMgmnt_s
{
    uint32_t crc32;
    struct FwOtaProcessData_s
    {
    	uint32_t imageSize_Bytes;		// Size of the image, set by Firmware Update Notification.
    	uint32_t newFirmwareVersion;	// New firmware version of the upgrade
    	uint32_t blocksWrittenInFlash;	// Block that are written in Flash
    	uint32_t retryCount;			// Number of OTA retries remaining.
    }FwOtaProcessData;
    // Structure for OTA failure simulation.
    struct OtaTestData_s
    {
    	bool bOTATestEn;				// Flag for controlling the simulation of OTA Test failures
    									// When set, enables the OTA Test flags / vars.
    	uint8_t exeOtaRetries;			// The count indicates how many retries to exercise.
    }OtaTestFailSimulation;

} OtaMgmnt_t;

extern BootCfg_t gBootCfg;							//! To access boot config information

extern uint8_t *pImageBuf;							//! Used for storing blocks coming from mqtt

extern tNvmData gNvmData;

extern bool checkLoaderGE_1_4(void);

static QueueHandle_t      EventQueue_App_Ota; 		//! Main queue for the task
static SemaphoreHandle_t otaSignal = NULL;

TaskHandle_t   _TaskHandle_APPLICATION_OTA;			// not static because of easy print of task info in the CLI

static int m_nNotifyTaskTimeout_ms = 180000;
static bool m_bIsNewBlockRecvd = false;

// OTA process management data RAM copy.
static OtaMgmnt_t gOtaMgmnt;

uint32_t handleFirmwareBlockReply_cb(void * buf);

// Publish image information
void publishManifest(ImageType_t imageType);

static bool otaFirmwareProcedure();

//===================== CLI Functions / Helpers ==============================//
static bool OTATest_ContinueOTA(uint32_t numOfBlocksWritten, uint32_t totalBlocks);
static bool cliOTADropConnection( uint32_t args, uint8_t * argv[], uint32_t * argi);
static bool cliOTAPrintProcessDataInfo( uint32_t args, uint8_t * argv[], uint32_t * argi);
static bool cliOTATestEnDisable( uint32_t args, uint8_t * argv[], uint32_t * argi);
static bool cliOTATestSet( uint32_t args, uint8_t * argv[], uint32_t * argi);
//============================================================================//

static uint32_t m_blocksDiscarded = 0;
static uint32_t m_blocksRetried = 0;

// INFO Note That for now the protobuf / nanopb requires a considerable amount of
// stack from the task that it uses. Maybe later on this can be redesigned that
// a separate task can handle the full protobuf / nanopb, so that the calling
// tasks do not need a large stacksize

/**
 * xTaskAppOta_Init
 *
 * @brief Task initialization Ota, creates queue and task
 */
void xTaskAppOta_Init()
{
	xTaskCreate( xTaskAppOta,              			// Task function name
	                 "OTA",             			// Task name string
					 STACKSIZE_XTASK_OTA , 			// Allocated stack size on FreeRTOS heap
	                 NULL,                   		// (void*)pvParams
					 PRIORITY_XTASK_OTA,   			// Task priority
	                 &_TaskHandle_APPLICATION_OTA );// Task handle
}

/**
 * xTaskAppOta
 *
 * @brief Main task entry for Ota, handling incoming queue events
 *
 * @param pvParameters unused
 *
 */
void xTaskAppOta( void *pvParameters )
{
	SvcFirmware_SetMaxFwOtaBlockSize(SvcFirmware_GetMqttMaxFwOtaBlockSize());
	otaSetNotifyTaskDefaultTimeout();

	// Create event queue
    EventQueue_App_Ota = xQueueCreate( EVENTQUEUE_NR_ELEMENTS_APP_OTA, sizeof(tBlockReplyInfo));
    vQueueAddToRegistry(EventQueue_App_Ota, "_EVTQ_APP_OTA");
    otaSignal = xSemaphoreCreateBinary();

    // main task loop
	while (1)
	{
		// night, night
		vTaskSuspend(NULL);

		// clean out the queue
		tBlockReplyInfo blockFlush;
		while(pdTRUE == xQueueReceive(EventQueue_App_Ota, &blockFlush, 100));

		// clear the counts
		m_blocksDiscarded = m_blocksRetried = 0;

		// Execute OTA
		LOG_DBG(LOG_LEVEL_APP,"FW Upgrade Started\n");
		otaFirmwareProcedure();

		// signal to the APP
		xSemaphoreGive(otaSignal);
	}
}

void otaRebootRequired(void)
{
	if(gBootCfg.cfg.FromApp.NewAppAddress > 0)
	{
	    // store that we did do a planned shutdown for detecting crashes next boot
	    Vbat_ClearFlag(VBATRF_FLAG_UNEXPECTED_LAST_SHUTDOWN);

	    // If the NVM Data has changed update the NVM with the RAM copy.
	    NvmDataUpdateIfChanged();

	    // If the CFG data has changed, update the NVM with the RAM copy.
	    NvmConfigUpdateIfChanged(false);

	    // here we do a a controlled reboot to start the application
	    Resources_Reboot(true, "App OTA Complete");
	}
}

//------------------------------------------------------------------------------
/// Set default timeout for OTA notification task.
///
/// @return void
//------------------------------------------------------------------------------
void otaSetNotifyTaskDefaultTimeout()
{
	 m_nNotifyTaskTimeout_ms = 180000;
}

//------------------------------------------------------------------------------
/// Set timeout for OTA notification task.
///
/// @param  nTimeout_ms (int) -- Timeout in milliseconds.
///
/// @return void
//------------------------------------------------------------------------------
void otaSetNotifyTaskTimeout(int nTimeout_ms)
{
	m_nNotifyTaskTimeout_ms = nTimeout_ms;
}

/**
 * otaNotifyStartTask
 *
 * @brief Interface to Notify the OTA task to start
 *
 * @param startTick - the time that the sensor started the upload
 */
void otaNotifyStartTask(uint32_t startTick)
{
	const int kExtendThreshold_s = 60;
	const int kAbandonOTA_ms = 6*60*1000; // bail out at a total of 6 minutes in case we are going to program Loader

	uint32_t start = xTaskGetTickCount();
	m_bIsNewBlockRecvd = false;
	xSemaphoreTake(otaSignal, 0);	// clean the semaphore

	// Currently no different behaviour based on the cause of wakeup
	vTaskResume(_TaskHandle_APPLICATION_OTA);

	//Wait for task to complete waking up in 1sec increments to check
	//if OTA activity is ongoing (block reply received)
	uint32_t nDownCnt_s = ((m_nNotifyTaskTimeout_ms - 1) / 1000) + 1;
	while(nDownCnt_s != 0)
	{
		// is it time to abandon the OTA
		if((xTaskGetTickCount() >= (kAbandonOTA_ms + startTick)) && (SvcFirmware_GetCurrentTransportPipe() != eTRANSPORT_PIPE_CLI))
		{
			nDownCnt_s = 0;
			break;
		}

		// check if the OTA is finished
		if(xSemaphoreTake(otaSignal, 1000) == pdTRUE)
		{
			//We're done
			break;
		}

		// as long as we are getting data slide the window to limit
		nDownCnt_s--;
		if(m_bIsNewBlockRecvd)
		{
			m_bIsNewBlockRecvd = false;

			// While there's block reply activity in the last minute, extend the timeout by one minute
			if(nDownCnt_s < kExtendThreshold_s)
			{
				nDownCnt_s = kExtendThreshold_s;
			}
		}
	}

	if(nDownCnt_s == 0)
	{
		// this mechanism could be used by the back end to terminate an OTA
		// ideally the backend should set hwVersion or fwVersion to say 1
		// and the OTA should be cleared, same as running out of retries
		tBlockReplyInfo blockReply = {
			.hwVersion = 0,
			.fwVersion = 0,
			.byteIndex = (uint32_t)-1,
			.size = (uint32_t)-1,
		};

		// send an abort message
		handleFirmwareBlockReply_cb(&blockReply);

		// wait on the OTA suspending
		xSemaphoreTake(otaSignal, ABORT_TIMEOUT);

		LOG_EVENT(LOG_EVENTCODE_OTA_TERMINATED, LOG_NUM_OTA, ERRLOGMAJOR, "%s(), OTA process terminated after %d secs",
				__func__, (xTaskGetTickCount() - start) / 1000);
	}
}

/**
 * handleFirmwareBlockReply_cb
 *
 * @brief Call back for handling the Firmware Block Reply, generate event
 *
 * @param buf pointer to a struct with block reply info
 *
 * @return always returns 0
 */
uint32_t handleFirmwareBlockReply_cb(void * buf)
{
	LOG_DBG( LOG_LEVEL_CLI, "%s(%08x) called\n", __func__, buf);

	// Send message to queue
	if (pdTRUE != xQueueSend(EventQueue_App_Ota, buf, 0))
	{
		LOG_DBG(LOG_LEVEL_CLI,"%s: xQueueSend failed\n", __func__);
	} else {
		m_bIsNewBlockRecvd = true;
	}

	return 0;
}

/**
 * OtaProcess_ResetVars
 *
 * @brief Rests the OTA process variables.
 *
 * @return void
 */
void OtaProcess_ResetVars()
{
	// Reset the OTA process management data vars.
	gOtaMgmnt.FwOtaProcessData.imageSize_Bytes = 0;
	gOtaMgmnt.FwOtaProcessData.blocksWrittenInFlash = 0;
	gOtaMgmnt.FwOtaProcessData.newFirmwareVersion = 0;
	gOtaMgmnt.FwOtaProcessData.retryCount = 0;
	// Reset the OTA Test flags.
	gOtaMgmnt.OtaTestFailSimulation.bOTATestEn = false;
}

/**
 * HandleException
 *
 * @brief Procedure to handle exception of Ota Process
 *
 * @param exceptionOta exception type encountered during ota process
 *
 * @return true if procedure was successful, return false if erasing failed
 */
static void handleException(OtaFirmwareException_t exceptionOta)
{
	// Check for Exceptions
	if(exceptionOta != OTAFIRMWAREEXCEP_NONE)
	{
		// Exception occurred
		const debugInfo_t *p = ((exceptionOta >= OTAFIRMWAREEXCEP_UNKNOWN_TYPE) ?
							&debugInfo[OTAFIRMWAREEXCEP_UNKNOWN_TYPE] : &debugInfo[exceptionOta]);

		LOG_DBG(LOG_LEVEL_CLI, "OTA Failed !!!, Exception(%s) - %s\n", p->debugType, p->debugMsg);
		LOG_EVENT(LOG_EVENTCODE_OTA_BASE + exceptionOta, LOG_NUM_OTA, p->eventType, "%s; OTA Retries left = %d", p->debugMsg, gOtaMgmnt.FwOtaProcessData.retryCount);

		// Check if we are allowed to retry again
		if(gOtaMgmnt.FwOtaProcessData.retryCount == 0)
		{
			LOG_EVENT(LOG_EVENTCODE_OTA_RETRIES, LOG_NUM_OTA, ERRLOGWARN, "Exceeded num of retries");

			// Reset the OTA process management data.
			OtaProcess_ResetVars();

			// TODO What to do if image is not correct??
			// + Start over again
			// + Report and wait for another Update Notification
		}
	}
}


/**
 * jsonExtractImageInformation
 *
 * @brief scans image to extract image type
 * @param meta_data - metadata string
 *
 * @return image type found
 */

#include "json.h"
#include "image.h"

// TODO move to OTA Unit Test file
static const char app_test_data[] = {
	"{"
	"\"Major\":1,"
	"\"Minor\":1,"
	"\"CommitDate\":\"2018-01-30\","
	"\"imageType\":\"MK24_Application\","
	"\"imageSize\":1234"
	"}"
};

static const char loader_test_data[] = {
	"{"
	"\"Major\":1,"
	"\"Minor\":1,"
	"\"CommitDate\":\"2018-01-30\","
	"\"imageType\":\"MK24_Loader\","
	"\"imageSize\":1234"
	"}"
};

static const char loader_test_data_lf[] = {
	"{\n"
	"\"Major\":1,\n"
	"\"Minor\":1,\n"
	"\"CommitDate\":\"2018-01-30\",\n"
	"\"imageType\":\"MK24_Loader\",\n"
	"\"imageSize\":1234\n"
	"}\n"
};

static const char loader_test_data_misc[] = {
	"{\t\n"
	"\t\t\t   \"Major\"\t:1,\t\n"
	"\"Minor\" : 1,  \t\r\n"
	"\"CommitDate\"\t : \"2018-01-30\",\n"
	"\"imageType\"\t    :\t \"MK24_Loader\",\n"
	"\"imageSize\"\t   : \t   1234\n"
	"}\n"
};

#include "../CUnit/CUnit.h"

// function for unit testing
void ut_json()
{
	uint32_t value;
	ImageType_t eImageType;
	uint32_t nImageSize;

	// use the sample buffer as test area
	char* meta_data = (char*)(__sample_buffer + 2*(uint32_t)__app_version_size);

	// make image invalid
	memset(meta_data, 0, (uint32_t)__app_version_size);
	CU_ASSERT(!image_jsonExtractImageInformation(meta_data, &eImageType, &nImageSize));
	CU_ASSERT(IMAGE_TYPE_UNKNOWN == eImageType);

	// make image an app
	memcpy(meta_data, app_test_data, strlen(app_test_data));
	CU_ASSERT(image_jsonExtractImageInformation(meta_data, &eImageType, &nImageSize));
	CU_ASSERT(IMAGE_TYPE_APPLICATION == eImageType);

	// make image a loader
	memset(meta_data, 0, (uint32_t)__app_version_size);
	memcpy(meta_data, loader_test_data, strlen(loader_test_data));
	CU_ASSERT(image_jsonExtractImageInformation(meta_data, &eImageType, &nImageSize));
	CU_ASSERT(IMAGE_TYPE_LOADER == eImageType);

	// let's try again but this time corrupt the data in all sorts of ways
	const char patterns[] = { 0, 1, '\n', '\r', '\"', '{', '}', '2', 'A', 'a', 0x55, 0xaa, 0xff };
	const char filler[] = { 0, 1, '\n', '\r', '2', 'A', 'a', 0x1F, 0x7F, 0xFF };

	for(int k = 0; k < sizeof(filler); k++)
	{
		for(int j = 0; j < sizeof(patterns); j++)
		{
			for(int i = 0; i <= strlen(loader_test_data); i++)
			{
				// make image a loader and corrupt it
				memset(meta_data, filler[k], (uint32_t)__app_version_size);
				memcpy(meta_data, loader_test_data, strlen(loader_test_data));
				meta_data[i] = patterns[j];
				CU_ASSERT(JSON_EOF == jsonFetch(meta_data, "imageSize1", &value, true));
			}
		}
	}

	// now lets test with a buffer full of x
	for(int i = 0; i < 256; i++)
	{
		memset(meta_data, i, (uint32_t)__app_version_size);
		CU_ASSERT(JSON_EOF == jsonFetch(meta_data, "imageSize1", &value, true));
	}

	// process metadata with line feeds
	value = 0;
	strcpy(meta_data, loader_test_data_lf);
	CU_ASSERT(JSON_value == jsonFetch(meta_data, "imageSize", &value, true));
	CU_ASSERT(value == 1234);

	// process metadata with line feeds, spaces, tabs carriage returns
	value = 0;
	strcpy(meta_data, loader_test_data_misc);
	CU_ASSERT(JSON_value == jsonFetch(meta_data, "imageSize", &value, true));
	CU_ASSERT(value == 1234);
}


/**
 * otaFirmwareProcedure
 *
 * @brief procedure to conduct over the air firmware upgrade
 *
 * @return
 *   true       otaFirmwareProcedure succeeded
 *   false		otaFirmwareProcedure failed
 */
static bool otaFirmwareProcedure()
{
	bool rc_ok= true;
	uint32_t resultCode = 0;

	uint8_t  numOfBlockReqRetries = 0;
	uint32_t totalNumOfBlocks = 0;
	uint32_t remainingNumOfBytes = 0;
	uint8_t extFlashId = 0;
	// If the sensor is a harvester sensor and contains a bigger flash, then use the
	// OTA scratch pad area as per new memory map
	if(Device_HasPMIC() && IS25_ReadProductIdentity(&extFlashId))
	{
		if(IS25_512_MBIT_PRODUCT_ID == extFlashId)
		{
			gBootCfg.cfg.ImageInfoFromLoader.OTAstartAddrForApp = PACKAGE_OTA_SCRATCH_PAD_ADDR;
		}
	}
    uint32_t addr = gBootCfg.cfg.ImageInfoFromLoader.OTAstartAddrForApp;

	OtaFirmwareException_t exceptionOta = OTAFIRMWAREEXCEP_NONE;

	// Initialise local variables, loaded or calculated values from Flash.
	// Decide if it needs to continue with a download process
	if(gOtaMgmnt.FwOtaProcessData.blocksWrittenInFlash != 0)
	{
		LOG_DBG( LOG_LEVEL_CLI,  "Firmware upgrade continue\n");
		remainingNumOfBytes = gOtaMgmnt.FwOtaProcessData.imageSize_Bytes -
				(gOtaMgmnt.FwOtaProcessData.blocksWrittenInFlash * SvcFirmware_GetMaxFwOtaBlockSize());

		// Post decrement retry count of OTA
		gOtaMgmnt.FwOtaProcessData.retryCount--;
	}
	else
	{
		if(gBootCfg.cfg.ImageInfoFromLoader.OTAmaxImageSize > 0)
		{
			rc_ok = IS25_PerformSectorErase(addr, gBootCfg.cfg.ImageInfoFromLoader.OTAmaxImageSize);
			if(rc_ok == false)
			{
				exceptionOta =  OTAFIRMWAREEXCEP_FLASH_ERASE_FAILED;
			}
			else
			{
				remainingNumOfBytes = gOtaMgmnt.FwOtaProcessData.imageSize_Bytes;
				// Set retry count for OTA to defined value, for post decrement
				// DESIGN DECISION: pre decrement is implemented to be able to register
				// retries if procedure malfunctions and in e.g. gets a watchdog timeout
				if((gOtaMgmnt.FwOtaProcessData.retryCount > 0) && (gOtaMgmnt.FwOtaProcessData.retryCount <= MAX_NUM_OF_OTA_RETRIES))
				{
					// This case happens when crc failed of the new image during OTA and
					// gOtaMgmnt.FwOtaProcessData.blocksWrittenInFlash was set to zero, then we should decrement else
					// an infinite loop of crc errors can occur.
					gOtaMgmnt.FwOtaProcessData.retryCount--;
				}
				else
				{
					gOtaMgmnt.FwOtaProcessData.retryCount = MAX_NUM_OF_OTA_RETRIES;
				}
			}
		}
		else
		{
			rc_ok = false;
			exceptionOta =  OTAFIRMWAREEXCEP_INVALID_FLASH_ERASE_SIZE;
		}
	}

	//  Total number of blocks that need to be written into flash: totalNumOfBlocks
	const uint32_t nMaxFwOtaBlockSize = SvcFirmware_GetMaxFwOtaBlockSize();
	totalNumOfBlocks = DIV_CEIL(gOtaMgmnt.FwOtaProcessData.imageSize_Bytes, nMaxFwOtaBlockSize);

	LOG_DBG( LOG_LEVEL_CLI,  "Firmware size: %d, remaining %d, blocks %d, remaining blocks %d of %d bytes\n",
			gOtaMgmnt.FwOtaProcessData.imageSize_Bytes, remainingNumOfBytes, totalNumOfBlocks,
			(totalNumOfBlocks-gOtaMgmnt.FwOtaProcessData.blocksWrittenInFlash), nMaxFwOtaBlockSize);

	if(rc_ok)
	{
		m_blocksDiscarded = 0;
		m_blocksRetried = 0;
		LOG_DBG( LOG_LEVEL_CLI,  "Firmware prepare block exchange\n");

		// Loop to Request Blocks and write them to Flash
		uint32_t i = gOtaMgmnt.FwOtaProcessData.blocksWrittenInFlash;
		while(i < totalNumOfBlocks)
		{
			LOG_DBG(LOG_LEVEL_CLI,  "\nRequest Block: %d\n", i + 1);
			// Calculate the start byte of the block that needs to be requested
			uint32_t byteIndex = gOtaMgmnt.FwOtaProcessData.imageSize_Bytes - remainingNumOfBytes;

			uint32_t requestedNumOfBytes = (remainingNumOfBytes >= nMaxFwOtaBlockSize) ? nMaxFwOtaBlockSize : remainingNumOfBytes;

			// BlockRequest
			// TODO REMOVE WORKAROUND BYTE_INDEX + 1, DEFECT 14344 registered @ On Time
			resultCode = ISvcFirmware_Block_Request((byteIndex + 1),requestedNumOfBytes, gOtaMgmnt.FwOtaProcessData.newFirmwareVersion);
			rc_ok = (ISVCFIRMWARERC_OK == resultCode);

			// If the OTA Test is enabled.
			// **** This is ONLY USED for Simulating a Connection Drop, for OTA testing."
			if((rc_ok == true) && (gOtaMgmnt.OtaTestFailSimulation.bOTATestEn == true))
			{
				rc_ok = OTATest_ContinueOTA(gOtaMgmnt.FwOtaProcessData.blocksWrittenInFlash, totalNumOfBlocks);
			}

			if (rc_ok==false)
			{
				LOG_DBG( LOG_LEVEL_CLI, "\nISvcFirmware_Block_Request not OK, result code: %d\n", resultCode);
				// Break out of firmware update loop since we lost connection
				exceptionOta = OTAFIRMWAREEXCEP_LOST_CONNECTION;
				break;
			}

			bool blockDiscarded = false;
			do
			{
				blockDiscarded = false;
				//Check for incoming messages [FirmwareBlock]
				tBlockReplyInfo blockReply;					//! Used for copying the info of the firmware block
				if (xQueueReceive( EventQueue_App_Ota, &blockReply, RECEPTION_TIMEOUT_IN_MS/portTICK_PERIOD_MS ))
				{
					LOG_DBG( LOG_LEVEL_CLI, "Chk Set to True %d vs %d\n",blockReply.byteIndex,byteIndex);

					// check for an abort message
					if((-1 == (int)blockReply.byteIndex) && (-1 == (int)blockReply.size))
					{
						exceptionOta = OTAFIRMWAREEXCEP_ABORTED;
						rc_ok = false;
						break;
					}

					//Received FirmwareBlock, check if this is the right block
					if(blockReply.byteIndex == byteIndex && blockReply.size == requestedNumOfBytes)
					{
						// Reset block retries if a valid block is received
						m_blocksRetried += numOfBlockReqRetries;
						numOfBlockReqRetries = 0;

						rc_ok = IS25_WriteBytes(addr + byteIndex, pImageBuf, requestedNumOfBytes);
						// Verify if writing went OK
						if(rc_ok)
						{
							remainingNumOfBytes-= requestedNumOfBytes;
							gOtaMgmnt.FwOtaProcessData.blocksWrittenInFlash++;
							LOG_DBG(LOG_LEVEL_CLI,  "Block %d written of %d\n",
									gOtaMgmnt.FwOtaProcessData.blocksWrittenInFlash,
									totalNumOfBlocks);

						}
						else
						{
							LOG_DBG(LOG_LEVEL_CLI,  "Flash Write Failed Block %d\n", gOtaMgmnt.FwOtaProcessData.blocksWrittenInFlash);
							exceptionOta = OTAFIRMWAREEXCEP_FLASH_WRITE_FAILED;
							break;
						}
						i++;
					}
					else
					{
						if(blockReply.byteIndex == (byteIndex - blockReply.size))
						{
							// reply for previous block so lets discard it
							LOG_DBG(LOG_LEVEL_CLI,  "Block discarded\n\n");
							m_blocksDiscarded++;
							blockDiscarded = true;
						}
						else
						{
							// not sure what block it is, could be a malformed block
							numOfBlockReqRetries++;
							LOG_DBG(LOG_LEVEL_CLI,
									"Incorrect block (req->got) byte_index %d -> %d, size %d -> %d\n",
									byteIndex, blockReply.byteIndex,
									requestedNumOfBytes, blockReply.size);
						}
					}
				}
				else
				{
					// Retry mechanism if connection is not lost but the
					// requested block is not received
					LOG_DBG(LOG_LEVEL_CLI,  "No Block received\n");
					numOfBlockReqRetries++;
				}
			} while(blockDiscarded);

			//  in case the flash write fails
			if(exceptionOta != OTAFIRMWAREEXCEP_NONE)
			{
				break;
			}

			if(numOfBlockReqRetries >= MAX_NUM_OF_BLOCK_REQ_RETRIES)
			{
				exceptionOta = OTAFIRMWAREEXCEP_NO_BLOCK_RECEIVED;
				m_blocksRetried += numOfBlockReqRetries;
				rc_ok = false;
				break;
			}
			else
			{
				// Since we want to request the same block do not increment i
			}
		}

		// did we discard any blocks due to retry issues?
		if(m_blocksDiscarded || m_blocksRetried)
		{
			LOG_EVENT(LOG_EVENTCODE_OTA_DISCARDS, LOG_NUM_OTA, ERRLOGINFO, "Blocks discarded=%d; retried=%d",
					m_blocksDiscarded, m_blocksRetried);
		}
	}

	// Verify the CRC if the Image if everything else went OK
	if(rc_ok)
	{
		// Image complete check CRC
		if(image_IsExtImageAtAddrValid(gBootCfg.cfg.ImageInfoFromLoader.OTAstartAddrForApp))
		{
			LOG_DBG(LOG_LEVEL_CLI, "APP Addr:0x%x CRC OK\n", gBootCfg.cfg.ImageInfoFromLoader.OTAstartAddrForApp);
		}
		else
		{
			exceptionOta = OTAFIRMWAREEXCEP_CRC_FAILED;

			// Start update all over again if the amount of retries permit that.
			gOtaMgmnt.FwOtaProcessData.blocksWrittenInFlash = 0;
		}
	}
	uint32_t saveNewFirmwareVersion = gOtaMgmnt.FwOtaProcessData.newFirmwareVersion;

	//Procedure to handle exceptions
	handleException(exceptionOta);

	// signal that we're done
	if(exceptionOta != OTAFIRMWAREEXCEP_NONE)
	{
		rc_ok = false;
	}
	else
	{
		const char *p = "";
		ImageType_t imageType = image_ExtractImageType(gBootCfg.cfg.ImageInfoFromLoader.OTAstartAddrForApp);

		// scan meta-data to decide image type that we have received
		switch(imageType)
		{
			default:
				handleException(OTAFIRMWAREEXCEP_UNKNOWN_TYPE);
				break;

			case IMAGE_TYPE_LOADER:
				if(image_UpdateLoaderFromExtFlashAddr(gBootCfg.cfg.ImageInfoFromLoader.OTAstartAddrForApp) == false)
				{
					LOG_EVENT( LOG_EVENTCODE_OTA_UNKNOWN_EVENT, LOG_NUM_OTA, ERRLOGMAJOR, "Loader Update Failed !");
				}
				p = sMK24_Loader;
			break;

			case IMAGE_TYPE_APPLICATION:
			case IMAGE_TYPE_PACKAGE:
				if(checkLoaderGE_1_4())
				{
					// Set the address to indicate the loader to program APP from this address.
					gBootCfg.cfg.FromApp.NewAppAddress = gBootCfg.cfg.ImageInfoFromLoader.OTAstartAddrForApp;

					if(imageType == IMAGE_TYPE_PACKAGE)
					{
						p = sPackage;
					}
					else
					{
						p = sMK24_Application;
					}
				}
				else
				{
					// to stop a LOG_EVENT set retryCount
					gOtaMgmnt.FwOtaProcessData.retryCount = 1;

					exceptionOta = OTAFIRMWAREEXCEP_MISSING_LOADER;
					handleException(exceptionOta);

					// don't want to do this again until we have a loader
					OtaProcess_ResetVars();
				}
				break;

			case IMAGE_TYPE_PMIC_APPLICATION:
				gBootCfg.cfg.FromApp.NewAppAddress = gBootCfg.cfg.ImageInfoFromLoader.OTAstartAddrForApp;
				p = sPMIC_Application;
			break;
		}
		if((exceptionOta == OTAFIRMWAREEXCEP_NONE) &&
		   (imageType != IMAGE_TYPE_UNKNOWN))
		{
			LOG_EVENT(LOG_EVENTCODE_OTA_UPDATE_DONE, LOG_NUM_OTA, ERRLOGWARN,
					"Updating %s firmware to %d.%d.%d",
					p,
					(int)((saveNewFirmwareVersion >> 24) & 0xFF),
					(int)((saveNewFirmwareVersion >> 16) & 0xFF),
					(int)((saveNewFirmwareVersion & 0xFFFF))>>1);

			// let the backend know about the upgrade for MK24 App or Loader
			if((imageType == IMAGE_TYPE_LOADER) || (imageType == IMAGE_TYPE_APPLICATION) || (imageType == IMAGE_TYPE_PACKAGE))
			{
				if(SvcFirmware_GetCurrentTransportPipe() != eTRANSPORT_PIPE_CLI)
				{
					publishManifest(imageType);
				}
				// No Exception so far, so set bootConfig upgrade to not upgrade anymore
				OtaProcess_ResetVars();
			}
		}
	}
	// Write the bootconfig changes to flash
	if(bootConfigWriteFlash(&gBootCfg) == false)
	{
		rc_ok = false;
		handleException(OTAFIRMWAREEXCEP_BOOTCFG_FLASH_FAILED);
	}

	// Write the OTA process management data changes to flash
	if(OtaProcess_WriteToFlash()==false)
	{
		rc_ok = false;
		LOG_EVENT(LOG_EVENTCODE_OTA_MGMNT_WRITE_FAIL, LOG_NUM_OTA, ERRLOGMAJOR, "%s(), OTA process data write failed.", __func__);
	}

	if(SvcFirmware_GetCurrentTransportPipe() == eTRANSPORT_PIPE_CLI)
	{
		//Send status back to clio and wait for TX to completes
		uint8_t bySuccess = rc_ok;
		binaryCLI_sendPacket(E_BinaryCli_OTACompleted, &bySuccess, 1);
		CLI_flushTx();
		vTaskDelay( 1000 /  portTICK_PERIOD_MS );
	}

	return rc_ok;
}


/**
 * publishManifest
 *
 * @brief Publishes the image manifest to MQTT.
 *        Not a static as can be called for testing and
 *        when sensor is commissioned.
 *
 * @param imageType - Type of image(App, loader etc)
 * @return
 */
void publishManifest(ImageType_t imageType)
{
	char *manifest = NULL;
	switch(imageType)
	{
	case IMAGE_TYPE_LOADER:
		manifest = image_createManifest(imageType, __app_version);
		break;

	case IMAGE_TYPE_APPLICATION:
	case IMAGE_TYPE_PMIC_APPLICATION:
	case IMAGE_TYPE_PACKAGE:
		manifest = image_buildOTACompleteManifest(imageType, gBootCfg.cfg.ImageInfoFromLoader.OTAstartAddrForApp);
		break;

	default:
		LOG_EVENT(LOG_EVENTCODE_OTA_BASE, LOG_NUM_OTA, ERRLOGMAJOR, "%s() Unsupported image type: %d", __func__, (int)imageType);
		manifest = NULL;
		break;
	}

	if(manifest != NULL)
	{
		extern tCommHandle* getCommHandle();
		char *pubTopic = mqttConstructTopicFromSubTopic(kstrOTACompleteSubTopic);
		if(COMM_ERR_OK != TaskComm_Publish(getCommHandle(), manifest, strlen(manifest), (uint8_t*)pubTopic,  10000 /* 10 seconds OK? */ ))
		{
			// we already logged an event so no need to do it again
		}
	}
}


/**
 * OtaProcess_LoadFromFlash
 *
 * @brief Copies the contents of OTA process management structure from the flash
 * 		  location into the RAM copy.
 *
 * @return
 */
void OtaProcess_LoadFromFlash()
{
	memcpy((uint8_t*) &gOtaMgmnt, (uint8_t*) __ota_mgmnt_data, sizeof(OtaMgmnt_t));
	// check CRC of config
	uint32_t crc32 = crc32_hardware((uint8_t*)&gOtaMgmnt.FwOtaProcessData, sizeof(gOtaMgmnt.FwOtaProcessData));
	if(crc32 != gOtaMgmnt.crc32)
	{
		// too early to call a LOG_EVENT
		// LOG_EVENT( LOG_EVENTCODE_OTA_MGMNT_CRC_FAIL, LOG_NUM_OTA, ERRLOGMAJOR, "OTA Mgmnt CRC failed.");
		memset(&gOtaMgmnt, 0, sizeof(gOtaMgmnt));
		gOtaMgmnt.crc32 = crc32_hardware((uint8_t*)&gOtaMgmnt.FwOtaProcessData, sizeof(gOtaMgmnt.FwOtaProcessData));
	}
}

/**
 * OtaProcess_WriteToFlash
 *
 * @brief Writes the contents of RAM copy of OTA process management structure
 * 		  into the Flash.
 *
 * @return true if the data is successfully written, false otherwise.
 */
bool OtaProcess_WriteToFlash()
{
    bool stat = true;

    // calculate crc
   	gOtaMgmnt.crc32 = crc32_hardware((uint8_t*)&gOtaMgmnt.FwOtaProcessData, sizeof(gOtaMgmnt.FwOtaProcessData));
    __disable_irq();// we may run in the same flash bank as the one we are erasing/programming, then interrupts may not execute code inside this bank !
    stat = DrvFlashEraseProgramSector((uint32_t*) __ota_mgmnt_data, (uint32_t*)&gOtaMgmnt, sizeof(gOtaMgmnt));
    __enable_irq();

    return stat;
}

/**
 * OtaProcess_GetRetryCount
 *
 * @brief Fetch the remaining no.of OTA retries.
 *
 * @return remaining OTA retries
 */
uint32_t OtaProcess_GetRetryCount()
{
	return gOtaMgmnt.FwOtaProcessData.retryCount;
}

/**
 * OtaProcess_SetRetryCount
 *
 * @brief Set the remaining OTA retry counts.
 *
 * @param retryCount - No.of OTA retries allowed
 *
 * @return
 */
void OtaProcess_SetRetryCount(uint32_t retryCount)
{
	gOtaMgmnt.FwOtaProcessData.retryCount = retryCount;
}

/**
 * OtaProcess_GetBlocksWrittenInFlash
 *
 * @brief Fetch the no.of blocks written in external flash.
 *
 * @return no.of blocks written in the external flash
 */
uint32_t OtaProcess_GetBlocksWrittenInFlash()
{
	return gOtaMgmnt.FwOtaProcessData.blocksWrittenInFlash;
}

/**
 * OtaProcess_SetBlocksWrtittenInFlash
 *
 * @brief Set the no.of blocks written in external flash.
 *
 *@param blocksWritten - No.of OTA blocks written.
 *
 * @return
 */
void OtaProcess_SetBlocksWrtittenInFlash(uint32_t blocksWritten)
{
	gOtaMgmnt.FwOtaProcessData.blocksWrittenInFlash = blocksWritten;
}


/**
 * OtaProcess_GetNewFwVer
 *
 * @brief Fetch new fw version number from the OTA process management struct.
 *
 * @return new fw version number.
 */
uint32_t OtaProcess_GetNewFwVer()
{
	return gOtaMgmnt.FwOtaProcessData.newFirmwareVersion;
}

/**
 * OtaProcess_SetNewFwVer
 *
 * @brief Sets the new fw version number in the OTA process management struct.
 *
 * @param FwVer -  New firmware version of the OTA.
 *
 * @return void
 */
void OtaProcess_SetNewFwVer(uint32_t FwVer)
{
	gOtaMgmnt.FwOtaProcessData.newFirmwareVersion = FwVer;
}

/**
 * OtaProcess_GetImageSize
 *
 * @brief Fetch the Image size from the OTA process management struct.
 *
 * @return Image size
 */
uint32_t OtaProcess_GetImageSize()
{
	return gOtaMgmnt.FwOtaProcessData.imageSize_Bytes;
}

/**
 * OtaProcess_SetImageSize
 *
 * @brief Sets the Image size data field of the OTA process management struct.
 *
 * @param ImageSize_Bytes - Image size in bytes.
 *
 * @return void
 */
void OtaProcess_SetImageSize(uint32_t ImageSize_Bytes)
{
	gOtaMgmnt.FwOtaProcessData.imageSize_Bytes = ImageSize_Bytes;
}

/**
 * OtaProcess_GetCRC
 *
 * @brief Fetch the CRC of the OTA process management data.
 *
 * @return CRC of the OTA process management data
 */
uint32_t OtaProcess_GetCRC()
{
	return gOtaMgmnt.crc32;
}

#if NOT_USED
/*
 * IsExtImageAtAddrValid
 *
 * @brief Checks if the image at the specified start address in the external
 * 		  flash is valid.
 *
 * @param extFlashStartAddr - start address of image in external flash.
 *
 * @return  true , if the image is valid, false otherwise.
 */
static bool IsExtImageAtAddrValid(uint32_t extFlashStartAddr)
{
	uint32_t image_size = (uint32_t)__app_image_size;
	uint32_t crcResult;
	char type[32];

	if((extFlashStartAddr >= EXT_IMAGE_APP) &&
	   (extFlashStartAddr <= LAST_IMAGE_EXT_FLASH_START_ADDR))
	{
		crc32_start();
		for(int i = 0; i < image_size; i += BLOCK_SIZE)
		{
			uint32_t length = ((i + BLOCK_SIZE) < image_size) ? BLOCK_SIZE : image_size - i;
			if(IS25_ReadBytes(extFlashStartAddr + i, (uint8_t*) __sample_buffer, length))
			{
				crc32_calc((uint8_t*)__sample_buffer, length);
			}
			if(i == 0)
			{
				// it's the first block so let's check for image type
				if((JSON_string == jsonFetch((char*)__sample_buffer + 0x410, "imageType", type, true)) &&
				   (0 == strcmp(type, sMK24_Loader)))
				{
					image_size = (uint32_t)__loader_image_size;
				}
			}
		}
		crcResult = crc32_finish();
		printf("\nIMAGE At Addr:0x%x, crcResult = 0x%08X \n", extFlashStartAddr, (unsigned int)crcResult);
		return (CRCTARGETVALUE == crcResult);
	}
	else
	{
		LOG_EVENT( LOG_EVENTCODE_OTA_UNKNOWN_EVENT, LOG_NUM_OTA, ERRLOGMAJOR, "ExtFlash Addr outside range: 0x%x", extFlashStartAddr);
		return false;
	}
}
#endif

//------------------------------------------------------------------------------
// CLI Help
//------------------------------------------------------------------------------

static const char OTAHelp[] =
{
        " OTA subcommands:\r\n"
        " info: show the OTA process management information (RAM content)\r\n"
        " dropcon <no.of ota retries>: Simulate data connection drop in OTA, \r\n\t\t\t\t option to specify no.of OTA retries (default is 1 , max is 5)\r\n"
		" en <0|1>: 1-> Enable OTA test, 0->Disables ota tests\r\n"
		" set <version> <size>\r\n"
};

// OTA Test CLI subcommands.
struct cliSubCmd OTASubCmds[] =
{
	{"info", 	cliOTAPrintProcessDataInfo},
	{"dropcon",	cliOTADropConnection},
	{"en",		cliOTATestEnDisable},
	{"set", 	cliOTATestSet},
};

/**
 * ota_cliHelp
 *
 * @brief Help function for CLI OTA tests.
 *
 * @return true if the command executed correctly, false otherwise.
 */
bool ota_cliHelp(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
    printf("%s",OTAHelp);
    return true;
}


static bool cliOTAPrintProcessDataInfo( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	printf("New Firmware Version = %d.%d.%d \n",
			(int)((gOtaMgmnt.FwOtaProcessData.newFirmwareVersion >> 24) & 0xFF),
			(int)((gOtaMgmnt.FwOtaProcessData.newFirmwareVersion >> 16) & 0xFF),
			(int)((gOtaMgmnt.FwOtaProcessData.newFirmwareVersion & 0xFFFF))>>1);
	printf("OTA Image size (Bytes) = %d\n", gOtaMgmnt.FwOtaProcessData.imageSize_Bytes);
	printf("OTA retries left = %d\n", gOtaMgmnt.FwOtaProcessData.retryCount);
	printf("No.of OTA blocks written into external flash = %d\n", gOtaMgmnt.FwOtaProcessData.blocksWrittenInFlash);

	// Ota Test Data
	printf("OTA test Enable flag = %s\n", (gOtaMgmnt.OtaTestFailSimulation.bOTATestEn == true) ? "true":"false");
	printf("OTA test drop connection = %d times\n", gOtaMgmnt.OtaTestFailSimulation.exeOtaRetries);

    return true;
}

static bool cliOTADropConnection( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	if(args > 0)
	{
		if((argi[0] > 0) && (argi[0] <= MAX_NUM_OF_OTA_RETRIES))
		{
			gOtaMgmnt.OtaTestFailSimulation.bOTATestEn = true;
			gOtaMgmnt.OtaTestFailSimulation.exeOtaRetries = argi[0];
		}
		else
		{
			printf("Invalid no.of OTA retries, should be >0 & <=%d \n", MAX_NUM_OF_OTA_RETRIES);
			return false;
		}
	}
	else
	{
		// OTA retry once.
		gOtaMgmnt.OtaTestFailSimulation.bOTATestEn = true;
		gOtaMgmnt.OtaTestFailSimulation.exeOtaRetries = 1;
	}
	OtaProcess_WriteToFlash();
	printf("OTA retry test set to = %d\n", gOtaMgmnt.OtaTestFailSimulation.exeOtaRetries);
    return true;
}

static bool cliOTATestEnDisable( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	if(args > 0)
	{
		if(argi[0] == 0)
		{
			gOtaMgmnt.OtaTestFailSimulation.bOTATestEn = false;
			OtaProcess_WriteToFlash();
		}
		else if(argi[0] == 1)
		{
			gOtaMgmnt.OtaTestFailSimulation.bOTATestEn = true;
			OtaProcess_WriteToFlash();
		}
		else if(argi[0] == 2)
		{
			// hidden option for debug purposes only
			extern void setLoaderVectorsToApp(void);
			setLoaderVectorsToApp();
		}
		else
		{
			printf("Invalid input, should be either 0->disable, 1->Enable\n");
			return false;
		}
	}
	else
	{
		printf("Invalid input, should be either 0->disable, 1->Enable\n");
		return false;
	}
	printf("OTA Test Status = %d\n", gOtaMgmnt.OtaTestFailSimulation.bOTATestEn);
    return true;
}

static bool cliOTATestSet( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	if(args == 2)
	{
		OtaProcess_SetRetryCount(MAX_NUM_OF_OTA_RETRIES);
		OtaProcess_SetImageSize(argi[1]);
		OtaProcess_SetNewFwVer(argi[0]);
		if(OtaProcess_WriteToFlash()==false)
		{
			printf("OTA process mgmnt data write failed.\n");
		}
	}
	return true;
}

/**
 * ota_cliOta
 *
 * @brief CLI function for OTA tests.
 *
 * @return true if the command executed correctly, false otherwise.
 */
bool ota_cliOta( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    bool rc_ok = true;

    if (args)
    {
		rc_ok = cliSubcommand(args,argv,argi, OTASubCmds, sizeof(OTASubCmds)/sizeof(*OTASubCmds));
		if (rc_ok == false)
		{
		  printf("Command failed!\n");
		  // but it was a valid syntax!
		  rc_ok = true;
		}
    }
    else
    {
    	printf("%s",OTAHelp);
    }
    return rc_ok;
}

/**
 * OTATest_ContinueOTA
 *
 * @brief OTA Test helper function for simulating connection drop.
 *
 * @return true if the OTA is to be continued, false if the connection drop
 * 		   is simulated for OTA testing.
 */
static bool OTATest_ContinueOTA(uint32_t numOfBlocksWritten, uint32_t totalBlocks)
{
	bool rc_ok = true;
	uint16_t div = 1;

	switch(gOtaMgmnt.OtaTestFailSimulation.exeOtaRetries)
	{
		case 1:	// Simulate connection drop once
			if(numOfBlocksWritten >= 98)
			{
				rc_ok = false;
			}
			break;
		case 2: // Simulate connection drop twice
			div = totalBlocks / 2;
			if(((numOfBlocksWritten > 0) && ((numOfBlocksWritten % div == 0) && (gOtaMgmnt.FwOtaProcessData.retryCount == MAX_NUM_OF_OTA_RETRIES - 1))) ||
				((numOfBlocksWritten > div) && (numOfBlocksWritten % div == 0)))
			{
				rc_ok = false;
			}
			break;
		case 3: // Simulate connection drop thrice
			div = totalBlocks / 3;
			if(((numOfBlocksWritten > 0) && ((numOfBlocksWritten % div == 0) && (gOtaMgmnt.FwOtaProcessData.retryCount == MAX_NUM_OF_OTA_RETRIES - 1)))  ||
				((numOfBlocksWritten > div) && ((numOfBlocksWritten % div == 0) && (gOtaMgmnt.FwOtaProcessData.retryCount == MAX_NUM_OF_OTA_RETRIES - 2))) ||
				((numOfBlocksWritten > div*2) && (numOfBlocksWritten % div == 0)))
			{
				rc_ok = false;
			}
			break;
		case 4: // Simulate connection drop four times
			div = totalBlocks / 4;
			if(((numOfBlocksWritten > 0) && ((numOfBlocksWritten % div == 0) && (gOtaMgmnt.FwOtaProcessData.retryCount == MAX_NUM_OF_OTA_RETRIES - 1)))  ||
				((numOfBlocksWritten > div) && ((numOfBlocksWritten % div == 0) && (gOtaMgmnt.FwOtaProcessData.retryCount == MAX_NUM_OF_OTA_RETRIES - 2))) ||
				((numOfBlocksWritten > div*2) && ((numOfBlocksWritten % div == 0) && (gOtaMgmnt.FwOtaProcessData.retryCount == MAX_NUM_OF_OTA_RETRIES - 3))) ||
				((numOfBlocksWritten > div*3) && (numOfBlocksWritten % div == 0)))
			{
				rc_ok = false;
			}
			break;
		case 5:
			div = totalBlocks / 5;
			if(((numOfBlocksWritten > 0) && ((numOfBlocksWritten % div == 0) && (gOtaMgmnt.FwOtaProcessData.retryCount == MAX_NUM_OF_OTA_RETRIES - 1)))  ||
				((numOfBlocksWritten > div) && ((numOfBlocksWritten % div == 0) && (gOtaMgmnt.FwOtaProcessData.retryCount == MAX_NUM_OF_OTA_RETRIES - 2))) ||
				((numOfBlocksWritten > div*2) && ((numOfBlocksWritten % div == 0) && (gOtaMgmnt.FwOtaProcessData.retryCount == MAX_NUM_OF_OTA_RETRIES - 3))) ||
				((numOfBlocksWritten > div*3) && ((numOfBlocksWritten % div == 0) && (gOtaMgmnt.FwOtaProcessData.retryCount == MAX_NUM_OF_OTA_RETRIES - 4))) ||
				((numOfBlocksWritten > div*4) && (numOfBlocksWritten % div == 0)))
			{
				rc_ok = false;
			}
			break;
		default:
			break;
	}

	if((rc_ok == false) &&
	   ((gOtaMgmnt.FwOtaProcessData.retryCount + gOtaMgmnt.OtaTestFailSimulation.exeOtaRetries) <= MAX_NUM_OF_OTA_RETRIES))
	{
		gOtaMgmnt.OtaTestFailSimulation.exeOtaRetries = 0;
		gOtaMgmnt.OtaTestFailSimulation.bOTATestEn = false;
	}

	return rc_ok;
}


#ifdef __cplusplus
}
#endif