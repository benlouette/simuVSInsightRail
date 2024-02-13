#ifdef __cplusplus
extern "C" {
#endif

/*
 * image.h
 *
 *  Created on: 21 Nov 2018
 *      Author: RZ8556
 */

#ifndef SOURCES_IMAGE_H_
#define SOURCES_IMAGE_H_

#include <stdint.h>
#include <stdbool.h>
#include "imageTypes.h"

#define EXT_IMAGE_APP 						(0xF00000)
#define MAX_EXT_IMAGES 						(2)
#define MAX_IMAGE_SIZE 						(0x80000)
// Indicates the start address of the last image location in the external flash.
#define LAST_IMAGE_EXT_FLASH_START_ADDR		(EXT_IMAGE_APP + (((MAX_EXT_IMAGES) - 1) * (MAX_IMAGE_SIZE)))

char* image_buildOTACompleteManifest(ImageType_t imageType, const uint32_t extFlashAddr);
char* image_createManifest(ImageType_t imageType, char* pAppMetaDataSrcAddr);
bool image_IsExtImageAtAddrValid(uint32_t extFlashStartAddr);
ImageType_t image_ExtractImageType(const uint32_t addr);
bool image_cliHelp(uint32_t argc, uint8_t * argv[], uint32_t * argi);
bool image_cliImage( uint32_t args, uint8_t * argv[], uint32_t * argi);
bool checkAPPBackedUp(void);
uint32_t getImageStartAddress(int index);
bool crcImage(int image);
bool image_UpdateLoaderFromExtFlashAddr(uint32_t nLoaderOTASrcAddr);
void appendToManifestBuf(const char* toBeAppended);
bool saveToImage(int image);
int image_UpdatePmicAppFromExtFlashAddr(uint32_t nPmicAppOTASrcAddr);
bool image_FirstTimePmicBackup();
bool image_jsonExtractImageInformation(const char* meta_data, ImageType_t* peImageType, uint32_t* pnImageSize);

/*
 * image_GetExtFlashImageStartAddress
 *
 * @brief Provides the information about the start address in the external flash
 * 		  where images are stored.
 *
 * @return  StartAddress in the external flash which is reserved for storing images.
 */
inline uint32_t image_GetExtFlashImageStartAddress()
{
	return EXT_IMAGE_APP;
}

#endif /* SOURCES_IMAGE_H_ */


#ifdef __cplusplus
}
#endif