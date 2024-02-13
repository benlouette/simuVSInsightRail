#ifdef __cplusplus
extern "C" {
#endif

/*
 * temperature.c
 *
 *  Created on: July 31, 2019
 *      Created by John McRobert
 */
#include <stdio.h>
#include <math.h>
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
#include "rtc.h"
#include "Measurement.h"
#include "device.h"
#include "json.h"
#include "linker.h"
#include "temperature.h"

// Temperature Measurement related Api's
// CLI Api's / helper functions.
static bool cliTempMeas_PrintRecords(uint32_t args, uint8_t * argv[], uint32_t * argi);
static bool cliTempMeas_EraseAllRecords(uint32_t args, uint8_t * argv[], uint32_t * argi);
static bool cliTempMeas_WriteRecords(uint32_t args, uint8_t * argv[], uint32_t * argi);

static bool TemperatureReadCheck(uint16_t startIndx, uint16_t readRecCount);
extern void LogAddressOverlapEvent(const char *pFunName, uint32_t startAddress, uint32_t numBytes);

static struct
{
	uint16_t totalTemperatureRecords;
	uint8_t* pTempRecBuf;
} tempRecStatus = {
		.totalTemperatureRecords = 0,
		.pTempRecBuf = NULL,
};

// Temperature Measurement CLI commands.
struct cliSubCmd tempMeasStorageSubCmds[] =
{
        {"erase", 	cliTempMeas_EraseAllRecords},
        {"write", 	cliTempMeas_WriteRecords},
        {"print", 	cliTempMeas_PrintRecords},
};

static const char extFlashTempHelp[] = {
        "TempMeas subcommands:\r\n"
        " erase <1 or 2> \terase the temperature storage sectors.\r\n"
        " write <NumRecords> \twrite entered no.of records.\r\n"
        " print <NumRecords> \tprints temperature records.\r\n"
};

////////////////////////////////////////////////////////////////////////////////
// Temperature Storage
////////////////////////////////////////////////////////////////////////////////

/*
 * Temperature_WriteRecord
 *
 * @brief	Interface to store the external temperature, its associated timestamp
 *
 * @param	remoteTemp - External temperature in deg C.
 * @param	timestamp  - Temperature measurement timestamp since epoch.
 *
 * @return  true - if successful , false otherwise.
 */
bool Temperature_WriteRecord(float remoteTemp, uint32_t timestamp)
{
	bool retval = false;
	uint16_t offset = 0;
	extFlash_NodeTemperatureRecord_t nodeTempData;

	if(Temperature_GetWrIndx(&offset))
	{
		nodeTempData.record.remoteTemp_degC = remoteTemp;
		nodeTempData.record.timeStamp = timestamp;
		nodeTempData.crc = crc32_hardware((void *)&nodeTempData.record, sizeof(nodeTempData.record));
		retval = IS25_WriteBytes( EXTFLASH_TEMP_MEAS_START_ADDR + offset, (uint8_t*) &nodeTempData, TEMPERATURE_RECORD_SIZE_BYTES);
	}
	return retval;
}

/*
 * Temperature_EraseAllRecords
 *
 * @brief	Erases both the sectors allocated for the temperature storage.
 *
 * @return  true - if successful , false otherwise.
 */
bool Temperature_EraseAllRecords()
{
   	// no need to range check
	return IS25_PerformSectorErase(EXTFLASH_TEMP_MEAS_START_ADDR, TEMPERATURE_MEAS_SIZE);
}

/*
 * Temperature_ReadRecords
 *
 * @brief	Reads the temperature records and stores them into the passed buffer
 * 			pDstBuf.
 * 			PRECONDITION: Temperature_RecordCount() must be called before
 *			records can be read.
 *
 * @param 	startIndx - Indicates the temperature record index to start reading from.
 * @param   readRecCount - Number of records to read.
 * @param   pDstBuf - Pointer to the destination buffer where the temperature records
 * 					  will be stored.
 *
 * @return  true - if successful , false otherwise.
 */
bool Temperature_ReadRecords(uint16_t startIndx, uint16_t readRecCount, uint8_t *pDstBuf)
{
	bool retval = false;
	extFlash_NodeTemperatureRecord_t tempRec;
	const uint32_t recSize_Bytes = sizeof(tempRec.record);
	const uint32_t offset = (startIndx * recSize_Bytes);

	if(TemperatureReadCheck(startIndx, readRecCount) && (pDstBuf != NULL))
	{
		memcpy((uint8_t *)pDstBuf, (uint8_t *)tempRecStatus.pTempRecBuf + offset, recSize_Bytes * readRecCount);
		retval = true;
	}

	return retval;
}

/*
 * Temperature_RecordCount
 *
 * @brief	Retrieves the temperature records and stores them into the
 * 			pTempRecBuf. This function must be called before reading the
 * 			temperature records.
 *
 * @param 	pNumRecords - pointer to the record count, stores the no.of records found.
 *
 * @return  true - if successful , false otherwise.
 */
bool Temperature_RecordCount(uint16_t *pNumRecords)
{
	bool retval = false;
	tempRecStatus.pTempRecBuf = &(((uint8_t*)g_pSampleBuffer)[TEMPERATURE_MEAS_SIZE + 10]);
	// Check how many records are available.
	retval = Temperature_GetRecordCount(&tempRecStatus.totalTemperatureRecords, tempRecStatus.pTempRecBuf);
	if(pNumRecords != NULL)
	{
		*pNumRecords = tempRecStatus.totalTemperatureRecords;
	}
	return retval;
}

/*
 * TemperatureReadCheck
 *
 * @brief	Performs a precondition check before the temperature records
 *			can be read.
 *
 * @param 	startIndx - Indicates the temperature record index to start reading from.
 * @param   readRecCount - Number of records to read.
 *
 * @return  true - if successful , false otherwise.
 */
static bool TemperatureReadCheck(uint16_t startIndx, uint16_t readRecCount)
{
	bool retval = false;

	if(tempRecStatus.pTempRecBuf == NULL)
	{
		printf("\nTemp. rec buffer pointer not init, call Temperature_RecordCount() first.\n");
	}
	else if((tempRecStatus.pTempRecBuf != NULL) &&
		    ((startIndx + readRecCount) <= tempRecStatus.totalTemperatureRecords))
	{
		retval = true;
	}
	else
	{
		LOG_DBG(LOG_LEVEL_APP,"Incorrect params, TotalRec:%d, StartIndx:%d, NumRecToRead:%d\n",
				tempRecStatus.totalTemperatureRecords, startIndx, readRecCount);
	}
	return retval;
}

/*
 * Temperature_GetRecordCount
 *
 * @brief	Counts the no.of temperature records available. A maximum of 681
 * 			records can be stored across 2 sectors.
 *			IMP NOTE: When using g_pSampleBuffer as the destination buffer,
 *			the first 8k bytes should not be used as this is where the entire
 *			temperature records with CRCs are retrieved.
 *
 * @param 	pRecordCount - pointer to the record count.
 * @param	pDstBuf	 	 - pointer to the destination buffer where the temperature
 * 			               records are to be stored. A NULL pointer should be passed
 * 			               if the records are NOT to be stored.
 *
 * @return  true - if successful , false otherwise.
 */
bool Temperature_GetRecordCount(uint16_t *pRecordCount, uint8_t *pDstBuf)
{
	bool retval = true;
	int offset = 0;
	uint16_t numRecords = 0;
	uint8_t *pBuf = (uint8_t*)g_pSampleBuffer;
	// Copy only temperature and its timestamp from the records.
	const uint8_t bytesToCopy = sizeof(((extFlash_NodeTemperatureRecord_t*)pBuf)->record);

	// Add a check to ensure that the pDstBuf is not overlapping the sample buffer
	// space used for retrieving the temperature records.
	if(pDstBuf != NULL)
	{
		if(((uint32_t)pDstBuf > ((uint32_t)(pBuf + TEMPERATURE_MEAS_SIZE))) ||
			((uint32_t)pDstBuf < ((uint32_t)pBuf)))
		{
			retval = true;
		}
		else
		{
			printf("\n*****ERROR:The Temp Dest buffer address overlaps the retrieved data.****\n");
			retval = false;
		}
	}
	if(retval)
	{
		retval = IS25_ReadBytes(EXTFLASH_TEMP_MEAS_START_ADDR, pBuf, TEMPERATURE_MEAS_SIZE);
	}
	// Scan through entire temperature storage allocation.
	for (int indx = 0; ((indx < TEMPERATURE_MAX_NO_RECORDS) && (retval == true)); indx++, offset += TEMPERATURE_RECORD_SIZE_BYTES)
	{
		if((indx % TEMPERATURE_RECORDS_IN_A_SECTOR) == 0)
		{
			// adjust the offset at the end of every sector
			offset = (indx / TEMPERATURE_RECORDS_IN_A_SECTOR) * IS25_SECTOR_SIZE_BYTES;
		}
		// Check for erase state.
		if (((extFlash_NodeTemperatureRecord_t*)(pBuf + offset))->crc != MEMORY_ERASE_STATE)
		{
			numRecords++;
			// If temperature records are to be copied.
			if(pDstBuf != NULL)
			{
				extFlash_NodeTemperatureRecord_t *tempMeas = ((extFlash_NodeTemperatureRecord_t*)(pBuf + offset));
				memcpy(pDstBuf, (uint8_t*)&tempMeas->record.remoteTemp_degC, bytesToCopy);
				uint32_t recordCrc = crc32_hardware((void *)pDstBuf, bytesToCopy);
				if(recordCrc != tempMeas->crc)
				{
					retval = false;
					LOG_DBG(LOG_LEVEL_APP, "\n***TEMP STORAGE ERROR: CRC mismatch !!!***\n");
				}
				pDstBuf += bytesToCopy;
			}
		}
	}

	if(pRecordCount != NULL)
	{
		*pRecordCount = numRecords;
	}

	return retval;
}

/*
 * Temperature_GetWrIndx
 *
 * @brief	Parses the temperature allocation to find the next write location.
 *
 * @param 	pOffset - pointer to the next free location. This is the offset from
 * 			the base address.
 *
 * @return  true - if successful , false otherwise.
 */
bool Temperature_GetWrIndx(uint16_t *pOffset)
{
	int offset = 0;
	uint8_t *pBuf = (uint8_t*)g_pSampleBuffer;

	if((pOffset != NULL) && IS25_ReadBytes(EXTFLASH_TEMP_MEAS_START_ADDR, pBuf, TEMPERATURE_MEAS_SIZE))
	{
		for (int indx = 0; (indx < TEMPERATURE_MAX_NO_RECORDS); indx++, offset += TEMPERATURE_RECORD_SIZE_BYTES)
		{
			if((indx % TEMPERATURE_RECORDS_IN_A_SECTOR) == 0)
			{
				// adjust the offset at the end of every sector
				offset = (indx / TEMPERATURE_RECORDS_IN_A_SECTOR) * IS25_SECTOR_SIZE_BYTES;
			}
			// Check for erase state.
			if (((extFlash_NodeTemperatureRecord_t*)(pBuf + offset))->crc == MEMORY_ERASE_STATE)
			{
				*pOffset = offset;
				return true;
			}
		}
	}
	return false;
}

//------------------------------------------------------------------------------
// CLI for Temperature measurement
//------------------------------------------------------------------------------

static bool cliTempMeas_EraseAllRecords(uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	uint32_t nbaseAddr = EXTFLASH_TEMP_MEAS_START_ADDR;
	uint32_t numBytes = TEMPERATURE_MEAS_SIZE;

	printf("\nErase from location:0x%x, MaxTempRecords:%d[Records], SingleRecordSize_Bytes:%d\n",
			nbaseAddr,
			TEMPERATURE_MAX_NO_RECORDS,
			TEMPERATURE_RECORD_SIZE_BYTES);
	if(argi[0] > TEMPERATURE_MEAS_SECTORS)
	{
		argi[0] = TEMPERATURE_MEAS_SECTORS;
		printf("\nMax %d sectors allocated for temperature storage.\n", TEMPERATURE_MEAS_SECTORS);
	}

	numBytes = IS25_SECTOR_SIZE_BYTES * argi[0];
	// if the erase op. is going to overlap IMAGE area, then abort
	if((nbaseAddr + numBytes) > EXTFLASH_GATED_START_ADDR)
	{
		LogAddressOverlapEvent(__func__, nbaseAddr, numBytes);
		return false;
	}

	bool retval = IS25_PerformSectorErase(nbaseAddr, numBytes);
	if(retval)
	{
		printf("\nErased :%d sectors\n", argi[0]);
	}
	return retval;
}

static bool cliTempMeas_WriteRecords(uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	extern RTC_Type * const g_rtcBase[RTC_INSTANCE_COUNT];
	bool retVal = true;
	uint32_t numRecords = 0;
	uint32_t secs = (argi[1] & 1) ? RTC_HAL_GetSecsReg(g_rtcBase[RTC_IDX]) : 1000000;
	uint16_t recCount = 0;

	if(args >= 1)
	{
		Temperature_GetRecordCount(&recCount, NULL);
		if(argi[0] <= ((TEMPERATURE_MAX_NO_RECORDS) - recCount))
		{
			numRecords = argi[0];
		}
		else
		{
			printf("\nMax possible records:%d\n", (TEMPERATURE_MAX_NO_RECORDS) - recCount);
		}
	}
	printf("\nNum Rec to Write:%d\n", numRecords);
	for (int i = 1; (i <= numRecords) && retVal; i++)
	{
		printf("\nWriting Temp:%d deg C\n", i);
		retVal = Temperature_WriteRecord((argi[1] & 4) ? NAN : i + 0.5f, secs + i);
	}
	return retVal;
}

static bool cliTempMeas_PrintRecords(uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	uint16_t records = 0, nRecords;
	uint8_t *pBuf = &(((uint8_t *)g_pSampleBuffer)[TEMPERATURE_MEAS_SIZE + 10]);
	extFlash_NodeTemperatureRecord_t *tempMeas;

	memset((uint8_t*)g_pSampleBuffer, 0 , TEMPERATURE_MEAS_SIZE);

	if(false == Temperature_GetRecordCount(&nRecords, pBuf))
	{
		printf("\nError getting record count\n");
		return false;
	}
	printf("\nRecords Found:%d\n", nRecords);

	if(args == 0)
	{
		records = nRecords;
	}
	else if (args == 1)
	{
		records = (argi[0] > nRecords) ? nRecords : argi[0];
	}

	if(records)
	{
		printf("\nPrinting Temp Measurements Records...\n");
	}

	for(int i = 0; i < records ; i++ )
	{
		printf("Record: %3d => Temp:%f, TimeStamp:%s\n",
				i+1,
				*((float*)pBuf),
				RtcUTCToString(*((uint32_t*)(pBuf + sizeof(tempMeas->record.remoteTemp_degC)))));
		pBuf += sizeof(tempMeas->record);
	}

	return true;
}

bool cliTempMeas( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    bool rc_ok = true;

    if (args)
    {
    	// Init the ext flash interface.
		rc_ok = cliSubcommand(args,argv,argi, tempMeasStorageSubCmds,
							 sizeof(tempMeasStorageSubCmds)/sizeof(*tempMeasStorageSubCmds));
		if (rc_ok == false)
		{
			printf("\nTempMeasStorage recorded error\n");
		}
    }
    else
    {
        printf("Try typing: help tempMeas\n");
    }

    return rc_ok;
}

bool cliTempMeasHelp(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
    printf(extFlashTempHelp);
    return true;
}


#ifdef __cplusplus
}
#endif