#ifdef __cplusplus
extern "C" {
#endif

/*
 * MQTTFreeRTOS.c
 *
 *  Created on: 27 dec. 2015
 *      Author: Daniel van der Velde
 *
 * MQTT FreeRTOS specifics
 */

/*
 * Includes
 */
#include "string.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "MQTTFreeRTOS.h"

/*
 * Functions
 */
// for commandline stack checking, the taskhandle made public here
TaskHandle_t MQTT_Rx_TaskHandle = NULL;

int MQTT_ThreadStart(MQTT_Thread* thread, void (*fn)(void*), void* arg)
{
    int rc = 0;
    uint16_t usTaskStackSize = (configMINIMAL_STACK_SIZE * 5);
    UBaseType_t uxTaskPriority = uxTaskPriorityGet(NULL) - 1; /* set the priority as the same as the calling task*/

    rc = xTaskCreate(fn,    /* The function that implements the task. */
        "MQTTTask",         /* Just a text name for the task to aid debugging. */
        usTaskStackSize,    /* The stack size is defined in FreeRTOSIPConfig.h. */
        arg,                /* The task parameter, not used in this case. */
        uxTaskPriority,     /* The priority assigned to the task is defined in FreeRTOSConfig.h. */
        &thread->task);     /* The task handle */

    MQTT_Rx_TaskHandle = thread->task;

    return rc;
}

int MQTT_ThreadSuspend(MQTT_Thread* thread)
{
    vTaskSuspend(thread->task);
    return 0;
}

int MQTT_ThreadResume(MQTT_Thread* thread)
{
    vTaskResume(thread->task);
    return 0;
}


void MQTT_MutexInit(MQTT_Mutex* mutex)
{
    mutex->sem = xSemaphoreCreateMutex();
}

int MQTT_MutexLock(MQTT_Mutex* mutex)
{
    return xSemaphoreTake(mutex->sem, portMAX_DELAY);
}

int MQTT_MutexUnlock(MQTT_Mutex* mutex)
{
    return xSemaphoreGive(mutex->sem);
}


void MQTT_TimerCountdownMS(MQTT_Timer* timer, unsigned int timeout_ms)
{
    timer->xTicksToWait = timeout_ms / portTICK_PERIOD_MS; /* convert milliseconds to ticks */
    vTaskSetTimeOutState(&timer->xTimeOut); /* Record the time at which this function was entered. */
}


void MQTT_TimerCountdown(MQTT_Timer* timer, unsigned int timeout)
{
    MQTT_TimerCountdownMS(timer, timeout * 1000);
}


int MQTT_TimerLeftMS(MQTT_Timer* timer)
{
    xTaskCheckForTimeOut(&timer->xTimeOut, &timer->xTicksToWait); /* updates xTicksToWait to the number left */
    return (timer->xTicksToWait < 0) ? 0 : (timer->xTicksToWait * portTICK_PERIOD_MS);
}


char MQTT_TimerIsExpired(MQTT_Timer* timer)
{
    return xTaskCheckForTimeOut(&timer->xTimeOut, &timer->xTicksToWait) == pdTRUE;
}


void MQTT_TimerInit(MQTT_Timer* timer)
{
    timer->xTicksToWait = 0;
    memset(&timer->xTimeOut, '\0', sizeof(timer->xTimeOut));
}




#ifdef __cplusplus
}
#endif