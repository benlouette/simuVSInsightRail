#ifdef __cplusplus
extern "C" {
#endif

/*
 * DrvTmp431.c
 *
 *  Created on: 19 Jan 2016
 *      Author: Rex Taylor BF1418 (Livingston)
 */


#ifndef DS137N_H_
#define DS137N_H_

#include "log.h"

#define DS_CONTROL_nEOSC	(1<<7)
#define DS_CONTROL_WACE		(1<<6)
#define DS_CONTROL_WDnALM	(1<<5)
#define DS_CONTROL_BBSQW	(1<<4)
#define DS_CONTROL_WDSTR	(1<<3)
#define DS_CONTROL_RS2		(1<<2)
#define DS_CONTROL_RS1		(1<<1)
#define DS_CONTROL_AIE		(1<<0)

// public function prototypes
bool DS137n_init();
bool DS137n_ReadStatus(uint8_t* pStatus);
bool DS137n_SetStatus(uint8_t value);
bool DS137n_ReadControl(uint8_t* pControl);
bool DS137n_SetControl(uint8_t value);
bool DS137n_SetAlarm(uint32_t seconds);
bool DS137n_ReadAlarm(uint32_t* count);
bool DS137n_ReadCounter(uint32_t* count);
bool DS137n_ResetCounter();

// CLI functions
bool cliDS1374(uint32_t args, uint8_t *argv[], uint32_t *argi);
bool cliDS1374LongHelp(uint32_t argc, uint8_t *argv[], uint32_t *argi);

#define DS1374_CLI {"ds1374", " [action] [param(s)]\tDS1374 actions", cliDS1374, cliDS1374LongHelp}

#endif /* DS137N_H_ */


#ifdef __cplusplus
}
#endif