#ifdef __cplusplus
extern "C" {
#endif

/*
 * json.h
 *
 *  Created on: 30 Jan 2018
 *      Author: John McRobert
 */

#ifndef SOURCES_CLI_PLATFORM_JSON_H_
#define SOURCES_CLI_PLATFORM_JSON_H_

typedef enum {
	JSON_null,
	JSON_boolean,
	JSON_value,
	JSON_string,
	JSON_EOF = -1
} JSON_types_t;

extern const char titleBootloader[] ;
extern const char titleApplication[] ;
extern const char titlePMIC_Application[] ;

JSON_types_t jsonFetch(char *json, char *name, void *value, bool search);
uint32_t getFirmwareVersion(void);
uint32_t getFirmwareVersionMeta(char *meta);
char* getFirmwareFullSemVersionMeta(char *meta);
void metadataDisplay(const char *title, char *meta, bool brief);
void metadataDisplayLoader(bool full);
bool cliVer( uint32_t args, uint8_t * argv[], uint32_t * argi);
bool jsonDisplayFullPmicMetadata(uint8_t* pSrcBuf, uint16_t numBytes);
bool jsonDisplayPmicVersion(uint8_t* pSrcBuf, uint16_t numBytes);
#endif /* SOURCES_CLI_PLATFORM_JSON_H_ */


#ifdef __cplusplus
}
#endif