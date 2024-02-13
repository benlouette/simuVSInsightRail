#ifdef __cplusplus
extern "C" {
#endif

/*
 * Log.h
 *
 *  Created on: 16-Dec-2015
 *      Author: Daniel van der Velde
 */

#ifndef LOG_H_
#define LOG_H_

/*
 * Includes
 */

#include <stdint.h>
#include "printgdf.h"
#include "xTaskCli.h"
#include "configLog.h"

/*
 * Macros
 */

// TODO Change to calling CLI write and off-load the calling task from writing to output
#ifdef DEBUG
#define LOG_DBG(m_level,...)       do { if ((dbg_logging) & (m_level)) printf(__VA_ARGS__); } while (0);
#define LOG_DBG_NB(m_level,...)    do { if ((dbg_logging) & (m_level)) printf_nb(__VA_ARGS__); } while (0);
//#define LOG_EVENT(...) do { printf("%s (%d) :",__FILE__,__LINE__); printf(__VA_ARGS__); } while (0);
#else
#define LOG_DBG(m_level,...)
#define LOG_DBG_NB(m_level,...)
#endif


/*
 * Types
 */

/*
 * Data
 */

extern uint32_t dbg_logging;

/*
 * Functions
 */

#endif /* LOG_H_ */


#ifdef __cplusplus
}
#endif