#ifdef __cplusplus
extern "C" {
#endif

/*
 * IS25_ExtFlash_TG5.h
 *
 *  Created on: Oct 17, 2016
 *      Author: BF1418
 */

#ifndef SOURCES_EXTFLASH_H_
#define SOURCES_EXTFLASH_H_

#include "FreeRTOS.h"
#include "queue.h"
#include "drv_is25.h"



/*****************************************************************************
 * Some useful information:
 *
 * MEASUREMENT SET - collection of data captured during a single measurement cycle
 * BLOCK - a block of captured data which has variable length
 * GATED MEASUREMENT - a measurement record containing information as to why an measurement was not taken
 * MRD - measurement record data; contains size and CRC for collected block 0-3
 *       the mrd block is appended to the measurement record
 *
 * External flash memory map
 *
 * 0x2000000  +--------------------------------------------------+
 *            |        OTA scratch pad                           | space allocated for downloading Combined PMIC & MK24 Images, Modem Image etc
 * 0x1000000  +--------------------------------------------------+
 *            |        Backup image 2                            | space allocated for MK24 firmware download/backup
 * 0xF80000   +--------------------------------------------------+
 *            |        Backup image 1                            | space allocated for MK24 firmware download/backup
 * 0xF00000   +--------------------------------------------------+
 *            |        Backup PMIC                               | space allocated for PMIC firmware download
 * 0xE00000   +--------------------------------------------------+
 *            |        SPARE                                     |
 * 0xB74000   +--------------------------------------------------+
 *            |        Measurement Set 30                        |
 * 0xB13000   +--------------------------------------------------+     +------------------------------------------------------------+
 *            ~                                                  ~   / |     MEASUREMENT SETS 1 - 30                                |
 *            ~        Measurement Sets 3 - 29                   ~  /  +------------------------------------------------------------+
 *            ~                                                  ~ /   |       MEASURE/MRD 4K   block 3; type IS25_MEASURED_DATA    |
 * 0x0D8000   +--------------------------------------------------+/    +------------------------------------------------------------+
 *            |        Measurement Set 2                         |     |       WHEELFLATS 64K   block 2; type IS25_WHEEL_FLAT_DATA  |
 * 0x077000   +--------------------------------------------------+\    +------------------------------------------------------------+
 *            |        Measurement Set 1                         | \   |       VIB/ENV3   64K   block 1; type IS25_VIBRATION_DATA   |
 * 0x016000   +--------------------------------------------------+  \  +------------------------------------------------------------+
 *            |        COMMS/Measurement Record                  |   \ |       RAW        256K  block 0; type IS25_RAW_SAMPLED_DATA |
 * 0x012000   +--------------------------------------------------+     +------------------------------------------------------------+
 *            |        Gated Measurements                        | sufficient space for 7 days, 50 per day of gated measurements
 * 0x006000   +--------------------------------------------------+
 *            |        Temperature Measurements                  | 7 days of temperature measurements
 * 0x002000   +--------------------------------------------------+
 *            |        Management Block                          | JSON manifest data
 * 0x000000   +--------------------------------------------------+
 */
#define EXTFLASH_MAXWAIT_MS    			(130000)   // no flash operation should take longer
#define EXTFLASH_CHIP_ERASE_MAXWAIT_MS	(270000) // Max 270secs (based on 512Mb part)

// have to find out why 30, better to make it dynamic, based on required size of the dataset(s) and the size of the memory chip
#define MAX_NUMBER_OF_DATASETS	(30)
#define FIRST_DATASET			(0)

//! definitions of data types (sources of the data stored in flash)
enum {
	IS25_RAW_SAMPLED_DATA,
	IS25_VIBRATION_DATA,
	IS25_WHEEL_FLAT_DATA,
	IS25_MEASURED_DATA,
	MAX_NO_OF_DATA_RECORD_TYPES
} eSetTypes;					// these are Raw, WF, VIB, measure and COMMS, log

// Management block
#define EXTFLASH_MANAGEMENT_START_ADDR		(0x0)
#define MANAGEMENT_MAX_SIZE_BYTES			(IS25_SECTOR_SIZE_BYTES * 2)

// Temperature measurement specifics.
// Start address for the temperature storage. It is immediately after the
// space reserved for the external flash directory.
#define	EXTFLASH_TEMP_MEAS_START_ADDR		(EXTFLASH_MANAGEMENT_START_ADDR + MANAGEMENT_MAX_SIZE_BYTES)
// Temperature record size.
#define TEMPERATURE_RECORD_SIZE_BYTES		(sizeof(extFlash_NodeTemperatureRecord_t))
#define TEMPERATURE_MEAS_SECTORS			(4)
#define TEMPERATURE_MEAS_SIZE				(IS25_SECTOR_SIZE_BYTES * TEMPERATURE_MEAS_SECTORS)
#define TEMPERATURE_RECORDS_IN_A_SECTOR		(IS25_SECTOR_SIZE_BYTES / TEMPERATURE_RECORD_SIZE_BYTES)
#define TEMPERATURE_MAX_NO_RECORDS			(TEMPERATURE_RECORDS_IN_A_SECTOR * TEMPERATURE_MEAS_SECTORS)
#define	MEMORY_ERASE_STATE					(0xFFFFFFFF)


#define EXTFLASH_GATED_START_ADDR			(EXTFLASH_TEMP_MEAS_START_ADDR + TEMPERATURE_MEAS_SIZE)
#define GATED_SECTORS						(12)
#define GATED_RECORD_SIZE_BYTES				(128)
#define GATED_MAX_SIZE_BYTES				(IS25_SECTOR_SIZE_BYTES * GATED_SECTORS)
#define GATED_MAX_RECORDS					(GATED_MAX_SIZE_BYTES / GATED_RECORD_SIZE_BYTES)

#define EXTFLASH_COMMS_START_ADDR			(EXTFLASH_GATED_START_ADDR + GATED_MAX_SIZE_BYTES)
#define COMMS_SECTORS						(4)
#define COMMS_MAX_SIZE_BYTES				(IS25_SECTOR_SIZE_BYTES * COMMS_SECTORS)

#define EXTFLASH_DATASET_START_ADDR			(EXTFLASH_COMMS_START_ADDR + COMMS_MAX_SIZE_BYTES)

#define EXTFLASH_BACKUP_PMIC_START_ADDR		(0xE00000)
#define BACKUP_PMIC_MAX_SIZE_BYTES			(0x100000)

#define EXTFLASH_BACKUP1_START_ADDR			(0xF00000)
#define EXTFLASH_BACKUP2_START_ADDR			(0xF80000)
#define BACKUP_MAX_SIZE_BYTES				(0x80000)

#define PACKAGE_OTA_SCRATCH_PAD_ADDR		(EXTFLASH_BACKUP2_START_ADDR + BACKUP_MAX_SIZE_BYTES)	// 0x1000000

void taskExtFlash_Init();



/*
 * convert to better better interface with proper timeouts, and parameter passing
 * keep for the moment 'rex' directory structure
 */
#if 0
// this is traditional
typedef enum
{
    extFlashErr_noError = 0,
    extFlashErr_spiFlashDriverFailure,
    extFlashErr_spiFlashDriverNotInitialized,
    extFlashErr_directoryNotOpened,
    extFlashErr_unknownRequest,
} tExtFlashErrCode;

#else
// this is the too lazy to type everything twice for a text string method I learned from 'John'  (so I do not get the blame that I created something cryptic)
#define FOREACH_ERROR(EXT_FLASH_ERROR) \
        EXT_FLASH_ERROR(extFlashErr_noError = 0)   \
        EXT_FLASH_ERROR(extFlashErr_spiFlashDriverFailure)  \
        EXT_FLASH_ERROR(extFlashErr_spiFlashDriverNotInitialized)   \
        EXT_FLASH_ERROR(extFlashErr_flashAddressOutOfRange)  \
        EXT_FLASH_ERROR(extFlashErr_flashMRDRead)  \
        EXT_FLASH_ERROR(extFlashErr_flashMRDWrite)  \
        EXT_FLASH_ERROR(extFlashErr_flashDriverEraseFailure)  \
        EXT_FLASH_ERROR(extFlashErr_flashDriverWriteFailure)  \
        EXT_FLASH_ERROR(extFlashErr_flashDriverWriteCRCFailure)  \
        EXT_FLASH_ERROR(extFlashErr_flashDriverReadFailure)  \
        EXT_FLASH_ERROR(extFlashErr_flashDriverReadCRCFailure)  \
        EXT_FLASH_ERROR(extFlashErr_dataSetIndexOutOfRange)  \
        EXT_FLASH_ERROR(extFlashErr_recordTypeOutOfRange)  \
        EXT_FLASH_ERROR(extFlashErr_crcErrorRead)  \
        EXT_FLASH_ERROR(extFlashErr_queueAccessError)  \
        EXT_FLASH_ERROR(extFlashErr_unknownRequest)  \

#define GENERATE_ENUM(err) err,
#define GENERATE_STRING(errs) #errs,


typedef enum {
    FOREACH_ERROR(GENERATE_ENUM)
} tExtFlashErrCode;

#if INCLUDE_EXT_FLASH_ERROR_STRINGS
static const char *extFlashErrorStrings[] = {
    FOREACH_ERROR(GENERATE_STRING)
};
#endif

#endif

typedef struct
{
    uint32_t baudrate;
} tExtFlashInitReq;

typedef struct
{
    // first the two 'rex' filing system items
    uint32_t dataType;          //! dataType - type of data. Example -  Vibration data
    uint16_t dataSet;            //! dataSet
    // then the more normal stuff
    uint8_t * address;           //! source Address - of the data
    uint32_t length;            //! length - length in bytes of the data
    //uint64_t dataTimestamp;     //! dataTimestamp - timestamp of the data (not used at the moment
} tExtFlashWriteReq;



typedef struct
{
    // first the two 'rex' filing system items
    uint32_t dataType;          //! dataType - type of data. Example -  Vibration data
    uint16_t dataSet;            //! dataSet index
    // then the more normal stuff
    uint8_t * address;           //! destination Address - of the data
    uint32_t length;            //! length - length in bytes of the data
} tExtFlashReadReq;

typedef struct
{
    // first the two 'rex' filing system items
    uint16_t dataSet;            //! dataSet index
} tExtFlashEraseReq;



// placeholder for when we have parameters for a request
typedef  union {
    tExtFlashInitReq    initReq;
    tExtFlashWriteReq   writeReq;
    tExtFlashReadReq    readReq;
    tExtFlashEraseReq   eraseReq;
    // tExtFlashWhateverReq whateverReq;
} tExtFlashReqData;


// reply structures

typedef struct {
    uint32_t calculatedBaudrate;
} tExtFlashInitRep;

typedef struct {
    uint32_t bytesRead;
} tExtFlashReadRep;

// data local to each task, to be allocated once after boot at the start of each task
// the response of the task will put the result of the request here.
typedef struct  {
    QueueHandle_t    EventQueue_extFlashRep;
} tExtFlashHandle;

// Temperature record structure.
#ifdef _MSC_VER
#define PACKED_STRUCT_START __pragma(pack(push, 1))
#define PACKED_STRUCT_END   __pragma(pack(pop))
#else
#define PACKED_STRUCT_START
#define PACKED_STRUCT_END   _CRT_SECURE_NO_WARNINGS
#endif
PACKED_STRUCT_START
typedef struct 
{
	uint32_t crc;                        				//! the data block checksum
	struct 
	{
		float remoteTemp_degC;                         	//! External temp in deg C
		uint32_t timeStamp;                      		//! Timestamp of the data, since epoch.
	} record;
} extFlash_NodeTemperatureRecord_t;
PACKED_STRUCT_END

bool extFlash_InitCommands(tExtFlashHandle * handle);// sets up the handle for all other calls, every task should call this once
int extFlash_WaitReady( tExtFlashHandle * handle, uint32_t maxWaitMs);

int extFlash_format(tExtFlashHandle * handle,  uint32_t maxWaitMs);// erase device, and put empty directory structure

int extFlash_erase(tExtFlashHandle * handle, uint16_t dataSet, uint32_t maxWaitMs);
int extFlash_write(tExtFlashHandle * handle, uint8_t * src_p, uint32_t length,  uint32_t dataType, uint16_t dataSet, uint32_t maxWaitMs);
int extFlash_read(tExtFlashHandle * handle, uint8_t * dst_p, uint32_t maxLength,  uint32_t dataType, uint16_t dataSet, uint32_t maxWaitMs);
const char *extFlash_ErrorString(int errCode);

uint16_t extFlash_getMeasureSetInfo(uint16_t measureSet);
void extFlash_commsRecordUpgrade();

// Cli functions
bool cliExtHelpExtFlash(uint32_t argc, uint8_t * argv[], uint32_t * argi);
bool cliExtFlash( uint32_t args, uint8_t * argv[], uint32_t * argi);
#define EXTFLASH_CLI {"extFlash", "cmd <params>\t\texternal flash commands", cliExtFlash, cliExtHelpExtFlash }

// gated measurement functions
int ExtFlash_gatedMeasurementCount(void);
bool ExtFlash_storeGatedMeasData(void);
bool ExtFlash_fetchGatedMeasData(uint16_t index);
void ExtFlash_eraseGatedMeasData(void);
void ExtFlash_printGatedMeasurement(uint16_t inx, bool display);

bool storeCommsData(void);
bool fetchCommsData(bool copyBoth);
char *gatingReason(uint32_t reason);

#endif /* SOURCES_EXTFLASH_H_ */


#ifdef __cplusplus
}
#endif