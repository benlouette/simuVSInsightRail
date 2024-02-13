#ifdef __cplusplus
extern "C" {
#endif

/*
 * schedule.h
 *
 *  Created on: 16 Aug 2017
 *      Author: RZ8556
 */

#ifndef SOURCES_APP_SCHEDULE_H_
#define SOURCES_APP_SCHEDULE_H_

#define DO_MEASUREMENTS	true
#define DO_DATA_UPLOADS false

bool schedule_ScheduleNextWakeup(uint32_t rtcSleepTime_mins, uint32_t ds137nSleepTime_mins);
bool schedule_CalculateNextWakeup(void);
// CLI functions
bool cliScheduleHelp(uint32_t argc, uint8_t * argv[], uint32_t * argi);
bool cliSchedule(uint32_t args, uint8_t * argv[], uint32_t * argi);

#endif /* SOURCES_APP_SCHEDULE_H_ */


#ifdef __cplusplus
}
#endif