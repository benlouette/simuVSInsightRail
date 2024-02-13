#ifdef __cplusplus
extern "C" {
#endif

/*
 * xTaskCLIEvent.h
 *
 *  Created on: Sep 23, 2014
 *      Author: George de Fockert
 */

#ifndef XTASKCLIEVENT_H_
#define XTASKCLIEVENT_H_

/*
 * Event descriptors
 */
typedef enum
{
    CLIEvt_Undefined = 0,

    // CLI task events
    CLIEvt_mainCliDataReceived,
    CLIEvt_secundaryCliDataReceived,

} tCLIEventDescriptor;

typedef struct {
    tCLIEventDescriptor Descriptor;   // Event Descriptor
    union {
        uint8_t placeholder;
        // Add additional event types here
    } Data;
} tCLIEvent;

#endif /* XTASKCLIEVENT_H_ */


#ifdef __cplusplus
}
#endif