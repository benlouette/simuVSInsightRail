#ifdef __cplusplus
extern "C" {
#endif

/*
 * IComm.h
 *
 *  Created on: 25 jan. 2016
 *      Author: Daniel van der Velde
 *
 * COMM interface
 *
 * The COMM component interface definition provided to client components.
 *
 * History:
 * 2016-Jan-25, DvdV: File creation
 *
 */

#ifndef SOURCES_COMM_MQTT_PLATFORM_ICOMM_H_
#define SOURCES_COMM_MQTT_PLATFORM_ICOMM_H_

/*
 * Includes
 */
#include <stdint.h>
#include <stddef.h>

/*
 * Defines
 */

#define ICOMM_NB_TIMEOUT_DEFAULT          (10)    // 10 milliseconds

/*
 * Types
 */

typedef enum {
    ICOMMRC_OK = 0,
    ICOMMRC_ERR_PARAM,
    ICOMMRC_ERR_STATE,
    ICOMMRC_ERR_TIMEOUT
} ICommRc_t;
typedef void (*nb_callback_fn)(ICommRc_t);


/*
 * Functions
 */

/**/
ICommRc_t IComm_Init( IComm_Config_t *config_p );
ICommRc_t IComm_Term( void );

ICommRc_t IComm_Enable( void );
//ICommRc_t IComm_Enable_nb( nb_callback_fn, uint32_t timeout ); // pass callback + timeout
ICommRc_t IComm_Disable( void );
//ICommRc_t IComm_Disable_nb( nb_callback_fn, uint32_t timeout );

ICommRc_t IComm_Connect( IComm_addr_t *addr_p );
//ICommRc_t IComm_Connect_nb( IComm_addr_t *addr_p, nb_callback_fn, uint32_t timeout );
ICommRc_t IComm_Disconnect( void );
//ICommRc_t IComm_Disconnect_nb( nb_callback_fn, uint32_t timeout );



#endif /* SOURCES_COMM_MQTT_PLATFORM_ICOMM_H_ */


#ifdef __cplusplus
}
#endif