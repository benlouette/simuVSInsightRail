#ifdef __cplusplus
extern "C" {
#endif

/*
 * configIDEF.h
 *
 *  Created on: 11 dec. 2015
 *      Author: Daniel van der Velde
 *
 * IDEF protocol/service configuration specific to the application
 */

#ifndef SOURCES_CONFIG_CONFIGIDEF_H_
#define SOURCES_CONFIG_CONFIGIDEF_H_

/*
 * Includes
 */
#include "configFeatures.h"
#include "svccommon.pb.h"

/*
 * Macros
 */

/*
 * Select IDEF protocol version
 */
#define CONFIG_IDEF_PROTOCOLVERSION              (0x0200US) // V2.0 in unsigned short

// Enable the Google Protobuf version
//#define PROTOBUF_GPB2

#define PROTOBUF_GPB2_1

#ifdef PROTOBUF_GPB2_1
	SKF_ProtoBufFileVersion_t verifyRightVersionFile = SKF_ProtoBufFileVersion_t_PROTOBUF_FILE_VERSION_GPB_2_1;
#endif

#ifdef PROTOBUF_GPB2
	SKF_ProtoBufFileVersion_t verifyRightVersionFile = SKF_ProtoBufFileVersion_t_PROTOBUF_FILE_VERSION_GPB_2;
#endif


/*
 * IDEF Message Definitions
 */

// Constraints
//#define IDEF_CST_STRING_MAX_LEN         (32)
#define MAX_REPLY_INFO_STRING_SIZE      (80)
/*
 * Define available IDEF services
 * - Data + Meta data is always available (services are requested through this service)
 * - Configuration service
 * - Firmware update service
 * - LVS0217: GNSS Ephemeris data service / This needs to be serviced by the application
 *
 */


#if 1

#include "configIDEFParam.h"

#else
// Definition helper macro, use property ID as upper 2 bytes of the parameter ID
#define IDEFPARAMID_ENUMVALUE(m_propid,m_paramid)    ((m_propid)<<16) + (m_paramid)

/**
 * IDEF_propid_t
 *
 * IDEF Property ID definitions
 */
typedef enum idefpropid_enum {

    IDEFPROPID_DEVICEINFO               = 0x0001,

    IDEFPROPID_FIRMWARE                 = 0x4657,
    IDEFPROPID_FIRMWAREREQUEST          = 0x4658,
    IDEFPROPID_FIRMWAREBLOCK            = 0x4659,

//    IDEFPROPID_MEASCONFIG_ALG0          = 0x0010,
//    IDEFPROPID_MEASDATA_ALG0            = 0x0011,
//    IDEFPROPID_MEASCONFIG_ALG1          = 0x0012,
//    IDEFPROPID_MEASDATA_ALG1            = 0x0013,
} IDEF_propid_t;

/**
 * IDEF_paramid_t
 *
 * IDEF Parameter ID definitions
 */

typedef enum idefparamid_enum {
    // Property: DeviceInfo
    IDEFPARAMID_SWVERSION               = IDEFPARAMID_ENUMVALUE(IDEFPROPID_DEVICEINFO, 0x0001),//!< IDEFPARAMID_SWVERSION
    IDEFPARAMID_HWVERSION               = IDEFPARAMID_ENUMVALUE(IDEFPROPID_DEVICEINFO, 0x0002),//!< IDEFPARAMID_HWVERSION
    IDEFPARAMID_APPID                   = IDEFPARAMID_ENUMVALUE(IDEFPROPID_DEVICEINFO, 0x0003),
    IDEFPARAMID_APPDESC                 = IDEFPARAMID_ENUMVALUE(IDEFPROPID_DEVICEINFO, 0x0004),
    IDEFPARAMID_DEVICETYPE              = IDEFPARAMID_ENUMVALUE(IDEFPROPID_DEVICEINFO, 0x0005),
    IDEFPARAMID_DEVICEDESC              = IDEFPARAMID_ENUMVALUE(IDEFPROPID_DEVICEINFO, 0x0006),

    // next list is a direct copy of the 'Uploaded Parameters' excel sheet (date 25-may-2016) I (george) made, as an example/kickoff
    IDEFPARAMID_Energy_Remaining,
    IDEFPARAMID_Movement_Detect,
    IDEFPARAMID_Temperature_Pcb,
    IDEFPARAMID_Temperature_External,
    IDEFPARAMID_GNSS_Lat_1,
    IDEFPARAMID_GNSS_NS_1,
    IDEFPARAMID_GNSS_Long_1,
    IDEFPARAMID_GNSS_EW_1,
    IDEFPARAMID_GNSS_Speed_To_Rotation_1,
    IDEFPARAMID_GNSS_Course_1,
    IDEFPARAMID_GNSS_Time_To_Fix_1,
    IDEFPARAMID_Is_Speed_Within_Env,
    IDEFPARAMID_Acceleration_Env3,
    IDEFPARAMID_Acceleration_Wheel_Flat_Detect,
    IDEFPARAMID_Acceleration_Raw,
    IDEFPARAMID_GNSS_Speed_To_Rotation_2,
    IDEFPARAMID_GNSS_Time_To_Fix_2,
    IDEFPARAMID_Rotation_Diff,
    IDEFPARAMID_GNSS_Time_Diff,
    IDEFPARAMID_Is_Good_Meas,
    IDEFPARAMID_GNSS_Sat_Id,
    IDEFPARAMID_GNSS_Sat_Snr,


#ifdef CONFIG_PLATFORM_IDEFSVCTESTDATA
    // TODO remove this testdataset
    IDEFTEST_BOOL       = IDEFPARAMID_ENUMVALUE(IDEFPROPID_DEVICEINFO, 0xf000),
    IDEFTEST_BYTE       = IDEFPARAMID_ENUMVALUE(IDEFPROPID_DEVICEINFO, 0xf001),
    IDEFTEST_SBYTE      = IDEFPARAMID_ENUMVALUE(IDEFPROPID_DEVICEINFO, 0xf002),
    IDEFTEST_INT16      = IDEFPARAMID_ENUMVALUE(IDEFPROPID_DEVICEINFO, 0xf003),
    IDEFTEST_UINT16     = IDEFPARAMID_ENUMVALUE(IDEFPROPID_DEVICEINFO, 0xf004),
    IDEFTEST_INT32      = IDEFPARAMID_ENUMVALUE(IDEFPROPID_DEVICEINFO, 0xf005),
    IDEFTEST_UINT32     = IDEFPARAMID_ENUMVALUE(IDEFPROPID_DEVICEINFO, 0xf006),
    IDEFTEST_INT64      = IDEFPARAMID_ENUMVALUE(IDEFPROPID_DEVICEINFO, 0xf007),
    IDEFTEST_UINT64     = IDEFPARAMID_ENUMVALUE(IDEFPROPID_DEVICEINFO, 0xf008),
    IDEFTEST_SINGLE     = IDEFPARAMID_ENUMVALUE(IDEFPROPID_DEVICEINFO, 0xf009),
    IDEFTEST_DOUBLE     = IDEFPARAMID_ENUMVALUE(IDEFPROPID_DEVICEINFO, 0xf00a),
    IDEFTEST_STRING     = IDEFPARAMID_ENUMVALUE(IDEFPROPID_DEVICEINFO, 0xf00b),
    IDEFTEST_DATETIME   = IDEFPARAMID_ENUMVALUE(IDEFPROPID_DEVICEINFO, 0xf00c),
    IDEFTEST_UINT32A    = IDEFPARAMID_ENUMVALUE(IDEFPROPID_DEVICEINFO, 0xf00d),
    IDEFTEST_UINT32ABIG = IDEFPARAMID_ENUMVALUE(IDEFPROPID_DEVICEINFO, 0xf00e),
    // TODO  endremove this testdataset
#endif
    // Property: Firmware
//    IDEFPARAMID_FIRMWARE                = IDEFPARAMID_ENUMVALUE(IDEFPROPID_FIRMWARE, 0x0001), // Heuh?

    // Property: DeviceMode
//    IDEFPARAMID_MODE                    = 200,
//    IDEFPARAMID_MODEPASSKEY             = 201,

    // Property: MeasConfig_ALG0
    // Property: MeasData_ALG0

    // Property: MeasConfig_ALG1
    // Property: MeasData_ALG1

    // Property: AlarmConfig
    // Property: AlarmData

    // Property: DiagConfig_PROC0
    // Property: DiagData_PROC0

    // Property: StateMeasureData
    // Property: StateCommData
    // Property: StateDeviceData

    // Property: DebugConfig
    // Property: DebugData

    // Property: AppConfig
    // Property: AppData

    // Property: GNSSConfig
    // Property: GNSSData

    // Property: TODO IDEF properties and parameters

} IDEF_paramid_t;
#endif

typedef struct {
    uint32_t parameterId;
     uint32_t dataStoreId;
} SvcDataParamValue_t;

typedef struct {
     uint32_t timestampDatastoreId;
     uint32_t numberOfParamValues;
     IDEF_paramid_t * ParamValue_p;
} SvcDataParamValueGroup_t;

typedef bool (* tSvcDataDataCallbackFuncPtr)(uint32_t iter, const SvcDataParamValueGroup_t * dpvg_p);
typedef struct {
     uint32_t numberOfParamGroups;
     SvcDataParamValueGroup_t * ParamValueGroup_p;
     tSvcDataDataCallbackFuncPtr dataData_cb;
} SvcDataData_t;



#ifdef CONFIG_PLATFORM_IDEFSVCTESTDATA
// TODO testdata, remove when tested
#define IDEFMAXPARAMVALUETESTS (5)
extern  const SvcDataParamValueGroup_t  idefParamValueTests[IDEFMAXPARAMVALUETESTS];

#define IDEFMAXDATATESTS (5)
extern const SvcDataData_t idefDataTests[IDEFMAXDATATESTS] ;

#else
#define IDEFMAXPARAMVALUETESTS (0)
extern const SvcDataParamValueGroup_t idefParamValueTests[IDEFMAXPARAMVALUETESTS];

#define IDEFMAXDATATESTS (0)
extern const SvcDataData_t idefDataTests[IDEFMAXDATATESTS] ;

#endif

extern const SvcDataData_t idefDataDataRecords[];

// defines for predefined sets of data to send using with idefDataDataRecords
// beware these values are also the array index values, make sure that they are changed in sync with the array idefDataDataRecords
typedef enum ePredefinedIdefDatasets {
    IDEF_dataMeasureRecord = 0,
    IDEF_dataWaveformEnv3 = 1,
    IDEF_dataWaveformWheelflat = 2,
    IDEF_dataWaveformRaw = 3,
    IDEF_dataCommsRecord = 4,
    IDEF_dataSignon = 5,
    IDEF_dataDeviceSettings = 6,
    IDEF_dataSignonDeviceSettings = 7,
} tPredefinedIdefDatasets;

uint32_t SvcData_IdefIdToDataStoreId(uint32_t IdefId);

#endif /* SOURCES_CONFIG_CONFIGIDEF_H_ */


#ifdef __cplusplus
}
#endif