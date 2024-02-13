#ifdef __cplusplus
extern "C" {
#endif

/*
 * MQTTFreeRTOS_Modem.c
 *
 *  Created on: 27 dec. 2015
 *      Author: Daniel van der Velde
 */

/*
 * Includes
 */
#include "MQTTFreeRTOS.h"
#include "MQTTFreeRTOS_Modem.h"
#include "MQTTClient.h"

#if (CONFIG_COMM_LOWER_STACK_SELECT == CONFIG_COMM_LOWER_STACK_SELECT_MODEM)


#include "log.h"

#include "configModem.h"
#include "xTaskModem.h"
#include "Resources.h"

/*
 * Macros
 */

/*
 * Types
 */

/*
 * Data
 */

// TODO: move to 'MQTT_Network* n->conn_p' and allocate dynamically when we get more than one modem connection on a device


int MQTTFreeRTOS_Modem_Read( MQTT_Network* n, unsigned char* buffer, int len, int timeout_ms )
{
#if 1
    int read, totalRead = 0;
    g_ModemDebugData[9] = 0;

    // disregard the timeout, the calling code gives sometimes too little time for this slow modem/serial connection to receive all requested characters,
    // and as a result the message is thrown away.
    // So, add some to the timeout, and keep reading as long as there are characters coming in.
    do
    {
        // this is a dirty correction for the tcp block size of 1500 bytes, which usually comes with some delays in between them
        uint32_t tcp_extras = (len-totalRead)/100 > 0 ? 200 : 10;
        read = Modem_read(&buffer[totalRead], len-totalRead, timeout_ms + tcp_extras  );
    	g_ModemDebugData[2] = timeout_ms + tcp_extras;
    	g_ModemDebugData[6] = read;
    	g_ModemDebugData[7] = len;
    	g_ModemDebugData[8] = totalRead;
    	g_ModemDebugData[9]++;
        g_ModemDebugData[10] = Modem_IsDCDEventFlagSet();
        if (Modem_IsDCDEventFlagSet() || ((read != len) && Modem_IsDCDEventFlagSet()))
        {
            // TODO: error handling return codes
            return FAILURE;
        }
        totalRead += read;
    } while ((totalRead != len) && (read != 0));// keep reading as long as there are characters coming in
// printf(" %d %d %d\n",len, totalRead, timeout_ms);
    return ((totalRead == len) || (totalRead == 0)) ? totalRead  : FAILURE;
#else
    uint32_t read, totalRead;
    TickType_t xTicksToWait = timeout_ms / portTICK_PERIOD_MS; /* convert milliseconds to ticks */
    TimeOut_t xTimeOut;

    vTaskSetTimeOutState(&xTimeOut); /* Record the time at which this function was entered. */
    totalRead=0;

    do {
        read = Modem_read(&buffer[totalRead], len-totalRead, xTicksToWait * portTICK_PERIOD_MS );

        if ((read != len) && Modem_IsDCDEventFlagSet()) {
            // TODO: error handling return codes
            return FAILURE;
        }
        totalRead += read;
    } while ((totalRead != len) && (xTaskCheckForTimeOut(&xTimeOut, &xTicksToWait) == pdFALSE));

    {// TODO: remove this
        if ((totalRead != 0) && (totalRead != len)) printf("#### only read %d of %d bytes in %d ms\n",totalRead, len, timeout_ms);
    }

    return (totalRead == len) || (totalRead == 0) ? totalRead  : FAILURE;
#endif
}

int MQTTFreeRTOS_Modem_Write( MQTT_Network* n, unsigned char* buffer, int len, int timeout_ms )
{
    int written = Modem_write(buffer, len, timeout_ms);
    if ((written != len) && Modem_IsDCDEventFlagSet())
    {
         // TODO: error handling return codes
         return FAILURE;
     }

    return written;
}

void MQTTFreeRTOS_Modem_NetworkInit( MQTT_Network* n )
{
    // TODO
    n->conn_p = NULL;
    n->mqttread = MQTTFreeRTOS_Modem_Read;
    n->mqttwrite = MQTTFreeRTOS_Modem_Write;
    n->disconnect = MQTTFreeRTOS_Modem_Disconnect;
    //n->errorhandler = NULL;
    n->lastdata_p = NULL;
    n->lastoffset = 0;

    Modem_ClearDCDEventFlag();
}

int MQTTFreeRTOS_Modem_NetworkConnect( MQTT_Network* n, char* addr , int port )
{
    bool rc=false;

    Modem_ClearDCDEventFlag();
    ModemSetCallback(Modem_cb_dcd_no_carrier, Modem_handleDCD);

    rc = configModem_connect( addr, port);

    if (rc == false) {
        // connect failed, cleanup !
        MQTTFreeRTOS_Modem_Disconnect(n);
    }

    return rc ? SUCCESS : FAILURE;// convert error codes from boot to mqtt api version
}

#if 0 // TODO MQTT TLS
int MQTTFreeRTOS_Modem_NetworkConnectTLS( MQTT_Network* n, char* addr, int port, ... )
{
    // TODO
}
#endif

void MQTTFreeRTOS_Modem_Disconnect( MQTT_Network* n )
{
	configModem_disconnect();
    ModemRemoveCallback(Modem_cb_dcd_no_carrier, Modem_handleDCD);
}


#endif // not configured



#ifdef __cplusplus
}
#endif