#ifdef __cplusplus
extern "C" {
#endif

/*
 * xTaskModemEvent.h
 *
 *  Created on: Sep 23, 2014
 *      Author: George de Fockert
 */

#ifndef XTASKMODEMEVENT_H_
#define XTASKMODEMEVENT_H_

#include "freeRTOS.h"
#include "queue.h"

#include "Log.h"

/*
 * Event descriptors
 */
typedef enum
{
    ModemEvt_Undefined = 0,
    ModemEvt_ModemDataReceived,
	ModemEvt_NoCarrier,
} tModemEventDescriptor;



/*
 * to keep the size of the structure small (it is copied to the queue by freeRTOS)
 * the actual request is linked using a pointer
 *
 */
typedef struct {
    tModemEventDescriptor Descriptor;   // Event Descriptor

} tModemEvent;






#endif /* XTASKMODEMEVENT_H_ */


#ifdef __cplusplus
}
#endif