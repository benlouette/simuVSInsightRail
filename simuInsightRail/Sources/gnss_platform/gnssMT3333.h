#ifdef __cplusplus
extern "C" {
#endif

/*
 * gnssMT3333.h
 *
 *  Created on: Feb 13, 2017
 *      Author: ka3112
 */

#ifndef SOURCES_APP_GNSSMT3333_H_
#define SOURCES_APP_GNSSMT3333_H_

typedef enum {
    E_pmtk_ack = 1,
    E_pmtk_sys_msg = 10,
    E_pmtk_txt_msg = 11,
    E_pmtk_epo_info = 707,
    E_pmtk_max = 1000,// PMTK commands go from 000 to 999, so this is out of that scope
    // not going to do the rest of all MT3333 commands here now...
} tPmtkType;


void MT3333_giveSemAcK(void );

bool MT3333_init(bool nodeIsATestBox);
bool MT3333_Startup( uint32_t maxStartupWaitMs);
bool MT3333_shutdown( uint32_t maxShutdownWaitMs);
bool MT3333_version( uint32_t maxShutdownWaitMs);
bool MT3333_SendPmtkCommand( char * cmd,  uint32_t type, uint32_t maxPMTKWaitMs);
bool MT3333_reporting( bool on, uint32_t maxPMTKWaitMs);
char *MT3333_getLastResponseMsg(void);
tPmtkType MT3333_getLastResponseType(void);

#endif /* SOURCES_APP_GNSSMT3333_H_ */


#ifdef __cplusplus
}
#endif