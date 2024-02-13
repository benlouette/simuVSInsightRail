#ifdef __cplusplus
extern "C" {
#endif

/*
 * nfc_cli.h
 *
 *  Created on: 29 Oct 2018
 *      Author: RZ8556
 */

#ifndef SOURCES_APP_NFC_NFC_CLI_H_
#define SOURCES_APP_NFC_NFC_CLI_H_

bool NFC_CLI_Help(uint32_t argc, uint8_t * argv[], uint32_t * argi);
bool NFC_CLI_cli( uint32_t args, uint8_t * argv[], uint32_t * argi);

#endif /* SOURCES_APP_NFC_NFC_CLI_H_ */


#ifdef __cplusplus
}
#endif