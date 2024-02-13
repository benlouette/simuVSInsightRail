#ifdef __cplusplus
extern "C" {
#endif

/*
 * SvcDataCLI.c
 *
 *      Author: Daniel van der Velde
 */

/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "CLIcmd.h"

#include "ISvcData.h"
#include "SvcData.h"
#include "configIDEF.h"

#include "Log.h"

/*
 * Macros
 */

/*
 * Types
 */

/*
 * Data
 */

/*
 * Functions
 */

static bool cliSvcData( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    ISvcDataRc_t rc = ISVCDATARC_ERR_STATE;
    bool svcdata_init_testValues_sending(uint32_t nrOfRecords, uint32_t maxRecordsPerBlock );

    if (args >= 1) {

        if (strcmp((const char*)argv[0], "start") == 0) {
            rc = ISvcData_Start();
            if (rc != ISVCDATARC_OK) {
                LOG_DBG( LOG_LEVEL_CLI, "IDEF Data Service start: failed\n");
            }
        }

        if (strcmp((const char*)argv[0], "stop") == 0) {
            rc = ISvcData_Stop();
            if (rc != ISVCDATARC_OK) {
                LOG_DBG( LOG_LEVEL_CLI, "IDEF Data Service stop: failed\n");
            }
        }

        if (strcmp((const char*)argv[0], "pubAlive") == 0) {
            rc = ISvcData_Publish_Alive();
            if (rc != ISVCDATARC_OK) {
                LOG_DBG( LOG_LEVEL_CLI, "Publish Alive: failed\n");
            }
        }

        if (strcmp((const char*)argv[0], "pubData") == 0) {
            uint32_t testnr = 0;

            if (args >1) testnr = argi[1];
            if (testnr>= IDEFMAXDATATESTS) {
                printf("invalid testnummer %d\n",testnr);
            } else {
                uint32_t nrElements = 0;
                if (args >2) nrElements = argi[2];
                svcdata_init_testValues_sending(nrElements, 10);// actually only needed for test 4

                rc = ISvcData_Publish_Data( ( SvcDataData_t *) &idefDataTests[testnr], nrElements, SKF_MsgType_PUBLISH, 0);
            }

            if (rc != ISVCDATARC_OK) {
                LOG_DBG( LOG_LEVEL_CLI, "Publish Data: failed\n");
            }
        }
        if (strcmp((const char*)argv[0], "storeData") == 0) {
            uint32_t testnr = 0;
            uint32_t messageId;
            if (args >1) testnr = argi[1];
            if (testnr >= IDEFMAXDATATESTS) {
                printf("invalid testnummer %d\n",testnr);
            } else {
                uint32_t nrElements = 3;

                if (args >2) nrElements = argi[2];
                svcdata_init_testValues_sending(nrElements, 10);// actually only needed for test 4

                rc = ISvcData_RequestStoreData( ( SvcDataData_t *) &idefDataTests[testnr], &messageId);
            }

            if (rc != ISVCDATARC_OK) {
                LOG_DBG( LOG_LEVEL_CLI, "request  storeData: failed\n");
            } else {
                LOG_DBG( LOG_LEVEL_CLI, "request returned message Id : %d\n", messageId);
            }
        }
        if (strcmp((const char*)argv[0], "replyStoreData") == 0) {// TODO remove this temporary test
            uint32_t testnr = 1234;

            int32_t result_code = 1122;

            if (args >1) testnr = argi[1];
            if (args >2) result_code = argi[2];

            rc = ISvcData_ReplyStoreData( testnr, result_code,  args>2 ? (char *) argv[3] : "test info storeData string" );

            if (rc != ISVCDATARC_OK) {
                LOG_DBG( LOG_LEVEL_CLI, "reply  storeData: failed\n");
            }
        }
        if (strcmp((const char*)argv[0], "getData") == 0) {
            uint32_t testnr = 0;
            uint32_t messageId;
            if (args >1) testnr = argi[1];
            if (testnr >= IDEFMAXPARAMVALUETESTS) {
                printf("invalid testnummer %d\n",testnr);
            } else {
                rc = ISvcData_RequestGetData( (SvcDataParamValueGroup_t * ) &idefParamValueTests[testnr], &messageId);
            }

            if (rc != ISVCDATARC_OK) {
                LOG_DBG( LOG_LEVEL_CLI, "request  getData: failed\n");
            } else {
                LOG_DBG( LOG_LEVEL_CLI, "request returned message Id : %d\n", messageId);
            }
        }
        if (strcmp((const char*)argv[0], "replyGetData") == 0) {// TODO remove this temporary test
            uint32_t testnr = 5678;

            int32_t result_code = 2211;

            if (args >1) testnr = argi[1];
            if (args >2) result_code = argi[2];

            rc = ISvcData_ReplyGetData( testnr, result_code,  args>2 ? (char *) argv[3] : "test info getData string" );

            if (rc != ISVCDATARC_OK) {
                LOG_DBG( LOG_LEVEL_CLI, "reply  getData: failed\n");
            }
        }

    }

    return (rc == 0);
}


/*
 * CLI command list
 */
static bool cliSvcDataLongHelp( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    printf( "actions:\n"
            "  start            Start IDEF Data Service\n"
            "  stop             Stop IDEF Data Service\n"
            "  pubAlive         Publish IDEF Alive message\n"
            "  pubData   <testnum> <elements>  Publish IDEF Data message (testdatasets)\n"
            "  storeData        request IDEF storeData message (testdatasets)\n"
            "  replyStoreData   reply IDEF StoreData message (test purposes only !)\n"
            "  getData          request IDEF getData message (testdatasets)\n"
            "  replyGetData     reply IDEF GetData message (test purposes only !)\n"

             );
    return true;
}

static const struct cliCmd svcDataCommands[] = {
        {"svcdata", " [action] [param(s)]\tIDEF Data Service commands", cliSvcData, cliSvcDataLongHelp },
};

bool SvcDataCLIInit()
{
    return cliRegisterCommands(svcDataCommands , sizeof(svcDataCommands)/sizeof(*svcDataCommands));
}




#ifdef __cplusplus
}
#endif