#ifdef __cplusplus
extern "C" {
#endif

/*
 * ExtFlash.c
 *
 *  Created on: Oct 17, 2016
 *      started by Rex,  refactored by George
 */


#include <stdio.h>
#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <xTaskDefs.h>
#include <semphr.h>
#include <Resources.h>
#include "string.h"
#include "ExtFlash.h"
#include "drv_is25.h"
#include "log.h"
#include "CLIcmd.h"
#include "CRC.h"
#include "image.h"
#include "configData.h"
#include "NvmData.h"
#include "rtc.h"
#include "Measurement.h"
#include "device.h"
#include "json.h"
#include "linker.h"
#include "temperature.h"

#define FLASH_VERSION						(1)

/*
 * defines and structures for the external flash layout
 */
// The allocation of pages must be in multiples of 16, so that when sector is erased
// only the intended dataset is erased not others. Also the allocation is done
// based on the max. possible samples as defined in the reqs.
#define RAW_DATA_MAX_NO_OF_PAGES            (1024)		// (1024 * 256) / 1024 = 256 kBytes
#define VIBRATION_DATA_MAX_NO_OF_PAGES      (256)
#define WHEEL_FLAT_DATA_MAX_NO_OF_PAGES     (256)
#define MEASURED_DATA_MAX_NO_OF_PAGES       (16)		// only one sector required
#define MAX_NO_OF_PAGES_PER_SET				(RAW_DATA_MAX_NO_OF_PAGES + \
											 VIBRATION_DATA_MAX_NO_OF_PAGES + \
											 WHEEL_FLAT_DATA_MAX_NO_OF_PAGES + \
											 MEASURED_DATA_MAX_NO_OF_PAGES)
#define MAX_NO_OF_PAGES						(MAX_NO_OF_PAGES_PER_SET * MAX_NUMBER_OF_DATASETS)

// is25 flash specifics
#define EXTFLASH_NO_OF_PAGES_IN_A_SECTOR   	(16)        // 4096 / 16

#define MEASUREMENT_BLANK_CRC_VALUE			(0xBDC64068)
#define EXTFLASH_DEFAULT_OP_TIME			(30000)    // default 30 second

#define V0_MAX								(3.7)

enum eErrorCodes
{
	eLOG_BACKUP			= 700,
	eLOG_ADDRESS_OVERLAP,
	eLOG_GATED_CRC,
	eLOG_PRESERVE_COMMS,
};

/***********************************************************************************************************************************************/
/******************************************* PLEASE READ ***************************************************************************************/
/***********************************************************************************************************************************************/
// A dataSet is a container of data of a certain type.
// Data types are
// block 0 - Raw waveforms -
// block 1 - Vibration waveforms -
// block 2 - WheelFlat waveforms -
// block 3 - Measurement record plus mrd * 4

// Each dataset contains 'n' data records
// Each data record contains a complete record of data associated with a wakeup event, together with data stamp, crc and data length details

/***********************************************************************************************************************************************/
#ifdef _MSC_VER
#define PACKED_STRUCT_START __pragma(pack(push, 1))
#define PACKED_STRUCT_END   __pragma(pack(pop))
#else
#define PACKED_STRUCT_START
#define PACKED_STRUCT_END   __attribute__((packed))
#endif
PACKED_STRUCT_START
typedef struct 
{
    uint32_t totalNumberOfBytes;	//! indicates the total number of bytes in the data block
    uint32_t crcCheckSum;			//! the data block checksum
} measureRecordDetails_t;
PACKED_STRUCT_END

typedef struct {
	char *desc;						// textual description of set block
	uint32_t maxNoOfPages;			// max no of pages for set block
	uint32_t noOfPagesOffset;		// offset from set block 0 start page in pages
} dataSetConfig_t;

const dataSetConfig_t dataSetConfig[MAX_NO_OF_DATA_RECORD_TYPES] =
{
		{ "RAW",       RAW_DATA_MAX_NO_OF_PAGES,		(0) },
		{ "VIB/ENV3",  VIBRATION_DATA_MAX_NO_OF_PAGES,	(RAW_DATA_MAX_NO_OF_PAGES) },
		{ "WHEELFLAT", WHEEL_FLAT_DATA_MAX_NO_OF_PAGES, (RAW_DATA_MAX_NO_OF_PAGES + VIBRATION_DATA_MAX_NO_OF_PAGES) },
		{ "MEASURE",   MEASURED_DATA_MAX_NO_OF_PAGES,	(RAW_DATA_MAX_NO_OF_PAGES + VIBRATION_DATA_MAX_NO_OF_PAGES + WHEEL_FLAT_DATA_MAX_NO_OF_PAGES) },
};

// Macros for data set calculations
#define DATASET_START_OF_PAGE_NO(dataSet, recordType)	((EXTFLASH_DATASET_START_ADDR / EXTFLASH_PAGE_SIZE_BYTES) + dataSetConfig[recordType].noOfPagesOffset + (MAX_NO_OF_PAGES_PER_SET * dataSet))
#define DATASET_START_ADDR(dataSet, recordType)			(DATASET_START_OF_PAGE_NO(dataSet, recordType) * EXTFLASH_PAGE_SIZE_BYTES)
#define DATASET_MAX_NO_OF_PAGES(recordType)				(dataSetConfig[recordType].maxNoOfPages)
#define DATASET_MRD_ADDR(dataSet, recordType)			(DATASET_START_ADDR(dataSet, IS25_MEASURED_DATA) + 0x100 + (recordType * sizeof(measureRecordDetails_t)))
#define DATASET_TYPE_DESC(recordType)					(dataSetConfig[recordType].desc)

/*
 * end defines and structures for the external flash layout
 */

/*
 * Event descriptors
 */
typedef enum
{
    ExtFlashEvt_format,			// erase flash and if necessary format it
    ExtFlashEvt_write,			// write block data to flash
    ExtFlashEvt_read,			// read block data from flash
    ExtFlashEvt_erase,			// erase a complete 'dataset'
} eExtFlashEventDescriptor_t;

typedef struct {
    eExtFlashEventDescriptor_t Descriptor;   // Event Descriptor
    QueueHandle_t replyQueue; // NULL when no reply is wanted
    tExtFlashReqData ReqData;
} ExtFlashEvent_t;

#define EVENTQUEUE_NR_ELEMENTS_EXT_FLASH      (2)

extern RTC_Type * const g_rtcBase[RTC_INSTANCE_COUNT];

// private function prototypes go here
static void taskExtFlash( void *pvParameters );
static int extFlash_Initialisation();

static int extFlash_WriteData(uint8_t* dataAddr,
							uint32_t recordType,
							uint32_t dataLength,
							uint16_t dataSetNo);
static int extFlash_ReadData(uint8_t* destAddress,
							uint32_t recordType,
							uint32_t maxNrOfBytes,
							uint16_t dataSetNo);

static int extFlash_EraseDataSet(uint16_t dataSetNo);
static bool GetFlashVersion(uint32_t *version, uint8_t *formatted);

// Functional Api's
void LogAddressOverlapEvent(const char *pFunName, uint32_t startAddress, uint32_t numBytes);
TaskHandle_t   _TaskHandle_extFlash;// not static because of easy print of task info in the CLI

static QueueHandle_t _EventQueue_extFlash;

static tExtFlashHandle extFlashHandle = { .EventQueue_extFlashRep=NULL} ;

static const char *extFlashErrorStrings[] = {
    FOREACH_ERROR(GENERATE_STRING)
};

/*
 * @function extFlash_ErrorString()
 *
 * @desc	Get a string for the required error code
 *
 * @params	int error code
 *
 * @return pointer to a string
 *
 */
const char *extFlash_ErrorString(int errCode)
{
	static char buf[32];
	if(errCode < extFlashErr_noError)
	{
		return extFlashErrorStrings[-errCode];
	}
	sprintf(buf, "Unknown(%d)", errCode);
	return buf;
}

void taskExtFlash_Init()
{
   // Set up task queue
	_EventQueue_extFlash = xQueueCreate(EVENTQUEUE_NR_ELEMENTS_EXT_FLASH, sizeof(ExtFlashEvent_t));
	vQueueAddToRegistry(_EventQueue_extFlash, "_EVTQ_EXT_FLASH");

    xTaskCreate( taskExtFlash,               	 // Task function name
	                 "EXT_FLASH",                // Task name string
					 STACKSIZE_XTASK_EXT_FLASH , // Allocated stack size on FreeRTOS heap
	                 NULL,                   	 // (void*)pvParams
					 PRIORITY_XTASK_EXT_FLASH,   // Task priority
	                 &_TaskHandle_extFlash );    // Task handle

 	extFlashHandle.EventQueue_extFlashRep = NULL;
	extFlash_InitCommands(&extFlashHandle);
	//    cliRegisterCommands(extFlashCommands , sizeof(extFlashCommands)/sizeof(*extFlashCommands));
}

static void taskExtFlash( void *pvParameters )
{
    ExtFlashEvent_t rxEvent;
    uint32_t version = 0;

    // check the external flash version. If it's not valid setup for a format
    if(!GetFlashVersion(&version, NULL) || (version != FLASH_VERSION))
    {
    	gNvmData.dat.is25.isReadyForUse = false;
    }

	for (;;)
	{
		if (xQueueReceive(_EventQueue_extFlash, &rxEvent, portMAX_DELAY))
	    {
			int errCode;

			switch (rxEvent.Descriptor)
			{
			case ExtFlashEvt_format:
				errCode = extFlash_Initialisation();
				if(errCode == extFlashErr_noError)
				{
					extern void initAppNvmDataIS25(void);
					// Messy calling back into the APP function but it's do it here
					// or where it's called from, so here it is
					initAppNvmDataIS25();
				}
				break;

			case ExtFlashEvt_read:
				errCode = extFlash_ReadData(
				rxEvent.ReqData.readReq.address,
				rxEvent.ReqData.readReq.dataType,
				rxEvent.ReqData.readReq.length,
				rxEvent.ReqData.readReq.dataSet);
				break;

			case ExtFlashEvt_write:
				errCode = extFlash_WriteData(
				rxEvent.ReqData.writeReq.address,
				rxEvent.ReqData.writeReq.dataType,
				rxEvent.ReqData.writeReq.length,
				rxEvent.ReqData.writeReq.dataSet);
				break;

			case ExtFlashEvt_erase:
				errCode = extFlash_EraseDataSet(rxEvent.ReqData.eraseReq.dataSet);
				break;

			default:
				errCode = -extFlashErr_unknownRequest;
				LOG_DBG( LOG_LEVEL_APP, "taskExtFlash(): unknown request type : %d\n",rxEvent.Descriptor);
				break;
			}	// switch

			if (rxEvent.replyQueue)
			{
				xQueueSend(rxEvent.replyQueue, &errCode, 0 );// nowait on this one
			}
	    }
	}
}

static bool GetFlashVersion(uint32_t *version, uint8_t *formatted)
{
    if(!IS25_ReadBytes(EXTFLASH_MANAGEMENT_START_ADDR, (uint8_t*)__sample_buffer, MANAGEMENT_MAX_SIZE_BYTES))
    {
    	return false;
    }

	if((JSON_value == jsonFetch((char*)__sample_buffer, "Version", version, true)))
	{
		if(formatted)
		{
			if(JSON_string == jsonFetch((char*)__sample_buffer, "Formatted", formatted, true))
			{
			}
		}
		return true;
	}
	return false;
}

/*
 * @function extFlash_Initialisation()
 *
 * @desc	Initialises the external flash if not configured
 *
 * @params	none
 *
 * @return int variable - Returns success or error code
 *
 */
static int extFlash_Initialisation()
{
    char buf[128];

	// erase the chip
	//	if(!IS25_PerformSectorErase(DATASET_START_OF_PAGE_NO(0, IS25_RAW_SAMPLED_DATA), MAX_NO_OF_PAGES * EXTFLASH_PAGE_SIZE_BYTES))
    LOG_DBG(LOG_LEVEL_APP, "Formatting external flash for Version %d mapping\n", FLASH_VERSION);
	if(!IS25_PerformChipErase())
	{
		return -extFlashErr_flashDriverEraseFailure;
	}

    sprintf(buf, "{\"Version\":%d,\"Formatted\":\"%s\"}", FLASH_VERSION, RtcUTCToString(RTC_HAL_GetSecsReg(RTC)));
    if(!IS25_WriteBytes(EXTFLASH_MANAGEMENT_START_ADDR, (uint8_t*)buf, strlen(buf) + 1))
    {
    	return -extFlashErr_flashDriverWriteFailure;
    }

    // backup the APP to the external flash
    if(!saveToImage(1))
    {
    	// log an event but don't fail the erase
    	LOG_EVENT(eLOG_BACKUP, LOG_NUM_APP, ERRLOGWARN, "Failed to backup APP to external flash image 1 after a flash format");
    }
    return extFlashErr_noError;
}

/*
 * @function extFlash_WriteData()
 *
 * @desc	Call to driver to write data to the external flash
 *
 * @params	dataAddr	- pointer to the data to write to flash
 *
 * @params  recordType  - the identity of the source data ex. Vibration data
 *
 * @params  dataLength  - how many bytes.
 *
 * @params  dataSetNo  - The index number of the dataset to update/write

 * @return int variable - Returns success or error code
 *
 */
static int extFlash_WriteData(uint8_t* dataAddr,
							uint32_t recordType,
							uint32_t dataLength,
							uint16_t dataSetNo)
{
	measureRecordDetails_t mrd;

	if (dataSetNo > MAX_NUMBER_OF_DATASETS)
	{
		return -extFlashErr_dataSetIndexOutOfRange;
	}

	if (recordType > IS25_MEASURED_DATA)
	{
		return -extFlashErr_recordTypeOutOfRange;
	}

	if(!IS25_ReadBytes(DATASET_MRD_ADDR(dataSetNo, recordType), (uint8_t*)&mrd, sizeof(mrd)))
	{
		return -extFlashErr_flashMRDRead;
	}

	// check that the data length does not exceed that allocated block space
	if(dataLength > (DATASET_MAX_NO_OF_PAGES(recordType) * EXTFLASH_PAGE_SIZE_BYTES))
	{	// TODO - Handle this case
		LOG_DBG( LOG_LEVEL_APP, "%s(): ERROR - dataLength exceeds allowed limit\n", __func__);
		return -extFlashErr_flashAddressOutOfRange;
	}

	// at this point the following is known
	// the address and length of the data to be saved to the flash
	// the block number to save the data to
	// the data source within the block
	// the number of pages allocated to the block/data source

	if (MEMORY_ERASE_STATE != mrd.totalNumberOfBytes)
	{
		return -extFlashErr_flashDriverEraseFailure;
	}

	// write the data to the measurement set block
	if(!IS25_WriteBytes(DATASET_START_ADDR(dataSetNo, recordType), dataAddr, dataLength))
	{   //TODO - Handle this case
		LOG_DBG( LOG_LEVEL_APP, "%s(): ERROR - IS25_WriteBytes failed\n", __func__);
		return -extFlashErr_flashDriverWriteFailure;
	}

	// write size + CRC
	mrd.crcCheckSum = crc32_hardware((void *)dataAddr,  dataLength);
	mrd.totalNumberOfBytes = dataLength;
	if(!IS25_WriteBytes(DATASET_MRD_ADDR(dataSetNo, recordType), (uint8_t*)&mrd, sizeof(mrd)))
	{
		return -extFlashErr_flashMRDWrite;
	}

	return mrd.totalNumberOfBytes;
}



/*
 * @function extFlash_EraseDataSet()
 *
 * @desc    Call to driver to erase a complete dataset
 *
 * @params  dataSetNo    - number of the dataset to erase (all records)

 * @return  int variable - Returns success or error code
 *
 */
static int extFlash_EraseDataSet( uint16_t dataSetNo)
{
	if (dataSetNo > MAX_NUMBER_OF_DATASETS)
	{
		return -extFlashErr_dataSetIndexOutOfRange;
	}

	measureRecordDetails_t mrd[MAX_NO_OF_DATA_RECORD_TYPES];

	if(!IS25_ReadBytes(DATASET_MRD_ADDR(dataSetNo, IS25_RAW_SAMPLED_DATA), (uint8_t*)&mrd, sizeof(mrd)))
	{
		return -extFlashErr_flashMRDRead;
	}

	bool performedErase = false;
	for (uint16_t recordType = 0; (recordType <= IS25_MEASURED_DATA); recordType++)
	{
		if((MEMORY_ERASE_STATE == mrd[recordType].totalNumberOfBytes) && !((recordType == IS25_MEASURED_DATA) & performedErase))
		{
			continue;
		}

		uint32_t startAddress = DATASET_START_OF_PAGE_NO(dataSetNo, recordType) * EXTFLASH_PAGE_SIZE_BYTES;
		uint32_t numBytes = DATASET_MAX_NO_OF_PAGES(recordType) * EXTFLASH_PAGE_SIZE_BYTES;

		// if the erase op. is going to overlap IMAGE area, then abort
		if((startAddress + numBytes) >= image_GetExtFlashImageStartAddress())
		{
			LogAddressOverlapEvent(__func__, startAddress, numBytes);
		}
		else
		{
			if(!IS25_PerformSectorErase(startAddress , numBytes))
			{
				return -extFlashErr_flashDriverEraseFailure;
			}
			else
			{
				performedErase = true;
			}
		}
	}
    return extFlashErr_noError;
}


/*
 * @function extFlash_ReadData()
 *
 * @desc	Call to driver to read data from external flash
 *
 * @params	destAddress	- pointer to the return destination address
 *
 * @params	recordType	- the identity of the source data ex. Vibration data
 *
 * @params maxNrOfBytes - no more than these should be read
 *
 * @params dataSetNo     - ataset index from where it should come from
 *
 * @return int variable - Returns # of bytes read or negative error code
 *
 */
static int extFlash_ReadData(uint8_t* destAddress,
							uint32_t recordType,
							uint32_t maxNrOfBytes,
							uint16_t dataSetNo)		// move on to next data block if possible
{
	measureRecordDetails_t mrd;

	if (dataSetNo > MAX_NUMBER_OF_DATASETS)
	{
		return -extFlashErr_dataSetIndexOutOfRange;
	}

	if (recordType > IS25_MEASURED_DATA)
	{
		return -extFlashErr_recordTypeOutOfRange;
	}

	if(!IS25_ReadBytes(DATASET_MRD_ADDR(dataSetNo, recordType), (uint8_t*)&mrd, sizeof(mrd)))
	{
		return -extFlashErr_flashMRDRead;
	}

	if(mrd.totalNumberOfBytes == 0x00 || mrd.totalNumberOfBytes > maxNrOfBytes )
	{
		return -extFlashErr_flashDriverReadFailure;
	}

	// read the data
	if(!IS25_ReadBytes(DATASET_START_ADDR(dataSetNo, recordType), destAddress, mrd.totalNumberOfBytes))
	{
		return -extFlashErr_flashDriverReadFailure;
	}

	// verify checksum
	uint32_t crc = crc32_hardware((void *)destAddress,  mrd.totalNumberOfBytes);
	if(mrd.crcCheckSum != crc)
	{
		// OK, we failed the record CRC so report an error
		return -extFlashErr_crcErrorRead;
	}

	return mrd.totalNumberOfBytes;
}


// procedure interface to the external flash functions

/*
 * below here, function interface to the  task,  to be called from another task
 * these functions run in the calling task context !
 */

/*
 * extFlash_InitCommands
 *
 * @brief           only run once in every task using this task calls after powerup
 *                  return false when failed
 *
 * @param handle    handle which hold local task data for interfacing with the  task
 * @return  true when successful, false otherwise
 */
bool extFlash_InitCommands(tExtFlashHandle * handle)
{
    // allocate reply queue
    if (handle->EventQueue_extFlashRep == NULL)
    {
        handle->EventQueue_extFlashRep  = xQueueCreate( 1, sizeof(int) );
    }

    return ( handle->EventQueue_extFlashRep != NULL);
}


static int waitReady(tExtFlashHandle * handle, TickType_t maxWait)
{
    int errCode;

    if (pdFALSE == xQueueReceive( handle->EventQueue_extFlashRep, &errCode, maxWait  ) )
    {
        errCode = -extFlashErr_queueAccessError;
    }

    return errCode;
}

/*
 * sendSimpleCommand
 *
 * @brief           common handling of most requests
 *
 * @param handle    handle which hold local task data for interfacing with the  task, if NULL, then it is a 'fire and forget' request
 * @param event     the message/event/parameters to send
 * @param maxWaitMs max time to wait in milliseconds,
 *                  if zero, and a valid handle with reply queue in it is passed, then the function does not wait for ready, but returns immediately.
 *                  The user should wait for ready with the  extFlash_WaitReady() function !
 * @return          true when successful, false on timeout or other failure
 */

static int sendSimpleCommand(tExtFlashHandle * handle , ExtFlashEvent_t * event, TickType_t maxWait)
{
    int errCode = extFlashErr_noError;

    if (handle)
    {
    	// caller wants to wait for the response, he should supply a valid queue pointer
         event->replyQueue = handle->EventQueue_extFlashRep;
         if (event->replyQueue )
         {
             // make sure response queue is empty, caller should make sure that a previous command completed !
               xQueueReset(event->replyQueue);
         }
     }
     if (pdTRUE == xQueueSend( _EventQueue_extFlash, event, 0 ))
     {
         if (event->replyQueue && (maxWait > 0))
         {
             errCode = waitReady(handle, maxWait);
         }
     }
     else
     {
         errCode = -extFlashErr_queueAccessError;
     }

     return errCode;
}

/*
 * extFlash_WaitReady
 *
 * @brief           wait for an earlier request to finish.
 *
 * @param handle    handle which hold local task data for interfacing with the  task
 * @param maxWaitMs max time to wait in milliseconds
 * @return          true when successful, false on timeout, or other failure
 */

int extFlash_WaitReady( tExtFlashHandle * handle, uint32_t maxWaitMs)
{
    return waitReady( handle,  (maxWaitMs + portTICK_PERIOD_MS -1) /  portTICK_PERIOD_MS ); // this is 'ciel()' for integers.
}

/*
 * extFlash_format
 *
 * @brief           initialise the flash memory by erasing if necessary put any structure in place
 *
 * @param handle    handle which hold local task data for interfacing with the  task, NULL for 'fire and forget'
 *
 * @param maxWaitMs max time to wait in milliseconds, when zero, and a handle is provided, the user must use the waitReady function to know it is ready/failed
 *
 * @return          no error or error code
 */

int extFlash_format(tExtFlashHandle * handle,  uint32_t maxWaitMs)
{
    // Execute  request
    ExtFlashEvent_t event =
    {
    	.Descriptor = ExtFlashEvt_format,
		.replyQueue = NULL
    };

	return sendSimpleCommand(handle, &event, ( maxWaitMs + portTICK_PERIOD_MS -1 ) /  portTICK_PERIOD_MS);
}

/*
 * extFlash_write
 *
 * @brief           write dataset element
 *
 * @param handle    handle which hold local task data for interfacing with the  task, NULL for 'fire and forget'
 *
 * @param src_p     processor memory start location to read
 *
 * @param length    length in bytes to write
 *
 * @param dataType  what dataset element
 *
 * @param dataSet   index of the dataset where it must be written in flash
 *
 * @param maxWaitMs max time to wait in milliseconds, when zero, and a handle is provided, the user must use the waitReady function to know it is ready/failed
 *
 * @return          no error or error code
 */
int extFlash_write(tExtFlashHandle * handle, uint8_t * src_p, uint32_t length,  uint32_t dataType, uint16_t dataSet, uint32_t maxWaitMs)
{
    // Execute  request
    ExtFlashEvent_t event =
    {
    	.Descriptor = ExtFlashEvt_write,
		.replyQueue = NULL,
	    .ReqData.writeReq.address = src_p,
	    .ReqData.writeReq.length = length,
	    .ReqData.writeReq.dataType = dataType,
	    .ReqData.writeReq.dataSet = dataSet
    };

	return sendSimpleCommand(handle, &event,( maxWaitMs + portTICK_PERIOD_MS -1 ) /  portTICK_PERIOD_MS);
}

/*
 * extFlash_read
 *
 * @brief           read dataset element
 *
 * @param handle    handle which hold local task data for interfacing with the  task, NULL for 'fire and forget'
 *
 * @param dst_p     processor memory start location to write
 *
 * @param maxLength maximum length in bytes to write
 *
 * @param dataType  what dataset element
 *
 * @param dataSet   index of the dataset where it must be written in flash
 *
 * @param maxWaitMs max time to wait in milliseconds, when zero, and a handle is provided, the user must use the waitReady function to know it is ready/failed
 *
 * @return          # of bytes read or error code
 */
int extFlash_read(tExtFlashHandle * handle, uint8_t * dst_p, uint32_t maxLength,  uint32_t dataType, uint16_t dataSet, uint32_t maxWaitMs)
{
    // Execute  request
    ExtFlashEvent_t event =
    {
    	.Descriptor = ExtFlashEvt_read,
		.replyQueue = NULL,
	    .ReqData.readReq.address = dst_p,
	    .ReqData.readReq.length = maxLength,
	    .ReqData.readReq.dataType = dataType,
	    .ReqData.readReq.dataSet = dataSet
    };

    return sendSimpleCommand(handle, &event, ( maxWaitMs + portTICK_PERIOD_MS -1 ) /  portTICK_PERIOD_MS);
}

/*
 * extFlash_erase
 *
 * @brief           erase a dataset  (and all elemets of it)
 *
 * @param handle    handle which hold local task data for interfacing with the  task, NULL for 'fire and forget'
 *
 * @param dataSet   index of the dataset to erase
 *
 * @param maxWaitMs max time to wait in milliseconds, when zero, and a handle is provided, the user must use the waitReady function to know it is ready/failed
 *
 * @return          no error or error code
 */
int extFlash_erase(tExtFlashHandle * handle, uint16_t dataSet, uint32_t maxWaitMs)
{
    // Execute  request
    ExtFlashEvent_t event =
    {
    	.Descriptor = ExtFlashEvt_erase,
		.replyQueue = NULL,
	    .ReqData.eraseReq.dataSet = dataSet
    };

	return sendSimpleCommand(handle, &event, ( maxWaitMs + portTICK_PERIOD_MS -1 ) /  portTICK_PERIOD_MS);
}

/*
 * extFlash_getMeasureSetInfo
 *
 * @brief           return storage info for a measurement set, at the moment not using the queue mechanism, but direct reading the task variables, which should be
 *                  guarded by a semaphore
 *
 * @param measureSetNum measure set to check
 *
 * @return          bit mask of waveforms available
 */

uint16_t extFlash_getMeasureSetInfo(uint16_t measureSetNum)
{
	uint16_t waves = 0;
	measureRecordDetails_t mrd[MAX_NO_OF_DATA_RECORD_TYPES];

	if((measureSetNum < MAX_NUMBER_OF_DATASETS) && IS25_ReadBytes(DATASET_MRD_ADDR(measureSetNum, IS25_RAW_SAMPLED_DATA), (uint8_t*)&mrd, sizeof(mrd)))
	{
		// if an area is erased, it does not contain waveform data !
		for(int i = 0; i < IS25_MEASURED_DATA; i++)
		{
			if(MEMORY_ERASE_STATE != mrd[i].totalNumberOfBytes)
			{
				waves |= (1 << i);
			}
		}
	}
    return waves;
}

/*
 * LogAddressOverlapEvent
 *
 * @brief	Log events when the ext flash routines overlaps the external flash
 * 			location reserved for storing IMAGES.
 *
 * @param 	pFunName - pointer to the function name.
 * @param	startAddress - start address of access in the external flash.
 * @param   numBytes - number of bytes to erase.
 *
 */
void LogAddressOverlapEvent(const char *pFunName, uint32_t startAddress, uint32_t numBytes)
{
	LOG_EVENT(eLOG_ADDRESS_OVERLAP, LOG_NUM_APP, ERRLOGFATAL, "%s() Erase aborted, StartAddr = %d, NumBytes = %d ", pFunName, startAddress, numBytes);
}


//------------------------------------------------------------------------------
// CLI
//------------------------------------------------------------------------------
/*
 * cli command line functions
 */

#include "NvmData.h"
#include "NvmConfig.h"

static void displayDataSet(uint32_t dataSet)
{
	measureRecordDetails_t mrd[MAX_NO_OF_DATA_RECORD_TYPES];
	uint32_t measure_addr = DATASET_MRD_ADDR(dataSet, IS25_RAW_SAMPLED_DATA);
	if(!IS25_ReadBytes(measure_addr, (uint8_t*)&mrd, sizeof(mrd)))
	{
		return;
	}
	printf( ".is25.dataSet[%d] :\n"
			"  S B   @Page #Pages    Bytes      CRC E\n",dataSet);
	for (int j = 0; j < MAX_NO_OF_DATA_RECORD_TYPES; j++)
	{
		bool erased = ((MEMORY_ERASE_STATE == mrd[j].totalNumberOfBytes) &&
				       (MEMORY_ERASE_STATE == mrd[j].crcCheckSum));
		printf("[%2d %d] %06x %06x %08x %08x %d (%s)\n",
				dataSet, j,
				DATASET_START_OF_PAGE_NO(dataSet, j),
				DATASET_MAX_NO_OF_PAGES(j),
				mrd[j].totalNumberOfBytes,
				mrd[j].crcCheckSum,
				erased,
				DATASET_TYPE_DESC(j));
	}
}

static void print_extFlashDir(bool log, uint32_t dataSets)
{
    if (dbg_logging & LOG_LEVEL_APP || log)
    {
		printf(	"NVDATA.is25 :\n"
				"  isReadyForUse                   = %3d\n", gNvmData.dat.is25.isReadyForUse);
		printf("  is25CurrentDatasetNo            = %3d\n", gNvmData.dat.is25.is25CurrentDatasetNo);
		printf("  measurementDatasetStartIndex    = %3d\n", gNvmData.dat.is25.measurementDatasetStartIndex);
		printf("  noOfMeasurementDatasetsToUpload = %3d\n", gNvmData.dat.is25.noOfMeasurementDatasetsToUpload);
		printf("  noOfCommsDatasetsToUpload       = %3d\n", gNvmData.dat.is25.noOfCommsDatasetsToUpload);

		uint16_t tempRecCount = 0;
		Temperature_GetRecordCount(&tempRecCount, NULL);
		printf("EXTFLASH Layout               @Page #Pages\n"
			   "  Management block           %06X %06X\n"
			   "  Temperature measurements   %06X %06X %d entries\n"
			   "  Gated measurements         %06X %06X %d entries\n"
			   "  COMMS record               %06X %06X\n",
			   (EXTFLASH_MANAGEMENT_START_ADDR / EXTFLASH_PAGE_SIZE_BYTES), (MANAGEMENT_MAX_SIZE_BYTES / EXTFLASH_PAGE_SIZE_BYTES),
			   (EXTFLASH_TEMP_MEAS_START_ADDR / EXTFLASH_PAGE_SIZE_BYTES), (TEMPERATURE_MEAS_SIZE / EXTFLASH_PAGE_SIZE_BYTES), tempRecCount,
			   (EXTFLASH_GATED_START_ADDR / EXTFLASH_PAGE_SIZE_BYTES), (GATED_MAX_SIZE_BYTES / EXTFLASH_PAGE_SIZE_BYTES), ExtFlash_gatedMeasurementCount(),
			   (EXTFLASH_COMMS_START_ADDR / EXTFLASH_PAGE_SIZE_BYTES), (COMMS_MAX_SIZE_BYTES / EXTFLASH_PAGE_SIZE_BYTES));
		if(Device_HasPMIC())
		{
			printf("  Backup PMIC                %06X %06X\n",
					(EXTFLASH_BACKUP_PMIC_START_ADDR / EXTFLASH_PAGE_SIZE_BYTES), (BACKUP_PMIC_MAX_SIZE_BYTES / EXTFLASH_PAGE_SIZE_BYTES));
		}
		printf("  Backup image 1             %06X %06X\n"
			   "  Backup image 2             %06X %06X\n",
			   (EXTFLASH_BACKUP1_START_ADDR / EXTFLASH_PAGE_SIZE_BYTES), (BACKUP_MAX_SIZE_BYTES / EXTFLASH_PAGE_SIZE_BYTES),
			   (EXTFLASH_BACKUP2_START_ADDR / EXTFLASH_PAGE_SIZE_BYTES), (BACKUP_MAX_SIZE_BYTES / EXTFLASH_PAGE_SIZE_BYTES));

		uint32_t version = 0;
		uint8_t formatted[64] = "";
		printf("EXTFLASH\n");
		if(GetFlashVersion(&version, formatted))
		{
			printf("....Version                = %d\n", version);
			printf("....Formatted              = %s\n", formatted);
		}
		printf("....page size              = 0x%04x\n"
		       "....maxNoOfPages           = %d\n"
			   "....MAX_NUMBER_OF_DATASETS = %d\n",
			   EXTFLASH_PAGE_SIZE_BYTES,
			   MAX_NO_OF_PAGES,
			   MAX_NUMBER_OF_DATASETS);

		int dataSetStart = gNvmData.dat.is25.measurementDatasetStartIndex;
		if (dataSets == 0)
		{
			dataSets = gNvmData.dat.is25.noOfMeasurementDatasetsToUpload;
		}
		else
		{
			if(dataSets > MAX_NUMBER_OF_DATASETS)
			{
				dataSets = MAX_NUMBER_OF_DATASETS;
			}
			dataSetStart = FIRST_DATASET;
		}

		// And the rest
		for (int dataSet = dataSetStart; dataSets; dataSets--)
		{
			displayDataSet(dataSet);
			if(++dataSet >= MAX_NUMBER_OF_DATASETS)
			{
				dataSet = FIRST_DATASET;
			}

		}
    }
}

static bool cliExtFlashDump( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    bool rc_ok = true;
	if (args == 0)
	{
		rc_ok = false;
	}
	else
	{
		unsigned int i;
		uint32_t strt, end;
		strt =  argi[0];
		end = strt+16;

		if (args>1) {
			if (argv[1][0] == '+') {
				end = strt + argi[1];
			} else {
				if (argi[1] > strt) {
					end = argi[1];
				}
			}
		}
		while (end > strt && rc_ok)
		{
			uint8_t buf[16];
			int count = (end - strt) > 16 ? 16 : (end - strt);

			printf(" %08x :",strt);

			rc_ok = IS25_ReadBytes(strt, buf, count);
			if (rc_ok) {
				for(i=0; i<count; i++) {
					if (i==8) printf(" ");
					printf(" %02x",buf[i]);
				}
				printf("  ");
				for(i=0; i<count; i++) {
					if (i==8) printf(" ");
					printf("%c",buf[i] < 32 || buf[i]> 126 ? '.' : buf[i]);
				}

				put_s("\r\n");
				strt += count;
			}
		}
	}

    return rc_ok;
}

static bool cliExtFlashTest( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    bool retval = false;
    uint8_t id = 0x00;

    retval = IS25_ReadProductIdentity(&id);
    if(retval == false)
    {
        printf("FAILED reading product ID of extFlash\n");
    }
    else
    {
        if(id == IS25_64_MBIT_PRODUCT_ID)
        {
            printf("Product Id = 0x16, Device Density 64Mb\n");
        }
        else if(id == IS25_128_MBIT_PRODUCT_ID)
        {
            printf("Product Id = 0x17, Device Density 128Mb\n");
        }
        else if(id == IS25_512_MBIT_PRODUCT_ID)
		{
			printf("Product Id = 0x19, Device Density 512Mb\n");
		}
        else
        {
            printf("WARNING - Unknown product id = %d\n", id);
        }
    }
#if 1
    if (args>0) {
        extern bool testFlashTransfers(uint32_t startByte, uint32_t numBytes, uint32_t seed);
        uint32_t start,num,seed;

        num = 1;
        seed = 0;
        start = argi[0];
        if (args>1) num  = argi[1];
        if (args>2) seed = argi[2];

        if (num>0) {
            retval=testFlashTransfers(start,num,seed);
        }
    }
#endif
    return retval;
}


static const char extFlashhelp[] = {
        "extFlash subcommands:\r\n"
        " info\t\t\t\t\tprint 'flash structure'\r\n"
        " format <timeoutMs>\t\t\terase external flash and write management data\r\n"
        " read  dataSet recordType <maxLength>\tread flash to 'samplebuffer' \r\n"
        " write dataSet recordType count\t\twrite 'samplebuffer' to flash\r\n"
        " erase dataSet <timeoutMs>\t\terase the flash (all record types) occupied by the dataSet number\r\n"
        " wait  <timeoutMs>\t\t\twait for flash operation ready\r\n"
        " dump address [ end | +count]\t\tdump external flash memory\r\n"
        " test <startaddr> <numbytes> <seed>\tTest external flash chip, warning: existing data will be over writen!\r\n"
		" gated <dataset#> <index=1-n, 0=all>\tdisplay gated entry measurements\r\n"
		" chip display manufacturer and device IDs\r\n"
};




bool cliExtHelpExtFlash(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
    printf(extFlashhelp);
    return true;
}

static bool cliExtFlashInfo( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    bool rc_ok = true;
    uint32_t dataSets = ((args > 0) ? argi[0] : 0);
    print_extFlashDir(true, dataSets);

    return rc_ok;
}

static bool cliExtFlashFormat( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	uint32_t timeout = ((args==1) ? argi[0] : EXTFLASH_CHIP_ERASE_MAXWAIT_MS); // default 30 second

	int errCode = extFlash_format(&extFlashHandle,  timeout);
    if(errCode < 0)
    {
    	printf("extFlash_format(%d) error %s\n", timeout,  extFlash_ErrorString(errCode));
    }
    else
    {
    	printf("extFlash_format(%d) bytesRead = %d\n", timeout, errCode);
    }

	return (errCode >= 0);
}

static bool cliExtFlashRead( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    uint16_t dataSet = ((args>=1) ? argi[0] : 0);
    uint32_t recordType = ((args>=2) ? argi[1] : 0);
    uint32_t maxLength = ((args>=3) ? argi[2] : (uint32_t) __sample_buffer_size);

    printf("read from flash dataSet %d recordType %d to address 0x%08x\n",dataSet, recordType, __sample_buffer);
    int bytesRead = extFlash_read(&extFlashHandle,  (uint8_t *) __sample_buffer,maxLength, recordType, dataSet, EXTFLASH_DEFAULT_OP_TIME);
    if(bytesRead < 0)
    {
    	printf("extFlash_read(%d) error %s\n", EXTFLASH_DEFAULT_OP_TIME,  extFlash_ErrorString(bytesRead));
    }
    else
    {
    	printf("extFlash_read(%d) bytesRead = %d\n", EXTFLASH_DEFAULT_OP_TIME, bytesRead);
    }

    return (bytesRead >= 0);
}

static bool cliExtFlashWrite( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    uint16_t dataSet = ((args>=1) ? argi[0] : 0);
    uint32_t recordType = ((args>=2) ? argi[1] : 0);
    uint32_t length = ((args>=3) ? argi[2] : 256);

    printf("write %d bytes to flash dataSet %d recordType %d from address 0x%08x\n", length, dataSet, recordType, __sample_buffer);
    int bytesWrite = extFlash_write(&extFlashHandle,  (uint8_t *) __sample_buffer, length, recordType, dataSet, EXTFLASH_DEFAULT_OP_TIME);
    if(bytesWrite != length)
    {
    	printf("extFlash_write(%d) error %s\n", EXTFLASH_DEFAULT_OP_TIME, extFlash_ErrorString(bytesWrite));
    }
    else
    {
    	printf("extFlash_write(%d) errCode = %d\n", EXTFLASH_DEFAULT_OP_TIME, bytesWrite);
    }

    return (bytesWrite == length);
}

static bool cliExtFlashErase( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    uint16_t dataSet = ((args>=1) ? argi[0] : 0);
    uint32_t timeout = ((args>=2) ? argi[1] : EXTFLASH_DEFAULT_OP_TIME); // default 30 second

    printf("erase dataset %d\n",dataSet);
    int errCode = extFlash_erase(&extFlashHandle,  dataSet, timeout);
    if(errCode < 0)
    {
    	printf("extFlash_erase(%d) error %s\n", dataSet, extFlash_ErrorString(errCode));
    }
    else
    {
    	printf("extFlash_erase(%d) OK\n", dataSet);
    }

    return (errCode >= 0);
}

static bool cliExtFlashWaitReady( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    uint32_t timeout = ((args>=1) ? argi[0] : EXTFLASH_DEFAULT_OP_TIME); // default 30 second

    printf("Wait for flash operation ready\n");
    int errCode = extFlash_WaitReady(&extFlashHandle, timeout);
    if(errCode < 0)
    {
        printf("extFlash_WaitReady(%d) error %s\n", timeout, extFlash_ErrorString(errCode));
    }

    return (errCode >= 0);
}


char *gatingReason(uint32_t reason)
{
	switch(reason)
	{
	case eNoFix : return "NO FIX";
	case eStopped: return "STOPPED";
	case eSpeedSlow: return "SLOW";
	case eSpeedFast: return "FAST";
	case eSpeedDiff: return "DIFFERENCE";
	case eSpeedBDS: return "DIFFERENCE+SLOW";
	case eSpeedBDF: return "DIFFERENCE+FAST";
	case eSpeedBS: return "MEAS SLOW";
	case eSpeedBF: return "MEAS FAST";
	case eSpeedBD: return "MEAS DIFFERENCE";
	}
	return "UNKNOWN";
}

/*
 * ExtFlash_printGatedMeasurement
 *
 * @brief	display the gated entry on the console
 * 			external flash at the specified block number and index.
 *
 * @param	dataSet (uint16_t) - measurement set to display.
 *
 * @param	nIndex (uint8_t) - gated entry index.
 *
 * @param   display - true = print to the console
 *
 * @return	void
 */
void ExtFlash_printGatedMeasurement(uint16_t inx, bool display)
{
	if(display)
	{
		printf("GATED MEASUREMENT RECORD %d @ %s\n", inx + 1, RtcUTCToString(measureRecord.timestamp/10000000));
		printf("  Gating Reason       = %s\n", gatingReason(measureRecord.params.Energy_Remaining));
		if((eNoFix != measureRecord.params.Energy_Remaining) && (eStopped != measureRecord.params.Energy_Remaining))
		{
			  printf("  Is Speed Within Env = %s\n", measureRecord.params.Is_Speed_Within_Env ? "YES" : "NO");
			  printf("  Is Good Speed Diff  = %s\n", measureRecord.params.Is_Good_Speed_Diff ? "YES" : "NO");
			  printf("  Rotation_Diff       = %f\n", measureRecord.params.Rotation_Diff);
			  printf("  -------------- GNSS -------------\n"
					 "  Latitude            = %f %s\n", measureRecord.params.GNSS_Lat_1, measureRecord.params.GNSS_NS_1);
			  printf("  Longitude           = %f %s\n", measureRecord.params.GNSS_Long_1, measureRecord.params.GNSS_EW_1);
			  printf("  Course              = %d\n", measureRecord.params.GNSS_Course_1);
			  printf("  Time To Fix         = %d\n", measureRecord.params.GNSS_Time_To_Fix_1);
			  printf("  Speed To Rotation 1 = %f\n", measureRecord.params.GNSS_Speed_To_Rotation_1);
			  printf("  Speed To Rotation 2 = %f\n", measureRecord.params.GNSS_Speed_To_Rotation_2);
			  printf("  Time_Diff           = %f\n", measureRecord.params.GNSS_Time_Diff);
			  printf("  Sat ID  =");
			  int noSat = 0;
			  for (int i = 0; i < MAX_GNSS_SATELITES; i++)
			  {
				  if(!measureRecord.params.GNSS_Sat_Id[i])
				  {
					  break;
				  }
				  printf(" %3d", measureRecord.params.GNSS_Sat_Id[i]);
				  noSat++;
			  }
			  printf("\n"
			         "      SNR =");
			  for (int i = 0; i < noSat; i++)
			  {
				  printf(" %3d", measureRecord.params.GNSS_Sat_Snr[i]);
			  }
			  printf("\n");
		}
		printf("\n");
	}
}

static bool cliGated( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	// sub-sub command
	if((args == 1) && (0 == strcmp((char*)argv[0], "erase")))
	{
		ExtFlash_eraseGatedMeasData();
		return true;
	}

	if((args >= 1) && (0 == strcmp((char*)argv[0], "write")))
	{
		int count = (args >= 2) ? argi[1] : 1;
		int written = 0;
		measureRecord.params.Energy_Remaining = eStopped;
		while(count-- && ExtFlash_storeGatedMeasData())
		{
			extern uint64_t ConfigSvcData_GetIDEFTime();
			measureRecord.timestamp = ConfigSvcData_GetIDEFTime();
			written++;
		}
		printf("%d gated records written to flash\n", written);
		return true;
	}

    for(int inx = 0; inx < GATED_MAX_RECORDS; inx++)
    {
		if(!ExtFlash_fetchGatedMeasData(inx))
		{
			break;
		}
		ExtFlash_printGatedMeasurement(inx, true);
    }
    return true;
}

static bool cliChipID( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	uint8_t man, dev;

	if(IS25_ReadManufacturerId(&man, &dev))
	{
		printf("manufacturer ID=%02X; device ID=%02X\n", (int)man, (int)dev);
	}
	else
	{
		printf("failed to read device\n");
	}
	return true;
}

struct cliSubCmd extFlashSubCmds[] = {
        {"info" , cliExtFlashInfo},
        {"format", cliExtFlashFormat},
        {"write", cliExtFlashWrite },
        {"read" , cliExtFlashRead },
        {"erase", cliExtFlashErase},
        {"wait", cliExtFlashWaitReady},
        {"dump", cliExtFlashDump},
        {"test", cliExtFlashTest},
		{"gated", cliGated },
		{"chip", cliChipID},
};

bool cliExtFlash( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    bool rc_ok = true;

    if (args)
    {
            rc_ok = cliSubcommand(args,argv,argi, extFlashSubCmds, sizeof(extFlashSubCmds)/sizeof(*extFlashSubCmds)  );
    }
    else
    {
        printf("Try typing: help extFlash\n");
    }

    return rc_ok;
}

#if 0
static const struct cliCmd extFlashCommands[] = {
        EXTFLASH_CLI,
};
#endif

/*
 * Structure for storing gated measurement data
 */
typedef struct {
    measure_record_t measureRecord;
    uint32_t crc;
} gatedMeasData_t;

/*
 * ExtFlash_gatedMeasurementCount
 *
 * @brief           returns number of gated measurements in the requested set number
 *
 * @param seNo       measure set to check
 *
 * @return          number of gated measurements
 */
int ExtFlash_gatedMeasurementCount()
{
	uint8_t buf[GATED_RECORD_SIZE_BYTES];

	for(int i = 0; i < GATED_MAX_RECORDS; i++)
	{
		if(!IS25_ReadBytes(EXTFLASH_GATED_START_ADDR + (GATED_RECORD_SIZE_BYTES * i), buf, GATED_RECORD_SIZE_BYTES))
		{
			return 0;
		}
		// is the time stamp all 'F' then blank record so exit
		if(0xFFFFFFFFFFFFFFFF == *(uint64_t *)&buf[0])
		{
			return i;
		}
	}
	return GATED_MAX_RECORDS;
}

/*
 * ExtFlash_storeGatedMeasData
 *
 * @brief	Stores the gated measurement to the LOG area of the
 * 			external flash at the specified block number.
 *
 * @param	nBlockNum (uint16_t) - External Flash Block number.
 *
 * @return	true - If the data write is successful,
 * 			false - otherwise.
 */
bool ExtFlash_storeGatedMeasData(void)
{
	gatedMeasData_t dataRecord;
	uint8_t buf[GATED_RECORD_SIZE_BYTES];

	LOG_DBG(LOG_LEVEL_APP, "Storing gated measurement, Reason: %s\n", gatingReason(measureRecord.params.Energy_Remaining));

	memset(&dataRecord, 0xFF, sizeof(dataRecord));
	memcpy(&dataRecord.measureRecord, &measureRecord, sizeof(measureRecord));
    dataRecord.crc = crc32_hardware(&dataRecord.measureRecord, sizeof(dataRecord.measureRecord));

    // This part of the code may need speeding up if we run to the maximum records
	for(int i = 0; i < GATED_MAX_RECORDS; i++)
	{
		int j;
		if(!IS25_ReadBytes(EXTFLASH_GATED_START_ADDR + (GATED_RECORD_SIZE_BYTES * i), buf, GATED_RECORD_SIZE_BYTES))
		{
			return false;
		}
		bool blank = true;
		for(j = 0; blank && j < (GATED_RECORD_SIZE_BYTES/sizeof(uint32_t)); j++)
		{
			if(MEMORY_ERASE_STATE != ((uint32_t*)&buf[0])[j])
			{
				blank = false;
			}
		}
		if(!blank)
		{
			continue;
		}
		return IS25_WriteBytes(EXTFLASH_GATED_START_ADDR + (GATED_RECORD_SIZE_BYTES * i), (uint8_t*)&dataRecord, sizeof(dataRecord));
	}

    return false;
}

/*
 * ExtFlash_fetchGatedMeasData
 *
 * @brief	Reads the gated measurement from the LOG area of the
 * 			external flash at the specified block number and index.
 *
 * @param	nBlockNum (uint16_t) - External Flash Block number.
 *
 * @param	nIndex (uint8_t) - index.
 *
 * @return	true - If the data read is successful,
 * 			false - otherwise.
 */
bool ExtFlash_fetchGatedMeasData(uint16_t index)
{
	gatedMeasData_t dataRecord;
	bool rc = true;

	// read the measurement record
	if(!IS25_ReadBytes(EXTFLASH_GATED_START_ADDR + (GATED_RECORD_SIZE_BYTES * index), (uint8_t*)&dataRecord, sizeof(dataRecord)))
	{
		return false;
	}

	// check the CRC for returned data
	uint32_t crc = crc32_hardware(&dataRecord.measureRecord, sizeof(measureRecord));
	if(dataRecord.crc == crc)
	{
		// good CRC so store data
		memcpy(&measureRecord, &dataRecord.measureRecord, sizeof(measureRecord));
	}
	else
	{
		// CRC when the flash is blank
		if(MEASUREMENT_BLANK_CRC_VALUE != crc)
		{
			// only log a message if there is data
			LOG_EVENT(eLOG_GATED_CRC, LOG_NUM_APP, ERRLOGWARN, "Gated measurement CRC error reading %d, CRC=0x%08X", index, crc);
		}
		rc = false;	// either bad CRC or end of records
	}

    return rc;
}

/*
 * ExtFlash_eraseGatedMeasData
 *
 * @brief	Erase the gated measurements from the LOG area of the external flash
 *
 */
void ExtFlash_eraseGatedMeasData(void)
{
	IS25_PerformSectorErase(EXTFLASH_GATED_START_ADDR, GATED_MAX_SIZE_BYTES);
}

/*
 * storeCommsData
 *
 * @brief	Stores the measurement and communication record (RAM copy) to the
 * 			external flash.
 *
 * @return	true - If the data write is successful,
 * 			false - otherwise.
 */
bool storeCommsData(void)
{
	struct
	{
		dataRecord_t r;
		uint32_t crc;
	} dataRecord;// not happy with this...
    uint32_t dataLength = sizeof(dataRecord);

    // erase the sector
	bool retval = IS25_PerformSectorErase(EXTFLASH_COMMS_START_ADDR, dataLength + sizeof(uint32_t));
	if(retval)
	{
		// copy the COMMS and measurement records to transfer buffer
		memcpy(&dataRecord.r.commsRecord, &commsRecord, sizeof(commsRecord));
		memcpy(&dataRecord.r.measureRecord, &measureRecord, sizeof(measureRecord));

		// CRC and write the buffer
		dataRecord.crc = crc32_hardware((void *)&dataRecord.r, sizeof(dataRecord.r));
		retval = IS25_WriteBytes(EXTFLASH_COMMS_START_ADDR, (uint8_t*)&dataRecord, dataLength);
	}
	return retval;
}

/*
 * fetchCommsData
 *
 * @brief	Fetches the measurement and communication record (RAM copy) from the
 * 			external flash.
 *
 * @param	copyBoth (bool) - true = copy both records, false = comms only
 *
 * @return	true - If the data read is successful,
 * 			false - otherwise.
 */
bool fetchCommsData(bool copyBoth)
{
	struct
	{
		dataRecord_t r;
		uint32_t crc;
	} dataRecord;// not happy with this...

    // read the buffer, validate the CRC and if ok copy to the real buffers
	bool retval = IS25_ReadBytes(EXTFLASH_COMMS_START_ADDR, (uint8_t*)&dataRecord, sizeof(dataRecord));
	if(retval)
	{
		retval = (dataRecord.crc == crc32_hardware((void *)&dataRecord.r, sizeof(dataRecord.r)));
		if(retval)
		{
			memcpy((void*)&commsRecord, (void*)&dataRecord.r.commsRecord, sizeof(commsRecord));
			if(copyBoth)
			{
				memcpy((void*)&measureRecord, (void*)&dataRecord.r.measureRecord, sizeof(measureRecord));
			}
		}
	}
	return retval;
}

/**
 * Versions 1.2.x, 1.3.x & 1.4.x have an external flash formatted to implement a flash
 * file system with a directory and measurement sets. The structure is shown below:
 *
 * +---------------------------------------------+
 * |                                             |
 * |                 Image 2                     |
 * |                                             |
 * +---------------------------------------------+ 0xF80000 Backup image 2
 * |                                             |
 * |                 Image 1                     |
 * |                                             |
 * +---------------------------------------------+ 0xF00000 Backup image 1
 * |                                             |
 * ~                Available                    ~
 * |                                             |
 * +---------------------------------------------+ 0x872000 Available
 * |                                             |
 * |             Measurement Set 29              |
 * |                                             |
 * +---------------------------------------------+ 0x82A000 Measurement set 29
 * ~       Measurement Sets 1 ~ 28               ~
 * +---------------------------------------------+ 0x92000 Measurement set 2 - 28
 * |                                             |
 * |             Measurement Set 1               |
 * |                                             |
 * +---------------------------------------------+ 0x4A000 Measurement set 1
 * |                                             |
 * |             Measurement Set 0               |
 * |                                             |
 * +---------------------------------------------+ 0x02000 Measurement set 0 (COMMS Record)
 * |                                             |
 * |                Directory                    |
 * |                                             |
 * +---------------------------------------------+ 0x00000 directory start
 *
 * Each measurement set contains 5 records as shown below:
 *
 * +---------------------------------------------+ 0x22800
 * |                  GATED                      |
 * +---------------------------------------------+ 0x22400
 * |               MEASUREMENT                   |
 * +---------------------------------------------+ 0x22000
 * |                WHEELFLAT                    |
 * +---------------------------------------------+ 0x21000
 * |                VIB/ENV3                     |
 * +---------------------------------------------+ 0x20000
 * |                   RAW                       |
 * +---------------------------------------------+ 0x00000
 *
 * The directory record has the structure as defined by the typedef extFlash_recordDetails_t.
 * In order to upgrade the COMMS record we simply need to read the COMMS record stored in
 * the measurement set 0 and store it to the global commsRecord within 1.10.0. At issue, the
 * directory could be corrupted due to the saving of a commsRecord/measurement set but the
 * directory not being updated due to perhaps an unexpected shutdown or hard fault. Hence
 * the checks to try and ascertain if the commsRecord is valid. The directory is CRCed with a
 * CRC being stored in the directory for each block/record type.
 *
 */
#define MAX_NUMBER_OF_DATASETS_1_4_3		(30)
#define MAX_NO_OF_DATA_RECORD_TYPES_1_4_3	(5)

/*****************************************************************************
 * I'm sure this information seemed useful at the time
 */

PACKED_STRUCT_START 
typedef struct
{
    uint16_t startOfBlockPageNo;                        //! indicates start of block page number, is seems it will always be zero ?
    uint32_t maxNoOfPages;                              //! max number of pages available to store data
    uint16_t nextBlockUploadIndex;                      //! index of the last data block used - used for deciding which data block to upload next
} extFlash_block_data_t;
PACKED_STRUCT_END
PACKED_STRUCT_START
/*****************************************************************************
 * Location and size information for the individual measurement sets.
 */
typedef struct 
{
    uint16_t startOfDataPageNo;                         //! indicates the start of data page number
    uint16_t maxNoOfPages;                              //! the number of pages allowed by this data block for the data source
    uint32_t totalNumberOfBytes;                        //! indicates the total number of bytes in the data block
    //uint32_t timeStamp;                                   //! timestamp of the data : note: gdf: not used for rel1.0, and that is good, because it should be uint64_t !!
    uint32_t crcCheckSum;                               //! the data block checksum
    bool unsent;                                        //! indicates if data needs to be uploaded
    bool erased;                                        // indicate erased or not.
} extFlash_recordDetails_t;
PACKED_STRUCT_END
PACKED_STRUCT_START
/*****************************************************************************
 * This is the structure of the directory written to address 0 of the external
 * flash.
 */
typedef struct  extFlash_DirectoryStruct
{
    uint32_t crc32; // 32 bit crc whole data struct

		PACKED_STRUCT_START
    struct extFlash_Structure
    {
        extFlash_block_data_t extFlashStatusInfo;
        extFlash_recordDetails_t dataSets[MAX_NUMBER_OF_DATASETS_1_4_3][MAX_NO_OF_DATA_RECORD_TYPES_1_4_3];       //! array of data blocks

    } allocation;
		PACKED_STRUCT_END

} tExtFlashDirectory;
PACKED_STRUCT_END

#define COMMS_RECORD_ADDR	0x42000		// only really care about set 0 which is the commsRecord

#ifdef CONVERT_RF
/**
 * @brief	convert register file format from 1.10.0 to 1.4.3
 *
 * @param	none
 *
 * @return	void
 */
void convertRegisterFileFormat(void)
{
	// this code is for converting the 1.10.0 VBAT format back to the 1.4.3 format.
	// we can detect if we need to do this by doing the conversion if the external
	// flash format is V1, convert the VBAT regs then change the external flash
	// version to V2. COmmented out till someone makes a decision.
	uint8_t vBat[VBAT_MAX_BYTES], *pVBat = (uint8_t*)RFVBAT_BASE;

	// we need to convert the vBat backup
	// this code should be in the bootloader
	memset((void*)&vBat, 0, sizeof(vBat));
	*(uint8_t*)&vBat[VBAT_DATA_INDX_NODE_TASK] = *(uint8_t*)&pVBat[VBAT_DATA_INDX_NODE_TASK];
	*(uint16_t*)&vBat[VBAT_DATA_INDX_FLAGS] = *(uint16_t*)&pVBat[VBAT_DATA_INDX_FLAGS];
	*(uint8_t*)&vBat[VBAT_DATA_INDX_ALARM] = *(uint8_t*)&pVBat[VBAT_DATA_INDX_ALARM];
	*(uint32_t*)&vBat[VBAT_DATA_INDX_UPTIME] = *(uint32_t*)&pVBat[VBAT_DATA_INDX_UPTIME+1];
	*(uint32_t*)&vBat[VBAT_DATA_INDX_TOTAL_ENERGY_USED] = *(uint32_t*)&pVBat[VBAT_DATA_INDX_TOTAL_ENERGY_USED+1];
	*(uint32_t*)&vBat[VBAT_DATA_INDX_COMMS_ENERGY_USED] = *(uint32_t*)&pVBat[VBAT_DATA_INDX_COMMS_ENERGY_USED+1];

	// now calculate the checksum
	for(int i = 0; i < VBAT_DATA_INDX_CHKSUM; i++)
	{
		vBat[VBAT_DATA_INDX_CHKSUM] ^= vBat[i];
	}
	memcpy((void*)pVBat, &vBat, sizeof(vBat));
}
#endif

/**
 * @brief	check if the old comms record is valid, if so copy it from external flash
 *
 * @param	none
 *
 * @return	void
 */
void extFlash_commsRecordUpgrade()
{
	tExtFlashDirectory *gExtFlashDir = (tExtFlashDirectory*)__sample_buffer;
	dataRecord_t dataRecord;

#ifdef CONVERT_RF
	convert_RegisterFileFormat();
#endif

	// read the directory and the COMMS/MEASURE record combo
	if(IS25_ReadBytes(0, (uint8_t*)gExtFlashDir, sizeof(tExtFlashDirectory)) &&
			IS25_ReadBytes(COMMS_RECORD_ADDR, (uint8_t*)&dataRecord, sizeof(dataRecord)))
	{
		// now validate the checksum (I don't think the directory CRC matters if the commsRecord CRC is OK)
		// uint32_t crcCheckDir = crc32_hardware((void *)&gExtFlashDir->allocation, sizeof(gExtFlashDir->allocation));
		uint32_t crcCheckCR = crc32_hardware((void *)&dataRecord, sizeof(dataRecord));
		bool bGoodCrCRC = (gExtFlashDir->allocation.dataSets[0][IS25_MEASURED_DATA].crcCheckSum == crcCheckCR);
		if((bGoodCrCRC) || (
				((dataRecord.commsRecord.params.EnergyRemaining_percent >= 0) &&
				 (dataRecord.commsRecord.params.EnergyRemaining_percent <= 100)) &&
				((dataRecord.commsRecord.params.V_0 >= 0.0) &&
				 (dataRecord.commsRecord.params.V_0 <= V0_MAX))))
		{
			// CRC is good or energy remaining and V_0 are sensible so copy it
			memcpy((void*)&commsRecord, (void*)&dataRecord.commsRecord, sizeof(commsRecord));
			LOG_EVENT(eLOG_PRESERVE_COMMS, LOG_NUM_APP, ERRLOGINFO, "Preserve previous COMMS Record");
			return;
		}
	}

	// ok, lets default to a cleared record
	memset((void*)&commsRecord, 0, sizeof(commsRecord));
}



#ifdef __cplusplus
}
#endif