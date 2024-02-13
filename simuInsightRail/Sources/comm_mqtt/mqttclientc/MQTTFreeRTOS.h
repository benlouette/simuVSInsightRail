#ifdef __cplusplus
extern "C" {
#endif

/*
 * MQTTFreeRTOS.h
 *
 *  Created on: 27 dec. 2015
 *      Author: Daniel van der Velde
 *
 * Define FreeRTOS specifics in this file
 *
 */

#ifndef SOURCES_COMM_MQTT_PLATFORM_MQTTCLIENTC_MQTTFREERTOS_H_
#define SOURCES_COMM_MQTT_PLATFORM_MQTTCLIENTC_MQTTFREERTOS_H_

/*
 * Includes
 */
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

/*
 * Macros
 */
// Enable stand-alone MQTT task taking care of (a.o.) timely PINGREQ requests
#define MQTT_TASK

/*
 * Types
 */
typedef struct MQTT_Timer
{
    TickType_t xTicksToWait;
    TimeOut_t xTimeOut;
} MQTT_Timer;

void MQTT_TimerInit(MQTT_Timer*);
char MQTT_TimerIsExpired(MQTT_Timer*);
void MQTT_TimerCountdownMS(MQTT_Timer*, unsigned int);
void MQTT_TimerCountdown(MQTT_Timer*, unsigned int);
int MQTT_TimerLeftMS(MQTT_Timer*);

typedef struct Mutex
{
    SemaphoreHandle_t sem;
} MQTT_Mutex;

void MQTT_MutexInit(MQTT_Mutex*);
int MQTT_MutexLock(MQTT_Mutex*);
int MQTT_MutexUnlock(MQTT_Mutex*);

typedef struct MQTT_Thread
{
    TaskHandle_t task;
} MQTT_Thread;

int MQTT_ThreadStart(MQTT_Thread*, void (*fn)(void*), void* arg);
int MQTT_ThreadSuspend(MQTT_Thread*);
int MQTT_ThreadResume(MQTT_Thread*);



/*
 * Functions
 */

#endif /* SOURCES_COMM_MQTT_PLATFORM_MQTTCLIENTC_MQTTFREERTOS_H_ */


#ifdef __cplusplus
}
#endif