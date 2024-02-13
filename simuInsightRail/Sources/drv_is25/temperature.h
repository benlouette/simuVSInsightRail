#ifdef __cplusplus
extern "C" {
#endif

/*
 * temperature.h
 *
 *  Created on: 1 Aug 2019
 *      Author: tk2319
 */

#ifndef SOURCES_DRV_IS25_TEMPERATURE_H_
#define SOURCES_DRV_IS25_TEMPERATURE_H_

// Temperature Measurement Apis
bool cliTempMeas( uint32_t args, uint8_t * argv[], uint32_t * argi);
bool cliTempMeasHelp(uint32_t argc, uint8_t * argv[], uint32_t * argi);
#define TEMP_MEAS_STORAGE_CLI {"tempMeas", "cmd <params>\t\texternal flash commands", cliTempMeas, cliTempMeasHelp }

bool Temperature_WriteRecord(float remoteTemp, uint32_t timestamp);
bool Temperature_EraseAllRecords();
bool Temperature_RecordCount(uint16_t *pNumRecords);
bool Temperature_ReadRecords(uint16_t startIndx, uint16_t readRecCount, uint8_t *pDstBuf);
bool Temperature_GetWrIndx(uint16_t *pOffset);
bool Temperature_GetRecordCount(uint16_t *pRecordCount, uint8_t *pDstBuf);

#endif /* SOURCES_DRV_IS25_TEMPERATURE_H_ */


#ifdef __cplusplus
}
#endif