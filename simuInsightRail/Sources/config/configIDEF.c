#ifdef __cplusplus
extern "C" {
#endif

/*
 * configIDEF.c
 *
 *  Created on: 29 mrt. 2016
 *      Author: Daniel van der Velde
 */

/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>

#include "DataDef.h"
#include "configData.h"
#include "svcData.h"
#include "configIDEF.h"

/*
 * Macro
 */

/*
 * Types
 */
#if 0
typedef struct {
    IDEF_paramid_t         id;


} IDEF_param_t;
#endif
/*
 * Data
 */

// table which links idef parameter ID's to datastore ID's

static const SvcDataParamValue_t svcIdefDataStoreId[] = {

#ifdef CONFIG_PLATFORM_IDEFSVCTESTDATA
        { IDEFTEST_BOOL , SVCDATATEST_BOOL},
        { IDEFTEST_BYTE , SVCDATATEST_BYTE},
        { IDEFTEST_SBYTE , SVCDATATEST_SBYTE},
        { IDEFTEST_INT16 , SVCDATATEST_INT16},
        { IDEFTEST_UINT16 , SVCDATATEST_UINT16},
        { IDEFTEST_INT32 , SVCDATATEST_INT32},
        { IDEFTEST_UINT32 , SVCDATATEST_UINT32},
        { IDEFTEST_INT64 , SVCDATATEST_INT64},
        { IDEFTEST_UINT64 , SVCDATATEST_UINT64},
        { IDEFTEST_SINGLE , SVCDATATEST_SINGLE},
        { IDEFTEST_DOUBLE , SVCDATATEST_DOUBLE},
        { IDEFTEST_STRING , SVCDATATEST_STRING},
        { IDEFTEST_UINT32A , SVCDATATEST_UINT32A},
        { IDEFTEST_UINT32ABIG , SVCDATATEST_UINT32ABIG},
        { IDEFTEST_DATETIME , SVCDATATEST_DATETIME},
#endif


        {   IDEFPARAMID_DEVICE_TYPE                                     , DR_Device_Type},
        {   IDEFPARAMID_DEVICE_DESCRIPTION                              , DR_Device_Description},
        {   IDEFPARAMID_DEVICE_SERIAL_NUMBER                            , DR_Device_Serial_Number},
        {   IDEFPARAMID_HARDWARE_VERSION                                , DR_Hardware_Version},
        {   IDEFPARAMID_FIRMWARE_VERSION                                , DR_Firmware_Version},
        {   IDEFPARAMID_UP_TIME                                         , CR_Up_Time},
        {   IDEFPARAMID_IS_SELF_TEST_SUCCESSFUL                         , CR_Is_Self_Test_Successful},
        {   IDEFPARAMID_STATUS_SELF_TEST                                , CR_Status_Self_Test},
        {   IDEFPARAMID_URL_SERVER_1                                    , SR_Url_Server_1 },
        {   IDEFPARAMID_URL_SERVER_2                                    , SR_Url_Server_2},
        {   IDEFPARAMID_URL_SERVER_3                                    , SR_Url_Server_3},
        {   IDEFPARAMID_URL_SERVER_4                                    , SR_Url_Server_4},
        {   IDEFPARAMID_IS_UPLOAD_OFFSET_ENABLED                        , SR_Is_Upload_Offset_Enabled},
        {   IDEFPARAMID_ENERGY_REMAINING                                , CR_Energy_Remaining},
        {   IDEFPARAMID_TEMPERATURE_ALARM_UPPER_LIMIT                   , SR_Temperature_Alarm_Upper_Limit},
        {   IDEFPARAMID_TEMPERATURE_ALARM_LOWER_LIMIT                   , SR_Temperature_Alarm_Lower_Limit},
        {   IDEFPARAMID_TEMPERATURE_PCB                                 , MR_Temperature_Pcb},
        {   IDEFPARAMID_TEMPERATURE_EXTERNAL                            , MR_Temperature_External},
        {   IDEFPARAMID_SAMPLE_RATE_RAW                                 , SR_Sample_Rate_Raw},
        {   IDEFPARAMID_SAMPLES_RAW                                     , SR_Samples_Raw},
        {   IDEFPARAMID_SCALING_RAW                                     , SR_Scaling_Raw},
        {   IDEFPARAMID_SAMPLE_RATE_BEARING                             , SR_Sample_Rate_Bearing},
        {   IDEFPARAMID_SAMPLES_BEARING                                 , SR_Samples_Bearing},
        {   IDEFPARAMID_SCALING_BEARING                                 , SR_Scaling_Bearing},
        {   IDEFPARAMID_IS_RAW_ACCELERATION_ENABLED                     , SR_Is_Raw_Acceleration_Enabled},
        {   IDEFPARAMID_ACCELERATION_ENV3                               , MR_Acceleration_Env3},
        {   IDEFPARAMID_ACCELERATION_RAW                                , MR_Acceleration_Raw},
        {   IDEFPARAMID_ACCELERATION_Y                                  , CR_Acceleration_Y},
        {   IDEFPARAMID_ACCELERATION_X                                  , CR_Acceleration_X},
        {   IDEFPARAMID_ACCELERATION_Z                                  , CR_Acceleration_Z},
        {   IDEFPARAMID_IS_VERBOSE_LOGGING_ENABLED                      , SR_Is_Verbose_Logging_Enabled},
        {   IDEFPARAMID_GNSS_LAT_1                                      , MR_GNSS_Lat_1},
        {   IDEFPARAMID_GNSS_NS_1                                       , MR_GNSS_NS_1},
        {   IDEFPARAMID_GNSS_LONG_1                                     , MR_GNSS_Long_1},
        {   IDEFPARAMID_GNSS_EW_1                                       , MR_GNSS_EW_1},
        {   IDEFPARAMID_GNSS_SPEED_TO_ROTATION_1                        , MR_GNSS_Speed_To_Rotation_1},
        {   IDEFPARAMID_GNSS_COURSE_1                                   , MR_GNSS_Course_1},
        {   IDEFPARAMID_GNSS_TIME_TO_FIX_1                              , MR_GNSS_Time_To_Fix_1},
        {   IDEFPARAMID_GNSS_TIME_DIFF                                  , MR_GNSS_Time_Diff},
        {   IDEFPARAMID_GNSS_SPEED_TO_ROTATION_2                        , MR_GNSS_Speed_To_Rotation_2},
        {   IDEFPARAMID_GNSS_SAT_ID                                     , MR_GNSS_Sat_Id},
        {   IDEFPARAMID_GNSS_SAT_SNR                                    , MR_GNSS_Sat_Snr},
        {   IDEFPARAMID_ROTATION_DIFF                                   , MR_Rotation_Diff},
        {   IDEFPARAMID_IS_MOVE_DETECT_MEAS                             , MR_Is_Move_Detect_Meas},
        {   IDEFPARAMID_IS_MOVING_GATING_ENABLED                        , SR_Is_Moving_Gating_Enabled},
        {   IDEFPARAMID_COM_TIMESTAMP                                   , CR_Com_timestamp},
        {   IDEFPARAMID_WAKEUP_REASON                                   , CR_Wakeup_Reason},
        {   IDEFPARAMID_COM_SIGNAL_QUALITY                              , CR_Com_Signal_Quality},
        {   IDEFPARAMID_IS_COM_SUCCESSFUL                          		, CR_Is_Last_Com_Successful},
        {   IDEFPARAMID_CONFIRM_UPLOAD                                  , SR_confirm_upload},
        {   IDEFPARAMID_IS_MOVE_DETECT_COM                              , CR_Is_Move_Detect_Com},
        {   IDEFPARAMID_HUMIDITY                                        , CR_Humidity},
        {   IDEFPARAMID_SENSOR_LOCATION_ANGLE                           , AR_Sensor_Location_Angle},
        {   IDEFPARAMID_SENSOR_ORIENTATION_ANGLE                        , AR_Sensor_Orientation_Angle},
        {   IDEFPARAMID_GNSS_LAT_COM                                    , CR_GNSS_Lat_Com},
        {   IDEFPARAMID_GNSS_NS_COM                                     , CR_GNSS_NS_Com},
        {   IDEFPARAMID_GNSS_LONG_COM                                   , CR_GNSS_Long_Com},
        {   IDEFPARAMID_GNSS_EW_COM                                     , CR_GNSS_EW_Com},
        {   IDEFPARAMID_GNSS_SPEED_TO_ROTATION_COM                      , CR_GNSS_Speed_To_Rotation_Com},
        {   IDEFPARAMID_TIME_LAST_WAKE                                  , CR_Time_Last_Wake},
        {   IDEFPARAMID_TIME_ELAP_REGISTRATION                          , CR_Time_Elap_Registration},
        {   IDEFPARAMID_TIME_ELAP_TCP_CONNECT                           , CR_Time_Elap_TCP_Connect},
        {   IDEFPARAMID_TIME_ELAP_TCP_DISCONNECT                        , CR_Time_Elap_TCP_Disconnect},
        {   IDEFPARAMID_TIME_ELAP_MODEM_ON                              , CR_Time_Elap_Modem_On},
        {   IDEFPARAMID_V_0                                             , CR_V_0},
        {   IDEFPARAMID_V_1                                             , CR_V_1},
        {   IDEFPARAMID_V_2                                             , CR_V_2},
        {   IDEFPARAMID_E_GNSS_CYCLE                                    , CR_E_GNSS_Cycle},
        {   IDEFPARAMID_E_MODEM_CYCLE                                   , CR_E_Modem_Cycle},
        {   IDEFPARAMID_E_PREVIOUS_WAKE_CYCLE                           , CR_E_Previous_Wake_Cycle},
        {   IDEFPARAMID_SCHEDULE_ID                                     , SR_Schedule_ID},
        {   IDEFPARAMID_ACCELERATION_WHEEL_FLAT_DETECT                  , MR_Acceleration_Wheel_Flat_Detect},
        {   IDEFPARAMID_IS_GOOD_SPEED_DIFF                              , MR_Is_Good_Speed_Diff},
        {   IDEFPARAMID_MIN_HZ                                          , SR_Min_Hz},
        {   IDEFPARAMID_MAX_HZ                                          , SR_Max_Hz},
        {   IDEFPARAMID_ALLOWED_ROTATION_CHANGE                         , SR_Allowed_Rotation_Change},
        {   IDEFPARAMID_IS_SPEED_WITHIN_ENV                             , MR_Is_Speed_Within_Env},
        {   IDEFPARAMID_SAMPLE_RATE_WHEEL_FLAT                          , SR_Sample_Rate_Wheel_Flat},
        {   IDEFPARAMID_SAMPLES_WHEEL_FLAT                              , SR_Samples_Wheel_Flat},
        {   IDEFPARAMID_SCALING_WHEEL_FLAT                              , SR_Scaling_Wheel_Flat},
        {   IDEFPARAMID_TIME_START_MEAS                                 , SR_Time_Start_Meas},
        {   IDEFPARAMID_RETRY_INTERVAL_MEAS                             , SR_Retry_Interval_Meas},
        {   IDEFPARAMID_MAX_DAILY_MEAS_RETRIES                          , SR_Max_Daily_Meas_Retries},
        {   IDEFPARAMID_REQUIRED_DAILY_MEAS                             , SR_Required_Daily_Meas},
        {   IDEFPARAMID_UPLOAD_TIME                                     , SR_Upload_Time},
        {   IDEFPARAMID_RETRY_INTERVAL_UPLOAD                           , SR_Retry_Interval_Upload},
        {   IDEFPARAMID_MAX_UPLOAD_RETRIES                              , SR_Max_Upload_Retries},
        {   IDEFPARAMID_UPLOAD_REPEAT                                   , SR_Upload_repeat},
        {   IDEFPARAMID_NUMBER_OF_UPLOAD                                , SR_Number_Of_Upload},
        {   IDEFPARAMID_ACCELERATION_THRESHOLD_MOVEMENT_DETECT          , SR_Acceleration_Threshold_Movement_Detect},
        {   IDEFPARAMID_ACCELERATION_AVG_TIME_MOVEMENT_DETECT           , SR_Acceleration_Avg_Time_Movement_Detect},
        {   IDEFPARAMID_IS_POWER_ON_FAILSAFE_ENABLED                    , SR_Is_Power_On_Failsafe_Enabled},
        {   IDEFPARAMID_TRAIN_OPERATOR                                  , AR_Train_Operator},
        {   IDEFPARAMID_TRAIN_FLEET                                     , AR_Train_Fleet},
        {   IDEFPARAMID_TRAIN_ID                                        , AR_Train_ID},
        {   IDEFPARAMID_VEHICLE_ID                                      , AR_Vehicle_ID},
        {   IDEFPARAMID_VEHICLE_NICKNAME                                , AR_Vehicle_Nickname},
        {   IDEFPARAMID_BOGIE_SERIAL_NUMBER                             , AR_Bogie_Serial_Number},
        {   IDEFPARAMID_WHEELSET_NUMBER                                 , AR_Wheelset_Number},
        {   IDEFPARAMID_WHEELSET_SERIAL_NUMBER                          , AR_Wheelset_Serial_Number},
        {   IDEFPARAMID_IS_WHEELSET_DRIVEN                              , AR_Is_Wheelset_Driven},
        {   IDEFPARAMID_WHEEL_SERIAL_NUMBER                             , AR_Wheel_Serial_Number},
        {   IDEFPARAMID_WHEEL_SIDE                                      , AR_Wheel_Side},
        {   IDEFPARAMID_WHEEL_DIAMETER                                  , AR_Wheel_Diameter},
        {   IDEFPARAMID_AXLEBOX_SERIAL_NUMBER                           , AR_Axlebox_Serial_Number},
        {   IDEFPARAMID_BEARING_BRAND_MODEL_NUMBER                      , AR_Bearing_Brand_Model_Number},
        {   IDEFPARAMID_BEARING_SERIAL_NUMBER                           , AR_Bearing_Serial_Number},
        {   IDEFPARAMID_IMEI                                            , CR_IMEI},
        {   IDEFPARAMID_ICCID                                           , CR_ICCID},
        {   IDEFPARAMID_TRAIN_NAME                                      , AR_Train_Name},
        {   IDEFPARAMID_BOGIE_NUMBER_IN_WAGON                           , AR_Bogie_Number_In_Wagon},

};




#ifdef CONFIG_PLATFORM_IDEFSVCTESTDATA
// example of how the data for one pubData message should be bundled
//what IDEF param ID should link to what datastore item for the test message
static const IDEF_paramid_t idefParamTest0_List[] = {
         IDEFTEST_UINT32 ,
};
static const IDEF_paramid_t idefParamTest1_List[] = {
         IDEFTEST_UINT32A ,
};
static const IDEF_paramid_t idefParamTest2_List[] = {
         IDEFTEST_BOOL  ,
         IDEFTEST_BYTE  ,
         IDEFTEST_SBYTE  ,
         IDEFTEST_INT16  ,
         IDEFTEST_UINT16  ,
         IDEFTEST_INT32  ,
         IDEFTEST_UINT32  ,
         IDEFTEST_INT64  ,
         IDEFTEST_UINT64  ,
         IDEFTEST_SINGLE  ,
         IDEFTEST_DOUBLE  ,
         IDEFTEST_STRING  ,

};
static const IDEF_paramid_t idefParamTest3_List[] = {
         IDEFTEST_UINT32ABIG
};

static const IDEF_paramid_t idefParamTest4_List[] = {
        IDEFTEST_SINGLE
};





/*
 * we have to typecast '(IDEF_paramid_t *)' when the initialiser is declared 'const', to prevent compiler warnings !
 */

const  SvcDataParamValueGroup_t  idefParamValueTests[IDEFMAXPARAMVALUETESTS]= {
        { SVCDATATEST_TIMESTAMPA, sizeof(idefParamTest0_List)/sizeof(*idefParamTest0_List),  (IDEF_paramid_t *) idefParamTest0_List },
        { SVCDATATEST_TIMESTAMPB, sizeof(idefParamTest1_List)/sizeof(*idefParamTest1_List),  (IDEF_paramid_t *) idefParamTest1_List },
        { SVCDATATEST_TIMESTAMPC, sizeof(idefParamTest2_List)/sizeof(*idefParamTest2_List),  (IDEF_paramid_t *) idefParamTest2_List },
        { SVCDATATEST_TIMESTAMPC, sizeof(idefParamTest3_List)/sizeof(*idefParamTest3_List),  (IDEF_paramid_t *) idefParamTest3_List },
        { SVCDATATEST_TIMESTAMPC, sizeof(idefParamTest4_List)/sizeof(*idefParamTest4_List),  (IDEF_paramid_t *) idefParamTest4_List },
};

extern bool svcdata_updateTestValues(uint32_t block_iter, const SvcDataParamValueGroup_t * dpvg_p);

const SvcDataData_t idefDataTests[IDEFMAXDATATESTS] = {
        { 1, ( SvcDataParamValueGroup_t *) &idefParamValueTests[0], NULL},// only publish
        { 2, ( SvcDataParamValueGroup_t *) &idefParamValueTests[0], NULL},
        { 3, ( SvcDataParamValueGroup_t *) &idefParamValueTests[0], NULL},
        { 1, ( SvcDataParamValueGroup_t *) &idefParamValueTests[3], NULL},
        { 1, ( SvcDataParamValueGroup_t *) &idefParamValueTests[4], (tSvcDataDataCallbackFuncPtr) &svcdata_updateTestValues}
};

#else

static const SvcDataParamValue_t svcIdefDataStoreId[] = {};
static const IDEF_paramid_t idefParamValueTest0_List[] = {};
static const IDEF_paramid_t idefParamValueTest1_List[] = {};
static const IDEF_paramid_t idefParamValueTest2_List[] = {};
const SvcDataParamValueGroup_t idefParamValueTests[IDEFMAXPARAMVALUETESTS]= {};
const SvcDataData_t idefDataTests[IDEFMAXDATATESTS] = {};
#endif


// proposal idef messages, what goes in what message
// 'measure record' based on 'Uploaded Parameters' excel sheet (date 25-may-2016)





// record containing the simple data (can be in one block)
static const IDEF_paramid_t IdefParamMeasureList01[] = {
        IDEFPARAMID_ENERGY_REMAINING,
        IDEFPARAMID_IS_MOVE_DETECT_MEAS,
        IDEFPARAMID_TEMPERATURE_PCB,
        IDEFPARAMID_TEMPERATURE_EXTERNAL,
        IDEFPARAMID_GNSS_LAT_1,
        IDEFPARAMID_GNSS_NS_1,
        IDEFPARAMID_GNSS_LONG_1,
        IDEFPARAMID_GNSS_EW_1,
        IDEFPARAMID_GNSS_SPEED_TO_ROTATION_1,
        IDEFPARAMID_GNSS_COURSE_1,
        IDEFPARAMID_GNSS_TIME_TO_FIX_1,
        IDEFPARAMID_IS_SPEED_WITHIN_ENV,
        IDEFPARAMID_IS_GOOD_SPEED_DIFF,
        IDEFPARAMID_GNSS_SPEED_TO_ROTATION_2,
        IDEFPARAMID_ROTATION_DIFF,
        IDEFPARAMID_GNSS_TIME_DIFF,
        IDEFPARAMID_GNSS_SAT_ID,
        IDEFPARAMID_GNSS_SAT_SNR,
#if 0
        // these are the configuration parameters (quick fix for TG5,)
        // actually these should not be used, we want actually send the configuration parameters used during the measurement,
        // and these need new paramid's (but Roland has to do also some work for that and don't want this now !)
        IDEFPARAMID_SAMPLE_RATE_RAW,
        IDEFPARAMID_SAMPLES_RAW,
        IDEFPARAMID_SCALING_RAW,
        IDEFPARAMID_SAMPLE_RATE_BEARING,
        IDEFPARAMID_SAMPLES_BEARING,
        IDEFPARAMID_SCALING_BEARING,
        IDEFPARAMID_SAMPLE_RATE_WHEEL_FLAT,
        IDEFPARAMID_SAMPLES_WHEEL_FLAT,
        IDEFPARAMID_SCALING_WHEEL_FLAT,

#endif
};

static const IDEF_paramid_t IdefParamMeasureList02[] = {
        IDEFPARAMID_ACCELERATION_ENV3,
};

static const IDEF_paramid_t IdefParamMeasureList03[] = {
        IDEFPARAMID_ACCELERATION_WHEEL_FLAT_DETECT,
};

static const IDEF_paramid_t IdefParamMeasureList04[] = {
        IDEFPARAMID_ACCELERATION_RAW,
 };

static const SvcDataParamValueGroup_t idefParamValueMeasureRecords[]= {
        { MR_Timestamp,         sizeof(IdefParamMeasureList01)/sizeof(*IdefParamMeasureList01), (IDEF_paramid_t *)  IdefParamMeasureList01 },
        { MR_Timestamp_Env3,    sizeof(IdefParamMeasureList02)/sizeof(*IdefParamMeasureList02), (IDEF_paramid_t *)   IdefParamMeasureList02 },
        { MR_Timestamp__Wheel_Flat_Detect, sizeof(IdefParamMeasureList03)/sizeof(*IdefParamMeasureList03), (IDEF_paramid_t *)   IdefParamMeasureList03 },
        { MR_Timestamp_Raw,     sizeof(IdefParamMeasureList04)/sizeof(*IdefParamMeasureList04), (IDEF_paramid_t *)   IdefParamMeasureList04 },

};



static const IDEF_paramid_t IdefParamCommsList01[] = {

        IDEFPARAMID_IMEI,
        IDEFPARAMID_ICCID,
        IDEFPARAMID_WAKEUP_REASON,
        IDEFPARAMID_FIRMWARE_VERSION,
        IDEFPARAMID_HARDWARE_VERSION,
        IDEFPARAMID_DEVICE_TYPE,
		IDEFPARAMID_COM_TIMESTAMP,
		IDEFPARAMID_ENERGY_REMAINING,
        IDEFPARAMID_COM_SIGNAL_QUALITY,
        IDEFPARAMID_IS_COM_SUCCESSFUL,
        IDEFPARAMID_IS_MOVE_DETECT_COM,
        IDEFPARAMID_ACCELERATION_X,
        IDEFPARAMID_ACCELERATION_Y,
        IDEFPARAMID_ACCELERATION_Z,
        IDEFPARAMID_IS_SELF_TEST_SUCCESSFUL,
        IDEFPARAMID_STATUS_SELF_TEST,
        IDEFPARAMID_HUMIDITY,
        IDEFPARAMID_UP_TIME,
        IDEFPARAMID_GNSS_LAT_COM,
        IDEFPARAMID_GNSS_NS_COM,
        IDEFPARAMID_GNSS_LONG_COM,
        IDEFPARAMID_GNSS_EW_COM,
        IDEFPARAMID_GNSS_SPEED_TO_ROTATION_COM,
        IDEFPARAMID_TIME_LAST_WAKE,
        IDEFPARAMID_TIME_ELAP_REGISTRATION,
        IDEFPARAMID_TIME_ELAP_TCP_CONNECT,
        IDEFPARAMID_TIME_ELAP_TCP_DISCONNECT,
        IDEFPARAMID_TIME_ELAP_MODEM_ON,
        IDEFPARAMID_V_0,
        IDEFPARAMID_V_1,
        IDEFPARAMID_V_2,

        IDEFPARAMID_E_GNSS_CYCLE,
        IDEFPARAMID_E_MODEM_CYCLE,
        IDEFPARAMID_E_PREVIOUS_WAKE_CYCLE,

};



static const SvcDataParamValueGroup_t idefParamValueCommsRecords[]= {
        { CR_Com_timestamp,         sizeof(IdefParamCommsList01)/sizeof(*IdefParamCommsList01), (IDEF_paramid_t *)   IdefParamCommsList01 },
};


static const IDEF_paramid_t IdefparamSignOnList01[] = {
        IDEFPARAMID_DEVICE_TYPE                                     ,
        IDEFPARAMID_HARDWARE_VERSION                                ,
        IDEFPARAMID_FIRMWARE_VERSION                                ,
};

static const SvcDataParamValueGroup_t idefParamValueSignOnRecords[]= {
        { DR_currentIdefTime,         sizeof(IdefparamSignOnList01)/sizeof(*IdefparamSignOnList01), (IDEF_paramid_t *)   IdefparamSignOnList01 },
};


static const IDEF_paramid_t IdefparamDeviceSettingsList01[] = {
         IDEFPARAMID_SCHEDULE_ID         ,
         IDEFPARAMID_IS_RAW_ACCELERATION_ENABLED,
         IDEFPARAMID_URL_SERVER_1        ,
         IDEFPARAMID_URL_SERVER_2        ,
         IDEFPARAMID_URL_SERVER_3        ,
         IDEFPARAMID_URL_SERVER_4        ,
         IDEFPARAMID_TEMPERATURE_ALARM_UPPER_LIMIT,
         IDEFPARAMID_TEMPERATURE_ALARM_LOWER_LIMIT,
         IDEFPARAMID_MIN_HZ              ,
         IDEFPARAMID_MAX_HZ,
         IDEFPARAMID_ALLOWED_ROTATION_CHANGE,
         IDEFPARAMID_SAMPLE_RATE_BEARING,
         IDEFPARAMID_SAMPLES_BEARING,
         IDEFPARAMID_SAMPLE_RATE_RAW,
         IDEFPARAMID_SAMPLES_RAW,
         IDEFPARAMID_SAMPLE_RATE_WHEEL_FLAT,
         IDEFPARAMID_SAMPLES_WHEEL_FLAT,
         IDEFPARAMID_TIME_START_MEAS,
         IDEFPARAMID_RETRY_INTERVAL_MEAS,
         IDEFPARAMID_MAX_DAILY_MEAS_RETRIES,
         IDEFPARAMID_REQUIRED_DAILY_MEAS,
         IDEFPARAMID_UPLOAD_TIME,
         IDEFPARAMID_RETRY_INTERVAL_UPLOAD,
         IDEFPARAMID_MAX_UPLOAD_RETRIES,
         IDEFPARAMID_UPLOAD_REPEAT,
         IDEFPARAMID_NUMBER_OF_UPLOAD,
         IDEFPARAMID_ACCELERATION_THRESHOLD_MOVEMENT_DETECT,
         IDEFPARAMID_ACCELERATION_AVG_TIME_MOVEMENT_DETECT,
         IDEFPARAMID_IS_POWER_ON_FAILSAFE_ENABLED,
         IDEFPARAMID_IS_MOVING_GATING_ENABLED,
         IDEFPARAMID_IS_UPLOAD_OFFSET_ENABLED,
         IDEFPARAMID_CONFIRM_UPLOAD,
         IDEFPARAMID_TRAIN_OPERATOR,
         IDEFPARAMID_TRAIN_FLEET,
         IDEFPARAMID_TRAIN_ID,
         IDEFPARAMID_VEHICLE_ID,
         IDEFPARAMID_VEHICLE_NICKNAME,
         IDEFPARAMID_BOGIE_SERIAL_NUMBER,
         IDEFPARAMID_WHEELSET_NUMBER,
         IDEFPARAMID_IS_WHEELSET_DRIVEN,
         IDEFPARAMID_WHEEL_SERIAL_NUMBER,
         IDEFPARAMID_WHEEL_SIDE,
         IDEFPARAMID_WHEEL_DIAMETER,
         IDEFPARAMID_AXLEBOX_SERIAL_NUMBER,
         IDEFPARAMID_BEARING_BRAND_MODEL_NUMBER,
         IDEFPARAMID_BEARING_SERIAL_NUMBER,
         IDEFPARAMID_SENSOR_LOCATION_ANGLE,
         IDEFPARAMID_SENSOR_ORIENTATION_ANGLE,
         IDEFPARAMID_TRAIN_NAME,
         IDEFPARAMID_BOGIE_NUMBER_IN_WAGON
};

// uses the same timestamp as the com timestamp !!
// make sure it is set correct
static const SvcDataParamValueGroup_t idefParamValueDeviceSettingsRecords[]= {
        { DR_configIdefTime,         sizeof(IdefparamDeviceSettingsList01)/sizeof(*IdefparamDeviceSettingsList01), (IDEF_paramid_t *)   IdefparamDeviceSettingsList01 },
};


static const SvcDataParamValueGroup_t idefParamValueSignOnDeviceSettingsRecords[]= {
        { DR_currentIdefTime,         sizeof(IdefparamSignOnList01)/sizeof(*IdefparamSignOnList01), (IDEF_paramid_t *)   IdefparamSignOnList01 },
        { DR_configIdefTime,          sizeof(IdefparamDeviceSettingsList01)/sizeof(*IdefparamDeviceSettingsList01), (IDEF_paramid_t *)   IdefparamDeviceSettingsList01 },
};


//
// keep this array in sync with: enum ePredefinedIdefDatasets
// it lists the predefined datasets to send over
//
const SvcDataData_t idefDataDataRecords[] = {
 /* measure_record      */      {1, (SvcDataParamValueGroup_t *) &idefParamValueMeasureRecords[0], NULL },
 /* waveform_env3       */      {1, (SvcDataParamValueGroup_t *) &idefParamValueMeasureRecords[1], NULL },
 /* waveform_wheelflat  */      {1, (SvcDataParamValueGroup_t *) &idefParamValueMeasureRecords[2], NULL },
 /* waveform_raw        */      {1, (SvcDataParamValueGroup_t *) &idefParamValueMeasureRecords[3], NULL },
 /* comms_record        */      {1, (SvcDataParamValueGroup_t *) &idefParamValueCommsRecords[0], NULL },
 /* signon record       */      {1, (SvcDataParamValueGroup_t *) &idefParamValueSignOnRecords[0], NULL },
 /* deviceSettings      */      {1, (SvcDataParamValueGroup_t *) &idefParamValueDeviceSettingsRecords[0], NULL },
 /* signon+deviceSettings */    {sizeof(idefParamValueSignOnDeviceSettingsRecords)/sizeof(SvcDataParamValueGroup_t), (SvcDataParamValueGroup_t *) &idefParamValueSignOnDeviceSettingsRecords[0], NULL },
};


/**
 * SvcData_IdefIdToDataStoreId
 *
 * @brief lookup the corresponding datastore Id for given IDEF Id
 * @param IdefId
 * @return datastore Id (or 0=RESERVED at not found)
 */
uint32_t SvcData_IdefIdToDataStoreId(uint32_t IdefId)
{
    uint32_t i;

    for (i=0; i<sizeof(svcIdefDataStoreId)/sizeof(*svcIdefDataStoreId); i++) {
            if (svcIdefDataStoreId[i].parameterId == IdefId ) {
                return svcIdefDataStoreId[i].dataStoreId;
            }
    }
    return DATASTORE_RESERVED;// not found
}






#ifdef __cplusplus
}
#endif