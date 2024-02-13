#ifdef __cplusplus
extern "C" {
#endif

/*
 * boardSpecificCLI.h
 *
 *  Created on: Oct 30, 2015
 *      Author: George de Fockert
 */

#ifndef SOURCES_BOARDSPECIFICCLI_H_
#define SOURCES_BOARDSPECIFICCLI_H_

bool boardSpecificCliInit();
bool i2cBus2SpecificCliInit();
bool i2cBus0SpecificCliInit();
bool rtcSpecificCliInit();
bool deviceFirstConfigInitialisation();

#endif /* SOURCES_BOARDSPECIFICCLI_H_ */


#ifdef __cplusplus
}
#endif