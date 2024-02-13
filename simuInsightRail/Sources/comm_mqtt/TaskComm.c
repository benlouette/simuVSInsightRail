#ifdef __cplusplus
extern "C" {
#endif

/*
 * TaskComm.c
 *
 *  Created on: 23 dec. 2015
 *      Author: Daniel van der Velde
 */

/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>

#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <portmacro.h>
#include <semphr.h>

#include "Resources.h"
#include "xTaskDefs.h"

#include "MQTTClient.h"
#include "MQTTNetwork.h"
#include "MQTTFreeRTOS_lwIP.h"
#include "MQTTFreeRTOS_Modem.h"

#include "TaskComm.h"
#include "TaskCommEvent.h"
#include "TaskCommInt.h"

#include "commCLI.h"

#include "configFeatures.h"
#include "configComm.h"
#include "configMQTT.h"

#include "CS1.h"
#include "Log.h"

// Services
#ifdef CONFIG_PLATFORM_SVCDATA
#include "SvcData.h"
#endif
#ifdef CONFIG_PLATFORM_SVCFIRMWARE
#include "SvcMqttFirmware.h"
#endif


/*
 * Macros
 */

/*
 * Types
 */

typedef struct {
    Comm_state_t          connState;
    xSemaphoreHandle      apiMutex;

    // MQTT message ID
    uint32_t              mqttLastId;

} COMM_t;

/*
 * Data
 */

/*
 * FreeRTOS local resources
 */
#define EVENTQUEUE_NR_ELEMENTS_COMM  8     //! Event Queue can contain this number of elements
static QueueHandle_t      sCommEventQueue;
/* static */ TaskHandle_t       sCommTaskHandle; // global for easy cli TASKS command access

static COMM_t             Comm;



/*
 * Network configuration
 */
#if (CONFIG_COMM_LOWER_STACK_SELECT == CONFIG_COMM_LOWER_STACK_SELECT_NET)
static struct MQTT_Network sMQTTNetwork = {
        .conn_p = NULL,
        .mqttread = MQTTFreeRTOS_lwIP_read,
        .mqttwrite = MQTTFreeRTOS_lwIP_write,
        .disconnect = MQTTFreeRTOS_lwIP_disconnect,
        .errorhandler = CommMQTT_ErrorHandler,
        .lastdata_p = NULL,
        .lastoffset = 0
};
#elif (CONFIG_COMM_LOWER_STACK_SELECT == CONFIG_COMM_LOWER_STACK_SELECT_MODEM)
static struct MQTT_Network sMQTTNetwork = {
        .conn_p = NULL,
        .mqttread = MQTTFreeRTOS_Modem_Read,
        .mqttwrite = MQTTFreeRTOS_Modem_Write,
        .disconnect = MQTTFreeRTOS_Modem_Disconnect,
        .errorhandler = CommMQTT_ErrorHandler,
        .lastdata_p = NULL,
        .lastoffset = 0
};

#else
#error "COMM: No lower stack selected"
#endif

/*
 * The MQTT Client instance
 * There is only one MQTT instance supported (i.e. implies one TCP connection over
 * a single interface).
 */
static MQTTClient sMQTTClient;

/*
 * MQTT send and receive buffers
 */
static uint8_t sSendBuf[CONFIG_MQTT_SEND_BUFFER_SIZE];
static uint8_t sRecvBuf[CONFIG_MQTT_RECV_BUFFER_SIZE];


/*
 * Functions
 */


/*
 * CommMQTT_GetNextMsgId
 * @brief Generate next MQTT ID
 */
uint16_t CommMQTT_GetNextMsgId(void)
{
    uint16_t nextId;
    CS1_CriticalVariable();

    // Increase thread-safe
    CS1_EnterCritical();
    nextId = ++(Comm.mqttLastId);
    CS1_ExitCritical();

    return nextId;
}

/*
 * CommMQTT_ErrorHandler
 * @brief Handle errors from MQTTClient read task
 * @param n Network pointer
 * @param err Network error code
 */
void CommMQTT_ErrorHandler(MQTT_Network *n, int err)
{
    tCommEvent event = { .Descriptor = CommEvt_Disconnect,
                        .replyQueue = NULL};

    // Check error code
    switch (err) {
    case MQTTNETWORK_ERR_NOCONN:
    default:
        // Abort pending communication through COMM task
        LOG_DBG( LOG_LEVEL_COMM, "CommMQTT_ErrorHandler: %d\n", err );
        // Proper disconnect sequence immediately
        xQueueSendToFront( sCommEventQueue, &event, 0 );
        break;
    }
}

/*
 * CommMQTT_SetState
 * @brief Set the new COMM state and inform subscribers
 * @param newState The new state
 */
static void CommMQTT_SetState( Comm_state_t newState )
{
    CS1_CriticalVariable();

    // Set state first, then inform subscribers, because every callback
    // might initiate a task switch and the state can be used through the API.
    CS1_EnterCritical();
    Comm.connState = newState;
    CS1_ExitCritical();

    // TODO Execute callbacks

}

/*
 * NetworkConnect
 *
 * @brief Connect to the network using the application configuration settings
 *
 * @param client_p MQTT client intance
 * @param n network connection
 * @return true when successful
 */
static bool CommMQTT_Connect( MQTTClient *client_p, MQTT_Network *n )
{
    int rc = -1;



    // Initialize
    if ( n->conn_p == NULL ) {

        char * url;
        uint16_t port;

        mqttGetServerConfig( (uint8_t **) &url, &port, NULL);

#if (CONFIG_COMM_LOWER_STACK_SELECT == CONFIG_COMM_LOWER_STACK_SELECT_NET)
        // Use default port and server IP address
        //rc = MQTTFreeRTOS_lwIP_NetworkConnect( n, CONFIG_MQTT_DEFAULT_SERVER_IP4ADDR, CONFIG_MQTT_DEFAULT_PORT );
        rc = MQTTFreeRTOS_lwIP_NetworkConnect( n, url, port );
#elif  (CONFIG_COMM_LOWER_STACK_SELECT == CONFIG_COMM_LOWER_STACK_SELECT_MODEM)
        rc = MQTTFreeRTOS_Modem_NetworkConnect( n, url, port);
#endif
        if ( rc == 0 ) {

            // Start MQTT task
            rc = MQTTStartTask(client_p);
            if (rc == pdPASS) {

                // Send MQTT Connect
                MQTTPacket_connectData pktConData = MQTTPacket_connectData_initializer;
                //pktConData.struct_id;
                //pktConData.struct_version;
                pktConData.MQTTVersion = 3;          // 3=v3.1; 4=v3.1.1
                pktConData.clientID.cstring = (char *) mqttGetDeviceId(); // Get Device ID from configuration
                pktConData.keepAliveInterval = 10;
                pktConData.cleansession = 1;
                pktConData.willFlag = 0;
                //pktConData.will;
                //pktConData.password;
                //pktConData.username;

                rc = MQTTConnect( client_p, &pktConData );
                if (rc == FAILURE) {
                    // Disable MQTT task
                    MQTTSuspendTask(client_p);
                    // Cleanup network connection
                    n->disconnect(n);

                    LOG_DBG( LOG_LEVEL_COMM, "MQTTConnect failed\n" );
                } else {
                    // Successfully connected
                    CommMQTT_SetState( COMM_STATE_CONNECTED );
                    LOG_DBG( LOG_LEVEL_COMM, "MQTTConnect OK\n" );
                }
            } else {
                // Disable MQTT task
                MQTTSuspendTask(client_p);

                // Disconnect from network                /
                n->disconnect(n);
                rc = FAILURE;
            }
        }

    }

    return (rc == SUCCESS);
}

/*
 * NetworkDisconnect
 *
 * @brief Disconnect from the network
 *
 * @param networkConfig_p Network configuration structure
 * @return true when successful
 */
static bool CommMQTT_Disconnect( MQTTClient *client_p, MQTT_Network *n )
{
    int rc = -1;

    if ( Comm.connState != COMM_STATE_CONNECTED )
        return false;

    // Set state to disconnected to get no client access during disconnect functions
    CommMQTT_SetState( COMM_STATE_DISCONNECTED );

    // MQTT Disconnect
    rc = MQTTDisconnect( client_p );
    if ( rc == FAILURE ) {
        LOG_DBG( LOG_LEVEL_COMM, "MQTTDisconnect failed, ignored\n" );
    }

    // Suspend MQTTRun task before shutting down the connection
    MQTTSuspendTask( client_p );

    // Continue disconnecting the network regardless of rc value
    n->disconnect(n);

    return true;
}


static bool CommMQTT_Publish( MQTTClient *client_p, MQTT_Network *n, tPublishReq * PublishReq )
{
    int rc = -1;
    MQTTMessage mqttMsg;

    mqttMsg.dup        = 0;    // At QOS0, DUP must be 0
    mqttMsg.id         = CommMQTT_GetNextMsgId();
    mqttMsg.payload    = PublishReq->payload;
    mqttMsg.payloadlen = PublishReq->payloadlen;
    mqttMsg.qos        = QOS0; // Default non-confirmed PUBLISH (at most once delivery)
    mqttMsg.retained   = 0; // Don't retain the message at the broker by default

    rc = MQTTPublish(&sMQTTClient, PublishReq->topic == NULL ? (const char  *)mqttGetPubTopic() : (const char *) PublishReq->topic, &mqttMsg );

    return (rc == SUCCESS);
}


/*
 * TaskComm_Init
 *
 * @brief Initialize task context before started by RTOS
 */
void TaskComm_Init(void) {

    /*
     * Setup task resources
     */
    // Create COMM API MUTEX
    Comm.apiMutex = xSemaphoreCreateMutex();

    // MQTT message ID increased on each call to CommMQTT_GetNextMsgId()
    Comm.mqttLastId = 0;

    // Create event queue
    sCommEventQueue = xQueueCreate( EVENTQUEUE_NR_ELEMENTS_COMM, sizeof( tCommEvent));
    vQueueAddToRegistry(sCommEventQueue, "_EVTQ_COMM");

    // Create COMM task
    xTaskCreate( TaskComm,              // Task function name
                 "Comm",                 // Task name string
                 STACKSIZE_TASK_COMM,   // Allocated stack size on FreeRTOS heap
                 NULL,                   // (void*)pvParams
                 PRIORITY_TASK_COMM,    // Task priority
                 &sCommTaskHandle );    // Task handle

    /*
     * Initialize Network
     */


    /*
     * Initialize MQTT
     */
#if (CONFIG_COMM_LOWER_STACK_SELECT == CONFIG_COMM_LOWER_STACK_SELECT_NET)
    MQTTClientInit( &sMQTTClient, &sMQTTNetwork, 1000 ,
            sSendBuf, CONFIG_MQTT_SEND_BUFFER_SIZE,
            sRecvBuf, CONFIG_MQTT_RECV_BUFFER_SIZE );

#elif (CONFIG_COMM_LOWER_STACK_SELECT == CONFIG_COMM_LOWER_STACK_SELECT_MODEM)

    // by far the longest time is the command to connect to the cellular network, so take this time plus one second as worst case
    MQTTClientInit( &sMQTTClient, &sMQTTNetwork, gNvmCfg.dev.modem.maxTimeToConnectMs +1000 ,
            sSendBuf, CONFIG_MQTT_SEND_BUFFER_SIZE,
            sRecvBuf, CONFIG_MQTT_RECV_BUFFER_SIZE );
#endif

    /*
     * Register Comm CLI commands
     */
    commCliInit();

    /*
     * Check Services to initialize
     */
#ifdef CONFIG_PLATFORM_SVCDATA
    // TODO Fill config
    SvcData_Init(&sMQTTClient, NULL);
#endif

#ifdef CONFIG_PLATFORM_SVCFIRMWARE
    SvcFirmware_Init(NULL,NULL);
#endif
}

/*
 * TaskComm
 *
 * @brief Comm task context to handle platform device communication
 *        Functions:
 *        - Decouples send and receive operations from clients
 *        - Controls the lower and upper layer stacks
 *        - Provides autonomous protocol handling (e.g. firmware updates)
 *
 */
void TaskComm( void *pvParameters ) {

    tCommEvent  event;
    (void)pvParameters;

#ifdef DEBUG
    if (dbg_logging & LOG_LEVEL_COMM) {
        // some delays to not mess up the bootup string
        vTaskDelay(100);
        LOG_DBG( LOG_LEVEL_COMM, "TaskComm: Start\n");
        vTaskDelay(100);
    }
#endif
    /*
     * Event handler
     */
    while (1) {

        if (xQueueReceive( sCommEventQueue, &event, portMAX_DELAY )) {
            bool rc_ok = true;

            switch(event.Descriptor) {

            case CommEvt_Connect:
                if ( !CommMQTT_Connect( &sMQTTClient, &sMQTTNetwork ) ) {
                    LOG_DBG( LOG_LEVEL_COMM, "CommMQTT_Connect failed\n" );
                    rc_ok = false;
                }
                break;

            case CommEvt_Disconnect:
                LOG_DBG( LOG_LEVEL_COMM, "CommMQTT_Disconnect RECEIVED\n" );
#if 1
                //not disconnect during debug 900kbaud problems, keep modem on, so modem URC's can be seen
                // not disconnecting will lead to errors when restarting without rebooting (state error)
                if ( !CommMQTT_Disconnect( &sMQTTClient, &sMQTTNetwork ) ) {
                    LOG_DBG( LOG_LEVEL_COMM, "CommMQTT_Disconnect failed\n" );
                    rc_ok = false;
                }
#endif
                break;

            case CommEvt_Publish:
                if ( !CommMQTT_Publish( &sMQTTClient, &sMQTTNetwork, &event.ReqData.PublishReq ) ) {
                    LOG_DBG( LOG_LEVEL_COMM, "CommMQTT_Publish failed\n" );
                    rc_ok = false;
                }
                break;

            case CommEvt_Undefined:
            default:
                LOG_DBG( LOG_LEVEL_COMM, "Comm task: unknown event %d\n", event.Descriptor );
                rc_ok = false;
                break;

            }
            // return result to caller
            if (event.replyQueue) {
                tCommResp resp;
                resp.rc_ok =  rc_ok;
                xQueueSend(event.replyQueue, &resp , 0/portTICK_PERIOD_MS );// nowait on this one
            }
        }
    }
}



/*
 * below here, function interface to the Comm task,  to be called from another task
 */

// only run once in every task using dust calls after powerup
// return false when failed
bool TaskComm_InitCommands(tCommHandle * handle)
{
    // allocate reply queue
    handle->EventQueue_CommResp  = xQueueCreate( 1, sizeof( tCommResp ) );

    return ( handle->EventQueue_CommResp != NULL);
}

static int32_t waitReady(tCommHandle * handle, TickType_t maxWait)
{
    int32_t rc = COMM_ERR_OK;

    if (pdFALSE == xQueueReceive( handle->EventQueue_CommResp, &handle->CommResp, maxWait  ) ) {
        rc = COMM_ERR_STATE;
    } else {
        rc = handle->CommResp.rc_ok ? COMM_ERR_OK : COMM_ERR_STATE;
    }

    return rc;
}

static int32_t sendSimpleCommand(tCommHandle * handle , tCommEvent * event, TickType_t maxWait)
{
    int32_t rc = COMM_ERR_OK;

    if (handle) {// caller wants to wait for the response, he should supply a valid queue pointer
         event->replyQueue = handle->EventQueue_CommResp;
         if (event->replyQueue ) {
             // make sure response queue is empty, caller should make sure that a previous command completed !
               xQueueReset(event->replyQueue);
         }
     }
     if (pdTRUE == xQueueSend( sCommEventQueue, event, 0 )) {
         if (event->replyQueue && (maxWait > 0)) {
             rc = waitReady(handle, maxWait);
         }
     } else {
         rc = COMM_ERR_STATE;
     }
     return rc;

}

int32_t TaskComm_WaitReady( tCommHandle * handle, uint32_t maxWaitMs)
{
    int32_t rc = COMM_ERR_OK;

    rc = waitReady( handle, maxWaitMs != portMAX_DELAY ? maxWaitMs /  portTICK_PERIOD_MS : portMAX_DELAY);

    return rc;
}

/*
 * TaskComm_Connect
 *
 */
int32_t TaskComm_Connect( tCommHandle * handle, uint32_t maxWaitMs )
{
    int32_t rc = COMM_ERR_OK;

    CS1_CriticalVariable();
    CS1_EnterCritical();
    Comm_state_t state = Comm.connState;
    CS1_ExitCritical();

    // Check state
    if ( state == COMM_STATE_DISCONNECTED ) {
        // Execute connect request
        tCommEvent event = { .Descriptor = CommEvt_Connect,
                            .replyQueue = NULL};
        rc = sendSimpleCommand(handle, &event, maxWaitMs != portMAX_DELAY ? maxWaitMs /  portTICK_PERIOD_MS : portMAX_DELAY);// TODO: timeout not forever !
    } else {
        rc = COMM_ERR_STATE;
    }

    return rc;
}

/*
 * TaskComm_Disconnect
 *
 */
int32_t TaskComm_Disconnect( tCommHandle * handle, uint32_t maxWaitMs )
{
    int32_t rc = COMM_ERR_OK;

    CS1_CriticalVariable();
    CS1_EnterCritical();
    Comm_state_t state = Comm.connState;
    CS1_ExitCritical();

    // Check state
    if ( state == COMM_STATE_CONNECTED ) {
        // Execute disconnect request
        tCommEvent event = { .Descriptor = CommEvt_Disconnect,
                            .replyQueue = NULL};
        rc = sendSimpleCommand(handle, &event, maxWaitMs != portMAX_DELAY ? maxWaitMs /  portTICK_PERIOD_MS : portMAX_DELAY);// TODO: timeout not forever !
    } else {
        rc = COMM_ERR_STATE;
    }

    return rc;
}



// debug function for the moment
/*
 * TaskComm_Publish
 *
 */
int32_t TaskComm_Publish( tCommHandle * handle, void *payload_p, int payloadlen, uint8_t * topic,  uint32_t maxWaitMs  )
{

    int rc = FAILURE;

    CS1_CriticalVariable();
    CS1_EnterCritical();
    Comm_state_t state = Comm.connState;
    CS1_ExitCritical();

    // Check state
    if ( state == COMM_STATE_CONNECTED )
    {
        tCommEvent event =
        {
        	.Descriptor = CommEvt_Publish,
            .replyQueue = NULL,
	        .ReqData.PublishReq.topic = (char *) topic,
	        .ReqData.PublishReq.payload = payload_p,
	        .ReqData.PublishReq.payloadlen = payloadlen,
        };

        rc = sendSimpleCommand(handle, &event, maxWaitMs != portMAX_DELAY ? maxWaitMs /  portTICK_PERIOD_MS : portMAX_DELAY);// TODO: timeout not forever !
#if 0
        MQTTMessage mqttMsg;

        mqttMsg.dup        =  0;    // At QOS0, DUP must be 0
        mqttMsg.id         = CommMQTT_GetNextMsgId();
        mqttMsg.payload    = payload_p;
        mqttMsg.payloadlen = payloadlen;
        mqttMsg.qos        = QOS0; // Default non-confirmed PUBLISH (at most once delivery)
        mqttMsg.retained   = 0; // Don't retain the message at the broker by default

        rc = MQTTPublish(&sMQTTClient, topic == NULL ? (const char  *)mqttGetPubTopic() : (const char *) topic, &mqttMsg );
#endif
    }

    if(rc == SUCCESS)
    {
		// why on earth does it say COMM_ERR_OK should be COMM_OK!!
    	return COMM_ERR_OK;
    }

    // log an event and return an error
    LOG_EVENT(2000, LOG_NUM_COMM, ERRLOGFATAL, "%s failed to publish to %s", __func__, topic);
    return COMM_ERR_STATE;
}


#ifdef __cplusplus
}
#endif