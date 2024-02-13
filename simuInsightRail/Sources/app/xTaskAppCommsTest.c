#ifdef __cplusplus
extern "C" {
#endif

/*
 * xTaskAppCommTest.c
 *
 *  Created on: 20 Oct 2016
 *      Author: George de Fockert
 *
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "printgdf.h"

#include "rtc.h"
#include "Timer.h"
#include "xTaskAppEvent.h"
#include "xTaskDefs.h"
#include "configLog.h"
#include "log.h"
#include "device.h"
#include "NvmConfig.h"
#include "pinconfig.h"


#include "dataDef.h"
#include "dataStore.h"
#include "configData.h"
#include "configSvcData.h"
#include "IsvcData.h"
#include "SvcDataMsg_PubData.h"
#include "configMQTT.h"
#include "EventLog.h"

#include "configBootloader.h"
#include "ISvcFirmware.h"
#include "SvcData.h"
#include "SvcMqttFirmware.h"
#include "xTaskAppOta.h"
#include "xTaskApp.h"
#include "xTaskAppGnss.h"
#include "ExtFlash.h"

#include "xTaskAppCommsTest.h"

#include "nvmdata.h"
#include "Vbat.h"
#include "commCLI.h"
#include "fsl_rtc_hal.h"
#include "gnssMT3333.h"
#include "image.h"
#include "temperature.h"
#include "PMIC.h"
#include "imageTypes.h"
#include "DrvTmp431.h"
#include "selftest.h"
#include "ephemeris.h"
#include "alarms.h"
#include "Modem.h"

#ifdef DEBUG
#include "ExtFlash.h"
#endif

extern RTC_Type * const g_rtcBase[RTC_INSTANCE_COUNT];
extern uint32_t crc32_hardware(void *pBuff, uint32_t length);
extern uint32_t handleFirmwareBlockReply_cb(void * buf);						//! To access boot config information
extern bool bWakeupIsMAGSwipe;
extern tExtFlashHandle extFlashHandle;

#define COMMS_RECORD	(0xFFFF)

#define EVENTQUEUE_NR_ELEMENTS_APP  8     //! Event Queue can contain this number of elements

#define USE_DELAYED_EPODOWNLOAD		1	//! This controls the use of delayed download of EPO data to GPS module. 0 - disable, 1-enable




static QueueHandle_t EventQueue_App;

static tCommHandle CommHandle = { .EventQueue_CommResp = NULL};

static bool checkIncommingMessages(uint32_t maxWaitMs, bool *requestReceived_p);

#if 0
// ideas for better administration of what dataset is uploaded, and flash storage can be made free.
// keep a list of measurement sets and message ID's which are 'under way'
// when a set is sent, store the messageId and the messageSet number in this list
// when a soreData_reply is coming in, look up which message Id it was, and 'free' the measurementset.

typedef struct {
    bool used;
    uint32_t measurementSetNr;
    int32_t messageId;
} tMeasureSetAckInfo;
#define MAX_DATASET_NACK (10)

struct {
    SemaphoreHandle_t semAccess;
    tMeasureSetAckInfo mesuresetAckInfo[MAX_DATASET_NACK];
    uint32_t highest;// debug info, how many are really used
} messageAckData = {
        .highest = 0,
        .semAccess = NULL,
};

static void clearAckInfo()
{
    for(uint16_t i=0; i<MAX_DATASET_NACK; i++) {
        messageAckData.mesuresetAckInfo[i].used = false;
    }
}

static bool initAckInfo()
{
    bool rc_ok = false;

    clearAckInfo();

    messageAckData.highest = 0;
    messageAckData.semAccess =  xSemaphoreCreateBinary( );

    if (messageAckData.semAccess != NULL) {
        xSemaphoreGive( messageAckData.semAccess );
        rc_ok = true;
    }
    return rc_ok;
}

static bool enterMessageIdAckInfo(uint32_t measurementSetNr, int32_t messageId, uint32_t maxWaitMs)
{
    bool rc_ok = false;
    // grab semaphore
    if (pdTRUE != xSemaphoreTake(   messageAckData.semAccess , maxWaitMs/portTICK_PERIOD_MS)) {
        LOG_DBG(LOG_LEVEL_MODEM,"enterMessageIdAckInfo: access semTake failed\n" );
    } else {
        for (uint16_t i=0; i<MAX_DATASET_NACK; i++) {
            if (messageAckData.mesuresetAckInfo[i].used == false) {
                messageAckData.mesuresetAckInfo[i].messageId = messageId;
                messageAckData.mesuresetAckInfo[i].measurementSetNr = measurementSetNr;
                messageAckData.mesuresetAckInfo[i].used = true;
                rc_ok = true;
                if (i > messageAckData.highest) {
                    LOG_DBG( LOG_LEVEL_CLI,"enterMessageIdAckInfo: messageId NACK count = %d\n",i);
                    messageAckData.highest++;
                }
                break;
            }
        }
        if (rc_ok == false) {
            LOG_DBG( LOG_LEVEL_CLI,"enterMessageIdAckInfo: number of not acknowledged messages reached %d !\n",MAX_DATASET_NACK);
        }

        // return access to the data struct
        xSemaphoreGive( messageAckData.semAccess );
    }

    return rc_ok;
}

static tMeasureSetAckInfo * lookupAckInfo(int32_t messageId)
{
    tMeasureSetAckInfo *  info_p=NULL;

    for (uint16_t i=0; i<MAX_DATASET_NACK; i++) {
        if (messageAckData.mesuresetAckInfo[i].used  && (messageAckData.mesuresetAckInfo[i].messageId == messageId )) {
            info_p = &messageAckData.mesuresetAckInfo[i];
            break;
        }
    }
    return info_p;
}

static tMeasureSetAckInfo * findMessageIdAckInfo(int32_t messageId, uint32_t maxWaitMs)
{
    tMeasureSetAckInfo * info_p = NULL;
    if (pdTRUE != xSemaphoreTake(   messageAckData.semAccess , maxWaitMs/portTICK_PERIOD_MS)) {
        LOG_DBG(LOG_LEVEL_MODEM,"enterMessageIdAckInfo: access semTake failed\n" );
    } else {
        info_p = lookupAckInfo(messageId);
        // return access to the data struct
        xSemaphoreGive( messageAckData.semAccess );

    }
    return info_p;
}

static bool removeMessageIdAckInfo(int32_t messageId, uint32_t maxWaitMs)
{
    bool rc_ok = false;
    // grab semaphore
    if (pdTRUE != xSemaphoreTake(   messageAckData.semAccess , maxWaitMs/portTICK_PERIOD_MS)) {
        LOG_DBG(LOG_LEVEL_MODEM,"enterMessageIdAckInfo: access semTake failed\n" );
    } else {
        tMeasureSetAckInfo * info_p;
        info_p = lookupAckInfo(messageId);
        if (info_p != NULL) {
            info_p->used = false;
            rc_ok = true;
        } else {
            LOG_DBG( LOG_LEVEL_CLI,"removeMessageIdAckInfo: messageId %d not found !\n",messageId);
        }

        // return access to the data struct
        xSemaphoreGive( messageAckData.semAccess );
    }
    return rc_ok;
}


#endif

typedef union {
    int32_t i;
    float f;
} tInt32Float;

static struct  {
    uint16_t total_records;
    uint16_t current_index;
    uint16_t max_records_in_block;
    uint64_t timestamp;
} temperature_status;

static const IDEF_paramid_t idefExtTempParam_List[] =
{
        IDEFPARAMID_TEMPERATURE_EXTERNAL
};
static SvcDataParamValueGroup_t  TemperatureIdefParamValue =
{
        MR_Timestamp, 1,  (IDEF_paramid_t *) idefExtTempParam_List
};

static  SvcDataData_t TemperatureRecordData =
{
        1, &TemperatureIdefParamValue, NULL
};


// allow other code to access the handle
tCommHandle* getCommHandle()
{
	return &CommHandle;
}

// Callback function for encoding the temperature records.
bool PrepareExtTempUpload_Cb(uint32_t recIter, const SvcDataParamValueGroup_t * dpvg_p)
{
	bool retval = false;
	extFlash_NodeTemperatureRecord_t tempRec;
	uint32_t temp_index = temperature_status.current_index + recIter;

	if ((recIter < temperature_status.max_records_in_block) && ( temp_index < temperature_status.total_records))
	{
		retval = Temperature_ReadRecords(temp_index, 1, (uint8_t *)&tempRec.record);
		measureRecord.timestamp = tempRec.record.timeStamp * 10000000ULL;
		measureRecord.params.Temperature_External = FIX_TEMP(tempRec.record.remoteTemp_degC);

		//LOG_DBG(LOG_LEVEL_CLI,"\nTempRead:%f, Timestamp:%llu, Iter:%d\n", measureRecord.params.Temperature_External, measureRecord.timestamp, recIter);
	}
	return retval;
}

// because of the protobuf encoding method, this function should always return the same parameters for the same input values !!!!
// it is called multiple times during the encoding process.
// so no increment/statechange actions in this function
bool updateExtTemperatureValues(uint32_t block_iter, const SvcDataParamValueGroup_t * dpvg_p)
{
    uint32_t temp_index = temperature_status.current_index + block_iter;
    if ( (block_iter < temperature_status.max_records_in_block) && ( temp_index < temperature_status.total_records)) {
        // now some fake values
        measureRecord.timestamp = (temperature_status.timestamp + ((block_iter + temperature_status.current_index) *10))*10000000ULL;// for generating fake timestemps every 10 seconds
        measureRecord.params.Temperature_External = (block_iter + temperature_status.current_index)/10.0;
        return true;// yes, this record should be sent
    } else {
        return false;// we are done
    }
}

static bool init_temperature_sending(bool simulationMode)
{
    bool rc_ok ;

    if (simulationMode)
    {
    	temperature_status.max_records_in_block = 100;
        temperature_status.total_records = 222;
        temperature_status.current_index = 0;
        temperature_status.timestamp = ConfigSvcData_GetIDEFTime()/10000000ULL - temperature_status.total_records *10; // for generating fake timestemps every 10 seconds
        TemperatureRecordData.dataData_cb = (tSvcDataDataCallbackFuncPtr)&updateExtTemperatureValues;
        rc_ok = true;
    }
    else
    {
    	temperature_status.max_records_in_block = 100;// this depends on the buffer size used in the encoding process, has to be found empyrical, and keep a safety margin because protobuf does some compression.
		temperature_status.current_index = 0;
    	rc_ok = Temperature_RecordCount(&temperature_status.total_records);
    	TemperatureRecordData.dataData_cb = (tSvcDataDataCallbackFuncPtr)&PrepareExtTempUpload_Cb;
    }

    LOG_DBG(LOG_LEVEL_CLI,"TotalTemperatureRecords:%d\n", temperature_status.total_records);
    return rc_ok;
}

// in place conversion from int32 to float format (both are 32bits)
void cvt_int32ToFloat(tInt32Float * value_p, uint32_t count, float scaling)
{
    while (count--) {
        value_p->f = value_p->i * scaling;
        value_p++;
    }
}

bool xTaskAppCommsTestInit()
{
    EventQueue_App         = xQueueCreate( EVENTQUEUE_NR_ELEMENTS_APP, sizeof(tAppEvent));
    return true;
}

const char msgCBCalled[] = "app():%s(%08x) called\n";
const char msgCBQueue[] = "app():%s: xQueueSend failed\n";

static uint32_t handleFirmwareUpdateNotification_cb(void * buf)
{
	LOG_DBG( LOG_LEVEL_CLI, msgCBCalled, __func__, buf);

	tAppEvent  event;

	// fill the message struct
	event.Descriptor = AppEvt_idef_firmwareUpdateNotification;

	// send message to queue
	if (pdTRUE != xQueueSend( EventQueue_App, &event, 0 ))
	{
		LOG_DBG(LOG_LEVEL_CLI, msgCBQueue, __func__);
	}

	return 0;
}


// these callbacks run in the comm task context !
static uint32_t handleStoreDataReply_cb(void * buf)
{
	tIdefStoreDataRep * data = (tIdefStoreDataRep * ) buf;
	LOG_DBG( LOG_LEVEL_CLI, msgCBCalled, __func__, buf);

	tAppEvent  event;

	// fill the message struct
	event.Descriptor = AppEvt_idef_storeDataReply;
	event.data.reply_result_code = data->result_code;

	// setrtctime(data->timestamp)

	// send message to queue
	if (pdTRUE != xQueueSend( EventQueue_App, &event, 0 ))
	{
		LOG_DBG(LOG_LEVEL_CLI, msgCBQueue, __func__);
	}

	return 0;
}

static uint32_t handleStoreDataRequest_cb(void * buf)
{
    tIdefStoreDataReq * data = (tIdefStoreDataReq * ) buf;
    tAppEvent  event;

    LOG_DBG( LOG_LEVEL_CLI, msgCBCalled, __func__, buf);

    // fill the message struct
    event.Descriptor = AppEvt_idef_storeDataRequest;
    event.data.IdefStoreDataReq.message_id = data->message_id;
    event.data.IdefStoreDataReq.timestamp = data->timestamp;

    // send message to queue
    if (pdTRUE != xQueueSend( EventQueue_App, &event, 0 ))
    {
        LOG_DBG(LOG_LEVEL_CLI, msgCBQueue, __func__);
    }

    if(commsRecord.params.Wakeup_Reason == WAKEUP_CAUSE_MAGNET)
	{
		xTaskApp_flashStatusLed(4);
	}

    return 0;
}

//#define UNBUFFERED_HANDLEGETDATAREQ
#ifdef UNBUFFERED_HANDLEGETDATAREQ
// local copy of the received IDEF parameter list
static struct getDataRequest_str{
    bool inUse;
    SvcDataParamValueGroup_t  paramGroupList;
    IDEF_paramid_t paramId[MAX_GETDATAPARAMS];
}    getDataRequestdata;

static uint32_t handleGetDataRequest_cb(void * buf)
{
    tIdefGetDataReq * data = (tIdefGetDataReq * ) buf;
    LOG_DBG( LOG_LEVEL_CLI, "app():handleGetDataRequest_cb(%08x) called\n", buf);
    if (false == getDataRequestdata.inUse) {
        uint32_t i;
        tAppEvent  event;

        // we need a local copy, because the buffers belong to another task !
        getDataRequestdata.paramGroupList.numberOfParamValues = data->paramGroupList_p->numberOfParamValues;

        for (i=0; i<getDataRequestdata.paramGroupList.numberOfParamValues; i++) {
            getDataRequestdata.paramId[i] = data->paramGroupList_p->ParamValue_p[i];
        }
        getDataRequestdata.paramGroupList.ParamValue_p = getDataRequestdata.paramId;
        getDataRequestdata.inUse = true;

        // fill the message struct
        event.Descriptor = AppEvt_idef_getDataRequest;
        event.data.IdefGetDataReq.message_id = data->message_id;
        event.data.IdefGetDataReq.paramGroupList_p = &getDataRequestdata.paramGroupList;
        // send message to queue
        if (pdTRUE != xQueueSend( EventQueue_App, &event, 0 )) {
            LOG_DBG(LOG_LEVEL_CLI,"handleGetDataRequest_cb: xQueueSend failed\n");
        }
    } else {
        LOG_DBG(LOG_LEVEL_CLI,"app():handleGetDataRequest_cb: previous request not (yet) handled\n");
    }

    return 0;
}

#else
#define MAXGETDATAREQBUFFERS (4)
// local copy of the received IDEF parameter list(s)
static struct getDataRequest_str{
    uint16_t in,out,cnt;

    struct {
        SvcDataParamValueGroup_t  paramGroupList;
        IDEF_paramid_t paramId[MAX_GETDATAPARAMS];
    } queue[MAXGETDATAREQBUFFERS];
}    getDataRequestdata;

static void init_handleGetDataRequest()
{
    getDataRequestdata.cnt=0;
    getDataRequestdata.in=0;
    getDataRequestdata.out=0;
}

static uint32_t handleGetDataRequest_cb(void * buf)
{
    tIdefGetDataReq * data = (tIdefGetDataReq * ) buf;
    LOG_DBG( LOG_LEVEL_CLI, "app():handleGetDataRequest_cb(%08x) called\n", buf);
    if (getDataRequestdata.cnt < MAXGETDATAREQBUFFERS) {
        uint32_t i;
        tAppEvent  event;
        uint16_t in = getDataRequestdata.in;

        getDataRequestdata.cnt++;

        // we need a local copy, because the buffers belong to another task !
        getDataRequestdata.queue[in].paramGroupList.numberOfParamValues = data->paramGroupList_p->numberOfParamValues;

        for (i=0; i<getDataRequestdata.queue[in].paramGroupList.numberOfParamValues; i++) {
            getDataRequestdata.queue[in].paramId[i] = data->paramGroupList_p->ParamValue_p[i];
        }
        getDataRequestdata.queue[in].paramGroupList.ParamValue_p = getDataRequestdata.queue[in].paramId;
        if (++getDataRequestdata.in >= MAXGETDATAREQBUFFERS) getDataRequestdata.in=0;

        // fill the message struct
        event.Descriptor = AppEvt_idef_getDataRequest;
        event.data.IdefGetDataReq.message_id = data->message_id;
        event.data.IdefGetDataReq.paramGroupList_p = &getDataRequestdata.queue[in].paramGroupList;
        // send message to queue
        if (pdTRUE != xQueueSend( EventQueue_App, &event, 0 )) {
            LOG_DBG(LOG_LEVEL_CLI,"handleGetDataRequest_cb: xQueueSend failed\n");
        }
    } else {
        LOG_DBG(LOG_LEVEL_CLI,"app():handleGetDataRequest_cb: previous request(s) not (yet) handled\n");
    }

    return 0;
}


#endif


// END : these callbacks run in the comm task context !

static bool generate_dummy_measurement_record(uint32_t seed)
{
    int i = seed;
    measure_record_t * pMR = &measureRecord;

    printf("Create dummy measurement %d\n",i);
    // flashread measurement  to ram structure
    // for now, fill it with rubbish
    pMR->timestamp = ConfigSvcData_GetIDEFTime();//12340000+i;
    pMR->params.Is_Move_Detect_Meas = (bool) (i & 1);
    pMR->params.Temperature_Pcb = 25.1+i;
    pMR->params.Temperature_External = 15.2+i;
    pMR->params.GNSS_Lat_1 = 600+i;
    pMR->params.GNSS_NS_1[0] = 'N';
    pMR->params.GNSS_NS_1[1] = '\0';
    pMR->params.GNSS_Long_1 = 900+i;
    pMR->params.GNSS_EW_1[0] = 'W';
    pMR->params.GNSS_EW_1[1] = '\0';
    pMR->params.GNSS_Speed_To_Rotation_1 = 30.3+i;
    pMR->params.GNSS_Course_1 = 40+i;
    pMR->params.GNSS_Time_To_Fix_1 = 50+i;
    pMR->params.Is_Speed_Within_Env = (i&1) == 0;
    pMR->params.GNSS_Speed_To_Rotation_2 = 60.6+i;

    pMR->params.Rotation_Diff = 80.8+i;
    pMR->params.GNSS_Time_Diff = 90.9+i;
    pMR->params.Is_Good_Speed_Diff = (i&1)==1;
    for (int j=0; j<MAX_GNSS_SATELITES; j++) {
        pMR->params.GNSS_Sat_Id[j] = i*20+j;
        pMR->params.GNSS_Sat_Snr[j] = i+j;
    }
    return true;
}

void print_measurement_record(void)
{
#ifdef DEBUG
  if (dbg_logging & LOG_LEVEL_CLI)
  {
	  measure_record_t * pMR = &measureRecord;

	  printf("\nMEASUREMENT RECORD:\n");
	  printf("Timestamp             = %s\n", RtcUTCToString(pMR->timestamp / 10000000LL));
      printf("Gating Reason         = %s\n", gatingReason(pMR->params.Energy_Remaining));
      printf("Is_Move_Detect_Meas   = %d\n", pMR->params.Is_Move_Detect_Meas);
      printf("Temperature_Pcb       = %f\n", pMR->params.Temperature_Pcb);
      printf("Temperature_External  = %f\n", pMR->params.Temperature_External);
      printf("GNSS_Lat_1            = %f\n", pMR->params.GNSS_Lat_1);
      printf("GNSS_NS_1             = %s\n", pMR->params.GNSS_NS_1);
      printf("GNSS_Long_1           = %f\n", pMR->params.GNSS_Long_1);
      printf("GNSS_EW_1             = %s\n", pMR->params.GNSS_EW_1);
      printf("GNSS_Speed_To_Rotation_1 = %f\n", pMR->params.GNSS_Speed_To_Rotation_1);
      printf("GNSS_Course_1         = %d\n", pMR->params.GNSS_Course_1);
      printf("GNSS_Time_To_Fix_1    = %d\n", pMR->params.GNSS_Time_To_Fix_1);
      printf("Is_Speed_Within_Env   = %d\n", pMR->params.Is_Speed_Within_Env);
      printf("GNSS_Speed_To_Rotation_2 = %f\n", pMR->params.GNSS_Speed_To_Rotation_2);
      printf("Rotation_Diff         = %f\n", pMR->params.Rotation_Diff);
      printf("GNSS_Time_Diff        = %f\n", pMR->params.GNSS_Time_Diff);
      printf("Is_Good_Speed_Diff    = %d\n", pMR->params.Is_Good_Speed_Diff);
      printf("GNSS_Sat_Id  =");
      for (int i=0; i<MAX_GNSS_SATELITES; i++) printf(" %3d", pMR->params.GNSS_Sat_Id[i]);
      printf("\n");
      printf("GNSS_Sat_Snr =");
      for (int i=0; i<MAX_GNSS_SATELITES; i++) printf(" %3d", pMR->params.GNSS_Sat_Snr[i]);
      printf("\n\n");
  }
#endif
}


/*
 * GetDatetimeInSecs
 *
 * @desc	gets the RTC time in seconds
 *
 * @returns time in seconds
 */
uint32_t GetDatetimeInSecs(void)
{
	uint32_t seconds = 0;

    RtcGetDatetimeInSecs( &seconds );
    return seconds;
}


static bool sendEventLog()
{
#ifdef CONFIG_PLATFORM_EVENT_LOG
    tEventLog_inFlash * addrEventLog = NULL;
    tEventLog_upload localLogCopy;
    uint8_t *logTopic = (uint8_t * ) mqttGetLogTopic();
    int logsSent = 0;

	// re-init the buffer details
	EventLog_InitFlashData();

	// loop sending log entries till complete
	while (EventLog_getLog(&addrEventLog))
    {
#ifdef DEBUG
        if (dbg_logging & LOG_LEVEL_CLI) {
            EventLog_printLogEntry(addrEventLog);
        }
#endif
       // dammit, we need to set it in network byte order, so have to copy it to RAM
        strcpy(&localLogCopy.logMsg[0], &addrEventLog->logMsg[0]);
        localLogCopy.woLogMsg.compNumber =  __builtin_bswap16(addrEventLog->logHeader.compNumber);
        localLogCopy.woLogMsg.eventCode  =  __builtin_bswap16(addrEventLog->logHeader.eventCode);
        localLogCopy.woLogMsg.unixTimestamp = __builtin_bswap64(addrEventLog->logHeader.unixTimestamp);
        localLogCopy.woLogMsg.sevLvl = addrEventLog->logHeader.sevLvl;
        localLogCopy.woLogMsg.repCounter = 0;
        localLogCopy.woLogMsg.logStart = '#';
        localLogCopy.woLogMsg.frameLength = sizeof(localLogCopy.woLogMsg) + strlen(localLogCopy.logMsg) + 1;

        LOG_DBG(LOG_LEVEL_CLI,"send to logtopic: %s\n",logTopic);
        if (COMM_ERR_OK != TaskComm_Publish( &CommHandle,  &localLogCopy , localLogCopy.woLogMsg.frameLength, logTopic,  10000))
        {
            // we already logged an event so no need to do it again
            return false;
        }
        logsSent++;
    }

    // next message signals that the log is read upto that point.
    if(logsSent > 0)
	{
        EventLog_Clear();
        LOG_DBG(LOG_LEVEL_CLI,"\n<<< %d Event Log Entries Uploaded & cleared >>>\n\n", logsSent);
	}
    else
    {
        LOG_DBG(LOG_LEVEL_CLI,"\n<<< No Event Log Entries Uploaded >>>\n\n");
    }
#endif

    return true;
}

extern int32_t __sample_buffer[];// defined in the linker file, used for transfering the waveforms

#if 1
#define NUMSINE (2)

static struct  {
    float offset;
    float sinefreqs[NUMSINE];
    float sineamps[NUMSINE];
} simulSignalParams = { 0, {5000.0, 80.0}, {0.5, 0.5} };

void setSimulSignalParams(float offset, float sinefreq, float sineamp, float modfreq, float moddepth, float sample_freq)
{
    simulSignalParams.offset = offset;
    simulSignalParams.sinefreqs[0] = sinefreq;
    simulSignalParams.sinefreqs[1] = modfreq;
    simulSignalParams.sineamps[0] = sineamp;
    simulSignalParams.sineamps[1] = moddepth;
}

static struct {
    int32_t amp;
    int32_t offset;
    int32_t alpha,beta;
    int32_t Tcos,Tsin;
} simul_am[NUMSINE];

void initSimulAmSignal(float sample_freq)
{
    float two_pi = 8*atan(1.0);
    float d_theta;
    int i;


    simul_am[0].offset = simulSignalParams.offset * (1<<30);// abusing this for the final signal offset, not for the carrier freq offset

    for (i=0; i<NUMSINE; i++) {
        float tmp;

        d_theta = two_pi * simulSignalParams.sinefreqs[i]/sample_freq;
        tmp = sin(d_theta/2);
        simul_am[i].alpha = (1<<30) * 2.0 *tmp*tmp ;
        simul_am[i].beta =  (1<<30) * sin(d_theta);

        simul_am[i].Tcos = ((double) simulSignalParams.sineamps[i]) * (double)((1<<30)-1);// take care, 2 complement has 1 bit larger range negative than positive, that is why the '-1' is put in.
        simul_am[i].Tsin = 0;
    }
}

void calcSimulAmSignal(uint32_t samples, int32_t * out);
static bool generate_dummy_waveform(uint32_t sampleRate, uint32_t samples);

static const char simulAmSignalHelp[] = {
        "simulAmSignal sets the parameters for the AM signal generator for generating test and simulation signals\r\n"
        "Maximum amplitude of 1.0 gives full range ADC signals, so (carrier amplitude) + (depth amplitude)/2 must stay below 1.0\r\n"
        "Example 5Khz carrier with 80Hz modulation depth of 50% and no DC offset :\r\n simulAmSignal 0.5 5000 0.50 80 0\r\n"
} ;

bool cliExtHelpSimulAmSignal(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
    printf("%s",simulAmSignalHelp);
    return true;
}

bool cliSimulAmSignal( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    bool rc_ok = true;;

    if (args >= 1) {
        if (args==1) {
        // 1 argument, can use it for enable/disable
            printf("enable/disable real time sample simulation not yet implemented\n");
            rc_ok = false;
        } else {
            simulSignalParams.offset = 0;

            if (args>=2) {
                // 2 arguments, simple sine, no modulation
                simulSignalParams.sineamps[0] = atof((char *)argv[0]);
                simulSignalParams.sinefreqs[0] = atof((char *)argv[1]);
                simulSignalParams.sineamps[1] = 0;
                simulSignalParams.sinefreqs[1] = 0;
                if (args==3) {
                    // lets assume that will be a dc offset for a simple sine
                    simulSignalParams.offset = atof((char *)argv[2]);
                }
            }
            if (args>=4) {
                simulSignalParams.sineamps[1] = atof((char *)argv[2]);
                simulSignalParams.sinefreqs[1] = atof((char *)argv[3]);
                if (args==5) {
                     // lets assume that will be a dc offset for a simple sine
                     simulSignalParams.offset = atof((char *)argv[4]);
                }
            }

        }
    } else {
        // print current values
        printf("simulAmSignal %f %f %f %f %f\n",
                simulSignalParams.sineamps[0],
                simulSignalParams.sinefreqs[0],
                simulSignalParams.sineamps[1],
                simulSignalParams.sinefreqs[1],
                simulSignalParams.offset);
        printf("So, signal is: %f*sin(%f)*(1+%f*sin(%f)) + %f\n",
                simulSignalParams.sineamps[0],
                 simulSignalParams.sinefreqs[0],
                 simulSignalParams.sineamps[1],
                 simulSignalParams.sinefreqs[1],
                 simulSignalParams.offset);

        {// just a simple printout for checking the ranges
            uint32_t samples;

            printf("Example result for 24bits ADC, with samplerate 16*(carrierfreq):\n");
            samples = 32;
            if((simulSignalParams.sineamps[1] != 0.0) && (simulSignalParams.sinefreqs[1] != 0.0))
            {
                samples *= simulSignalParams.sinefreqs[0]/simulSignalParams.sinefreqs[1];
            }

            if(samples>32768)
            {
            	samples=32768;// where is the correct define ?
            }

            generate_dummy_waveform(simulSignalParams.sinefreqs[0]*16, samples);

            for(int i = 0; i < samples; i++)
            {
                printf("%5d %d\n",i, __sample_buffer[i]);
            }
        }

    }

    return rc_ok;
}
#endif

static bool generate_dummy_waveform(uint32_t sampleRate, uint32_t samples)
{
    initSimulAmSignal( sampleRate);

    calcSimulAmSignal(samples, __sample_buffer);

    return true;
}

bool generate_dummy_communication_record(uint32_t seed)
{
    int i=seed;
    comms_record_t *pCR = &commsRecord;

    pCR->timestamp = ConfigSvcData_GetIDEFTime(); //56780000+i;

    pCR->params.EnergyRemaining_percent     = 0.1;
    pCR->params.Com_Signal_Quality   = 98-i;
    pCR->params.Is_Move_Detect_Com   = true;
    pCR->params.Acceleration_X       = 1.1+i;
    pCR->params.Acceleration_Y       = 2.2+i;
    pCR->params.Acceleration_Z       = 3.3+i;
    pCR->params.Is_Self_Test_Successful = true;
    for (int j=0; j<sizeof(pCR->params.Status_Self_Test)/sizeof(*pCR->params.Status_Self_Test);j++) {
            pCR->params.Status_Self_Test[j]     = i+j;
    }
    pCR->params.Humidity             = 4.4+i;
    pCR->params.Up_Time              = 22;
    pCR->params.GNSS_Lat_Com         = 5.5+i;
    pCR->params.GNSS_NS_Com[0]       = 'N';
    pCR->params.GNSS_NS_Com[1]       = '\0';
    pCR->params.GNSS_Long_Com        = 6.6+i;
    pCR->params.GNSS_EW_Com[0]       = 'E';
    pCR->params.GNSS_EW_Com[1]       = '\0';
    pCR->params.GNSS_Speed_To_Rotation_Com = 7.7+i;
    pCR->params.Time_Last_Wake       = ConfigSvcData_GetIDEFTime();
    pCR->params.Time_Elap_Registration = 100+i;
    pCR->params.Time_Elap_TCP_Connect  = 200+i;
    pCR->params.Time_Elap_TCP_Disconnect = 300+i;
    pCR->params.Time_Elap_Modem_On   = 400+i;
    pCR->params.V_0                  = 220+i;
    pCR->params.V_1                  = 230+i;
    pCR->params.V_2                  = 240+i;
    pCR->params.E_GNSS_Cycle         = 8.8+i;
    pCR->params.E_Modem_Cycle        = 9.9+i;
    pCR->params.E_Previous_Wake_Cycle = 10.01+i;
    return true;
}

void print_communication_record(void)
{
#ifdef DEBUG
  if (dbg_logging & LOG_LEVEL_CLI)
  {
	comms_record_t * pCR = &commsRecord;
    printf("\nCOMMUNICATION RECORD:\n");
    printf("Timestamp           = %s\n", RtcUTCToString(pCR->timestamp / 10000000LL));
    printf("Wakeup_Reason       = %d\n", pCR->params.Wakeup_Reason);
    printf("EnergyRemaining_percent    = %.2f\n", pCR->params.EnergyRemaining_percent);
    printf("Com_Signal_Quality  = %d\n", pCR->params.Com_Signal_Quality );
    printf("Is_Move_Detect_Com  = %d\n", pCR->params.Is_Move_Detect_Com );
    printf("Acceleration_X      = %f\n", pCR->params.Acceleration_X );
    printf("Acceleration_Y      = %f\n", pCR->params.Acceleration_Y );
    printf("Acceleration_Z      = %f\n", pCR->params.Acceleration_Z );
    printf("Is_Self_Test_Successful = %d\n", pCR->params.Is_Self_Test_Successful );
    printf("Status_Self_Test    =\n" );
    for (int j=0; j<sizeof(pCR->params.Status_Self_Test)/sizeof(*pCR->params.Status_Self_Test);j++) {
        printf(" %2d",pCR->params.Status_Self_Test[j]);
    }
    printf("\n");
    printf("Humidity            = %f\n", pCR->params.Humidity );
    printf("Up_Time             = %lu\n", pCR->params.Up_Time );
    printf("GNSS_Lat_Com        = %f\n", pCR->params.GNSS_Lat_Com );
    printf("GNSS_NS_Com         = %s\n", pCR->params.GNSS_NS_Com );
    printf("GNSS_Long_Com       = %f\n", pCR->params.GNSS_Long_Com );
    printf("GNSS_EW_Com         = %s\n", pCR->params.GNSS_EW_Com );
    printf("GNSS_Speed_To_Rotation_Com = %f \n", pCR->params.GNSS_Speed_To_Rotation_Com );
    printf("Time_Last_Wake          = %s\n", RtcUTCToString(pCR->params.Time_Last_Wake / 10000000LL));
    printf("Time_Elap_Registration  = %d\n", pCR->params.Time_Elap_Registration );
    printf("Time_Elap_TCP_Connect   = %d\n", pCR->params.Time_Elap_TCP_Connect );
    printf("Time_Elap_TCP_Disconnect= %d\n", pCR->params.Time_Elap_TCP_Disconnect );
    printf("Time_Elap_Modem_On      = %d\n", pCR->params.Time_Elap_Modem_On );
    printf("V_0  = %.2f\n", pCR->params.V_0 );
    printf("V_1  = %.2f\n", pCR->params.V_1 );
    printf("V_2  = %.2f\n", pCR->params.V_2 );
    printf("E_GNSS_Cycle            = %.2f\n", pCR->params.E_GNSS_Cycle );
    printf("E_Modem_Cycle           = %.2f\n", pCR->params.E_Modem_Cycle );
    printf("E_Previous_Wake_Cycle   = %.2f\n", pCR->params.E_Previous_Wake_Cycle );

  }
#endif
}
\


static bool setupConnection()
{
	bool rc_ok = false;
	int32_t rc = -1;

	LOG_DBG( LOG_LEVEL_CLI, "\nCommunication start.\n");

	do
	{
		if (CommHandle.EventQueue_CommResp == NULL)
		{
			if (false == TaskComm_InitCommands(&CommHandle))
			{
				LOG_DBG( LOG_LEVEL_CLI, "comm initialization Commands failed\n");
				break;
			}
		}

		rc = TaskComm_Connect(&CommHandle, 0);
		if ((rc != COMM_ERR_OK) )
		{
			LOG_DBG( LOG_LEVEL_CLI,  "TaskComm connect: failed (rc=%d)\n", rc);
			break;
		}

		// Wait till the connection ready
		uint32_t timeoutMs = Modem_getMaxTimeToConnectMs(false) + 5000;
		rc = TaskComm_WaitReady( &CommHandle,  timeoutMs);// max configured time + 5 seconds OK ?
		if ((rc != COMM_ERR_OK) || (CommHandle.CommResp.rc_ok == false) )
		{
			 LOG_DBG( LOG_LEVEL_CLI,  "TaskComm wait for connect: failed (rc=%d)\n", rc);
			 break;
		}

		if (ISVCDATARC_OK != ISvcData_Start())
		{
			LOG_DBG( LOG_LEVEL_CLI, "IDEF Data Service start: failed\n");
			break;
		}
		rc_ok = true;
	} while(0);

	return rc_ok;
}


static bool terminateConnection()
{
	bool rc_ok = true;

	// stop the data service
	if(ISVCDATARC_OK != ISvcData_Stop())
	{
		LOG_DBG( LOG_LEVEL_CLI, "IDEF Data Service stop: failed\n");
		rc_ok = false;
	}

	// close the connection
	if((0 != TaskComm_Disconnect(&CommHandle, portMAX_DELAY)) ||
		(CommHandle.CommResp.rc_ok == false))
	{
		LOG_DBG( LOG_LEVEL_CLI,  "taskComm disconnect: failed\n");
		rc_ok = false;
	}

	LOG_DBG( LOG_LEVEL_CLI, "\nCommunication done.\n");
    bWakeupIsMAGSwipe = false;

	return rc_ok;
}

struct {
    uint32_t measurement_index;
    bool ack_ok;
} storedataReply;



/*
 * checkIncommingMessages
 *
 * @desc	reads the event queue of the app, to see if a message was received
 * 			an processes it.
 *
 * @param 	maxWaitMs number of milliseconds the function will wait till a
 * 			message is received else it times out.
 *
 * @param	requestReceived_p the boolean is set to true if a message is received
 * 			and the boolean pointer supplied. NOTE that a boolean that is already
 * 			true will not be changed by this function
 *
 * @returns true if ok else false
 */
static bool checkIncommingMessages(uint32_t maxWaitMs, bool *requestReceived_p)
{
    tAppEvent  event;
    bool rc_ok = true;

    /*
     * Event handler
     */
    if (xQueueReceive( EventQueue_App, &event, maxWaitMs/portTICK_PERIOD_MS )) {


        switch(event.Descriptor) {
        case AppEvt_idef_storeDataReply:
            LOG_DBG(LOG_LEVEL_CLI, "\ncheckIncommingMessages: store data reply result code = %d\n", event.data.reply_result_code);
            // last storedataRequest has found its destination
            // notify that a measurement etc. can be marked sent
            if(commCLI_IsCommsAckIgnoreSet() == false)
            {
            	storedataReply.ack_ok = true;
            }
            // mark_as_sent( storedataReply.measurement_index );
            // send reply to server

            //if (requestReceived_p) *requestReceived_p = true; // do not count the reply from the server
            break;

        case AppEvt_idef_getDataRequest:
            {
                ISvcDataRc_t rc = ISVCDATARC_OK;

                LOG_DBG( LOG_LEVEL_CLI, "checkIncommingMessages: received getData request, now first send the publish with the paramvaluelists.\n");

                SvcDataData_t DataData;

                DataData.numberOfParamGroups = 1;
                DataData.ParamValueGroup_p = event.data.IdefGetDataReq.paramGroupList_p;
                DataData.dataData_cb = NULL;

                // timestamp with the current time
                event.data.IdefGetDataReq.paramGroupList_p->timestampDatastoreId = DR_currentIdefTime;
                currentIdefTime = ConfigSvcData_GetIDEFTime();
                LOG_DBG( LOG_LEVEL_CLI, "checkIncommingMessages:  send getdata requested parameters (%d) with a publish Data message.\n",DataData.ParamValueGroup_p->numberOfParamValues);

                // now send the requested parameter/value list
                rc = ISvcData_Publish_Data( &DataData, 0, SKF_MsgType_REPLY, event.data.IdefGetDataReq.message_id);
                if (rc != ISVCDATARC_OK) {
                    LOG_DBG( LOG_LEVEL_CLI, "checkIncommingMessages: ISvcData_Publish_Data failed, send reply with error code %d\n", -1);
                    rc = ISvcData_ReplyGetData( event.data.IdefGetDataReq.message_id, -1,  "ISvcData_Publish_Data failed.");
                    rc_ok = false;
                }

#ifdef UNBUFFERED_HANDLEGETDATAREQ
                getDataRequestdata.inUse = false;
#else
                getDataRequestdata.cnt--;
                if (++getDataRequestdata.out >= MAXGETDATAREQBUFFERS) getDataRequestdata.out = 0;
#endif

                if (requestReceived_p) *requestReceived_p = true;
            }
            break;

        case AppEvt_idef_storeDataRequest:
            {
                ISvcDataRc_t rc = ISVCDATARC_OK;

                LOG_DBG( LOG_LEVEL_CLI, "checkIncommingMessages: received storeData request.\n");
#if 0
                // to be added if we want time set by the server when gnss reception not available
                typedef enum {
                    TIMESOURCE_NONE=0, // initial value when no time has been received yet (rtc still somewhere in 1970).
                    TIMESOURCE_SERVER,
                    TIMESOURCE_GNSS,
                    TIMESOURCE_CLI
                }  tTimeSource;
                struct setTime_struct {
                    tTimeSource source;
                    uint64_t time;
                } setTime;


                if (gNvmCfg.dev.general.setTime.source == TIMESOURCE_NONE) {
                    // take this time if we did not set it
                    /* gNvmCfg.dev.general. */setTime.source = TIMESOURCE_SERVER;
                    /* gNvmCfg.dev.general. */setTime.idefTime = event.data.IdefStoreDataReq.timestamp;
                    /* set the real time clock () */

                }
#endif

                if (event.data.IdefStoreDataReq.timestamp > gNvmCfg.dev.general.timestamp) gNvmCfg.dev.general.timestamp = event.data.IdefStoreDataReq.timestamp; // TODO : make nicer interface for this global poking

                LOG_DBG( LOG_LEVEL_CLI, "checkIncommingMessages() send storeDataReply with ID= %d\n",event.data.IdefStoreDataReq.message_id);
                rc = ISvcData_ReplyStoreData( event.data.IdefStoreDataReq.message_id, 0,  NULL );

                if (rc != ISVCDATARC_OK) {
                    LOG_DBG( LOG_LEVEL_CLI, "reply  storeData: failed\n");
                }
                if (requestReceived_p) *requestReceived_p = true;
            }
            break;

        case AppEvt_idef_firmwareUpdateNotification:
        	// During the normal communication cycle a firmware Update Notification can be send
        	LOG_DBG( LOG_LEVEL_CLI, "checkIncommingMessages: received Firmware Update Notification.\n");
        	if (requestReceived_p) *requestReceived_p = true;
        	break;
        default:
            LOG_DBG( LOG_LEVEL_CLI, "checkIncommingMessages: unhandled event code %d\n", event.Descriptor);
            break;

        }

    } else {
        LOG_DBG( LOG_LEVEL_CLI, "checkIncommingMessages: Nothing received...\n");
    }
    return rc_ok;
}


static bool comms_test_wait_reply()
{
    bool rc_ok = true;
    uint32_t dummycount = 5;// poll 5 times ?

    while (rc_ok && dummycount--)
    {
        rc_ok = checkIncommingMessages(1000,NULL);// 100ms ?
    }

    return rc_ok;
}

/*
 * Server sends get_data request message, Device replies with OK and sends the requested data
 */

static bool comms_test_1(bool simulationMode)
{
    LOG_DBG( LOG_LEVEL_CLI,  "\ncomms_test_1: Server sends get_data request message, Device replies with OK and sends the requested data\n");

    return comms_test_wait_reply();
}

/*
 * Device sends store_data request message. Server sends store_data reply OK message back.
 */
static bool comms_test_2(bool simulationMode)
{
    bool rc_ok = true;
    bool sendingDone = false;
    uint32_t dummycount = 5;

    LOG_DBG( LOG_LEVEL_CLI,  "\ncomms_test_2: Device sends store_data request message(s). Server sends store_data reply OK message back.\n");

    while (rc_ok && !sendingDone)
    {
        rc_ok = checkIncommingMessages(1000,NULL);
        if (dummycount)
        {
            // only if previous measurement data is ack'ed
            if (storedataReply.ack_ok == true)
            {
                uint32_t messageId;

                LOG_DBG( LOG_LEVEL_CLI,  "Send dummy measurement record\n");
                // lets assume 'dummycount' measurements are ready to send
                generate_dummy_measurement_record(dummycount);

                storedataReply.ack_ok = false;
                storedataReply.measurement_index = dummycount;
                LOG_DBG( LOG_LEVEL_CLI, "\nsend measurement data (storedatarequest)  %d\n",dummycount);
                rc_ok = (ISVCDATARC_OK == ISvcData_RequestStoreData( (SvcDataData_t * ) &idefDataDataRecords[IDEF_dataMeasureRecord], &messageId));
                // TODO wait for store data reply, should have same messageId
                if (rc_ok==false)
                {
                	LOG_DBG( LOG_LEVEL_CLI, "\nISvcData_RequestStoreData not OK\n");
                }
            }
            else
            {
                LOG_DBG( LOG_LEVEL_CLI, "\nnot sending next storedata because no reply for previous received\n");
            }

            dummycount--;
        }
        else
        {
            sendingDone = true;
        }
    }

    if (rc_ok)
    {
    	rc_ok = checkIncommingMessages(1000,NULL);// 100ms ?
    }


    return rc_ok;
}

/*
 * Server sends store_data request message. Device sends store_data reply OK message back and stores the data.
 */
static bool comms_test_3(bool simulationMode)
{
    LOG_DBG( LOG_LEVEL_CLI,  "\ncomms_test_3: Server sends store_data request message. Device sends store_data reply OK message back and stores the data.\n");

    return comms_test_wait_reply();
}
/*
 * Device sends one waveform, server should eat it
 */
static bool comms_test_4(bool simulationMode)
{
    LOG_DBG( LOG_LEVEL_CLI,  "\ncomms_test_4: Device sends one waveform, server should eat it.\n"
                             "\nSend dummy raw waveform\n");

    generate_dummy_waveform(gNvmCfg.dev.measureConf.Sample_Rate_Raw, gNvmCfg.dev.measureConf.Samples_Raw);
    cvt_int32ToFloat( (tInt32Float *) __sample_buffer, gNvmCfg.dev.measureConf.Samples_Raw, gNvmCfg.dev.measureConf.Scaling_Raw);

    timestamp_raw = ConfigSvcData_GetIDEFTime();//12350000+dummycount; // rubbish example value
    bool rc_ok = (ISVCDATARC_OK == ISvcData_Publish_Data( (SvcDataData_t * ) &idefDataDataRecords[IDEF_dataWaveformRaw], gNvmCfg.dev.measureConf.Samples_Raw, SKF_MsgType_PUBLISH, 0));// raw
    if (rc_ok==false)
    {
    	LOG_DBG( LOG_LEVEL_CLI, "\nISvcData_Publish_Data not OK\n");
    }

    return comms_test_wait_reply();
}


/*
 * comms_test_10
 *
 * @brief Communication test used for Unit test, to test the firmware update notification
 *
 * @return false if connection failed else return true
 *
 */
static bool comms_test_10()
{
    LOG_DBG( LOG_LEVEL_CLI,  "\ncomms_test_10: Server sends firmware update notification.\n");

    return comms_test_wait_reply();
}

/*
 * comms_test_11
 *
 * @brief Communication test used for Unit test, Device sends Firmware Block Request
 *
 * @return false if connection failed else return true
 *
 */
static bool comms_test_11()
{
	LOG_DBG( LOG_LEVEL_CLI,  "\ncomms_test_11: Device sends Firmware Block Request.\n");

	bool rc_ok = (ISVCFIRMWARERC_OK == ISvcFirmware_Block_Request(0x20,0x323,DataStore_GetUint32(DR_Firmware_Version, 0)));// raw
	if (rc_ok==false)
	{
		LOG_DBG( LOG_LEVEL_CLI, "\nISvcFirmware_Block_Request not OK\n");
	}

	return rc_ok;
}

/*
 * comms_test_12
 *
 * @brief Communication test used for Unit test, Server sends firmware block reply.
 *
 * @return false if connection failed else return true
 *
 */
static bool comms_test_12()
{
    LOG_DBG( LOG_LEVEL_CLI,  "\ncomms_test_12: Server sends firmware block reply.\n");

    return comms_test_wait_reply();
}

/*
 * BEGIN interface functions to read the data stored in external flash
 * interface buffer for the moment the big __samples[] buffer
 */

typedef struct
{
    uint16_t	measurementSetNr;
    uint16_t	waves;
} whatToSend;

// some local administration, probably more needed, or can we do without it all
static struct {
    bool simulation_mode;
    uint16_t totalNoOfBlocks;
    uint16_t blocksLeft;
} dataUploadVars;


/*
 *
    communication cycle always does :

    - start communication
        while something to send :
            send measurement record with optional associated waveform(s)
        end while
    - sends communications record
    - ends communication

 */

/*
 * initialize internal data structure(s) for getting the stuff out of flash
 */
static void initDataToUpload(bool simulationMode)
{
    dataUploadVars.simulation_mode = simulationMode;

    if (simulationMode)
    {
        // SIMULATION CODE

        // simulated get data from flash
        dataUploadVars.blocksLeft = 1;
        dataUploadVars.totalNoOfBlocks = 1;
        // END SIMULATION CODE
    }
    else
    {
        // REAL GET FROM FLASH CODE
        // real get the data from flash
        dataUploadVars.blocksLeft =
        dataUploadVars.totalNoOfBlocks = gNvmData.dat.is25.noOfCommsDatasetsToUpload + gNvmData.dat.is25.noOfMeasurementDatasetsToUpload;
        // END REAL GET FROM FLASH CODE
    }

}


/*
 * confirmation that a dataset is sent
 * TODO : (out of performance reasons, it may be changed that it is only effective e when all data has been sent, so when the measurementSetNr is the last of all data)
 *
 */
static void ackUploadedData(uint16_t  measurementSetNr)
{
    if (dataUploadVars.simulation_mode)
    {
        // SIMULATION CODE

        // simulate that we did send a block
        if(dataUploadVars.blocksLeft)
        {
        	dataUploadVars.blocksLeft--;
        }
    }
    else
    {
        // REAL 'GET FROM FLASH' CODE
        // administer that 'measurementSetNr' is save to remove

    	//if (dataUploadVars.blocksLeft) dataUploadVars.blocksLeft--;

    	dataUploadVars.totalNoOfBlocks--;

    	if(measurementSetNr != COMMS_RECORD)
    	{
    		if(++gNvmData.dat.is25.measurementDatasetStartIndex >= MAX_NUMBER_OF_DATASETS)
    		{
    			gNvmData.dat.is25.measurementDatasetStartIndex = FIRST_DATASET;
    		}

    		if(gNvmData.dat.is25.noOfMeasurementDatasetsToUpload > 0)
    		{
    			gNvmData.dat.is25.noOfMeasurementDatasetsToUpload--;
    		}

    		LOG_DBG( LOG_LEVEL_APP,
    				"%s() - noOfMeasurementDatasetsToUpload %d\n",
					__func__,
    				gNvmData.dat.is25.noOfMeasurementDatasetsToUpload);
    	}
    	else
    	{
    		gNvmData.dat.is25.noOfCommsDatasetsToUpload = 0x00;		// --;
    		Vbat_SetFlag(VBATRF_FLAG_LAST_COMMS_OK);

    		// set alarm lockouts if required
    		alarms_SetLockouts();

    		if(Device_HasPMIC())
    		{
    			if(Vbat_IsFlagSet(VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG + VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG))
    			{
    				// Clear the temperature alarm flags.
					Vbat_ClearFlag(VBATRF_FLAG_RED_TEMPERATURE_ALARM_TRIG + VBATRF_FLAG_AMBER_TEMPERATURE_ALARM_TRIG);
    			}
    			else
    			{
    				Vbat_SetAlarmState(TEMPERATURE_ALARM_RE_ARM);
    			}
    		}

    		LOG_DBG( LOG_LEVEL_APP,
    				"\n"
    				"%s() - noOfCommsDatasetsToUpload: %d\n\n"
    				"########## COMMS SUCCESSFUL, ACK RECEIVED ##############\n",
					__func__,
    				gNvmData.dat.is25.noOfCommsDatasetsToUpload);

			// Clear the temperature measurement storage.
    		Temperature_EraseAllRecords();

			// Clear the gated measurement storage.
    		ExtFlash_eraseGatedMeasData();
    	}

    	// END REAL GET FROM FLASH CODE
    }
}

// should return what kind of data we must send back.
// if no data left to send, it should return FALSE
/*
 * returns a structure with the mesurement number to retrieve, and flags which signal what waveform belong to it.
 * if no data left to send, it should return FALSE
 */
static bool checkWhatDataToUpload( whatToSend * what)
{
	if (NULL == what)
	{
		return false;
	}

    if (dataUploadVars.simulation_mode)
    {
        // SIMULATION CODE

        // simulated what to get from flash administration
        if(dataUploadVars.blocksLeft > 0)
        {
            what->waves = (1 << IS25_RAW_SAMPLED_DATA) | (1 << IS25_VIBRATION_DATA) | (1 << IS25_WHEEL_FLAT_DATA);
            what->measurementSetNr =  dataUploadVars.totalNoOfBlocks - dataUploadVars.blocksLeft  +1;
            return true;
        }
        // END SIMULATION CODE
        return false;
    }

    // REAL GET FROM FLASH CODE
	// the real what to get from flash administration
	if(0 == dataUploadVars.totalNoOfBlocks)
	{
		return false;
	}

	// anymore measurements to upload ?
	if(0 == gNvmData.dat.is25.noOfMeasurementDatasetsToUpload)
	{
		// no more measurements to upload. ONLY comms records pending to be sent.
		what->measurementSetNr = COMMS_RECORD;
		LOG_DBG(LOG_LEVEL_APP, "\nNo Measurements to Upload, Next Send Temperature Records >>>>>\n");
		return false;	// Return false to indicate the end of measurements to upload.
	}
	what->measurementSetNr = gNvmData.dat.is25.measurementDatasetStartIndex;
	what->waves = 0;

	if((COMMS_RECORD != what->measurementSetNr) && (dataUploadVars.blocksLeft > 0))
	{   // waveforms and measurement record
		// check which waveforms are stored !
		what->waves = extFlash_getMeasureSetInfo(what->measurementSetNr);
		LOG_DBG(LOG_LEVEL_CLI,  "what->cycle = uploadWaveformCycle - dataset no %d\r\n", what->measurementSetNr);
		return true;
	}

	LOG_DBG(LOG_LEVEL_CLI,  "what->cycle = magnetSwipeCycle - dataset no %d\r\n", what->measurementSetNr);

	// END REAL GET FROM FLASH CODE

	return true;
}

// so with every measurement block, we get a comms block thrown in, but we have only one comms block to send at the end of the communication cycle,
// so which one will it be ??
/*
 * retrieve the measurement record indicated by measurementSetNr, return it in measureRecord_p
 * return false when something goes wrong.
 */
static int getMeasurementRecord(uint16_t measurementSetNr)
{
    if (dataUploadVars.simulation_mode)
    {
        // SIMULATION CODE

        // simulated record
        LOG_DBG( LOG_LEVEL_CLI,  "Make dummy measurement record\n");
        // lets assume 'dummycount' measurements are ready to send, and use dummycount as 'seed' for the dummydata
        generate_dummy_measurement_record(measurementSetNr);

        return sizeof(measureRecord);
        // END SIMULATION CODE
    }

    // REAL GET FROM FLASH CODE
	// real get from flash
	LOG_DBG( LOG_LEVEL_CLI,  "\nRetrieve measurement record from external flash for timestamp - sending later, MeasSetNum:%d \r\n", measurementSetNr);
	return extFlash_read(&extFlashHandle, (uint8_t*)&measureRecord, sizeof(measureRecord), IS25_MEASURED_DATA,  measurementSetNr, EXTFLASH_MAXWAIT_MS);
	// END REAL GET FROM FLASH CODE
}


/*
 * retrieve the waveform selected by measurementSetNr and waveform type, which belongs to
 * the measurement record retrieved under the same measurementSetNr and send waveform if ok
 * returns also the number of samples in the waveform.
 *
 * return false when something goes wrong.
 */

static bool sendWaveform(whatToSend *what, uint16_t waveformType,  float scaling, int IDEF_data)
{
	const char *strType[] =
	{
			"raw",
			"vibration",
			"wheelflat"
	};
	const char *strWave[] =
	{
			"IS25_RAW_SAMPLED_DATA",
			"IS25_VIBRATION_DATA",
			"IS25_WHEEL_FLAT_DATA"
	};
	uint32_t sampleRate, samples = 0;
    bool serverRequests = false;
	bool connection_ok = true;		// status of the MQTT connection

	// make sure of a valid waveformType and
	// have we a sample for the requested wave type
	if((waveformType >= IS25_MEASURED_DATA) || (0 == (what->waves & (1 << waveformType))))
	{
		return true;
	}

	// simulation mode ?
	if (dataUploadVars.simulation_mode)
	{
		if(IS25_RAW_SAMPLED_DATA == waveformType)
		{
			sampleRate = gNvmCfg.dev.measureConf.Sample_Rate_Raw;
			samples = gNvmCfg.dev.measureConf.Samples_Raw;
		}
		else if(IS25_VIBRATION_DATA == waveformType)
		{
			sampleRate = gNvmCfg.dev.measureConf.Sample_Rate_Bearing;
			samples = gNvmCfg.dev.measureConf.Samples_Bearing;
		}
		else
		{
			sampleRate = gNvmCfg.dev.measureConf.Sample_Rate_Wheel_Flat;
			samples = gNvmCfg.dev.measureConf.Samples_Wheel_Flat;
		}

		// now do the work
		LOG_DBG( LOG_LEVEL_CLI,  "\nSend dummy %s waveform\n", strType[waveformType]);
		generate_dummy_waveform(sampleRate, samples);
	}
	else
	{
		// get real data from flash
		LOG_DBG( LOG_LEVEL_CLI,  "\nRetrieve %s waveform from external flash and send it.\n\n", strType[waveformType]);
		int bytesRead = extFlash_read(&extFlashHandle, (uint8_t*)g_pSampleBuffer, (uint32_t)__sample_buffer_size,  waveformType,  what->measurementSetNr, EXTFLASH_MAXWAIT_MS);
		if(bytesRead < 0)
		{
			LOG_EVENT(1100 + waveformType, LOG_NUM_COMM, ERRLOGMAJOR, "FAILED %s read for set # %d; error %s",
					strWave[waveformType], what->measurementSetNr, extFlash_ErrorString(bytesRead));
		}
		else
		{
			samples = bytesRead/sizeof(int32_t);
		}
	}

	if(samples > 0)
	{
        LOG_DBG( LOG_LEVEL_CLI,
       		 "%s read okay\n\n"
    			 " ~~~~~~ Send %s Wave ~~~~~ Num:%d\n",
				 strWave[waveformType],
				 strType[waveformType],
				 what->measurementSetNr);
        cvt_int32ToFloat( (tInt32Float *) __sample_buffer, samples, scaling);
        checkIncommingMessages(10, &serverRequests);// just check before the time consuming waveupload something came in ?
        if (ISVCDATARC_OK != ISvcData_Publish_Data( (SvcDataData_t * ) &idefDataDataRecords[IDEF_data], samples, SKF_MsgType_PUBLISH, 0))
        {
        	LOG_DBG( LOG_LEVEL_CLI, "\nISvcData_Publish_Data %s not OK\n", strType[waveformType]);
        	connection_ok = false;
        }
	}

	// return connection status
	return connection_ok;
}



/*
 * END interface functions to read the data stored in external flash
 */

static bool uploadGatedMeasurements()
{
	bool rc_ok = true;
	bool serverRequests = false;
	uint32_t gatedIndex = 0;

	while(rc_ok && ExtFlash_fetchGatedMeasData(gatedIndex))
	{
		uint32_t messageId;
		// send a gated measurement record
		// commsRecord.params.EnergyRemaining_percent = measureRecord.params.Energy_Remaining;
		ExtFlash_printGatedMeasurement(gatedIndex, dbg_logging & LOG_LEVEL_CLI);
		LOG_DBG( LOG_LEVEL_CLI, "\r\n Send Measurement Record (storedatarequest) Num=%d \r\n", gatedIndex + 1);
		rc_ok = (ISVCDATARC_OK == ISvcData_RequestStoreData( (SvcDataData_t * ) &idefDataDataRecords[IDEF_dataMeasureRecord], &messageId));
		storedataReply.ack_ok = false;// not really nice, but unlikely that a reply is in before this code is executed.
		// TODO wait for store data reply, should have same messageId
		// now the server should send a reply
		if (rc_ok)
		{
			rc_ok = checkIncommingMessages(1000, &serverRequests);// 1000ms ?
		}
		else
		{
			LOG_DBG( LOG_LEVEL_CLI, "\nISvcData_RequestStoreData not OK\n");
		}
		gatedIndex++;
	}
	return rc_ok;
}

// template for the real application communication flow
static bool commsUpload(bool simulationMode)
{
    bool rc_ok = true;		// result of communication functions
    whatToSend whatToUpload = {0, 0};
    uint16_t datasetsToUpload = gNvmData.dat.is25.noOfMeasurementDatasetsToUpload;
    uint16_t msrmntDatasetStartIndex = gNvmData.dat.is25.measurementDatasetStartIndex;
    bool serverRequests = false;
    uint16_t serverWaits = 10;// when we received a server request, we will wait some extra time, maybe the server has more to ask
    uint32_t messageId;

    initDataToUpload(simulationMode);

    do
    {
		// loop while ok and we have something to upload
		while (checkWhatDataToUpload(&whatToUpload))
		{
			rc_ok = checkIncommingMessages(1000, &serverRequests);	// 1000ms ?
			if(false == rc_ok) break;

			// first retrieve the general measurement data, it holds the timestamp
			int errCode = getMeasurementRecord(whatToUpload.measurementSetNr);
			if(errCode < 0)
			{
				// if reading the measurement record fails then we drop the whole measurement set
				LOG_EVENT(1103, LOG_NUM_COMM, ERRLOGMAJOR, "FAILED measureRecord read for set # %d; error %s",
						whatToUpload.measurementSetNr, extFlash_ErrorString(errCode));
			}
			else
			{
				// we do not send it yet, lets first do the waveforms (if any)
				// timestamp when the measurement is performed (that probably is not the current time !!!
				timestamp_raw = timestamp_env3 = timestamp_wheelflat = measureRecord.timestamp;

				if(false == (rc_ok = sendWaveform(&whatToUpload,
											IS25_VIBRATION_DATA,
											(MEASURE_BEARING_ENV3_SCALING * gNvmCfg.dev.measureConf.Scaling_Bearing),
											IDEF_dataWaveformEnv3)))
					break;

				if(false == (rc_ok = sendWaveform(&whatToUpload,
											IS25_WHEEL_FLAT_DATA,
											(MEASURE_WHEELFLAT_SCALING * gNvmCfg.dev.measureConf.Scaling_Wheel_Flat),
											IDEF_dataWaveformWheelflat)))
					break;

				if(false == (rc_ok = sendWaveform(&whatToUpload,
											IS25_RAW_SAMPLED_DATA,
											(MEASURE_RAW_SCALING * gNvmCfg.dev.measureConf.Scaling_Raw),
											IDEF_dataWaveformRaw)))
					break;

				// we did not send it earlier, but lets do it now after the waveforms
				print_measurement_record();
				LOG_DBG( LOG_LEVEL_CLI, "\r\n Send Measurement Record (storedatarequest) Num:%d \r\n", whatToUpload.measurementSetNr);
				rc_ok = (ISVCDATARC_OK == ISvcData_RequestStoreData( (SvcDataData_t * ) &idefDataDataRecords[IDEF_dataMeasureRecord], &messageId));
				storedataReply.ack_ok = false;		// not really nice, but unlikely that a reply is in before this code is executed.
				// TODO wait for store data reply, should have same messageId
				if (false == rc_ok)
				{
					LOG_DBG( LOG_LEVEL_CLI, "\nISvcData_RequestStoreData not OK\n");
					break;
				}

				// now the server should send a reply
				rc_ok = checkIncommingMessages(1000, &serverRequests);// 1000ms ?
			}

			storedataReply.ack_ok = true;// TODO: server takes way too long to react, so for the moment the reply is ignored. must be fixed later
			if (storedataReply.ack_ok == true)
			{
				ackUploadedData(whatToUpload.measurementSetNr );// when the reply of the storedataRequest is received we can signal that this set can be deleted on the sensor
			}
			else
			{
				// no reply received, could stop communication now ?
			}
		}	// End of While

		// did we fail in the while loop?
		if(false == rc_ok) break;

		// Now we send any gated measurements.
    	rc_ok = uploadGatedMeasurements();
		if(false == rc_ok) break;

		if (init_temperature_sending(simulationMode))
		{
			// if the initialization went Ok, we can do the real encoding/sending
			while(rc_ok && (temperature_status.current_index < temperature_status.total_records))
			{
				LOG_DBG( LOG_LEVEL_CLI, "\nSend temperature Records (storedatarequest) startindex:%d\n", temperature_status.current_index);
				rc_ok = (ISVCDATARC_OK == ISvcData_RequestStoreData( (SvcDataData_t * ) &TemperatureRecordData, &messageId));
				temperature_status.current_index += temperature_status.max_records_in_block;
			}
		}
		if(false == rc_ok) break;

		// fetch the COMMS record from external flash
	    if (dataUploadVars.simulation_mode)
	    {
	        LOG_DBG( LOG_LEVEL_CLI,  "\nSend dummy communication record\n");
	        generate_dummy_communication_record(1); // just some dummy data as test for the moment
	    }
	    else
	    {
			// real get from flash
			LOG_DBG( LOG_LEVEL_CLI,  "\nData upload - Retrieve COMMS record from external flash and now send it\n\n");
			if(!fetchCommsData(false))
			{
				LOG_EVENT(1104, LOG_NUM_COMM, ERRLOGMAJOR, "FAILED commsRecord read");
				memset((char*)&commsRecord, 0, sizeof(commsRecord));
				commsRecord.timestamp = ConfigSvcData_GetIDEFTime();
				commsRecord.params.Status_Self_Test[0] = SELFTEST_COMMS_RECORD_READ_ERROR;
			}
	    }
		if(false == rc_ok) break;

		Modem_metrics_t *pModem_metrics = Modem_getMetrics();
		if(pModem_metrics->accessTechnology == eACT_2G)
		{
			commsRecord.params.Com_Signal_Quality = pModem_metrics->signalQuality.csq;
		}
		else if(pModem_metrics->accessTechnology == eACT_3G)
		{
			commsRecord.params.Com_Signal_Quality = 130 + pModem_metrics->signalQuality.smoni;
		}
		else if(pModem_metrics->accessTechnology == eACT_4G)
		{
			commsRecord.params.Com_Signal_Quality = 140 + pModem_metrics->signalQuality.smoni;
		}

		print_communication_record();

   		// send the communication record
   		LOG_DBG(LOG_LEVEL_CLI, "\r\n~~~~~~ Sending Comms Record ~~~~~~\r\n");
   		storedataReply.ack_ok = false;
		rc_ok = (ISVCDATARC_OK == ISvcData_RequestStoreData( (SvcDataData_t * ) &idefDataDataRecords[IDEF_dataCommsRecord], &messageId));
		if(false == rc_ok) break;

		// if no message was received from the server yet it should wait 30 seconds
		if(serverRequests==false)
		{
			rc_ok = checkIncommingMessages(30000, &serverRequests);
		}
		if(false == rc_ok) break;

    	// Waiting loop if when receiving multiple subsequent messages
        while (serverRequests && (serverWaits > 0))
        {
            LOG_DBG( LOG_LEVEL_CLI, "Extra wait of 15secs for incoming messages inserted!\n");
            serverRequests = false;
            rc_ok = checkIncommingMessages(15000, &serverRequests);
            serverWaits-- ;
        }
		if(false == rc_ok) break;

#if 0
		// I want to test, but the server does not send replies, so fake them
		storedataReply.ack_ok = true;
#endif

		if (storedataReply.ack_ok == true)
		{
			// ack all sets should be stored in the database
			ackUploadedData(whatToUpload.measurementSetNr );// when the reply of the storedataRequest is received we can signal that this set can be deleted on the sensor
			LOG_DBG( LOG_LEVEL_CLI, "CommsRecord Successful %d!\n",serverWaits);
		}
		else
		{
			// no reply received, could stop communication now ?
			Vbat_ClearFlag(VBATRF_FLAG_LAST_COMMS_OK);
			gNvmData.dat.is25.noOfMeasurementDatasetsToUpload = datasetsToUpload;
			gNvmData.dat.is25.measurementDatasetStartIndex = msrmntDatasetStartIndex;
			LOG_EVENT(0, LOG_NUM_COMM, ERRLOGINFO, "COMMS ACK not rcvd, Attempt %d of %d",
					gNvmData.dat.schedule.noOfUploadAttempts, gNvmCfg.dev.commConf.Max_Upload_Retries);
		}

		LOG_DBG( LOG_LEVEL_CLI, "Ended Comm serverwait is %d!\n",serverWaits);
    } while(0);

    return rc_ok;
}

//
// simplistic way to use the alternative url's in the configuration
// should move to the comm driver in time
//
static char * selectUrl()
{
    LOG_DBG( LOG_LEVEL_CLI, "selectUrl(): index = %d\n", gNvmData.dat.comm.url_server_idx);
    switch (gNvmData.dat.comm.url_server_idx)
    {
    case 0:
    	break;	// default

    case 1:
        return gNvmCfg.dev.commConf.Url_Server_2;

    case 2:
        return gNvmCfg.dev.commConf.Url_Server_3;

    case 3:
        return gNvmCfg.dev.commConf.Url_Server_4;

    default:
        LOG_DBG( LOG_LEVEL_CLI, "selectUrl(): invalid url index : %d, resetting to 0\n",
        		gNvmData.dat.comm.url_server_idx);
        gNvmData.dat.comm.url_server_idx = 0;
        break;
    }
    // default
    return gNvmCfg.dev.commConf.Url_Server_1;
}

static bool invalidValue(char *str, int len)
{
	// is the string the right length?
	if((0 == len) || (len - 1) != strlen(str))
	{
		return true;
	}

	// check the string only contains numerical characters
	for(int i = 0; i < (len - 1); i++)
	{
		if((str[i] < '0') || (str[i] > '9'))
		{
			return true;
		}
	}

	// it's the right length full of numbers so hopefully good
	return false;
}

void checkInitialSetup()
{
    bool rc_ok = true;
    // test if the configuration is reset with the defaults,
    // if yes, this may be caused by a very first time boot of the device, or otherwise corrupted configuration
    // we need to retrieve some data from the modem  (imei etc), and possibly set it at the default baud rate
    if (invalidValue((char*)gNvmCfg.dev.modem.imei, MODEM_IMEI_LEN) || invalidValue((char*)gNvmCfg.dev.modem.iccid, MODEM_ICCID_LEN))
    {
        bool deviceFirstConfigInitialisation();// it is now in the boardspecificCLI file, but this should move to the device task
        LOG_DBG( LOG_LEVEL_CLI, "\nConfiguration was RESET, initializing (will take some time) !\n");

        rc_ok = deviceFirstConfigInitialisation();
        if (rc_ok == false)
        {
            // this initialisation is important, lets try again after some waiting
            LOG_DBG( LOG_LEVEL_CLI, "\nConfiguration initializing  failed, will try again after some delay (10 seconds?)\n");
            vTaskDelay( 10000 /  portTICK_PERIOD_MS );
            rc_ok = deviceFirstConfigInitialisation();
        }
    }

    // which MQTT server url should we take  ? go for the first one this time
	char *url = selectUrl();

	//split url and port number
	char *tmp = strchr((const char *)url, ':');
	if (tmp)
	{
		strncpy ( gNvmCfg.dev.mqtt.serviceProfile[0].url,  url, ( tmp - url ));
		gNvmCfg.dev.mqtt.serviceProfile[0].url[( tmp - url )] = '\0';// do not forget to terminate the string !
		gNvmCfg.dev.mqtt.serviceProfile[0].portnr = atoi(&tmp[1]);
		LOG_DBG( LOG_LEVEL_CLI, "\nusing MQTT server url : %s : %d\n",
				gNvmCfg.dev.mqtt.serviceProfile[0].url,
				gNvmCfg.dev.mqtt.serviceProfile[0].portnr);
	}
	else
	{
		LOG_DBG( LOG_LEVEL_CLI, "\nERROR in server url !\n");
	}
}

/*
 determine wakeup reason :
 case 'time to do measurements':
         - do measurements
         - store in external flash
         powerdown node
  case ' time to send data to server':
        connect to server
        send 'signon' message
        while (something to do and no errors) {
            check if incomming message, if yes handle it
            if measurement data still there:
                retrieve one measurement from flash
                send measurement data to server (idef/mqtt)
                publishData or storeData (wait for reply in case of storedata)
            else
                if eventlogging not all sent
                send eventlogging portion
         }
         send commsrecord (and wait for reply)
         closedown connection
         if ramcopy config changed, write config to flash
        powerdown node
 */
static bool comms_application(uint32_t testScenario, bool simulationMode)
{

    bool rc_ok = true;
    uint32_t loopcount = testScenario >> 16;
    uint32_t startTick = xTaskGetTickCount();

    testScenario &= 0xffff;

    //LOG_EVENT(0, LOG_NUM_APP, ERRLOGINFO, "Application startup");

    checkInitialSetup();// if the configuration is reset, we have to do some initial stuff !

    printf("\n---- loopcount = %d\n\n",loopcount);

    if (loopcount == 0) loopcount = 1; // at least 1 times

#ifdef UNBUFFERED_HANDLEGETDATAREQ
    getDataRequestdata.inUse = false; 	// no so happy with this one...
#else
    init_handleGetDataRequest();
#endif
    storedataReply.ack_ok = true; 		// new cycle, so all right to send

    if(!setupConnection())
    {
    	// make sure we set up for Alarm retries when comms fails
    	Vbat_ClearFlag(VBATRF_FLAG_LAST_COMMS_OK);

        // connection to server failed (for whatever reason), so try next one in list for the following connection attempt.
		if ( ++gNvmData.dat.comm.url_server_idx > 3)
		{
			gNvmData.dat.comm.url_server_idx=0 ;
		}

		// if configuration dirty, write to flash
		NvmConfigUpdateIfChanged(true);

		// the modem failed to create a connection so let's put it out of its misery
		return false;
    }

    do {
        storedataReply.ack_ok = true; // new cycle, so all right to send

        if (rc_ok)
        {
            // read current time
            currentIdefTime = ConfigSvcData_GetIDEFTime();
            // read HW id bits
            HardwareVersionForConfig = (uint32_t)Device_GetHardwareVersion();
            // firmware version is in firmware flash
            LOG_DBG( LOG_LEVEL_CLI, "\nIDEF_Time= 0x%016llx, Hardware version ID= 0x%08x, Firmware_Version= 0x%08x\n", currentIdefTime,  HardwareVersionForConfig, DataStore_GetUint32( DR_Firmware_Version, 0 ));
#if 1
            // send signon message
            rc_ok = (ISVCDATARC_OK == ISvcData_Publish_Data( (SvcDataData_t * ) &idefDataDataRecords[IDEF_dataSignon], 0, SKF_MsgType_PUBLISH, 0));
#else
            LOG_DBG( LOG_LEVEL_CLI,"\n\tSending signon and 'IDEF_dataDeviceSettings' record\n");
            // as requested, send all device settings with the timestamp of the comms record (thats why it is sent after reading the comms record from flash ?
            rc_ok = (ISVCDATARC_OK == ISvcData_Publish_Data( (SvcDataData_t * ) &idefDataDataRecords[IDEF_dataSignonDeviceSettings], 0, SKF_MsgType_PUBLISH, 0));
#endif
        }
        // Publish the ImagesManifestUpdate
        if(rc_ok)
        {
        	rc_ok = publishImagesManifestUpdate();
        }

        if (rc_ok)
        {
            switch (testScenario)
            {
            case 1:
                comms_test_1(simulationMode);
                break;

            case 2:
                 comms_test_2(simulationMode);
                 break;
            case 3:
                 comms_test_3(simulationMode);
                 break;
            case 4:
                 comms_test_4(simulationMode);
                 break;
            case 10:
				comms_test_10();
				break;
            case 11:
				comms_test_11();
				break;
			case 12:
				comms_test_12();
				break;
            case 15:
                rc_ok = commsUpload(simulationMode);// abused as the main application communication loop now, so better return the resultcode
                break;

#if 0
            case 18:
            	comms_TG5(simulationMode);
                break;
#endif
            default:
                LOG_DBG(LOG_LEVEL_CLI, "unknown comms test scenario %d\n",testScenario);
                break;
            }
        }

        if (--loopcount && rc_ok)
        {
			printf("\nLoopcount = %d, now max 10 second wait\n",loopcount);
			//vTaskDelay( 10000 /  portTICK_PERIOD_MS );
			checkIncommingMessages(10000, NULL);
        }

    } while (loopcount && rc_ok);

#if (USE_DELAYED_EPODOWNLOAD == 0)
    if (rc_ok && !gNvmCfg.dev.general.gnssIgnoreSysMsg && (Device_GetHardwareVersion() >= HW_PASSRAIL_REV4 || Device_HasPMIC()))
	{
    	enum EpoStatus result = Ephemeris_Update();
    	if(result >= MQTT_SUBSCRIBE_ERROR)
    	{
    		LOG_EVENT(8200 + result, LOG_NUM_COMM, ERRLOGWARN, "Ephemeris failed update ERROR: EPO_%s", EpoErrorString[result]);
    		if(Ephemeris_CheckCommsError(result))
    		{
    			rc_ok = false;
    		}
    	}
    	else if (result == EPO_UPDATED_OK)
    	{
            // Ephemeris updated
    		LOG_EVENT(0, LOG_NUM_COMM, ERRLOGINFO, "Ephemeris updated, expires %s",
            		RtcUTCToString(gNvmData.dat.gnss.epoExpiry_secs));
    	}
	}
#endif
    if(rc_ok && (OtaProcess_GetImageSize() > 0))
    {
    	if(Device_IsHarvester())
    	{
    		if(PMIC_IsTLIVoltageGoodToStartOTA())
    		{
    			otaNotifyStartTask(startTick);
    		}
    		else
    		{
    			LOG_EVENT(0, LOG_NUM_COMM, ERRLOGINFO, "OTA notification Rx, but not started due to low Voltage.");
    		}
    	}
    	else
    	{
    		otaNotifyStartTask(startTick);
    	}
    }

#if (USE_DELAYED_EPODOWNLOAD == 1)
	enum EpoStatus epo_dl_result = NO_UPDATE;
	if (rc_ok && !gNvmCfg.dev.general.gnssIgnoreSysMsg && (Device_GetHardwareVersion() >= HW_PASSRAIL_REV4 || Device_HasPMIC()))
	{
		// just do download
		epo_dl_result = Ephemeris_Download();
		if( epo_dl_result >= MQTT_SUBSCRIBE_ERROR )
		{
			LOG_EVENT(8200 + epo_dl_result, LOG_NUM_COMM, ERRLOGWARN, "Ephemeris failed update ERROR: EPO_%s", EpoErrorString[epo_dl_result]);
			if(Ephemeris_CheckCommsError(epo_dl_result))
			{
				rc_ok = false;
			}
		}
		else if (epo_dl_result == EPO_DOWNLOAD_OK)
		{
			LOG_EVENT(0, LOG_NUM_COMM, ERRLOGINFO, "Ephemeris download ok, expires %s",
					RtcUTCToString(gNvmData.dat.gnss.epoExpiry_secs));
		}
	}
#endif
    // Send the logs as the last thing before terminating the connection.
    if (rc_ok)
	{
    	LOG_DBG( LOG_LEVEL_COMM,  "\r\n<<<Now Uploading Event Logs>>\r\n");
    	sendEventLog();
	}

    // terminate unconditionally, we want to terminate even when something earlier went wrong
	bool rc_ok2 = terminateConnection();
	if (rc_ok)
	{
		rc_ok = rc_ok2;// only return the terminate result if it was the first error
	}
#if (USE_DELAYED_EPODOWNLOAD == 1)
	// After modem connection terminated and modem turned off
	// new EPO data can be downloaded to GPS module with less energy use
	if (epo_dl_result == EPO_DOWNLOAD_OK)
	{
		enum EpoStatus epo_update_result = Ephemeris_epoToGNSS();
		if( epo_update_result ==  EPO_UPDATED_OK )
		{
			// Ephemeris updated
			LOG_EVENT(0, LOG_NUM_COMM, ERRLOGINFO, "Ephemeris updated, expires %s",
					  RtcUTCToString(gNvmData.dat.gnss.epoExpiry_secs));
		}
		else
		{
			// Ephemeris update failure
			LOG_EVENT(8200 + epo_update_result, LOG_NUM_COMM, ERRLOGWARN,
					  "Ephemeris failed update ERROR: EPO_%s", EpoErrorString[epo_update_result]);
		}
	}
#endif
    // if config dirty, write to flash
	NvmConfigUpdateIfChanged(true);

    return rc_ok;
}



static void commEnableOtaCallbacks();
static void commDisableOtaCallbacks();


//------------------------------------------------------------------------------
/// Set up sensor to do OTA ***only***
///
/// @return bool true = Success, false = Failure
//------------------------------------------------------------------------------
bool commsDoOtaOnly()
{
    bool rc_ok = true;

    SvcDataSetCallback(SvcData_cb_storeDataReply, handleStoreDataReply_cb);
	SvcDataSetCallback(SvcData_cb_getDataRequest, handleGetDataRequest_cb);
	SvcDataSetCallback(SvcData_cb_storeDataRequest, handleStoreDataRequest_cb);


	commEnableOtaCallbacks();

	if (CommHandle.EventQueue_CommResp == NULL)
	{
		if (false == TaskComm_InitCommands(&CommHandle))
		{
			LOG_DBG( LOG_LEVEL_CLI, "comm initialisation Commands failed\n");
			return false;
		}
	}

	if (rc_ok)
	{
		// read current time
		currentIdefTime = ConfigSvcData_GetIDEFTime();
		// read HW id bits
		HardwareVersionForConfig = (uint32_t)Device_GetHardwareVersion();
		// firmware version is in firmware flash
		LOG_DBG( LOG_LEVEL_CLI, "\nIDEF_Time= 0x%016llx, Hardware version ID= 0x%08x, Firmware_Version= 0x%08x\n", currentIdefTime,  HardwareVersionForConfig, DataStore_GetUint32( DR_Firmware_Version, 0 ));
	}

	// Wait for 30 seconds or a server request
	bool serverRequests = false;
	for(int nRetryCnt = 0; rc_ok && !serverRequests && (nRetryCnt < 30); nRetryCnt++)
	{
		rc_ok = checkIncommingMessages(1000, &serverRequests);
	}

    if(rc_ok && (OtaProcess_GetImageSize() > 0))
    {
		otaNotifyStartTask(xTaskGetTickCount());
    }

    // if config dirty, write to flash
	NvmConfigUpdateIfChanged(true);

	// cleanup the callbacks
	SvcDataRemoveCallback(SvcData_cb_storeDataRequest, handleStoreDataRequest_cb);
	SvcDataRemoveCallback(SvcData_cb_getDataRequest, handleGetDataRequest_cb);
	SvcDataRemoveCallback(SvcData_cb_storeDataReply, handleStoreDataReply_cb);

	commDisableOtaCallbacks();

	//Make sure OTA timeouts are reset to default
	otaSetNotifyTaskDefaultTimeout();

    return rc_ok;
}

static void commEnableOtaCallbacks()
{
#ifndef PROTOBUF_GPB2
    SvcFirmwareSetCallback(SvcFirmware_cb_updateNotification,handleFirmwareUpdateNotification_cb);
    SvcFirmwareSetCallback(SvcFirmware_cb_blockReply,handleFirmwareBlockReply_cb);
#endif
}

static void commDisableOtaCallbacks()
{
#ifndef PROTOBUF_GPB2
    SvcFirmwareRemoveCallback(SvcFirmware_cb_updateNotification, handleFirmwareUpdateNotification_cb);
    SvcFirmwareRemoveCallback(SvcFirmware_cb_blockReply,handleFirmwareBlockReply_cb);
#endif
}


bool commHandling(uint32_t param, bool simulationMode)
{
    bool rc_ok = true;
    SvcDataSetCallback(SvcData_cb_storeDataReply, handleStoreDataReply_cb);
    SvcDataSetCallback(SvcData_cb_getDataRequest, handleGetDataRequest_cb);
    SvcDataSetCallback(SvcData_cb_storeDataRequest, handleStoreDataRequest_cb);

    commEnableOtaCallbacks();

    if (CommHandle.EventQueue_CommResp == NULL)
    {
        if (false == TaskComm_InitCommands(&CommHandle))
        {
        	// make sure we set up for Alarm retries when comms fails
        	Vbat_ClearFlag(VBATRF_FLAG_LAST_COMMS_OK);
            LOG_DBG( LOG_LEVEL_CLI, "comm initialisation Commands failed\n");
            return false;
        }
    }
    rc_ok = comms_application(param, simulationMode);

    // cleanup the callbacks
    SvcDataRemoveCallback(SvcData_cb_storeDataRequest, handleStoreDataRequest_cb);
    SvcDataRemoveCallback(SvcData_cb_getDataRequest, handleGetDataRequest_cb);
    SvcDataRemoveCallback(SvcData_cb_storeDataReply, handleStoreDataReply_cb);

    commDisableOtaCallbacks();

    return rc_ok;
}

/**
 * publishImagesManifestUpdate
 *
 * @brief Publishes the image manifest to MQTT.
 *        Not a static as can be called for testing and
 *        when sensor is commissioned.
 *
 * @return True is the publish has been successful, false otherwise
 */
bool publishImagesManifestUpdate()
{
	bool retVal = true;

	char *manifest = image_createManifest(IMAGE_TYPE_APPLICATION, __app_version);
	// This can never fail as a pointer to a manifest of some description, is always returned
	if(manifest)
	{
		char *pubTopic = mqttConstructTopicFromSubTopic(kstrImagesManifestUpdateSubTopic);

		if(COMM_ERR_OK != TaskComm_Publish(getCommHandle(), manifest, strlen(manifest), (uint8_t*)pubTopic,  10000 /* 10 seconds OK? */ ))
		{
			// we already logged an event so no need to do it again
			retVal = false;
		}
	}
	return retVal;
}

/*
 * this function is placed here, because the pragma pop does not work, it is a confirmed compiler bug
 * to prevent the effect on the rest of the code, this is placed at the end of the file
 */
#pragma GCC push_options
#pragma GCC optimize ("O3")
void calcSimulAmSignal(uint32_t samples, int32_t * out)
{
   int32_t Ncos[NUMSINE],Nsin[NUMSINE];
   int i;

   if (out==NULL) return;

   while (samples--)
   {
       for(i=0; i<NUMSINE; i++)
       {
           Ncos[i] = ((int64_t) simul_am[i].alpha * simul_am[i].Tcos + (int64_t) simul_am[i].beta * simul_am[i].Tsin) >>30;
           Nsin[i] = ((int64_t) simul_am[i].alpha * simul_am[i].Tsin - (int64_t) simul_am[i].beta * simul_am[i].Tcos) >>30;
           simul_am[i].Tcos -= Ncos[i];
           simul_am[i].Tsin -= Nsin[i];
       }
       *out++ =     (
                       ((
                               (int64_t) simul_am[0].Tcos * ((1<<30) +  simul_am[1].Tcos)
                       ) >> 30) + simul_am[0].offset
                   ) >> 7;// scale to 24bits adc lookalike q30 to q23
       //printf(" %08x %08x %08x\n",simul_am[0].Tcos,simul_am[1].Tcos, out[-1]);
   }
   return ;
}
// #pragma GCC pop_options  (does not work, bug in the GNU compiler)
// so this function goes to the end of this file,


#ifdef __cplusplus
}
#endif