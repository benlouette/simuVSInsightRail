#ifdef __cplusplus
extern "C" {
#endif

/*
 * xTaskAppOta.h
 *
 *  Created on: Oct 12, 2016
 *      Author: B. Peters
 */

#ifndef SOURCES_APP_XTASKAPPOTA_H_
#define SOURCES_APP_XTASKAPPOTA_H_


#include "xTaskApp.h"
#include "json.h"

// Maximum number of allowed retries for Over The Air Update
#define MAX_NUM_OF_OTA_RETRIES					(5)

typedef struct {
	uint32_t hwVersion;
	uint32_t fwVersion;
	uint32_t byteIndex;
	uint32_t size;
} tBlockReplyInfo;

void xTaskAppOta( void *pvParameters );
void xTaskAppOta_Init();
void otaSetNotifyTaskDefaultTimeout();
void otaSetNotifyTaskTimeout(int nTimeout_ms);
void otaNotifyStartTask(uint32_t startTick);
void otaRebootRequired(void);

//************* OTA Process Management APIs ****************//
void OtaProcess_LoadFromFlash();
bool OtaProcess_WriteToFlash();
uint32_t OtaProcess_GetRetryCount();
uint32_t OtaProcess_GetBlocksWrittenInFlash();
uint32_t OtaProcess_GetNewFwVer();
uint32_t OtaProcess_GetImageSize();
uint32_t OtaProcess_GetCRC();
void OtaProcess_SetNewFwVer(uint32_t nFwVer);
void OtaProcess_SetImageSize(uint32_t nImageSize_Bytes);
void OtaProcess_SetBlocksWrtittenInFlash(uint32_t blocksWritten);
void OtaProcess_SetRetryCount(uint32_t retryCount);
void OtaProcess_ResetVars();

//********************* OTA CLI ***************************//
bool ota_cliOta( uint32_t args, uint8_t * argv[], uint32_t * argi);
bool ota_cliHelp(uint32_t argc, uint8_t * argv[], uint32_t * argi);
#endif /* SOURCES_APP_XTASKAPPOTA_H_ */


#ifdef __cplusplus
}
#endif