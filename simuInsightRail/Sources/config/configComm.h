#ifdef __cplusplus
extern "C" {
#endif

/*
 * configComm.h
 *
 *  Created on: 27 dec. 2015
 *      Author: Daniel van der Velde
 */

#ifndef SOURCES_CONFIG_CONFIGCOMM_H_
#define SOURCES_CONFIG_CONFIGCOMM_H_

/*
 * Configuration of COMM component
 */

/*
 * Enable/disable TLS v1.2
 * On top of the lwIP stack the WolfSSL open source package will be used
 * When using the Centurion MODEM, a TLS implementation is provided by the cellular module
 */
#define CONFIG_COMM_NETWORK_USE_TLS                (0)

/*
 * Select Lower network stack
 * - (0) Network: IP stack (lwIP or FreeRTOS-TCP)
 * - (1) Modem
 */
#define CONFIG_COMM_LOWER_STACK_SELECT_NET         (0)
#define CONFIG_COMM_LOWER_STACK_SELECT_MODEM       (1)
#define CONFIG_COMM_LOWER_STACK_SELECT             CONFIG_COMM_LOWER_STACK_SELECT_MODEM


#endif /* SOURCES_CONFIG_CONFIGCOMM_H_ */


#ifdef __cplusplus
}
#endif