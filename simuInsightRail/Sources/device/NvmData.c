#ifdef __cplusplus
extern "C" {
#endif

/*
 * NvmData.c
 *
 *  Created on: Oct 12, 2016
 *      Author: ka3112
 */



/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "printgdf.h"

#include "flash.h"

#include "NvmData.h"
#include "Vbat.h"
#include "CRC.h"
#include "extFlash.h"

tNvmData gNvmData;// for data that must be stored over a powercycle

static const tDataSchedule scheduleData = {
	.noOfMeasurementAttempts = 0x00,
	.noOfGoodMeasurements = 0x00,

	.bDoMeasurements = false,
	.bDoAcceleratedScheduling = false,

	.noOfUploadAttempts = 0x00,
	.scheduleStartSecs = 0x00,
};

static const tDataIs25 is25Data = {
	.is25CurrentDatasetNo = FIRST_DATASET,
	.noOfMeasurementDatasetsToUpload = 0x00,
	.noOfCommsDatasetsToUpload = 0x00,
	.measurementDatasetStartIndex = FIRST_DATASET,

	.isReadyForUse = false,
};

static const tSpare spare = {
		.bSpare1 = false,
		.bSpare2 = false,
};

static const tDataComm commDefaults = {
        .url_server_idx = 0,
        .failed_connections_in_a_row = 0,
};

static const tDataGNSS GNSSDefaults = {
	.baud_rate = 0,
	.epoExpiry_secs = 0
};

/*!
 * NvmDataUpdateIfChanged
 *
 * @brief      checks to see if the NvmData has changed from the flash image if it has update the flash copy
 *
 */

void NvmDataUpdateIfChanged(void)
{
    if(memcmp( (uint8_t *)&gNvmData, (uint8_t*) __app_nvdata,  sizeof(tNvmData)) != 0)
    {
    	NvmDataWrite( &gNvmData );
    }
}

/*!
 * NvmDataRead
 *
 * @brief      reads the Nvm data
 *
 * @param      pointer to the destination buffer
 *
 * @returns true when successful, false otherwise
 */

bool NvmDataRead( tNvmData * data)
{
    bool stat = false;

    if (data != NULL)
    {
        memcpy( (uint8_t *) data,(uint8_t*) __app_nvdata,  sizeof(*data)) ;
        // check CRC
		stat = (data->crc32 == crc32_hardware((void *)&data->dat, sizeof(data->dat)));
    }
    return stat;
}

/*!
 * NvmDataWrite
 *
 * @brief      writes the Nvm data
 *
 * @param      pointer to the source buffer
 *
 * @returns true when successful, false otherwise
 */
bool NvmDataWrite( tNvmData * data)
{
    bool stat = false;

    if (data != NULL)
    {
        // calculate crc
        data->crc32 = crc32_hardware((void *)&data->dat, sizeof(data->dat));
        stat = DrvFlashEraseProgramSector((uint32_t*) __app_nvdata, (uint32_t*) data, sizeof(*data));
    }
    return stat;
}


/*!
 * NvmDataDefaults
 *
 * @brief      initialises and writes the Nvm data
 *
 * @param      pointer to the source buffer
 *
 * @returns
 */

void NvmDataDefaults(tNvmData * data)
{
    memset( (uint8_t *) data, 0xff, sizeof(*data));// all to default flash empty state

    memcpy(&data->dat.comm, &commDefaults, sizeof(commDefaults));
    memcpy(&data->dat.schedule, &scheduleData, sizeof(scheduleData));
    memcpy(&data->dat.is25, &is25Data, sizeof(is25Data));
    memcpy(&data->dat.spare, &spare, sizeof(spare));
    memcpy(&data->dat.gnss, &GNSSDefaults, sizeof(GNSSDefaults));
}

static void printGeneralConfig(void)
{
    printf("\nBEGIN GENERAL:\n");
    printf("Unexpected Last Shutdown = %d\n", Vbat_IsFlagSet(VBATRF_FLAG_UNEXPECTED_LAST_SHUTDOWN));
    printf("END GENERAL\n\n");
}

static void printCommConfig(tDataComm * comm)
{
    printf("\nBEGIN COMM:\n");
    printf("Url Server index = %d\n", comm->url_server_idx);
    printf("failed connections in a row = %d\n", comm->failed_connections_in_a_row);
    printf("END COMM\n\n");
}

void NvmPrintData()
{

    printf("\nNvm Data dump:\n\n");
    printGeneralConfig();
    printCommConfig(&gNvmData.dat.comm);
}

#if 0
bool NvmDataUpdateForOTA()
{
	// Reset Schedule Data params
	gNvmData.dat.schedule.bDoMeasurements = false;
	gNvmData.dat.schedule.noOfGoodMeasurements = 0;
	gNvmData.dat.schedule.noOfMeasurementAttempts = 0;
	gNvmData.dat.schedule.noOfUploadAttempts = 0;
	// Rest the IS25 params
	gNvmData.dat.is25.is25CurrentDatasetNo = FIRST_DATASET;
	gNvmData.dat.is25.measurementDatasetStartIndex = FIRST_DATASET;
	gNvmData.dat.is25.noOfCommsDatasetsToUpload = 0;
	gNvmData.dat.is25.noOfMeasurementDatasetsToUpload = 0;

	return NvmDataWrite(&gNvmData);
}
#endif


#ifdef __cplusplus
}
#endif