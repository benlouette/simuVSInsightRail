#ifdef __cplusplus
extern "C" {
#endif

/*
 * xTaskDeviceEvent.h
 *
 *  Created on: 23 aug. 2014
 *      Author: Daniel
 */

#ifndef XTASKDEVICEEVENT_H_
#define XTASKDEVICEEVENT_H_

#include <stdint.h>

/*
 * Macros
 */

/*
 * Event descriptors
 */
typedef enum {
    DeviceEvt_Undefined = 0,
	DeviceEvt_SetWakeUpTime,
    DeviceEvt_Shutdown,
} tDeviceEventDescriptor;

/*
 * Event data structures
 */
#if 0
typedef struct {
    uint32_t      clkCfgId;
} tDeviceEventData_ClkCfgReq;

// Log entry data is 32 bytes long
typedef struct {
    //tTimestamp   timestamp;
    uint8_t      moduleId;
    uint8_t      logEntryId;
    uint8_t      textData[30];
} tDeviceEventData_LogEntry;

typedef struct {
    uint64_t      revcounter;
} tDeviceEventData_writeRevcounter;
#endif

typedef struct {
	uint32_t rtcSleepTime;
	uint32_t ds137nSleepTime;
} tDeviceEventData_WakeUptime;

typedef struct {
    uint32_t wakeupMode;
    bool    engineeringMode;
} tDeviceEventData_OperatingMode;

/*
 * Event structure for Device task
 */
typedef struct {
    tDeviceEventDescriptor Descriptor;   // Event Descriptor
    union
	{
    	bool bIsThisCommsCycle;							// True if this was a Comms Cycle.
    	tDeviceEventData_WakeUptime wakeUpTime;			// Schedule next wake up time
 #if 0
        tDeviceEventData_ClkCfgReq  clkCfgReqData;      // Clock configuration request
        tDeviceEventData_LogEntry   logEntry;           // Log entry data, also usable for reboot event
#endif
    } Data;
} tDeviceEvent;


#if 0 // TODO Remove when ready
/*
 * Event descriptors Timer subtask
 */
typedef enum {
    Device_TimerEvt_Undefined = 0,
    // Internal
    Device_TimerEvt_RTCsecondIrq,
    Device_TimerEvt_TBD,
} tDevice_TimerEventDescriptor;

/*
 * Event structure for Device task
 */
typedef struct {
    tDevice_TimerEventDescriptor Descriptor;   // Event Descriptor
} tDevice_TimerEvent;
#endif


#endif /* XTASKDEVICEEVENT_H_ */


#ifdef __cplusplus
}
#endif