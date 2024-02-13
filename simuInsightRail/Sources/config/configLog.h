#ifdef __cplusplus
extern "C" {
#endif

/*
 * configLog.h
 *
 *  Created on: 30 nov. 2015
 *      Author: D. van der Velde
 *
 * Log levels for Passenger Rail
 */

#ifndef SOURCES_CONFIG_CONFIGLOG_H_
#define SOURCES_CONFIG_CONFIGLOG_H_

#include "configFeatures.h"

#ifdef CONFIG_PLATFORM_EVENT_LOG

#include <stdint.h>
#include "EventLog.h"
//from linker scripts
extern uint32_t __event_log[] ,
        __eventlog_size;

//#define DEBUGPRINTKEY

#define LOG_EVENT(eventcode, compNum, sevLevel, ...) EventLog_In(eventcode, compNum, sevLevel, __VA_ARGS__)

#else
#define LOG_EVENT(eventcode, compNum, sevLevel, logmsg) printf("err:%d comp:%d level:%d, %s\n",eventcode, compNum, 0, logmsg)
#endif


/*
 * Definition of MODULE numbers and bit map for the LOG LEVELS
 * these numbers are the module component numbers to use in the event/error logger !
 */
#define LOG_NUM_APP     	(1)
#define LOG_NUM_GNSS    	(2)
#define LOG_NUM_NET     	(3)
#define LOG_NUM_MODEM   	(4)
#define LOG_NUM_CLI     	(5)
#define LOG_NUM_I2C     	(6)
#define LOG_NUM_COMM    	(7)
#define LOG_NUM_OTA			(8)
#define LOG_NUM_PMIC		(9)

#define LOG_LEVEL_APP                       (1<<(LOG_NUM_APP -1))
#define LOG_LEVEL_GNSS                      (1<<(LOG_NUM_GNSS -1))
#define LOG_LEVEL_NET                       (1<<(LOG_NUM_NET -1))
#define LOG_LEVEL_MODEM                     (1<<(LOG_NUM_MODEM -1))
#define LOG_LEVEL_CLI                       (1<<(LOG_NUM_CLI -1))
#define LOG_LEVEL_I2C                       (1<<(LOG_NUM_I2C -1))
#define LOG_LEVEL_COMM                      (1<<(LOG_NUM_COMM -1))
#define LOG_LEVEL_PMIC                      (1<<(LOG_NUM_PMIC -1))

/*
 * LOG_LEVEL_ALL (Logical OR of all levels)
 * Used for iterating over bits
 */
#define LOG_LEVEL_ALL (\
	LOG_LEVEL_APP   | \
	LOG_LEVEL_GNSS  | \
	LOG_LEVEL_NET   | \
	LOG_LEVEL_MODEM | \
	LOG_LEVEL_CLI   | \
	LOG_LEVEL_I2C   | \
	LOG_LEVEL_COMM  | \
	LOG_LEVEL_PMIC  \
)

/*
 * LEVEL to STRING macro for CLI 'log' command
 */

#define LOG_LEVELTOSTRING(m_level) \
	((m_level) == LOG_LEVEL_APP)   ? "Application" : \
	((m_level) == LOG_LEVEL_GNSS)  ? "GNSS" 	   : \
	((m_level) == LOG_LEVEL_NET)   ? "Network" 	   : \
	((m_level) == LOG_LEVEL_MODEM) ? "Modem" 	   : \
	((m_level) == LOG_LEVEL_CLI)   ? "CLI"		   : \
	((m_level) == LOG_LEVEL_I2C)   ? "I2C" 		   : \
	((m_level) == LOG_LEVEL_COMM)  ? "COMM"        : \
	((m_level) == LOG_LEVEL_PMIC)  ? "PMIC"        : \
									 "unknown"



#endif /* SOURCES_CONFIG_CONFIGLOG_H_ */


#ifdef __cplusplus
}
#endif