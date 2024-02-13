#ifdef __cplusplus
extern "C" {
#endif

/*
 * configData.c
 *
 *  Created on: Feb 19, 2016
 *      Author: George de Fockert
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "Insight.h"
#include "configFeatures.h"

#include "log.h"
#include "DataDef.h"
#include "configData.h" // project file with the project items to store
#include "NvmConfig.h"

#ifdef CONFIG_PLATFORM_IDEFSVCTESTDATA

#include "configSvcData.h"
#include "configIdef.h"

#define TESTARRAYSIZE (4)
#define TESTARRAYSIZEBIG (10)

// some dummy stuff to test the svcdata/idef stuff
bool    svcdata_bool;
uint8_t svcdata_byte    = 'b';
int8_t  svcdata_sbyte   = 's';
uint16_t svcdata_uint16 = ('u'<<8 ) | 's';
int16_t  svcdata_int16  = ('s'<<8 ) | 's';
uint32_t svcdata_uint32 = ('u'<<8 ) | 'l';
int32_t  svcdata_int32  = ('s'<<8 ) | 'l';
uint64_t svcdata_uint64 = ('u'<<16) | ('l'<<8) | 'l';
int64_t  svcdata_int64  = ('s'<<16) | ('l'<<8) | 'l';;
float   svcdata_single  = 'f';
double  svcdata_double  = ('l'<<8) | 'f';
uint8_t svcdata_string[20] = "one string";
uint64_t svcdata_datetime ;
uint64_t svcdata_datetimeA = 10000;
uint64_t svcdata_datetimeB = 20000;
uint64_t svcdata_datetimeC = 30000;

uint32_t svcdata_uint32a[TESTARRAYSIZE] = {0x31323334,0x35363738,0x21222324,0x25262728};
uint32_t svcdata_uint32abig[TESTARRAYSIZEBIG] ;

// dynamic paramvalue group list sending (for multiple parameters with different timestamps in one message

static struct  {
    int total_records;
    int current_index;
    int max_records_in_block;
    uint64_t timestamp;
} testValues_status;

bool svcdata_init_testValues_sending(uint32_t nrOfRecords, uint32_t maxRecordsPerBlock )
{
    bool rc_ok ;

    testValues_status.max_records_in_block = maxRecordsPerBlock;// this depends on the buffer size used in the encoding process, has to be found empyrical, and keep a safety margin because protobuf does some compression.
    testValues_status.total_records = nrOfRecords;
    testValues_status.current_index = 0;
    testValues_status.timestamp = ConfigSvcData_GetIDEFTime()/10000000ULL - testValues_status.total_records *10; // for generating fake timestemps every 10 seconds
    rc_ok = true;

    return rc_ok;
}

// because of the protobuf encoding method, this function should always return the same parameters for the same input values !!!!
// it is called multiple times during the encoding process.
// so no increment/statechange actions in this function
bool svcdata_updateTestValues(uint32_t block_iter, const SvcDataParamValueGroup_t * dpvg_p)
{
    uint32_t temp_index = testValues_status.current_index + block_iter;
    if ( (block_iter < testValues_status.max_records_in_block) && ( temp_index < testValues_status.total_records)) {

        // now some fake values
        svcdata_datetimeC = (testValues_status.timestamp + (block_iter*10))*10000000ULL;// for generating fake timestemps every 10 seconds
        svcdata_single = 25+(block_iter % 50);

        return true;// yes, this record should be sent
    } else {
        return false;// we are done
    }
}

#endif  /* CONFIG_PLATFORM_IDEFSVCTESTDATA */

static const char device_description[] = "Insight railway sensor";
static const char device_type[] = "INSIGHT_RAILWAY_SENSOR";

static const char unknown_parameter[] = "Unknown Parameter";

// Layout of binary version number : 32 bits
//  INSIGHT_VERSION_MAJOR           :  8 bits
//  INSIGHT_VERSION_MINOR           :  8 bits
//  INSIGHT_BUILD_NUMBER            : 16 bits

// This is the RAM copy of the node's fw version. This variable is used by the
// datastore to send the fw version.
uint32_t Firmware_Version = 0;

// TODO : get these filled with the right values
uint32_t HardwareVersionForConfig = 0;// read from hardware io lines

// TODO : move these measure/comms records to appropriate location !
// first example (template) used for sending the measurement record

measure_record_t measureRecord;

uint64_t timestamp_env3;
uint64_t timestamp_wheelflat;
uint64_t timestamp_raw;

//uint32_t extflash_env3_adress;// contains byte address where the envelope 3 waveform in external flash starts
//uint32_t extflash_wheelflat_adress; // etc.
//uint32_t extflash_raw_adress;

uint64_t currentIdefTime;// used for timestamping with the time of sending.

extern int32_t __sample_buffer[];// defined in the linker file, used for transfering the waveforms


comms_record_t commsRecord;

// Represents the last comms status. True indicates the last comms was successful.
bool lastCommsOk = false;




/*
 *  range check stuff, probably better defined/declared at the place where also the defaults are
 * to prevent blowing up the main table, separate here
 */
static bool modem_baudrateCheck(uint32_t * baud_p)
{
    bool rc_ok = false;
    switch (*baud_p) {
    case 115200:
    case 115200*2:
    case 115200*4:
    case 115200*8:
    case 500000:
    case 750000:
        rc_ok = true;
        break;
    }
    return rc_ok;
}

static DataDef_RangeCheck_t modem_baudrateRange = { (bool (*)( void * ))&modem_baudrateCheck, .hi.u8 = 0, .lo.u8 = 0};
static DataDef_RangeCheck_t modem_providerprofileRange = {NULL, .hi.u8 = 1, .lo.u8 = 0};
static DataDef_RangeCheck_t modem_serviceprofileRange = {NULL, .hi.u8 = 1, .lo.u8 = 0};


static bool sampleRateBearingCheck(uint32_t * value_p)
{
    bool rc_ok = false;
    switch (*value_p) {
    case 1280:
    case 2560:
    case 5120:
        rc_ok = true;
        break;
    }
    return rc_ok;
}

static bool samplesBearingCheck(uint32_t * value_p)
{
    bool rc_ok = false;
    switch (*value_p) {
    case 2048:
    case 4096:
        rc_ok = true;
        break;
    }
    return rc_ok;
}

static bool samplesRawCheck(uint32_t * value_p)
{
    bool rc_ok = false;
    switch (*value_p) {
    case 16384:
    case 32768:
        rc_ok = true;
        break;
    }
    return rc_ok;
}

static bool sampleRateWheelFlatCheck(uint32_t * value_p)
{
    bool rc_ok = false;
    switch (*value_p) {
    case 256:
    case 512:
    case 1280:
        rc_ok = true;
        break;
    }
    return rc_ok;
}

static bool samplesWheelFlatCheck(uint32_t * value_p)
{
    bool rc_ok = false;
    switch (*value_p) {
    case 1024:
    case 2048:
    case 4096:
        rc_ok = true;
        break;
    }
    return rc_ok;
}

#define SECS_IN_A_DAY	(24 * 60 * 60)
static bool UploadRepeatCheck(uint32_t * value_p)
{
	uint32_t mod = *value_p % SECS_IN_A_DAY;
	uint32_t div = *value_p / SECS_IN_A_DAY;
	bool logError = false, add1 = (mod > (SECS_IN_A_DAY /2));

	if((div == 0) || (div > 31) || (0 != mod))
	{
		logError = true;
	}
	if(div > 31)
	{
		// no more than 31 days
		div = 31;
	}
	else if(0 == div)
	{
		// must be at least one day
		div = 1;
	}
	else
	{
		// round up if required
		if(add1)
		{
			div++;
		}
	}

	// write back corrected value
	*value_p = (div * SECS_IN_A_DAY);

	if(logError)
	{
		LOG_EVENT(0, LOG_NUM_APP, ERRLOGWARN, "Upload_repeat must be multiples of %d secs, corrected to %d", SECS_IN_A_DAY, *value_p);
	}
    return true;
}

static DataDef_RangeCheck_t sampleRateBearingRange = { (bool (*)( void * ))&sampleRateBearingCheck, .hi.u8 = 0, .lo.u8 = 0};
static DataDef_RangeCheck_t samplesBearingRange = { (bool (*)( void * ))&samplesBearingCheck, .hi.u8 = 0, .lo.u8 = 0};

static DataDef_RangeCheck_t samplesRawRateRange = { NULL, .hi.u32 = 25600*4, .lo.u32 = 1};
static DataDef_RangeCheck_t samplesRawRange = { (bool (*)( void * ))&samplesRawCheck, .hi.u8 = 0, .lo.u8 = 0};

static DataDef_RangeCheck_t sampleRateWheelFlatRange = { (bool (*)( void * ))&sampleRateWheelFlatCheck, .hi.u8 = 0, .lo.u8 = 0};
static DataDef_RangeCheck_t samplesWheelFlatRange = { (bool (*)( void * ))&samplesWheelFlatCheck, .hi.u8 = 0, .lo.u8 = 0};

static DataDef_RangeCheck_t RetryIntervalMeasRange = { NULL, .hi.u32 = 60*60*24, .lo.u32 = 60};// unit seconds, range 1 minute to one day
static DataDef_RangeCheck_t MaxDailyMeasRetriesRange = { NULL, .hi.u32 = 1440, .lo.u32 = 1};
static DataDef_RangeCheck_t RequiredDailyMeasRange = { NULL, .hi.u32 = 1440, .lo.u32 = 1};
static DataDef_RangeCheck_t RetryIntervalUploadRange = { NULL, .hi.u32 = 60*60*24, .lo.u32 = 60};// unit seconds, range 1 minute to one day
static DataDef_RangeCheck_t UploadRepeatUploadRange = { (bool (*)( void * ))&UploadRepeatCheck, .hi.u32 = 60*60*24*31, .lo.u32 =  MIN_UPLOAD_REPEAT_SECS}; // unit in secs.




/**
 * sDataDef
 * Array with
 */
const DataDef_t sDataDef[] = {
// {objectId, 					memId, 		 configObject,	length, 			type,               rw,    	cliName,                paramCheckPointer,  address,},
    {DATASTORE_RESERVED,        INT_RAM,        false,      sizeof(unknown_parameter),DD_TYPE_STRING, DD_R, NULL,                    NULL,               (uint8_t *) &unknown_parameter[0]},

    { MODEM_BAUDRATE,           CONFIG_RAM,     true,       1,                  DD_TYPE_UINT32,     DD_RW,  "modem_baudrate",       &modem_baudrateRange,(uint8_t *) &gNvmCfg.dev.modem.baudrate},
    { MODEM_IMEI, 				CONFIG_RAM,     true, 		MODEM_IMEI_LEN, 	DD_TYPE_STRING, 	DD_R,   "modem_imei",           NULL, 			    (uint8_t *) gNvmCfg.dev.modem.imei},
	{ MODEM_ICCID, 				CONFIG_RAM,     true, 		MODEM_ICCID_LEN, 	DD_TYPE_STRING, 	DD_R,   "modem_iccid",          NULL, 			    (uint8_t *)	gNvmCfg.dev.modem.iccid},
	{ MODEM_PROVIDERPROFILENR, 	CONFIG_RAM,  	true, 		1, 					DD_TYPE_BYTE, 		DD_RW,  "modem_providerprofile",&modem_providerprofileRange,(uint8_t *) &gNvmCfg.dev.modem.provider},
	{ MODEM_SIMPIN0, 			CONFIG_RAM,  	true, 		MODEM_SIMPIN_LEN, 	DD_TYPE_STRING, 	DD_RW,  "modem_simpin0",        &modem_serviceprofileRange,(uint8_t *) gNvmCfg.dev.modem.providerProfile[0].simpin},
	{ MODEM_APN0, 				CONFIG_RAM,  	true, 		MODEM_APN_LEN, 		DD_TYPE_STRING,		DD_RW,  "modem_apn0",           NULL, 			    (uint8_t *)	gNvmCfg.dev.modem.providerProfile[0].apn},
	{ MODEM_SIMPIN1, 			CONFIG_RAM,  	true, 		MODEM_SIMPIN_LEN, 	DD_TYPE_STRING, 	DD_RW,  "modem_simpin1",        NULL, 			    (uint8_t *)	gNvmCfg.dev.modem.providerProfile[1].simpin},
	{ MODEM_APN1, 				CONFIG_RAM,  	true, 		MODEM_APN_LEN, 		DD_TYPE_STRING,		DD_RW,  "modem_apn1",           NULL, 			    (uint8_t *)	gNvmCfg.dev.modem.providerProfile[1].apn},
	{ MODEM_SERVICEPROFILENR, 	CONFIG_RAM,  	true, 		1, 					DD_TYPE_BYTE, 		DD_RW,  "modem_serviceprofile", NULL, 	            (uint8_t *)	&gNvmCfg.dev.modem.service},
	{ MODEM_URL0, 				CONFIG_RAM,  	true, 		MODEM_URL_LEN, 		DD_TYPE_STRING, 	DD_RW,  "modem_url0",           NULL, 			    (uint8_t *)	gNvmCfg.dev.modem.serviceProfile[0].url},
	{ MODEM_PORTNR0,		 	CONFIG_RAM,  	true, 		1, 					DD_TYPE_UINT16, 	DD_RW,  "modem_port0",          NULL, 			    (uint8_t *)	&gNvmCfg.dev.modem.serviceProfile[0].portnr},
	{ MODEM_SECURE0,            CONFIG_RAM,     true,       1,                  DD_TYPE_BOOL,       DD_RW,  "modem_secure0",        NULL,               (uint8_t *) &gNvmCfg.dev.modem.serviceProfile[0].secure},
	{ MODEM_URL1, 				CONFIG_RAM,  	true, 		MODEM_URL_LEN, 		DD_TYPE_STRING, 	DD_RW,  "modem_url1",           NULL, 			    (uint8_t *)	gNvmCfg.dev.modem.serviceProfile[1].url},
	{ MODEM_PORTNR1,		 	CONFIG_RAM,  	true, 		1, 					DD_TYPE_UINT16, 	DD_RW,  "modem_port1",          NULL, 			    (uint8_t *)	&gNvmCfg.dev.modem.serviceProfile[1].portnr},
    { MODEM_SECURE1,            CONFIG_RAM,     true,       1,                  DD_TYPE_BOOL,       DD_RW,  "modem_secure1",        NULL,               (uint8_t *) &gNvmCfg.dev.modem.serviceProfile[1].secure},

	{ MODEM_MINCSQ,			 	CONFIG_RAM,  	true, 		1, 					DD_TYPE_BYTE,	 	DD_RW,  "modem_mincsq",         NULL, 			    (uint8_t *)	&gNvmCfg.dev.modem.minimumCsqValue},
	{ MODEM_MAXCONNECTTIME,	 	CONFIG_RAM,  	true, 		1, 					DD_TYPE_UINT32,	 	DD_RW,  "modem_maxtimetoconnect", NULL,             (uint8_t *)	&gNvmCfg.dev.modem.maxTimeToConnectMs},
	{ MODEM_MAXTERMINATETIME, 	CONFIG_RAM,  	true, 		1, 					DD_TYPE_UINT32,	 	DD_RW,  "modem_maxtimetoterminate", NULL, 		    (uint8_t *)	&gNvmCfg.dev.modem.maxTimeToTerminateMs},
    { MODEM_RADIOACCESSTECH,    CONFIG_RAM,     true,       1,                  DD_TYPE_BYTE,       DD_RW,  "modem_radioaccesstechnology", NULL,        (uint8_t *) &gNvmCfg.dev.modem.radioAccessTechnology},

    { MQTT_DEVICEID,            CONFIG_RAM,     true,       MQTT_DEVICEID_LEN,   DD_TYPE_STRING,    DD_RW,  "mqtt_deviceid",        NULL,               (uint8_t *) gNvmCfg.dev.mqtt.deviceId},
    { MQTT_SUBTOPICPREFIX,      CONFIG_RAM,     true,       MQTT_TOPIC_PREFIX_LEN,DD_TYPE_STRING,   DD_RW,  "mqtt_subTopicPrefix",  NULL,               (uint8_t *) gNvmCfg.dev.mqtt.subTopicPrefix},
    { MQTT_PUBTOPICPREFIX,      CONFIG_RAM,     true,       MQTT_TOPIC_PREFIX_LEN,DD_TYPE_STRING,   DD_RW,  "mqtt_pubTopicPrefix",  NULL,               (uint8_t *) gNvmCfg.dev.mqtt.pubTopicPrefix},
    { MQTT_LOGTOPICPREFIX,      CONFIG_RAM,     true,       MQTT_TOPIC_PREFIX_LEN,DD_TYPE_STRING,   DD_RW,  "mqtt_logTopicPrefix",  NULL,               (uint8_t *) gNvmCfg.dev.mqtt.logTopicPrefix},
    { MQTT_EPOTOPICPREFIX,      CONFIG_RAM,     true,       MQTT_TOPIC_PREFIX_LEN,DD_TYPE_STRING,   DD_RW,  "mqtt_epoTopicPrefix",  NULL,               (uint8_t *) gNvmCfg.dev.mqtt.epoTopicPrefix},
    { MQTT_SERVICEPROFILENR,    CONFIG_RAM,     true,       1,                  DD_TYPE_BYTE,       DD_RW,  "mqtt_serviceprofile",  NULL,               (uint8_t *) &gNvmCfg.dev.mqtt.service},
    { MQTT_URL0,                CONFIG_RAM,     true,       MODEM_URL_LEN,      DD_TYPE_STRING,     DD_RW,  "mqtt_url0",            NULL,               (uint8_t *) gNvmCfg.dev.mqtt.serviceProfile[0].url},
    { MQTT_PORTNR0,             CONFIG_RAM,     true,       1,                  DD_TYPE_UINT16,     DD_RW,  "mqtt_port0",           NULL,               (uint8_t *) &gNvmCfg.dev.mqtt.serviceProfile[0].portnr},
    { MQTT_SECURE0,             CONFIG_RAM,     true,       1,                  DD_TYPE_BOOL,       DD_RW,  "mqtt_secure0",         NULL,               (uint8_t *) &gNvmCfg.dev.mqtt.serviceProfile[0].secure},
    { MQTT_URL1,                CONFIG_RAM,     true,       MODEM_URL_LEN,      DD_TYPE_STRING,     DD_RW,  "mqtt_url1",            NULL,               (uint8_t *) gNvmCfg.dev.mqtt.serviceProfile[1].url},
    { MQTT_PORTNR1,             CONFIG_RAM,     true,       1,                  DD_TYPE_UINT16,     DD_RW,  "mqtt_port1",           NULL,               (uint8_t *) &gNvmCfg.dev.mqtt.serviceProfile[1].portnr},
    { MQTT_SECURE1,             CONFIG_RAM,     true,       1,                  DD_TYPE_BOOL,       DD_RW,  "mqtt_secure1",         NULL,               (uint8_t *) &gNvmCfg.dev.mqtt.serviceProfile[1].secure},


	{ SCHEDULE_MEMS_SAMPLESPERSECOND, CONFIG_RAM,   true,   1,                  DD_TYPE_BYTE,		DD_RW,  "mems_samplesPerSecond",NULL,               (uint8_t *) &gNvmCfg.dev.schedule.mems_samplesPerSeconds},
	{ SCHEDULE_MEMS_SAMPLEDURATION, CONFIG_RAM, true,       1,                  DD_TYPE_BYTE,		DD_RW,  "mems_sampleDuration",  NULL,               (uint8_t *) &gNvmCfg.dev.schedule.mems_sampleDuration},

	{ SCHEDULE_GNSS_MAX_TIME_TO_VALID_DATA,	CONFIG_RAM, true,   1,              DD_TYPE_UINT16,		DD_RW,  "gnss_maxTimeToValidData",NULL,             (uint8_t *) &gNvmCfg.dev.schedule.gnss_maxSecondsToAquireValidData},

	{ GNSS_IGNORE_STARTUP_SYSMSG,   CONFIG_RAM, true,       1,                  DD_TYPE_BOOL,   	DD_R,  "gnss_SetupForTestBox",   NULL,               (uint8_t *) &gNvmCfg.dev.general.gnssIgnoreSysMsg },

	{ GENERAL_SELF_TEST_TEMP_DELTA,	CONFIG_RAM, true,       1,					DD_TYPE_BYTE,		DD_RW,  "selfTestTempDelta",    NULL,               (uint8_t *) &gNvmCfg.dev.selftest.maxSelfTestTempDelta},

	{ ADC_SETTLE_MIN,         	    CONFIG_RAM, true,       1,					DD_TYPE_UINT32,		DD_RW,  "adcSettleMin",       NULL,                 (uint8_t *) &gNvmCfg.dev.selftest.adcSettleMin},
	{ ADC_SETTLE_MAX,         	    CONFIG_RAM, true,       1,					DD_TYPE_UINT32,		DD_RW,  "adcSettleMax",       NULL,                 (uint8_t *) &gNvmCfg.dev.selftest.adcSettleMax},
	{ ADC_SETTLE_TIME,           	CONFIG_RAM, true,       1,					DD_TYPE_BYTE,		DD_RW,  "adcSettleTime",        NULL,               (uint8_t *) &gNvmCfg.dev.selftest.adcSettleTime},

#ifdef CONFIG_PLATFORM_IDEFSVCTESTDATA
    {SVCDATATEST_BOOL,          INT_RAM,        false,      1,                  DD_TYPE_BOOL,       DD_RW,  "svctest_bool",         NULL,               (uint8_t *) &svcdata_bool },
    {SVCDATATEST_BYTE,          INT_RAM,        false,      1,                  DD_TYPE_BYTE,       DD_RW,  "svctest_byte",         NULL,               (uint8_t *) &svcdata_byte },
    {SVCDATATEST_SBYTE,         INT_RAM,        false,      1,                  DD_TYPE_SBYTE,      DD_RW,  "svctest_sbyte",        NULL,               (uint8_t *) &svcdata_sbyte },
    {SVCDATATEST_INT16,         INT_RAM,        false,      1,                  DD_TYPE_INT16,      DD_RW,  "svctest_int16",        NULL,               (uint8_t *) &svcdata_int16 },
    {SVCDATATEST_UINT16,        INT_RAM,        false,      1,                  DD_TYPE_UINT16,     DD_RW,  "svctest_uint16",       NULL,               (uint8_t *) &svcdata_uint16 },
    {SVCDATATEST_INT32,         INT_RAM,        false,      1,                  DD_TYPE_INT32,      DD_RW,  "svctest_int32",        NULL,               (uint8_t *) &svcdata_int32 },
    {SVCDATATEST_UINT32,        INT_RAM,        false,      1,                  DD_TYPE_UINT32,     DD_RW,  "svctest_uint32",       NULL,               (uint8_t *) &svcdata_uint32 },
    {SVCDATATEST_INT64,         INT_RAM,        false,      1,                  DD_TYPE_INT64,      DD_RW,  "svctest_int64",        NULL,               (uint8_t *) &svcdata_int64 },
    {SVCDATATEST_UINT64,        INT_RAM,        false,      1,                  DD_TYPE_UINT64,     DD_RW,  "svctest_uint64",       NULL,               (uint8_t *) &svcdata_uint64 },
    {SVCDATATEST_SINGLE,        INT_RAM,        false,      1,                  DD_TYPE_SINGLE,     DD_RW,  "svctest_single",       NULL,               (uint8_t *) &svcdata_single },
    {SVCDATATEST_DOUBLE,        INT_RAM,        false,      1,                  DD_TYPE_DOUBLE,     DD_RW,  "svctest_double",       NULL,               (uint8_t *) &svcdata_double },
    {SVCDATATEST_STRING,        INT_RAM,        false, sizeof(svcdata_string) , DD_TYPE_STRING,     DD_RW,  "svctest_string",       NULL,               (uint8_t *) &svcdata_string },
    {SVCDATATEST_DATETIME,      INT_RAM,        false,      1,                  DD_TYPE_DATETIME,   DD_RW,  "svctest_datetime",     NULL,               (uint8_t *) &svcdata_datetime },
    {SVCDATATEST_UINT32A,       INT_RAM,        false,      TESTARRAYSIZE,      DD_TYPE_UINT32,     DD_RW,  "svctest_uint32a",      NULL,               (uint8_t *) &svcdata_uint32a[0]},
    {SVCDATATEST_UINT32ABIG,    INT_RAM,        false,      TESTARRAYSIZEBIG,   DD_TYPE_UINT32,     DD_RW,  "svctest_uint32abig",   NULL,               (uint8_t *) &svcdata_uint32abig[0]},
    {SVCDATATEST_TIMESTAMPA,    INT_RAM,        false,      1,                  DD_TYPE_DATETIME,   DD_RW,  "svctest_datetimeA",    NULL,               (uint8_t *) &svcdata_datetimeA },
    {SVCDATATEST_TIMESTAMPB,    INT_RAM,        false,      1,                  DD_TYPE_DATETIME,   DD_RW,  "svctest_datetimeB",    NULL,               (uint8_t *) &svcdata_datetimeB },
    {SVCDATATEST_TIMESTAMPC,    INT_RAM,        false,      1,                  DD_TYPE_DATETIME,   DD_RW,  "svctest_datetimeC",    NULL,               (uint8_t *) &svcdata_datetimeC },

#endif
    // {objectId,                   memId,       configObject,  length,             type,               rw,     cliName,                paramCheckPointer,  address,},

    {DR_Device_Type,            INT_RAM,        false,      sizeof(device_type), DD_TYPE_STRING,     DD_R,  NULL,                   NULL,               (uint8_t *) &device_type[0] },
    {DR_Device_Serial_Number,   INT_RAM,        true,       MAXDEVICESERIALNUMBERSIZE, DD_TYPE_STRING,DD_RW, NULL,                  NULL,               (uint8_t *) &gNvmCfg.dev.general.deviceSerialNumber },
    {DR_Firmware_Version,       INT_RAM,        false,      1,                  DD_TYPE_UINT32,     DD_R,   NULL,                   NULL,               (uint8_t *) &Firmware_Version },
    {DR_Hardware_Version,       INT_RAM,        false,      1,                  DD_TYPE_UINT32,     DD_R,   NULL,                   NULL,               (uint8_t *) &HardwareVersionForConfig },

    {DR_Device_Description,     INT_RAM,        false,      sizeof(device_description), DD_TYPE_STRING, DD_R,NULL,                  NULL,               (uint8_t *) &device_description[0] },
    {DR_currentIdefTime,        INT_RAM,        false,      1,                  DD_TYPE_DATETIME,   DD_RW, NULL,                    NULL,               (uint8_t *) &currentIdefTime },
    {DR_configIdefTime,         INT_RAM,        false,      1,                  DD_TYPE_DATETIME,   DD_R,  NULL,                    NULL,               (uint8_t *) &gNvmCfg.dev.general.timestamp },


    {MR_Timestamp,              INT_RAM,        false,      1,                  DD_TYPE_DATETIME,   DD_RW,  NULL,                   NULL,               (uint8_t *) &measureRecord.timestamp },
    {MR_Energy_Remaining,       INT_RAM,        false,      1,                  DD_TYPE_BYTE,       DD_RW,  NULL,                   NULL,               (uint8_t *) &measureRecord.params.Energy_Remaining },
    {MR_Is_Move_Detect_Meas,    INT_RAM,        false,      1,                  DD_TYPE_BOOL,       DD_RW,  NULL,                   NULL,               (uint8_t *) &measureRecord.params.Is_Move_Detect_Meas },
    {MR_Temperature_Pcb,        INT_RAM,        false,      1,                  DD_TYPE_SINGLE,     DD_RW,  NULL,                   NULL,               (uint8_t *) &measureRecord.params.Temperature_Pcb },
    {MR_Temperature_External,   INT_RAM,        false,      1,                  DD_TYPE_SINGLE,     DD_RW,  NULL,                   NULL,               (uint8_t *) &measureRecord.params.Temperature_External },
    {MR_GNSS_Lat_1,             INT_RAM,        false,      1,                  DD_TYPE_SINGLE,     DD_RW,  NULL,                   NULL,               (uint8_t *) &measureRecord.params.GNSS_Lat_1 },
    {MR_GNSS_NS_1,              INT_RAM,        false,      MAXCARDINALDIRECTIONLENGTH, DD_TYPE_STRING, DD_RW, NULL,                NULL,               (uint8_t *) &measureRecord.params.GNSS_NS_1[0] },
    {MR_GNSS_Long_1,            INT_RAM,        false,      1,                  DD_TYPE_SINGLE,     DD_RW, NULL,                    NULL,               (uint8_t *) &measureRecord.params.GNSS_Long_1 },
    {MR_GNSS_EW_1,              INT_RAM,        false,      MAXCARDINALDIRECTIONLENGTH, DD_TYPE_STRING, DD_RW, NULL,                NULL,               (uint8_t *) &measureRecord.params.GNSS_EW_1[0] },
    {MR_GNSS_Speed_To_Rotation_1, INT_RAM,      false,      1,                  DD_TYPE_SINGLE,     DD_RW,  NULL,                   NULL,               (uint8_t *) &measureRecord.params.GNSS_Speed_To_Rotation_1 },
    {MR_GNSS_Course_1,          INT_RAM,        false,      1,                  DD_TYPE_UINT16,     DD_RW,  NULL,                   NULL,               (uint8_t *) &measureRecord.params.GNSS_Course_1 },
    {MR_GNSS_Time_To_Fix_1,     INT_RAM,        false,      1,                  DD_TYPE_UINT32,     DD_RW,  NULL,                   NULL,               (uint8_t *) &measureRecord.params.GNSS_Time_To_Fix_1 },
    {MR_Is_Speed_Within_Env,    INT_RAM,        false,      1,                  DD_TYPE_BOOL,       DD_RW,  NULL,                   NULL,               (uint8_t *) &measureRecord.params.Is_Speed_Within_Env },
    {MR_GNSS_Speed_To_Rotation_2, INT_RAM,      false,      1,                  DD_TYPE_SINGLE,     DD_RW,  NULL,                   NULL,               (uint8_t *) &measureRecord.params.GNSS_Speed_To_Rotation_2 },
    {MR_Rotation_Diff,          INT_RAM,        false,      1,                  DD_TYPE_SINGLE,     DD_RW,  NULL,                   NULL,               (uint8_t *) &measureRecord.params.Rotation_Diff},
    {MR_GNSS_Time_Diff,         INT_RAM,        false,      1,                  DD_TYPE_SINGLE,     DD_RW,  NULL,                   NULL,               (uint8_t *) &measureRecord.params.GNSS_Time_Diff },
    {MR_GNSS_Sat_Id,            INT_RAM,        false,      MAX_GNSS_SATELITES, DD_TYPE_BYTE,       DD_RW,  NULL,                   NULL,               (uint8_t *) &measureRecord.params.GNSS_Sat_Id[0] },
    {MR_GNSS_Sat_Snr,           INT_RAM,        false,      MAX_GNSS_SATELITES, DD_TYPE_BYTE,       DD_RW,  NULL,                   NULL,               (uint8_t *) &measureRecord.params.GNSS_Sat_Snr[0] },
    {MR_Timestamp_Env3,         INT_RAM,        false,      1,                  DD_TYPE_DATETIME,   DD_RW,  NULL,                   NULL,               (uint8_t *) &timestamp_env3 },
    {MR_Acceleration_Env3,      INT_RAM,        false,      MAX_ENV3_SAMPLES,   DD_TYPE_SINGLE,     DD_RW,  NULL,                   NULL,               (uint8_t *) __sample_buffer/* &extflash_env3_adress */ },
    {MR_Timestamp__Wheel_Flat_Detect, INT_RAM,  false,      1,                  DD_TYPE_DATETIME,   DD_RW,  NULL,                   NULL,               (uint8_t *) &timestamp_wheelflat },
    {MR_Acceleration_Wheel_Flat_Detect, INT_RAM,false,      MAX_FLAT_SAMPLES,   DD_TYPE_SINGLE,     DD_RW,  NULL,                   NULL,               (uint8_t *) __sample_buffer /*&extflash_wheelflat_adress */},
    {MR_Timestamp_Raw,          INT_RAM,        false,      1,                  DD_TYPE_DATETIME,   DD_RW,  NULL,                   NULL,               (uint8_t *) &timestamp_raw },
    {MR_Acceleration_Raw,       INT_RAM,        false,      MAX_RAW_SAMPLES,    DD_TYPE_SINGLE,     DD_RW,  NULL,                   NULL,               (uint8_t *) __sample_buffer /*&extflash_raw_adress */},
    {MR_Is_Good_Speed_Diff,     INT_RAM,        false,      1,                  DD_TYPE_BOOL,       DD_RW,  NULL,                   NULL,               (uint8_t *) &measureRecord.params.Is_Good_Speed_Diff },


    {CR_Com_timestamp,          INT_RAM,        false,      1,                  DD_TYPE_DATETIME,   DD_RW,  NULL,                   NULL,               (uint8_t *) &commsRecord.timestamp },
    {CR_IMEI,                   CONFIG_RAM ,    false,      MODEM_IMEI_LEN,     DD_TYPE_STRING,     DD_R,   "modem_imei",           NULL,               (uint8_t *) gNvmCfg.dev.modem.imei},
    {CR_ICCID,                  CONFIG_RAM ,    false,      MODEM_ICCID_LEN,    DD_TYPE_STRING,     DD_R,   "modem_iccid",          NULL,               (uint8_t *) gNvmCfg.dev.modem.iccid},
    {CR_Wakeup_Reason,          INT_RAM,        false,      1,                  DD_TYPE_BYTE,       DD_RW,  NULL,                   NULL,               (uint8_t *) &commsRecord.params.Wakeup_Reason },
    {CR_Energy_Remaining,       INT_RAM,        false,      1,                  DD_TYPE_SINGLE,     DD_RW,  NULL,                   NULL,               (uint8_t *) &commsRecord.params.EnergyRemaining_percent },
    {CR_Com_Signal_Quality,     INT_RAM,        false,      1,                  DD_TYPE_BYTE,       DD_RW,  NULL,                   NULL,               (uint8_t *) &commsRecord.params.Com_Signal_Quality },
    {CR_Is_Last_Com_Successful, INT_RAM,        false,      1,                  DD_TYPE_BOOL,       DD_R,  "Is_Last_Com_Successful",NULL,               (uint8_t *) &lastCommsOk },
    {CR_Is_Move_Detect_Com,     INT_RAM,        false,      1,                  DD_TYPE_BOOL,       DD_RW,  NULL,                   NULL,               (uint8_t *) &commsRecord.params.Is_Move_Detect_Com },
    {CR_Acceleration_X,         INT_RAM,        false,      1,                  DD_TYPE_SINGLE,     DD_RW,  NULL,                   NULL,               (uint8_t *) &commsRecord.params.Acceleration_X },
    {CR_Acceleration_Y,         INT_RAM,        false,      1,                  DD_TYPE_SINGLE,     DD_RW,  NULL,                   NULL,               (uint8_t *) &commsRecord.params.Acceleration_Y },
    {CR_Acceleration_Z,         INT_RAM,        false,      1,                  DD_TYPE_SINGLE,     DD_RW,  NULL,                   NULL,               (uint8_t *) &commsRecord.params.Acceleration_Z },
    {CR_Is_Self_Test_Successful,INT_RAM,        false,      1,                  DD_TYPE_BOOL,       DD_RW,  NULL,                   NULL,               (uint8_t *) &commsRecord.params.Is_Self_Test_Successful },
    {CR_Status_Self_Test,       INT_RAM,        false,      MAXSTATUSSELFTESTLENGTH, DD_TYPE_BYTE,  DD_RW,  NULL,                   NULL,               (uint8_t *) &commsRecord.params.Status_Self_Test },
    {CR_Humidity,               INT_RAM,        false,      1,                  DD_TYPE_SINGLE,     DD_RW,  NULL,                   NULL,               (uint8_t *) &commsRecord.params.Humidity },
    {CR_Up_Time,                INT_RAM,        false,      1,                  DD_TYPE_UINT32,     DD_RW,  NULL,                   NULL,               (uint8_t *) &commsRecord.params.Up_Time },
    {CR_GNSS_Lat_Com,           INT_RAM,        false,      1,                  DD_TYPE_SINGLE,     DD_RW,  NULL,                   NULL,               (uint8_t *) &commsRecord.params.GNSS_Lat_Com },
    {CR_GNSS_NS_Com,            INT_RAM,        false,      MAXCARDINALDIRECTIONLENGTH, DD_TYPE_STRING, DD_RW, NULL,                NULL,               (uint8_t *) &commsRecord.params.GNSS_NS_Com[0] },
    {CR_GNSS_Long_Com,          INT_RAM,        false,      1,                  DD_TYPE_SINGLE,     DD_RW,  NULL,                   NULL,               (uint8_t *) &commsRecord.params.GNSS_Long_Com },
    {CR_GNSS_EW_Com,            INT_RAM,        false,      MAXCARDINALDIRECTIONLENGTH, DD_TYPE_STRING, DD_RW, NULL,                NULL,               (uint8_t *) &commsRecord.params.GNSS_EW_Com[0] },
    {CR_GNSS_Speed_To_Rotation_Com, INT_RAM,    false,      1,                  DD_TYPE_SINGLE,     DD_RW, NULL,                    NULL,               (uint8_t *) &commsRecord.params.GNSS_Speed_To_Rotation_Com },
    {CR_Time_Last_Wake,         INT_RAM,        false,      1,                  DD_TYPE_DATETIME,   DD_RW, NULL,                    NULL,               (uint8_t *) &commsRecord.params.Time_Last_Wake },

    {CR_Time_Elap_Registration, INT_RAM,        false,      1,                  DD_TYPE_UINT32,     DD_RW, NULL,                    NULL,               (uint8_t *) &commsRecord.params.Time_Elap_Registration },
    {CR_Time_Elap_TCP_Connect,  INT_RAM,        false,      1,                  DD_TYPE_UINT32,     DD_RW, NULL,                    NULL,               (uint8_t *) &commsRecord.params.Time_Elap_TCP_Connect },
    {CR_Time_Elap_TCP_Disconnect, INT_RAM,      false,      1,                  DD_TYPE_UINT32,     DD_RW, NULL,                    NULL,               (uint8_t *) &commsRecord.params.Time_Elap_TCP_Disconnect },
    {CR_Time_Elap_Modem_On,     INT_RAM,        false,      1,                  DD_TYPE_UINT32,     DD_RW, NULL,                    NULL,               (uint8_t *) &commsRecord.params.Time_Elap_Modem_On },

    {CR_V_0,                    INT_RAM,        false,      1,                  DD_TYPE_SINGLE,     DD_RW, NULL,                    NULL,               (uint8_t *) &commsRecord.params.V_0},
    {CR_V_1,                    INT_RAM,        false,      1,                  DD_TYPE_SINGLE,     DD_RW, NULL,                    NULL,               (uint8_t *) &commsRecord.params.V_1},
    {CR_V_2,                    INT_RAM,        false,      1,                  DD_TYPE_SINGLE,     DD_RW, NULL,                    NULL,               (uint8_t *) &commsRecord.params.V_2},

    {CR_E_GNSS_Cycle,           INT_RAM,        false,      1,                  DD_TYPE_SINGLE,     DD_RW, NULL,                    NULL,               (uint8_t *) &commsRecord.params.E_GNSS_Cycle },
    {CR_E_Modem_Cycle,          INT_RAM,        false,      1,                  DD_TYPE_SINGLE,     DD_RW, NULL,                    NULL,               (uint8_t *) &commsRecord.params.E_Modem_Cycle },
    {CR_E_Previous_Wake_Cycle,  INT_RAM,        false,      1,                  DD_TYPE_SINGLE,     DD_RW, NULL,                    NULL,               (uint8_t *) &commsRecord.params.E_Previous_Wake_Cycle },

    {SR_Schedule_ID,            INT_RAM,        true,       1,    				DD_TYPE_STRING,     DD_RW, "Schedule_ID",           NULL,               (uint8_t *) &gNvmCfg.dev.measureConf.Schedule_ID},
    {SR_Is_Raw_Acceleration_Enabled, INT_RAM,   true,       1,                  DD_TYPE_BOOL,       DD_RW, "Is_Raw_Acceleration_Enabled", NULL,         (uint8_t *) &gNvmCfg.dev.measureConf.Is_Raw_Acceleration_Enabled},
    {SR_Url_Server_1,           INT_RAM,        true,       MAXURLSIZE,         DD_TYPE_STRING,     DD_RW, "Url_Server_1",           NULL,               (uint8_t *) &gNvmCfg.dev.commConf.Url_Server_1},
    {SR_Url_Server_2,           INT_RAM,        true,       MAXURLSIZE,         DD_TYPE_STRING,     DD_RW, "Url_Server_2",          NULL,               (uint8_t *) &gNvmCfg.dev.commConf.Url_Server_2},
    {SR_Url_Server_3,           INT_RAM,        true,       MAXURLSIZE,         DD_TYPE_STRING,     DD_RW, "Url_Server_3",          NULL,               (uint8_t *) &gNvmCfg.dev.commConf.Url_Server_3},
    {SR_Url_Server_4,           INT_RAM,        true,       MAXURLSIZE,         DD_TYPE_STRING,     DD_R,  "Url_Server_4",          NULL,               (uint8_t *) &gNvmCfg.dev.commConf.Url_Server_4},
    {SR_Temperature_Alarm_Upper_Limit, INT_RAM, true,       1,                  DD_TYPE_SINGLE,     DD_RW, "Temperature_Alarm_Upper_Limit", NULL,       (uint8_t *) &gNvmCfg.dev.measureConf.Temperature_Alarm_Upper_Limit},
    {SR_Temperature_Alarm_Lower_Limit, INT_RAM, true,       1,                  DD_TYPE_SINGLE,     DD_RW, "Temperature_Alarm_Lower_Limit", NULL,       (uint8_t *) &gNvmCfg.dev.measureConf.Temperature_Alarm_Lower_Limit},

	{SR_Min_Hz,                 INT_RAM,        true,       1,                  DD_TYPE_UINT32,     DD_RW, "Min_Hz",                NULL,               (uint8_t *) &gNvmCfg.dev.measureConf.Min_Hz},
    {SR_Max_Hz,                 INT_RAM,        true,       1,                  DD_TYPE_UINT32,     DD_RW, "Max_Hz",                NULL,               (uint8_t *) &gNvmCfg.dev.measureConf.Max_Hz},
    {SR_Allowed_Rotation_Change,    INT_RAM,    true,       1,                  DD_TYPE_SINGLE,     DD_RW, "Allowed_Rotation_Change", NULL,             (uint8_t *) &gNvmCfg.dev.measureConf.Allowed_Rotation_Change},

    {SR_Sample_Rate_Bearing,    INT_RAM,        true,       1,                  DD_TYPE_UINT32,     DD_RW, "Sample_Rate_Bearing", &sampleRateBearingRange,  (uint8_t *) &gNvmCfg.dev.measureConf.Sample_Rate_Bearing},
    {SR_Samples_Bearing,        INT_RAM,        true,       1,                  DD_TYPE_UINT32,     DD_RW, "Samples_Bearing", &samplesBearingRange,     (uint8_t *) &gNvmCfg.dev.measureConf.Samples_Bearing},
    {SR_Scaling_Bearing,        INT_RAM,        true,       1,                  DD_TYPE_SINGLE,     DD_R , "Scaling_Bearing",       NULL,               (uint8_t *) &gNvmCfg.dev.measureConf.Scaling_Bearing},

    {SR_Sample_Rate_Raw,        INT_RAM,        true,       1,                  DD_TYPE_UINT32,     DD_RW, "Sample_Rate_Raw", &samplesRawRateRange,     (uint8_t *) &gNvmCfg.dev.measureConf.Sample_Rate_Raw},
    {SR_Samples_Raw,            INT_RAM,        true,       1,                  DD_TYPE_UINT32,     DD_RW, "Samples_Raw", &samplesRawRange,             (uint8_t *) &gNvmCfg.dev.measureConf.Samples_Raw},
    {SR_Scaling_Raw,            INT_RAM,        true,       1,                  DD_TYPE_SINGLE,     DD_R , "Scaling_Raw",           NULL,               (uint8_t *) &gNvmCfg.dev.measureConf.Scaling_Raw},

    {SR_Sample_Rate_Wheel_Flat, INT_RAM,        true,       1,                  DD_TYPE_UINT32,     DD_RW, "Sample_Rate_Wheel_Flat", &sampleRateWheelFlatRange,(uint8_t *) &gNvmCfg.dev.measureConf.Sample_Rate_Wheel_Flat},
    {SR_Samples_Wheel_Flat,     INT_RAM,        true,       1,                  DD_TYPE_UINT32,     DD_RW, "Samples_Wheel_Flat", &samplesWheelFlatRange,    (uint8_t *) &gNvmCfg.dev.measureConf.Samples_Wheel_Flat},
    {SR_Scaling_Wheel_Flat,     INT_RAM,        true,       1,                  DD_TYPE_SINGLE,     DD_R , "Scaling_Wheel_Flat",    NULL,               (uint8_t *) &gNvmCfg.dev.measureConf.Scaling_Wheel_Flat},

    {SR_Time_Start_Meas,        INT_RAM,        true,       1,                  DD_TYPE_UINT32,     DD_RW, "Time_Start_Meas",       NULL,               (uint8_t *) &gNvmCfg.dev.measureConf.Time_Start_Meas},
    {SR_Retry_Interval_Meas,    INT_RAM,        true,       1,                  DD_TYPE_UINT32,     DD_RW, "Retry_Interval_Meas", &RetryIntervalMeasRange,  (uint8_t *) &gNvmCfg.dev.measureConf.Retry_Interval_Meas},
    {SR_Max_Daily_Meas_Retries, INT_RAM,        true,       1,                  DD_TYPE_UINT32,     DD_RW, "Max_Daily_Meas_Retries", &MaxDailyMeasRetriesRange,(uint8_t *) &gNvmCfg.dev.measureConf.Max_Daily_Meas_Retries},
    {SR_Required_Daily_Meas,    INT_RAM,        true,       1,                  DD_TYPE_UINT32,     DD_RW, "Required_Daily_Meas", &RequiredDailyMeasRange,  (uint8_t *) &gNvmCfg.dev.measureConf.Required_Daily_Meas},

    {SR_Upload_Time,            INT_RAM,        true,       1,                  DD_TYPE_UINT32,     DD_RW, "Upload_Time",           NULL,               (uint8_t *) &gNvmCfg.dev.commConf.Upload_Time},
    {SR_Is_Verbose_Logging_Enabled, INT_RAM,    true,       1,                  DD_TYPE_BOOL,       DD_RW, "Is_Verbose_Logging_Enabled", NULL,          (uint8_t *) &gNvmCfg.dev.commConf.Is_Verbose_Logging_Enabled},
    {SR_Max_Upload_Retries,     INT_RAM,        true,       1,                  DD_TYPE_UINT32,     DD_RW, "Max_Upload_Retries",    NULL,               (uint8_t *) &gNvmCfg.dev.commConf.Max_Upload_Retries},
    {SR_Upload_repeat,          INT_RAM,        true,       1,                  DD_TYPE_UINT32,     DD_RW, "Upload_repeat", &UploadRepeatUploadRange,   (uint8_t *) &gNvmCfg.dev.commConf.Upload_repeat},
    {SR_Number_Of_Upload,       INT_RAM,        true,       1,                  DD_TYPE_UINT32,     DD_RW, "Number_Of_Upload",      NULL,               (uint8_t *) &gNvmCfg.dev.commConf.Number_Of_Upload},
    {SR_Retry_Interval_Upload,      INT_RAM,    true,       1,                  DD_TYPE_UINT32,     DD_RW, "Retry_Interval_Upload", &RetryIntervalUploadRange, (uint8_t *) &gNvmCfg.dev.commConf.Retry_Interval_Upload},

    {SR_Acceleration_Threshold_Movement_Detect, INT_RAM,    true,   1,          DD_TYPE_SINGLE,     DD_RW, "Acceleration_Threshold_Movement_Detect", NULL,  (uint8_t *) &gNvmCfg.dev.measureConf.Acceleration_Threshold_Movement_Detect},
    {SR_Acceleration_Avg_Time_Movement_Detect, INT_RAM,     true,   1,          DD_TYPE_UINT32,     DD_RW, "Acceleration_Avg_Time_Movement_Detect", NULL,   (uint8_t *) &gNvmCfg.dev.measureConf.Acceleration_Avg_Time_Movement_Detect},

    {SR_Is_Power_On_Failsafe_Enabled,   INT_RAM,    true,   1,                  DD_TYPE_BOOL,       DD_RW, "Is_Power_On_Failsafe_Enabled", NULL,        (uint8_t *) &gNvmCfg.dev.measureConf.Is_Power_On_Failsafe_Enabled},
    {SR_Is_Moving_Gating_Enabled,   INT_RAM,    true,       1,                  DD_TYPE_BOOL,       DD_RW, "Is_Moving_Gating_Enabled", NULL,            (uint8_t *) &gNvmCfg.dev.measureConf.Is_Moving_Gating_Enabled},
    {SR_Is_Upload_Offset_Enabled,   INT_RAM,    true,       1,                  DD_TYPE_BOOL,       DD_RW, "Is_Upload_Offset_Enabled", NULL,            (uint8_t *) &gNvmCfg.dev.measureConf.Is_Upload_Offset_Enabled},
    {SR_confirm_upload,             INT_RAM,    true,       1,                  DD_TYPE_BOOL,       DD_RW, "confirm_upload",        NULL,               (uint8_t *) &gNvmCfg.dev.commConf.Confirm_Upload},


    {AR_Train_Operator,             INT_RAM,  true,       MAX_ASSETID_SIZE,     DD_TYPE_STRING,     DD_RW, NULL,                    NULL,               (uint8_t *) &gNvmCfg.dev.assetConf.Train_Operator},
    {AR_Train_Fleet,                INT_RAM,  true,       MAX_ASSETID_SIZE,     DD_TYPE_STRING,     DD_RW, NULL,                    NULL,               (uint8_t *) &gNvmCfg.dev.assetConf.Train_Fleet},
    {AR_Train_ID,                   INT_RAM,  true,       MAX_ASSETID_SIZE,     DD_TYPE_STRING,     DD_RW, NULL,                    NULL,               (uint8_t *) &gNvmCfg.dev.assetConf.Train_ID},
    {AR_Vehicle_ID,                 INT_RAM,  true,       MAX_ASSETID_SIZE,     DD_TYPE_STRING,     DD_RW, NULL,                    NULL,               (uint8_t *) &gNvmCfg.dev.assetConf.Vehicle_ID},
    {AR_Vehicle_Nickname,           INT_RAM,  true,       MAX_ASSETID_SIZE,     DD_TYPE_STRING,     DD_RW, NULL,                    NULL,               (uint8_t *) &gNvmCfg.dev.assetConf.Vehicle_Nickname},
    {AR_Bogie_Serial_Number,        INT_RAM,  true,       MAX_ASSETID_SIZE,     DD_TYPE_STRING,     DD_RW, NULL,                    NULL,               (uint8_t *) &gNvmCfg.dev.assetConf.Bogie_Serial_Number},
    {AR_Wheelset_Number,            INT_RAM,  true,       MAX_ASSETID_SIZE,     DD_TYPE_STRING,     DD_RW, NULL,                    NULL,               (uint8_t *) &gNvmCfg.dev.assetConf.Wheelset_Number},
    {AR_Wheelset_Serial_Number,     INT_RAM,  true,       MAX_ASSETID_SIZE,     DD_TYPE_STRING,     DD_RW, NULL,                    NULL,               (uint8_t *) &gNvmCfg.dev.assetConf.Wheelset_Serial_Number},
    {AR_Is_Wheelset_Driven,         INT_RAM,  true,       1,                    DD_TYPE_BOOL,       DD_RW, NULL,                    NULL,               (uint8_t *) &gNvmCfg.dev.assetConf.Is_Wheelset_Driven},
    {AR_Wheel_Serial_Number,        INT_RAM,  true,       MAX_ASSETID_SIZE,     DD_TYPE_STRING,     DD_RW, NULL,                    NULL,               (uint8_t *) &gNvmCfg.dev.assetConf.Wheel_Serial_Number},
    {AR_Wheel_Side,                 INT_RAM,  true,       MAX_WHEELSIDE_SIZE,   DD_TYPE_STRING,     DD_RW, NULL,                    NULL,               (uint8_t *) &gNvmCfg.dev.assetConf.Wheel_Side},
    {AR_Wheel_Diameter,             INT_RAM,  true,       1,                    DD_TYPE_SINGLE,     DD_RW, NULL,                    NULL,               (uint8_t *) &gNvmCfg.dev.assetConf.Wheel_Diameter},
    {AR_Axlebox_Serial_Number,      INT_RAM,  true,       MAX_ASSETID_SIZE,     DD_TYPE_STRING,     DD_RW, NULL,                    NULL,               (uint8_t *) &gNvmCfg.dev.assetConf.Axlebox_Serial_Number},
    {AR_Bearing_Brand_Model_Number, INT_RAM,  true,       MAX_ASSETID_SIZE,     DD_TYPE_STRING,     DD_RW, NULL,                    NULL,               (uint8_t *) &gNvmCfg.dev.assetConf.Bearing_Brand_Model_Number},
    {AR_Bearing_Serial_Number,      INT_RAM,  true,       MAX_ASSETID_SIZE,     DD_TYPE_STRING,     DD_RW, NULL,                    NULL,               (uint8_t *) &gNvmCfg.dev.assetConf.Bearing_Serial_Number},
    {AR_Sensor_Location_Angle,      INT_RAM,  true,       1,                    DD_TYPE_SINGLE,     DD_RW, NULL,                    NULL,               (uint8_t *) &gNvmCfg.dev.assetConf.Sensor_Location_Angle},
    {AR_Sensor_Orientation_Angle,   INT_RAM,  true,       1,                    DD_TYPE_SINGLE,     DD_RW, NULL,                    NULL,               (uint8_t *) &gNvmCfg.dev.assetConf.Sensor_Orientation_Angle},
    {AR_Train_Name,                 INT_RAM,  true,       MAX_ASSETID_SIZE,     DD_TYPE_STRING,     DD_RW, NULL,                    NULL,               (uint8_t *) &gNvmCfg.dev.assetConf.Train_Name},
    {AR_Bogie_Number_In_Wagon,      INT_RAM,  true,       MAX_ASSETID_SIZE,     DD_TYPE_STRING,     DD_RW, NULL,                    NULL,               (uint8_t *) &gNvmCfg.dev.assetConf.Bogie_Number_In_Wagon},

    // {objectId,                   memId,       configObject,  length,             type,               rw,     cliName,                paramCheckPointer,  address,},
};





/*
 * Functions
 */

uint32_t getNrDataDefElements()
{
	return sizeof(sDataDef)/sizeof(*sDataDef);
}

/*
 * copyBytesRamToStore
 *
 * @desc    glorified memcpy, but extendable to copy to internal and external flash etc.
 *
 * @param   dst_info   pointer to the structure which describes the datastore metadata about the object
 * @param   byteOffset offset in bytes relative to the start address of the object
 * @param   ram_p   source address in internal processor ram.
 * @param   count   number of bytes to copy from ram to destination object
 * @param   su      super_user, write data although it is flagged readonly (DD_R will be ignored, and the data will be treated as DD_RW)
 *
 * @returns false  on failure
 *
 */

bool copyBytesRamToStore(const DataDef_t * dst_info, uint8_t byteOffset, uint8_t *ram_p, uint32_t count, bool su)
{
    bool rc_ok = true;

    if ((dst_info->rw != DD_RW) ) {
        if (su) {
            LOG_DBG( LOG_LEVEL_COMM, "copyBytesRamToStore : WRITING read only item %d, because su is true !!\n", dst_info->objectId);
        } else {
            LOG_DBG( LOG_LEVEL_COMM, "copyBytesRamToStore : not writing read only item %d\n", dst_info->objectId);
            return true;// read only item, so not a good idea to write
        }
    }


    switch (dst_info->memId) {

    case CONFIG_RAM:
        // TODO: nice place to set variable to indicate that config has changed, and must be written to FLASH before system goes powerdown
    case INT_RAM:
        memcpy(&dst_info->address[byteOffset], ram_p, count);
        break;


    case EXT_FLASH:
        // not implemented (yet)
        rc_ok = true;// do not return an error for now
        break;
    case PROGRAM_FLASH:
    case CONFIG_FLASH:

    default:
        //not supported memory type
        rc_ok = false;
        break;
    }
    return rc_ok;
}


/*
 * copyBytesStoreToRam
 *
 * @desc    glorified memcpy, but extendable to copy to internal and external flash etc.
 *
 * @param   ram_p      destination address in internal processor ram.
 * @param   src_info   pointer to the structure which describes the datastore metadata about the object
 * @param   byteOffset offset in bytes relative to the start address of the object

 * @param   count   number of bytes to copy from source object to ram
 *
 * @returns false  on failure
 *
 */

bool copyBytesStoreToRam(uint8_t *ram_p, const DataDef_t * src_info, uint32_t byteOffset,  uint32_t count)
{
    bool rc_ok = true;

    switch (src_info->memId) {

    case CONFIG_RAM:
    case INT_RAM:

        memcpy(ram_p, &src_info->address[byteOffset], count);
        break;

    case EXT_FLASH:
        // not implemented (yet)
        {
         // uint32_t startAddress = *( (uint32_t *) src_info->address);
        // dst_info->address points , in this EXT_FLASH case, to a uint32_t variable which holds the starting address
        // startPage?? = *( (uint32_t *) dst_info->address); // what if we want to read starting halfway a page ???
        // datalength = count
        // destAddress = ram_p
        // retval = IS25_NormalRead(startPage, destAddress, dataLength);

        // printf("copyBytesStoreToRam: here should be a function to read from EXT_FLASH address %08x, %04x bytes\n", startAddress+byteOffset, count);
        }
        // rc_ok = false;
        break;

    case PROGRAM_FLASH:
    case CONFIG_FLASH:

    default:
        //not supported memory type
        rc_ok = false;
        break;
    }
    return rc_ok;
}



#ifdef __cplusplus
}
#endif