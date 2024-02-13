#ifdef __cplusplus
extern "C" {
#endif

/*
 * EventLog.h
 *
 *  Created on: Jun 21, 2016
 *      Author: sm2622
 */

#ifndef SOURCES_ERRLOG_PLATFORM_ERRLOG_H_
#define SOURCES_ERRLOG_PLATFORM_ERRLOG_H_


#include <stdint.h>
#include <stdbool.h>
#include "configLog.h"

#ifndef ERRLOG_H_
#define ERRLOG_H_

typedef enum {
    ERRLOGRESERVED = 0,
    ERRLOGFATAL,
    ERRLOGMAJOR,
    ERRLOGMINOR,
    ERRLOGWARN,    // Undesired/ desired events can be come under this
    ERRLOGDEBUG,
    ERRLOGINFO
} tEventLogSevlvl;

#define MAXERRLOGFRAMELENGTH 128 //128 bytes in total

#define PMIC_EVENTLOG_BAND 3000

#pragma pack(push,1) //to avoid structure padding
struct errLog_header {
	uint8_t             crc;
	uint8_t             tag;
    uint8_t             compNumber;
    tEventLogSevlvl     sevLvl;
    uint16_t            eventCode;
    uint16_t            id;
	uint64_t            unixTimestamp;
};

#define SYS_FLASH_ERRLOG_SIZE (uint32_t)__eventlog_size

#define ERRORLOGFIXEDPART sizeof(struct errLog_header)
#define MAXLOGSTRINGLENGTH (MAXERRLOGFRAMELENGTH- ERRORLOGFIXEDPART)

typedef struct {
    struct errLog_header logHeader;
    char            logMsg[ MAXLOGSTRINGLENGTH];
}tEventLog_inFlash;

typedef struct {
    struct errLog_fixedpart {
        uint8_t             frameLength;
        uint64_t            unixTimestamp;
        uint16_t            compNumber;
        uint16_t            eventCode;
        uint8_t             repCounter;
        tEventLogSevlvl     sevLvl;
        char                logStart;
    } woLogMsg;
    char            logMsg[ MAXLOGSTRINGLENGTH];
}tEventLog_upload;

#pragma pack(pop)


bool EventLog_In(uint16_t eventcode, uint16_t compNum, uint8_t sevlevel, const char *fmt,  ...);
void EventLog_InitFlashData(void);
bool EventLog_getLog(tEventLog_inFlash ** addrEventLog);
void EventLog_printLogEntry(tEventLog_inFlash * addrEventLog);
void EventLog_Clear(void);
bool cliExtHelpEventLog(uint32_t argc, uint8_t * argv[], uint32_t * argi);
bool cliEventLog( uint32_t args, uint8_t * argv[], uint32_t * argi);
#endif /* ERRLOG_H_ */



#endif /* SOURCES_ERRLOG_PLATFORM_ERRLOG_H_ */


#ifdef __cplusplus
}
#endif