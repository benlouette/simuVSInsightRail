#ifdef __cplusplus
extern "C" {
#endif

/*
 * NvmData.h
 *
 *  Created on: Oct 12, 2016
 *      Author: ka3112
 */

#ifndef SOURCES_DEVICE_NVMDATA_H_
#define SOURCES_DEVICE_NVMDATA_H_


#ifdef _MSC_VER
#define PACKED_STRUCT_START __pragma(pack(push, 1))
#define PACKED_STRUCT_END   __pragma(pack(pop))
#else
#define PACKED_STRUCT_START
#define PACKED_STRUCT_END   PACKED_STRUCT_END
	PACKED_STRUCT_START
#endif
// next externals are defined in the linker script !
extern uint32_t  __app_nvdata[],
                 __app_nvdata_size[];

PACKED_STRUCT_START
typedef struct  commDataStruct {
    uint8_t url_server_idx;// the server url index, will be incremented to the next after a failed communication cycle
    uint16_t failed_connections_in_a_row;// keep a count of continuous failed connections in a row, to once in a while increment the connection timeout time
} tDataComm;
PACKED_STRUCT_END
PACKED_STRUCT_START
typedef struct  scheduleDataStruct {
	uint8_t noOfMeasurementAttempts;
	uint8_t noOfGoodMeasurements;
	bool bDoMeasurements;
	bool bDoAcceleratedScheduling;

	uint8_t noOfUploadAttempts;
	uint32_t scheduleStartSecs;			// when the current measurement schedule started - used with schedule upload_repeat
} tDataSchedule;
PACKED_STRUCT_END
PACKED_STRUCT_START
typedef struct  spareStruct {
	bool bSpare1;
	bool bSpare2;
} tSpare;
PACKED_STRUCT_END
PACKED_STRUCT_START
typedef struct  is25DataStruct {
	uint8_t is25CurrentDatasetNo;// appears to be the next (empty) dataset where new measurement data can be stored
	uint8_t noOfMeasurementDatasetsToUpload;// ??
	uint8_t noOfCommsDatasetsToUpload;// there is only one ? so it can be zero or 1 ?
	uint8_t measurementDatasetStartIndex;// starts at 1 (zero is the comms record 'dataset'),

	bool isReadyForUse;		// tells the system that the is25 is ready for use
} tDataIs25;
PACKED_STRUCT_END
PACKED_STRUCT_START
typedef struct  GNSSDataStruct
{
	uint32_t baud_rate;			// last used GNSS baud rate
	uint32_t epoExpiry_secs;	// UTC expiry time for the Ephemeris data
	uint32_t lastPO;            // last time gnss was powered off
} tDataGNSS;
PACKED_STRUCT_END
PACKED_STRUCT_START
// nvmData stuff
struct  nvmDataStruct {
    uint32_t crc32; // 32 bit crc whole data struct
    bool  spareb;
    //bool  dirty; // when true, ram copy has changed, NV copy must be updated

    struct  dataStruct {

        // some general  parameters
        tDataComm comm;

        tDataSchedule schedule;
        tDataIs25 is25;

        tSpare spare;

        tDataGNSS gnss;
        // etc.


    } dat;
};
PACKED_STRUCT_END

typedef struct nvmDataStruct tNvmData;

extern tNvmData gNvmData;

void NvmDataUpdateIfChanged(void);
bool NvmDataRead( tNvmData * data);
bool NvmDataWrite( tNvmData * data);
void NvmDataDefaults( tNvmData * data);
bool NvmDataUpdateForOTA();


void NvmPrintData();


#endif /* SOURCES_DEVICE_NVMDATA_H_ */


#ifdef __cplusplus
}
#endif