#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Copyright (c) 2014, 2015 IBM Corp.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 * The Eclipse Public License is available at
 *    http://www.eclipse.org/legal/epl-v10.html
 * and the Eclipse Distribution License is available at
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Allan Stockdill-Mander/Ian Craggs - initial API and implementation and/or initial documentation
 *
 *
 *    01-jul-2016 gdf: MQTTUnsubscribe, toficFilter to unsubscribe now removed from the topicFilter list !
 *    04-oct-2016 gdf: waitfor() now also exits when reading gives an error
 *******************************************************************************/
#include "MQTTClient.h"
#include "MQTTFreeRTOS.h"
#include "Log.h"
#include <string.h>
#include "Resources.h"


// define for activating modem specific optimalizations (project specific for passengerrail
#define SKFMODEM

#ifdef SKFMODEM
#include "xTaskModem.h"

SemaphoreHandle_t incommingCharsSem = NULL;

static uint8_t handleIncomingData(uint8_t *buf, uint32_t len, tModemResultFunc * params)
{
    xSemaphoreGive( incommingCharsSem );
    return 0;
}

#endif

static void NewMessageData(MessageData* md, MQTTString* aTopicName, MQTTMessage* aMessage) {
    md->topicName = aTopicName;
    md->message = aMessage;
}


static int getNextPacketId(MQTTClient *c) {
    return c->next_packetid = (c->next_packetid == MAX_PACKET_ID) ? 1 : c->next_packetid + 1;
}


static int sendPacket(MQTTClient* c, int length, MQTT_Timer* timer)
{
    int rc = FAILURE, 
        sent = 0;
    
    while (sent < length && !MQTT_TimerIsExpired(timer))
    {
        rc = c->ipstack->mqttwrite(c->ipstack, &c->buf[sent], length, MQTT_TimerLeftMS(timer));
        if (rc < 0)  // there was an error writing the data
            break;
        sent += rc;
    }
    if (sent == length)
    {
        MQTT_TimerCountdown(&c->ping_timer, c->keepAliveInterval); // record the fact that we have successfully sent the packet
        rc = SUCCESS;
    }
    else
        rc = FAILURE;
    return rc;
}


void MQTTClientInit(MQTTClient* c, MQTT_Network* network, unsigned int command_timeout_ms,
		unsigned char* sendbuf, size_t sendbuf_size, unsigned char* readbuf, size_t readbuf_size)
{
    int i;
    c->ipstack = network;
    
    for (i = 0; i < MAX_MESSAGE_HANDLERS; ++i)
        c->messageHandlers[i].topicFilter = 0;
    c->command_timeout_ms = command_timeout_ms;
    c->buf = sendbuf;
    c->buf_size = sendbuf_size;
    c->readbuf = readbuf;
    c->readbuf_size = readbuf_size;
    c->isconnected = 0;
    c->ping_outstanding = 0;
    c->defaultMessageHandler = NULL;
	c->next_packetid = 1;
    MQTT_TimerInit(&c->ping_timer);
#if defined(MQTT_TASK)
    MQTT_MutexInit(&c->mutex);
    c->thread.task = NULL;
#endif
}

static int decodePacket(MQTTClient* c, int* value, int timeout)
{
    unsigned char i;
    int multiplier = 1;
    int len = 0;
    const int MAX_NO_OF_REMAINING_LENGTH_BYTES = 4;

    *value = 0;
    do
    {
        int rc = MQTTPACKET_READ_ERROR;

        if (++len > MAX_NO_OF_REMAINING_LENGTH_BYTES)
        {
            rc = MQTTPACKET_READ_ERROR; /* bad data */
            goto exit;
        }
        rc = c->ipstack->mqttread(c->ipstack, &i, 1, timeout);
        if (rc < 0) {
            rc = FAILURE;
            goto exit;
        } else if (rc != 1) {
            rc = SUCCESS; // Ignore rc = 0, since it's non-fatal
            goto exit;
        }

        *value += (i & 127) * multiplier;
        multiplier *= 128;
    } while ((i & 128) != 0);
exit:
    return len;
}


static int readPacket(MQTTClient* c, MQTT_Timer* timer)
{
    int rc = SUCCESS;
    MQTTHeader header = {0};
    int len = 0;
    int rem_len = 0;

    /* 1. read the header byte.  This has the packet type in it */
#if 1
    rc = c->ipstack->mqttread(c->ipstack, c->readbuf, 1, 1);// GDF: poll 1 char, if something has arrived, do not wait too long
#else
    rc = c->ipstack->mqttread(c->ipstack, c->readbuf, 1, MQTT_TimerLeftMS(timer));
#endif
    if (rc < 0)
    {
        rc = FAILURE;
        g_ModemDebugData[3] = 1;
        goto exit;
    }
    else if (rc != 1)
    {
        rc = SUCCESS; // Ignore rc = 0, since it's non-fatal
        goto exit;
    }

    len = 1;
    /* 2. read the remaining length.  This is variable in itself */
    decodePacket(c, &rem_len, MQTT_TimerLeftMS(timer));
    len += MQTTPacket_encode(c->readbuf + 1, rem_len); /* put the original remaining length back into the buffer */

    /* 3. read the rest of the buffer using a callback to supply the rest of the data */
    if (rem_len > 0)
    {
        rc = c->ipstack->mqttread(c->ipstack, c->readbuf + len, rem_len, MQTT_TimerLeftMS(timer));
        if (rc < 0)
        {
        	g_ModemDebugData[3] = 2;
        	g_ModemDebugData[4] = rem_len;
        	g_ModemDebugData[5] = MQTT_TimerLeftMS(timer);
            rc = FAILURE;
            goto exit;
        }
        else if (rc != rem_len)
        {
            rc = SUCCESS; // Ignore rc = 0, since it's non-fatal
            goto exit;
        }
    }
    header.byte = c->readbuf[0];
    rc = header.bits.type;
exit:
    return rc;
}


// assume topic filter and name is in correct format
// # can only be at end
// + and # can only be next to separator
static char isTopicMatched(char* topicFilter, MQTTString* topicName)
{
    char* curf = topicFilter;
    char* curn = topicName->lenstring.data;
    char* curn_end = curn + topicName->lenstring.len;
#if 0
    {// TODO: remove this
        int i;
        printf("topicName = ");
        for (i=0; i<topicName->lenstring.len; i++) printf("%c",topicName->lenstring.data[i]);
        printf("\ntopicFilter = %s\n",   topicFilter);vTaskDelay(200);
    }
#endif
    while (*curf && curn < curn_end)
    {
        if (*curn == '/' && *curf != '/')
            break;
        if (*curf != '+' && *curf != '#' && *curf != *curn)
            break;
        if (*curf == '+')
        {   // skip until we meet the next separator, or end of string
            char* nextpos = curn + 1;
            while (nextpos < curn_end && *nextpos != '/')
                nextpos = ++curn + 1;
        }
        else if (*curf == '#')
            curn = curn_end - 1;    // skip until end of string
        curf++;
        curn++;
    };
    
    return (curn == curn_end) && (*curf == '\0');
}


int deliverMessage(MQTTClient* c, MQTTString* topicName, MQTTMessage* message)
{
    int i;
    int rc = FAILURE;

    // we have to find the right message handler - indexed by topic
    for (i = 0; i < MAX_MESSAGE_HANDLERS; ++i)
    {
        if (c->messageHandlers[i].topicFilter != 0 && (MQTTPacket_equals(topicName, (char*)c->messageHandlers[i].topicFilter) ||
                isTopicMatched((char*)c->messageHandlers[i].topicFilter, topicName)))
        {
            if (c->messageHandlers[i].fp != NULL)
            {
                MessageData md;
                NewMessageData(&md, topicName, message);
                c->messageHandlers[i].fp(&md);
                rc = SUCCESS;
            }
        }
    }
    
    if (rc == FAILURE && c->defaultMessageHandler != NULL) 
    {
        MessageData md;
        NewMessageData(&md, topicName, message);
        c->defaultMessageHandler(&md);
        rc = SUCCESS;
    }   
    
    return rc;
}


int keepalive(MQTTClient* c)
{
    int rc = FAILURE;

    if (c->keepAliveInterval == 0)
    {
        rc = SUCCESS;
        goto exit;
    }

    if (MQTT_TimerIsExpired(&c->ping_timer))
    {
        if (!c->ping_outstanding)
        {
            //LOG_DBG( LOG_LEVEL_COMM, "MQTT ping...\n");
            MQTT_Timer timer;
            MQTT_TimerInit(&timer);
            MQTT_TimerCountdownMS(&timer, 1000);
            int len = MQTTSerialize_pingreq(c->buf, c->buf_size);
            if (len > 0 && (rc = sendPacket(c, len, &timer)) == SUCCESS) // send the ping packet
                c->ping_outstanding = 1;
        }
    }

exit:
    return rc;
}


int cycle(MQTTClient* c, MQTT_Timer* timer)
{
    unsigned short packet_type = 0;
    int len = 0,
        rc = SUCCESS;

    // read the socket, see what work is due
    rc = readPacket(c, timer);
    if (rc == FAILURE)
    {
    	LOG_EVENT(10, LOG_NUM_MODEM, ERRLOGMAJOR, "***readPacket failed***");
    	Modem_WriteModemDebugDataAsEvents();
        goto exit;
    }
    else
    {
        packet_type = (unsigned short)rc;
    }
    
    switch (packet_type)
    {
        case CONNACK:
        case PUBACK:
        case SUBACK:
            break;
        case PUBLISH:
        {
            MQTTString topicName;
            MQTTMessage msg;
            int intQoS;
            if (MQTTDeserialize_publish(&msg.dup, &intQoS, &msg.retained, &msg.id, &topicName,
               (unsigned char**)&msg.payload, (int*)&msg.payloadlen, c->readbuf, c->readbuf_size) != 1)
                goto exit;
            msg.qos = (enum QoS)intQoS;
#if 0
            {// TODO remove this
                int i;
                printf("\n### topicName len= %d\n",topicName.lenstring.len);
                for (i=0; i<100 && i<topicName.lenstring.len;i++) printf("%c",topicName.lenstring.data[i]);
                printf("\n");vTaskDelay(100);
            }
#endif
            deliverMessage(c, &topicName, &msg);
            if (msg.qos != QOS0)
            {
                if (msg.qos == QOS1)
                    len = MQTTSerialize_ack(c->buf, c->buf_size, PUBACK, 0, msg.id);
                else if (msg.qos == QOS2)
                    len = MQTTSerialize_ack(c->buf, c->buf_size, PUBREC, 0, msg.id);
                if (len <= 0)
                    rc = FAILURE;
                else
                    rc = sendPacket(c, len, timer);
                if (rc == FAILURE)
                    goto exit; // there was a problem
            }
            break;
        }
        case PUBREC:
        {
            unsigned short mypacketid;
            unsigned char dup, type;
            if (MQTTDeserialize_ack(&type, &dup, &mypacketid, c->readbuf, c->readbuf_size) != 1)
                rc = FAILURE;
            else if ((len = MQTTSerialize_ack(c->buf, c->buf_size, PUBREL, 0, mypacketid)) <= 0)
                rc = FAILURE;
            else if ((rc = sendPacket(c, len, timer)) != SUCCESS) // send the PUBREL packet
                rc = FAILURE; // there was a problem
            if (rc == FAILURE)
                goto exit; // there was a problem
            break;
        }
        case PUBCOMP:
            break;
        case PINGRESP:
            c->ping_outstanding = 0;
            break;
    }
    keepalive(c);
exit:
    if (rc == SUCCESS)
        rc = packet_type;
    return rc;
}


int MQTTYield(MQTTClient* c, int timeout_ms)
{
    int rc = SUCCESS;
    MQTT_Timer timer;

    MQTT_TimerInit(&timer);
    MQTT_TimerCountdownMS(&timer, timeout_ms);

	do
    {
        if (cycle(c, &timer) == FAILURE)
        {
            rc = FAILURE;
            break;
        }
	} while (!MQTT_TimerIsExpired(&timer));
        
    return rc;
}

#if 0 // Using the MUTEXes in MQTT functions, but the read task is custom built
void MQTTRun(void* parm)
{
	MQTT_Timer timer;
	MQTTClient* c = (MQTTClient*)parm;

	MQTT_TimerInit(&timer);

	while (1)
	{
#if defined(MQTT_TASK)
	    MQTT_MutexLock(&c->mutex);
#endif
        MQTT_TimerCountdownMS(&timer, 500); /* Don't wait too long if no traffic is incoming */
        if (cycle(c, &timer) == FAILURE) {
            LOG_DBG( LOG_LEVEL_COMM, "MQTTRun: cycle failed\n");
        } else {
            LOG_DBG( LOG_LEVEL_COMM, "MQTTRun: cycle OK\n" );
        }
#if defined(MQTT_TASK)
		MQTT_MutexUnlock(&c->mutex);
#endif
	}
}
#else


void MQTTRun(void*param)
{
    MQTT_Timer timer;
    MQTTClient* c = (MQTTClient*)param;
    bool neterror = false;

#ifdef SKFMODEM
    incommingCharsSem = xSemaphoreCreateBinary();
    ModemSetCallback(Modem_cb_incoming_data, handleIncomingData);
#endif

#if 1
    LOG_DBG( LOG_LEVEL_COMM, "MQTTRun: task starting\n" )
    vTaskDelay(1000);
#endif
    MQTT_TimerInit(&timer);

    while (1)
    {
        // Start running, assume no neterror
        neterror = false;

#ifdef  SKFMODEM
        // we want to wait for characters to enter, but no longer than the keepalive period !
        if (pdTRUE == xSemaphoreTake(   incommingCharsSem , MQTT_TimerLeftMS(&c->ping_timer)/portTICK_PERIOD_MS)) {
            // quick and dirty test. do not take the semaphore when nothing in receive buffer
            // because that blocks also the mqtt write process

#endif


#if defined(MQTT_TASK)
        MQTT_MutexLock(&c->mutex);
#endif

        MQTT_TimerCountdownMS(&timer, 200); /* Don't wait too long if no traffic is incoming */
        if (cycle(c, &timer) == FAILURE)
        {
            neterror = true;
            LOG_EVENT(10, LOG_NUM_MODEM, ERRLOGDEBUG, "MQTTRun: cycle failed");
            c->ipstack->errorhandler(c->ipstack, MQTTNETWORK_ERR_NOCONN);
        }
        else
        {
            //LOG_DBG( LOG_LEVEL_COMM, "MQTTRun: cycle OK\n" );
        }

#if defined(MQTT_TASK)
        MQTT_MutexUnlock(&c->mutex);
#endif

        // On neterror, suspend
        if (neterror) {
            MQTT_ThreadSuspend(&c->thread); // Suspend self
        }

#ifdef  SKFMODEM
        } else {
            keepalive(c);
        }
#endif

    }
}

#endif


#if defined(MQTT_TASK)
int MQTTStartTask(MQTTClient* client)
{
    if (client->thread.task == NULL) {
	    return MQTT_ThreadStart(&client->thread, &MQTTRun, client);
    } else {
        // Resume
        MQTT_ThreadResume(&client->thread);
    }
    return 1;
}
#endif

int MQTTSuspendTask(MQTTClient* client)
{
    if (client->thread.task == NULL)
        return FAILURE;

#if defined(MQTT_TASK)
    MQTT_MutexLock(&client->mutex);

    // Suspend task
    MQTT_ThreadSuspend(&client->thread);

    MQTT_MutexUnlock(&client->mutex);
#endif
    return 0;
}

int MQTTResumeTask(MQTTClient* client)
{
    if (client->thread.task == NULL)
        return FAILURE;

#if defined(MQTT_TASK)
    MQTT_MutexLock(&client->mutex);

    // Resume
    MQTT_ThreadResume(&client->thread);

    MQTT_MutexUnlock(&client->mutex);
#endif
    return 0;
}

int waitfor(MQTTClient* c, int packet_type, MQTT_Timer* timer)
{
    int rc = FAILURE;
    
    do
    {
        if (MQTT_TimerIsExpired(timer))
            break; // we timed out
        rc = cycle(c, timer);
        if (rc == FAILURE)// gdf: also exit the loop when an error occurs
            break;
    }
    while (rc != packet_type);
    
    return rc;
}


int MQTTConnect(MQTTClient* c, MQTTPacket_connectData* options)
{
    MQTT_Timer connect_timer;
    int rc = FAILURE;
    MQTTPacket_connectData default_options = MQTTPacket_connectData_initializer;
    int len = 0;

#if defined(MQTT_TASK)
    MQTT_MutexLock(&c->mutex);
#endif
	if (c->isconnected) /* don't send connect packet again if we are already connected */
		goto exit;
    
	MQTT_TimerInit(&connect_timer);
	MQTT_TimerCountdownMS(&connect_timer, c->command_timeout_ms);

    if (options == 0)
        options = &default_options; /* set default options if none were supplied */
    
    c->keepAliveInterval = options->keepAliveInterval;
    MQTT_TimerCountdown(&c->ping_timer, c->keepAliveInterval);
    if ((len = MQTTSerialize_connect(c->buf, c->buf_size, options)) <= 0)
        goto exit;
    if ((rc = sendPacket(c, len, &connect_timer)) != SUCCESS)  // send the connect packet
        goto exit; // there was a problem
    
    // this will be a blocking call, wait for the connack
    if (waitfor(c, CONNACK, &connect_timer) == CONNACK)
    {
        unsigned char connack_rc = 255;
        unsigned char sessionPresent = 0;
        if (MQTTDeserialize_connack(&sessionPresent, &connack_rc, c->readbuf, c->readbuf_size) == 1)
            rc = connack_rc;
        else
            rc = FAILURE;
    }
    else
        rc = FAILURE;
    
exit:
    if (rc == SUCCESS)
        c->isconnected = 1;

#if defined(MQTT_TASK)
    MQTT_MutexUnlock(&c->mutex);
#endif

    return rc;
}


int MQTTSubscribe(MQTTClient* c, const char* topicFilter, enum QoS qos, messageHandler messageHandler)
{ 
    int rc = FAILURE;  
    MQTT_Timer timer;
    int len = 0;
    MQTTString topic = MQTTString_initializer;
    topic.cstring = (char *)topicFilter;
    
#if defined(MQTT_TASK)
    MQTT_MutexLock(&c->mutex);
#endif
	if (!c->isconnected)
		goto exit;

	MQTT_TimerInit(&timer);
	MQTT_TimerCountdownMS(&timer, c->command_timeout_ms);
    
    len = MQTTSerialize_subscribe(c->buf, c->buf_size, 0, getNextPacketId(c), 1, &topic, (int*)&qos);
    if (len <= 0)
        goto exit;
    if ((rc = sendPacket(c, len, &timer)) != SUCCESS) // send the subscribe packet
        goto exit;             // there was a problem
    
    if (waitfor(c, SUBACK, &timer) == SUBACK)      // wait for suback 
    {
        int count = 0, grantedQoS = -1;
        unsigned short mypacketid;
        if (MQTTDeserialize_suback(&mypacketid, 1, &count, &grantedQoS, c->readbuf, c->readbuf_size) == 1)
            rc = grantedQoS; // 0, 1, 2 or 0x80 
        if (rc != 0x80)
        {
            int i;
            for (i = 0; i < MAX_MESSAGE_HANDLERS; ++i)
            {
                if (c->messageHandlers[i].topicFilter == 0)
                {
                    c->messageHandlers[i].topicFilter = topicFilter;
                    c->messageHandlers[i].fp = messageHandler;
                    rc = 0;
                    break;
                }
            }
        }
    }
    else 
        rc = FAILURE;
        
exit:
#if defined(MQTT_TASK)
    MQTT_MutexUnlock(&c->mutex);
#endif
    return rc;
}


int MQTTUnsubscribe(MQTTClient* c, const char* topicFilter)
{   
    int rc = FAILURE;
    MQTT_Timer timer;
    MQTTString topic = MQTTString_initializer;
    topic.cstring = (char *)topicFilter;
    int len = 0;

#if defined(MQTT_TASK)
    MQTT_MutexLock(&c->mutex);
#endif
	if (!c->isconnected)
		goto exit;

	MQTT_TimerInit(&timer);
	MQTT_TimerCountdownMS(&timer, c->command_timeout_ms);
    
    if ((len = MQTTSerialize_unsubscribe(c->buf, c->buf_size, 0, getNextPacketId(c), 1, &topic)) <= 0)
        goto exit;
    if ((rc = sendPacket(c, len, &timer)) != SUCCESS) // send the subscribe packet
        goto exit; // there was a problem
    
    if (waitfor(c, UNSUBACK, &timer) == UNSUBACK)
    {
        unsigned short mypacketid;  // should be the same as the packetid above
        if (MQTTDeserialize_unsuback(&mypacketid, c->readbuf, c->readbuf_size) == 1)
            rc = 0; 
    }
    else
        rc = FAILURE;
#if 1
    // gdf: code to clean up the topic filter list when the unsubscribe was successfull
    if (rc == 0) {
        int i;
        for (i = 0; i < MAX_MESSAGE_HANDLERS; ++i)
        {
            if (strcmp(c->messageHandlers[i].topicFilter, topicFilter) == 0) {
                //printf("MQTTUnsubscribe: removing topicFilter entry %d (%s)\n",i, c->messageHandlers[i].topicFilter );
                c->messageHandlers[i].topicFilter = NULL;
            }
        }

    }
#endif
exit:
#if defined(MQTT_TASK)
    MQTT_MutexUnlock(&c->mutex);
#endif
    return rc;
}


int MQTTPublish(MQTTClient* c, const char* topicName, MQTTMessage* message)
{
    int rc = FAILURE;
    MQTT_Timer timer;
    MQTTString topic = MQTTString_initializer;
    topic.cstring = (char *)topicName;
    int len = 0;

    //LOG_DBG(LOG_LEVEL_COMM,"MQTTPublish: topic=%s\n", topicName);

#if defined(MQTT_TASK)
	MQTT_MutexLock(&c->mutex);
#endif
	if (!c->isconnected)
		goto exit;

	MQTT_TimerInit(&timer);
	MQTT_TimerCountdownMS(&timer, c->command_timeout_ms);

    if (message->qos == QOS1 || message->qos == QOS2)
        message->id = getNextPacketId(c);
    
    len = MQTTSerialize_publish(c->buf, c->buf_size, 0, message->qos, message->retained, message->id, 
              topic, (unsigned char*)message->payload, message->payloadlen);
    if (len <= 0)
        goto exit;
    if ((rc = sendPacket(c, len, &timer)) != SUCCESS) // send the subscribe packet
        goto exit; // there was a problem
    
    if (message->qos == QOS1)
    {
        if (waitfor(c, PUBACK, &timer) == PUBACK)
        {
            unsigned short mypacketid;
            unsigned char dup, type;
            if (MQTTDeserialize_ack(&type, &dup, &mypacketid, c->readbuf, c->readbuf_size) != 1)
                rc = FAILURE;
        }
        else
            rc = FAILURE;
    }
    else if (message->qos == QOS2)
    {
        if (waitfor(c, PUBCOMP, &timer) == PUBCOMP)
        {
            unsigned short mypacketid;
            unsigned char dup, type;
            if (MQTTDeserialize_ack(&type, &dup, &mypacketid, c->readbuf, c->readbuf_size) != 1)
                rc = FAILURE;
        }
        else
            rc = FAILURE;
    }
    
exit:
#if defined(MQTT_TASK)
    MQTT_MutexUnlock(&c->mutex);
#endif
    return rc;
}


int MQTTDisconnect(MQTTClient* c)
{  
    int rc = FAILURE;
    MQTT_Timer timer;     // we might wait for incomplete incoming publishes to complete
    int len = 0;

#if defined(MQTT_TASK)
    MQTT_MutexLock(&c->mutex);
#endif
    MQTT_TimerInit(&timer);
    MQTT_TimerCountdownMS(&timer, c->command_timeout_ms);

	len = MQTTSerialize_disconnect(c->buf, c->buf_size);
    if (len > 0)
        rc = sendPacket(c, len, &timer);            // send the disconnect packet
        
    c->isconnected = 0;

#if defined(MQTT_TASK)
    MQTT_MutexUnlock(&c->mutex);
#endif
    return rc;
}



#ifdef __cplusplus
}
#endif