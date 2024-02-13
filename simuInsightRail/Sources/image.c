#ifdef __cplusplus
extern "C" {
#endif

/*
 * image.c
 *
 *  Created on: 21 Nov 2018
 *      Author: RZ8556
 */
#include "freertos.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "crc.h"
#include "drv_is25.h"
#include "flash.h"
#include "log.h"
#include "configBootloader.h"
#include "CLIio.h"
#include "CLIcmd.h"
#include "json.h"
#include "linker.h"
#include "imageTypes.h"
#include "image.h"
#include "NvmConfig.h"
#include "Device.h"
#include "pmic.h"
#include "queue.h"
#include "ExtFlash.h"

/*
 * Routines for management of 'App' images
 * TODO - this code will break if we move to larger App images
 */

// is25 flash specifics
#define EXTFLASH_NO_OF_PAGES_IN_A_SECTOR   	(16)
#define BLOCK_SIZE 							(0x8000)

#define APP_MINIMUM_MAJOR_VERSION 	(1)
#define APP_MINIMUM_MINOR_VERSION 	(4)

#define MAX_MANIFEST_LENGTH 		((uint32_t)__loader_version_size + (uint32_t)__app_version_size)
#define MAX_VALUE_LENGTH 			(50) 	// Length of hex representation of SHA-1 plus some overhead
#define MAX_METADATA_JSON_LEN		(0x7F0) //(see https://skfdc.visualstudio.com/SKF%20Insight%20Rail/_wiki/wikis/SKF%20Insight%20Rail.wiki/3945/OTA-Upgrade
/*
 * Image definitions
 *
 * We have 2 external flash images (1,2) for now
 * 0 is the 'internal' image
 */
typedef enum {
	APPLICATION_IMAGE,
	EXT_IMAGE_1,
	EXT_IMAGE_2
} image_t;

static bool extFlashStatus_initialized = false;

static char* g_strManifestBuf = (char *)__sample_buffer;
static uint32_t g_nManifestLength = 0;
static char m_aszCachedMetaData[MAX_METADATA_JSON_LEN + 1]; //Add one byte for terminating zero

static bool inline validImage(int index)
{
	return ((index >= 0) && (index <= MAX_EXT_IMAGES));
}

uint32_t getImageStartAddress(int index)
{
	if(validImage(index))
	{
		if(index == APPLICATION_IMAGE)
		{
			return (uint32_t)__app_origin;
		}
		else
		{
			// TODO Need to change this, only Loader knows!
			return EXT_IMAGE_APP + (index-1)*MAX_IMAGE_SIZE;
		}
	}

	return 0;
}

/*
We publish a 'manifest' of installed images to the broker.
JSON format, example:

	{
	"IMEI" : "357040066865911",
	"Images" :
	  [
		{"ImageType" : "MK24_Application", "FullSemVer" : "1.4.0-alpha.100", "SHA" : "xxxxxxxxxxxxxxxxxxxxxx"},
		{"ImageType" : "MK24_Loader", "FullSemVer" : "1.4.0-alpha.100", "SHA" : "xxxxxxxxxxxxxxxxxxxxx"},
		{"ImageType" : "PMIC_Application", "FullSemVer" : "0.1.0-alpha.100", "SHA" : "xxxxxxxxxxxxxxxxxxxxx"}
	  ]
	}
*/

/*
 * Helper function to add Image information object. Searches for matching 'type'
 * in loader and app metadata and if it is found adds the JSON object
 */
static void add_image_json(int *count, const char* image_type, char *pSrcMetadata)
{
	char found_value[MAX_VALUE_LENGTH];

	const JSON_types_t type = jsonFetch(pSrcMetadata, "imageType", found_value, true);
	if(type == JSON_string)
	{
		if(strcmp(found_value, image_type) == 0)
		{
			if(*count)
			{
				appendToManifestBuf(",");
			}
			appendToManifestBuf("{\"ImageType\":\"");
			appendToManifestBuf(found_value);

			appendToManifestBuf("\",\"FullSemVer\":\"");
			if(JSON_string != jsonFetch(pSrcMetadata, "FullSemVer", found_value, true))
			{
				snprintf(found_value, sizeof(found_value), "FullSemVer not found");
				LOG_EVENT(0, LOG_NUM_APP, ERRLOGMAJOR,  "Build Manifest, FullSemVer not found in metadata");
			}
			appendToManifestBuf(found_value);

			appendToManifestBuf("\",\"SHA\":\"");
			if(JSON_string != jsonFetch(pSrcMetadata, "Sha", found_value, true))
			{
				snprintf(found_value, sizeof(found_value), "Sha not found");
				LOG_EVENT(0, LOG_NUM_APP, ERRLOGMAJOR,  "Build Manifest, Sha not found in metadata");
			}
			appendToManifestBuf(found_value);

			appendToManifestBuf("\"}");
			(*count)++;
		}
	}
	else
	{
		LOG_EVENT(0, LOG_NUM_APP, ERRLOGMAJOR,  "Failed to add image information to manifest. Image Type: %s not found in metadata. (%d)", image_type, type);
	}
}

/*
 * Helper function to form IMEI key value pair for build manifest.
 */
static void buildManifestIMEIKeyValPair(const char* imei)
{
	appendToManifestBuf("{\"IMEI\":\"");

	if(strlen(imei))
	{
		// now copy the device number
		appendToManifestBuf(imei);
		appendToManifestBuf("\"");
	}
	else
	{
		// use an empty number
		appendToManifestBuf("0\"");
	}
}

/*
 * @desc: 	Helper function to form Images Info array for build manifest.
 * 			The "pAppMetaDataSrcAddr" param is used to get the manifest metadata
 * 			for the image specified by the param "imageType".
 *
 * @param   imageType - Used to indicate whether it is Loader, MK24 Application
 * 						or PMIC application image.
 * @param  	pAppMetaDataSrcAddr - Pointer to the start address of the Image.
 *
 * return 	void
 */
static void buildManifestImagesInfoArray(ImageType_t imageType, char *pAppMetaDataSrcAddr)
{
	int count = 0;
	appendToManifestBuf(",\"Images\":[");
	if(imageType == IMAGE_TYPE_PACKAGE)
	{
		add_image_json(&count, sPackage, pAppMetaDataSrcAddr);
	}
	else
	{
		add_image_json(&count, sMK24_Application, pAppMetaDataSrcAddr);
		// Loader image manifest is always read from the "__loader_version" location
		add_image_json(&count, sMK24_Loader, __loader_version);
		if(Device_HasPMIC())
		{
			add_image_json(&count, sPMIC_Application, (char*)PMIC_getMetadata());
		}
	}
	appendToManifestBuf("]}");
}

/*
 * image_buildAppOTACompleteManifest
 *
 * @brief Build JSON image manifest for indicating OTA complete. This being
 * 		 a special case (the OTAComplete is notified before the App / PMIC image
 * 		 actually becomes active), therefore the build manifest is built from the
 * 		 recently downloaded App OTA Image, which should be applied by the loader
 * 		 in the next bootup. The PMIC image is applied in the same wake cycle
 * 		 before MK24 goes to sleep.
 *
 * @param imageType - Type of image(App, loader, Pmic etc)
 * @param extFlashAddr - Ext. flash address where the most recently downloaded
 * 						 OTAimage is stored.
 *
 * @return  JSON based build manifest string.
 */
char* image_buildOTACompleteManifest(ImageType_t imageType, const uint32_t extFlashAddr)
{
	// __loader_version is the offset from start address for metadata
	if(!IS25_ReadBytes(extFlashAddr + (uint32_t)__loader_version, (uint8_t*)m_aszCachedMetaData, (uint32_t)__app_version_size))
	{
		// process an empty string
		strcpy(m_aszCachedMetaData, "{}");
	}

	return image_createManifest(imageType, m_aszCachedMetaData);
}

/*
 * image_createManifest
 *
 * @brief Create JSON image manifest.
 *
 * @param imageType - Type of image(App, loader, Pmic etc)
 * @param pAppMetaDataSrcAddr - Start addr. of MK24 App Image, used for metadata.
 *
 * @return  JSON based build manifest string.
 */
char* image_createManifest(ImageType_t imageType, char* pAppMetaDataSrcAddr)
{
	g_nManifestLength = 0;
	g_strManifestBuf[0] = '\0';

	buildManifestIMEIKeyValPair((const char*)gNvmCfg.dev.modem.imei);
	buildManifestImagesInfoArray(imageType, pAppMetaDataSrcAddr);
	return g_strManifestBuf;
}

bool image_initExternalFlashInterface(uint32_t baudrate, uint32_t *calculatedBaudrate)
{
    bool rc_ok = true;

    if (extFlashStatus_initialized == true)
    {
    	// already initialised, so terminate current settings
    	rc_ok = IS25_Terminate();
    }
    if (rc_ok)
    {
    	rc_ok = IS25_Init(baudrate, calculatedBaudrate);
    }

    extFlashStatus_initialized = rc_ok;

    if (rc_ok == false)
    {
    	LOG_DBG(LOG_LEVEL_APP,"image_initExternalFlashInterface: initialise is25 low level driver failed !\n");
    }

    return rc_ok;
}

// local function declarations

/*
 * saveToImage
 *
 * @brief Copy from internal flash -> external flash image
 *
 *
 * @param Image index
 *
 * @return  true - if successful , false otherwise.
 */
bool saveToImage(int image)
{
	// only accept an external flash image index
	if(validImage(image) && (image > 0))
	{
		uint32_t image_size;
		uint32_t image_start;
		bool rc_ok;

		// TODO we need to revisit image size, if the new image is smaller
		image_size = (uint32_t)__app_image_size;
		image_start = getImageStartAddress(image);

		do
		{
			// check internal App is possibly valid..
			rc_ok = crcImage(APPLICATION_IMAGE);
			if(!rc_ok)
			{
				// TODO Log the error!
				break;
			}

			// erase external image
			// TODO need to erase full image?
			rc_ok = IS25_PerformSectorErase(image_start, image_size);
			if(!rc_ok)
			{
				// TODO Log the error!
				break;
			}

			// copy the bytes
			rc_ok = IS25_WriteBytes(image_start , (uint8_t*)__app_origin, image_size);
			if(!rc_ok)
			{
				// TODO Log the error!
				break;
			}

			// check the result
			rc_ok = crcImage(image);
			if(!rc_ok)
			{
				// TODO Log the error!
				break;
			}

		} while(0);

		return rc_ok;
	}
	// TODO Log the error!
	return false;
}

/*
 * Extract Image type from the the image metadata contained withing external flash
 */
 ImageType_t image_ExtractImageType(const uint32_t addr)
{
	 ImageType_t eImageType = IMAGE_TYPE_UNKNOWN;

	 // __loader_version is the offset from start address for metadata
	 if(IS25_ReadBytes(addr + (uint32_t)__loader_version, (uint8_t*)m_aszCachedMetaData, (uint32_t)__app_version_size))
	 {
		 uint32_t nImageSize;
		 image_jsonExtractImageInformation(m_aszCachedMetaData, &eImageType, &nImageSize);
	 }

	 return eImageType;
}

 //TODO: Update to allow backing up of PMIC images larger than 128kb
bool image_FirstTimePmicBackup()
{
	bool bOk = false;

	do
	{
		//Check if an image is already backed up
		if(image_ExtractImageType(EXTFLASH_BACKUP_PMIC_START_ADDR) == IMAGE_TYPE_PMIC_APPLICATION)
		{
			break;
		}

		printf("Retrieving PMIC image to backup\n");
		//Get the current PMIC image from the PMIC
		PMIC_SendDumpReqMsg();
		if(!PMIC_IsDumpRcvd())
		{
			printf("\n\nFailed to get PMIC image from pmic!\n");
			break;
		}

		//Get image size from PMIC metadata
		uint32_t nImageSize, flashSize;
		if(JSON_value != jsonFetch((char*)&__sample_buffer + 0x410, "imageSize", &nImageSize, true))
		{
			break;
		}

		if(JSON_value != jsonFetch((char*)&__sample_buffer + 0x410, "flashSize", &flashSize, true))
		{
			break;
		}

		crc32_start();
		crc32_calc((uint8_t*)&__sample_buffer[0], flashSize);
		if(CRCTARGETVALUE != crc32_finish())
		{
			printf("PMIC image failed CRC. Flash size was 0x%08X\n", flashSize);
			break;
		}

		printf("Backing up PMIC image\n");
		//Erase the PMIC backup flash area then write the PMIC image to the PMIC backup area
		IS25_PerformSectorErase(EXTFLASH_BACKUP_PMIC_START_ADDR, flashSize);
		if(!IS25_WriteBytes(EXTFLASH_BACKUP_PMIC_START_ADDR, (uint8_t*)&__sample_buffer, nImageSize))
		{
			LOG_EVENT(0, LOG_NUM_APP, ERRLOGMAJOR,  "%s(): Failed to write to external flash\n", __func__);
			break;
		}

		bOk = image_IsExtImageAtAddrValid(EXTFLASH_BACKUP_PMIC_START_ADDR);

		printf("PMIC Image backup %s\n", bOk ? "Successful" : "Failed");
	}
	while(0);

	return bOk;
}

/*
 * IsExtImageAtAddrValid
 *
 * @brief Checks if the image at the specified start address in the external
 * 		  flash is valid.
 *
 * @param extFlashStartAddr - start address of image in external flash.
 *
 * @return  true , if the image is valid, false otherwise.
 */
bool image_IsExtImageAtAddrValid(uint32_t extFlashStartAddr)
{
	uint32_t flashSize = (uint32_t)__app_image_size;
	uint32_t crcResult;
	char imageType[32];

	crc32_start();
	for(int i = 0; i < flashSize; i += BLOCK_SIZE)
	{
		uint32_t length = ((i + BLOCK_SIZE) < flashSize) ? BLOCK_SIZE : flashSize - i;
		if(IS25_ReadBytes(extFlashStartAddr + i, (uint8_t*) __sample_buffer, length))
		{
			crc32_calc((uint8_t*)__sample_buffer, length);
		}

		if(i == 0)
		{
			if(JSON_string != jsonFetch((char*)&__sample_buffer + 0x410, "imageType", &imageType, true))
			{
				break;
			}

			if(JSON_value != jsonFetch((char*)&__sample_buffer + 0x410, "flashSize", &flashSize, true))
			{
				break;
			}
		}
	}
	crcResult = crc32_finish();

	printf("IMAGE type %s At Addr:0x%x, crcResult = 0x%08X %s\n",
			imageType,
			extFlashStartAddr,
			(unsigned int)crcResult,
			(0x1210611A == crcResult) ? "(IMAGE BLANK)" : "");

	return (CRCTARGETVALUE == crcResult);
}

bool crcImage(int image)
{
	bool crcResult = false;

	if(validImage(image))
	{
		uint32_t image_start = getImageStartAddress(image);

		if(image == APPLICATION_IMAGE)
		{
			crcResult = (CRCTARGETVALUE == crc32_hardware((void*)image_start, (uint32_t)__app_image_size));
		}
		else
		{
			crcResult = image_IsExtImageAtAddrValid(image_start);
		}
	}
	// TODO Log the error!
	return crcResult;
}

#define FLASH_RETRIES	3
#define FLASH_VECTOR_SIZE		0x410	// note the interrupt table is 0xx400 bytes
                                        // the additional 0x10 bytes are the
                                        // control bytes for the reset logic
const char metaVectors[] = "{\"FullSemVer\":\"0.0.0-Vectors\",\"Sha\":\"9999999999999999999999999999999999999999\",\"imageType\":\"MK24_Loader\"}";

/*
 * setLoaderVectorsToApp
 *
 * @brief Copy the application interrupt vectors to the loader space
 *
 **/
void setLoaderVectorsToApp(void)
{
	for(int i = 0; i < FLASH_RETRIES; i++)
	{
		// ok, lets overwrite the bootloader vectors
		if(DrvFlashEraseProgramSector((uint32_t*)__loader_origin, (uint32_t*) __app_origin, 0x410) &&
				DrvFlashProgram((uint32_t*)0x410, (uint32_t*)metaVectors, sizeof(metaVectors)) &&
				(memcmp((char*)__loader_origin, (char*) __app_origin, FLASH_VECTOR_SIZE) == 0))
		{
			// let's put in pre rev 1.4 values
			gBootCfg.cfg.ImageInfoFromLoader.OTAmaxImageSize = 0x80000;
			gBootCfg.cfg.ImageInfoFromLoader.OTAstartAddrForApp = 0xF80000;
			gBootCfg.cfg.ImageInfoFromLoader.newAppProgStatus = 0;
			if(false == bootConfigWriteFlash(&gBootCfg))
			{
				// too early to write an event log message so nothing to do here
			}
			return;
		}
	}
	// help! what do we do here?
}

void appendToManifestBuf(const char* toBeAppended)
{
	g_nManifestLength += strlen(toBeAppended);

	if(g_nManifestLength >= MAX_MANIFEST_LENGTH)
	{
		LOG_EVENT(0, LOG_NUM_APP, ERRLOGMAJOR,  "Manifest Build, sample Buf Overrun count %d \n", g_nManifestLength);
	}
	else
	{
		strcat(g_strManifestBuf, toBeAppended);
	}
}

//------------------------------------------------------------------------------------------------------------
/// Extract image information from meta-data.
///
/// @param  meta_data (const char*) -- Pointer to meta-data.
/// @param  peImageType (ImageType_t*) -- Pointer to returned image type. Can **NOT** be NULL.
/// @param  pnImageSize (uint32_t*) -- Pointer to returned image size. Can **NOT** be NULL.
///
/// @return bool  true = Success, false = Failure
//------------------------------------------------------------------------------------------------------------
bool image_jsonExtractImageInformation(const char* meta_data, ImageType_t* peImageType, uint32_t* pnImageSize)
{
	bool bSuccess = false;

	*peImageType = IMAGE_TYPE_UNKNOWN;
	*pnImageSize = 0;

	char image_type[32];
	if((JSON_string == jsonFetch((char*)meta_data, "imageType", image_type, true)) &&
	   (JSON_value == jsonFetch((char*)meta_data, "imageSize", pnImageSize, true)))
	{
		bSuccess = true;
	}

	if(bSuccess)
	{
		if(strcmp(sMK24_Loader, image_type) == 0)
		{
			*peImageType = IMAGE_TYPE_LOADER;
		}
		else if(strcmp(sMK24_Application, image_type) == 0)
		{
			*peImageType = IMAGE_TYPE_APPLICATION;
		}
		else if(strcmp(sPMIC_Application, image_type) == 0)
		{
			*peImageType = IMAGE_TYPE_PMIC_APPLICATION;
		}
		else if(strcmp(sPackage, image_type) == 0)
		{
			*peImageType = IMAGE_TYPE_PACKAGE;
		}
	}

	return bSuccess;
}

/*
 * image_UpdateLoaderFromExtFlashAddr
 *
 * @brief Copy from external flash image to loader flash
 *
 *
 * @param extFlashAddr - Start address of the recently downloaded Loader OTA, in
 * 						 the external flash.
 *
 * @return  true - if successful , false otherwise.
 */
bool image_UpdateLoaderFromExtFlashAddr(uint32_t nLoaderOTASrcAddr)
{
	bool rc_ok = false;

	// only accept an external flash image index
	if((nLoaderOTASrcAddr < EXT_IMAGE_APP) &&
	   (nLoaderOTASrcAddr > LAST_IMAGE_EXT_FLASH_START_ADDR))
	{
		return false;
	}

	/*
	 * Always find meta-data here.. if we change this then breaking change!
	 */
	int meta_offset = ((uint32_t)__app_version) - ((uint32_t)__app_origin);
	if(!IS25_ReadBytes(nLoaderOTASrcAddr + meta_offset, (uint8_t*)m_aszCachedMetaData, (int)__app_version_size))
	{
		return false;
	}

	uint32_t major = 0, minor = 0, imageSize = 0;
	ImageType_t eImageType;

	// check image type and size
	if(!image_jsonExtractImageInformation(m_aszCachedMetaData, &eImageType, &imageSize) ||
	   (eImageType != IMAGE_TYPE_LOADER) ||
	   (imageSize > (int)__loader_image_size) ||
	   (imageSize == 0))
	{
		return false;
	}

	// check for minimum major/minor values
	if(JSON_value != jsonFetch(m_aszCachedMetaData, "Major", &major, true))
	{
		return false;
	}

	if(JSON_value != jsonFetch(m_aszCachedMetaData, "Minor", &minor, true))
	{
		return false;
	}

	if(major < APP_MINIMUM_MAJOR_VERSION)
	{
		return false;
	}

	if((major == APP_MINIMUM_MAJOR_VERSION) && (minor < APP_MINIMUM_MINOR_VERSION))
	{
		return false;
	}

	// OK, things are looking good for the image so let's flash it
	// just in case we should retry the flashing of the bootloader
	for(int retry = 0; retry < FLASH_RETRIES; retry++)
	{
		uint32_t image_size = (uint32_t)__loader_image_size;
		uint32_t image_start = (uint32_t)__loader_origin;

		// Check if the application flash is already erased
		rc_ok = blank_check((uint8_t*)image_start, image_size);
		if(rc_ok == false)
		{
			// Flash was not erased before - erase all sectors
			rc_ok = DrvFlashEraseSector((uint32_t*)image_start, image_size);
			if(rc_ok == false)
			{
				LOG_EVENT(0, LOG_NUM_APP, ERRLOGMAJOR,  "Internal flash failed sector erase");
				continue;
			}
		}

		LOG_DBG(LOG_LEVEL_CLI,  "LOADER_IMAGE erased\n");

		// OK, let's flash the loader
		for(int i = 0; i < imageSize; i += BLOCK_SIZE)
		{
			// read a block from external flash
			uint32_t length = ((i + BLOCK_SIZE) < imageSize) ? BLOCK_SIZE : imageSize - i;
			rc_ok = IS25_ReadBytes(nLoaderOTASrcAddr + i, (uint8_t*) __sample_buffer, length);
			if(!rc_ok)
			{
				LOG_EVENT(0, LOG_NUM_APP, ERRLOGMAJOR,  "External flash failed read operation");
				break;
			}

			// program to flash
			uint32_t flash_addr = (uint32_t)__loader_origin + i;
			if (DrvFlashProgram((uint32_t*) flash_addr, (uint32_t*) __sample_buffer, length)==false)
			{
				printf("Flash ERROR at %X\n", flash_addr );
				// TODO log error
				rc_ok = false; /* flash programming failed */
			}
			else
			{
				// verify !
				if(0 != memcmp((void*)flash_addr, (void*)__sample_buffer, length))
				{
					printf("flash verify error\n");
					rc_ok = false;
				}
			}

			if(!rc_ok)
			{
				LOG_EVENT(0, LOG_NUM_APP, ERRLOGMAJOR,  "Internal Loader flash failed to program, retry = %d", (retry + 1));
				break;
			}
		}

		if(rc_ok)
		{
			// looking good so lets CRC the BOOTLOADER image
			if (CRCTARGETVALUE == crc32_hardware((void*)__loader_origin, (uint32_t)__loader_image_size))
			{
				return true;
			}
		}
	}

	// we didn't succeed so write the APP vectors to the BOOTLOADER space and hope it works
	setLoaderVectorsToApp();
	return rc_ok;
}

/*
 * image_UpdatePmicAppFromExtFlashAddr
 *
 * @brief Copy from external flash image to PMIC flash
 *
 *
 * @param nPmicAppOTASrcAddr - Start address of the recently downloaded PMIC App
 * 							   Image in the external flash.
 *
 * @return  result code from flash read or PMIC programming operation. 0 is success.
 */
int image_UpdatePmicAppFromExtFlashAddr(uint32_t nPmicAppOTASrcAddr)
{
	/*
	 * Always find meta-data here.. if we change this then breaking change!
	 */
	if(!IS25_ReadBytes(nPmicAppOTASrcAddr + 0x410, (uint8_t*)m_aszCachedMetaData, (int)__app_version_size))
	{
		return -1;
	}

	uint32_t major = 0, minor = 0, imageSize = 0;
	ImageType_t eImageType;

	// check image type and size
	if(!image_jsonExtractImageInformation(m_aszCachedMetaData, &eImageType, &imageSize) ||
		   (eImageType != IMAGE_TYPE_PMIC_APPLICATION) ||
		   (imageSize == 0))
	{
		return -1;
	}

	// check for minimum major/minor values
	if(JSON_value != jsonFetch(m_aszCachedMetaData, "Major", &major, true))
	{
		return -1;
	}

	if(JSON_value != jsonFetch(m_aszCachedMetaData, "Minor", &minor, true))
	{
		return -1;
	}

	if(!IS25_ReadBytes(nPmicAppOTASrcAddr, (uint8_t*) __sample_buffer, imageSize))
	{
		return -1;
	}
	extern int PMICprogram(const uint8_t *image, uint32_t size);
	return PMICprogram((uint8_t*)__sample_buffer, imageSize);
}

/*
 * checkLoaderGE_1_4
 *
 * @brief Check if the loader is version 1.4 or above.
 *        If not set the interrupt vectors to the app
 *
 * @return	true if loader is version 1.4 or greater
 *
 */
bool checkLoaderGE_1_4(void)
{
	uint32_t major = 0, minor = 0;
	char *p = (char *)__loader_version;

	// first check if the vectors have already been set
	if(memcmp((char*)__loader_origin, (char*)__app_origin, FLASH_VECTOR_SIZE) == 0)
	{
		return false;
	}

	// now check the version number
	if((*(uint8_t*)p == 0xFF) ||
	   (JSON_value != jsonFetch(p, "Major", &major, true)) ||
	   (JSON_value != jsonFetch(p, "Minor", &minor, true)) ||
	   (((major << 16) + minor) < 0x00010004))
	{
		// ok, less  than 1.4 so set the vectors
		setLoaderVectorsToApp();
		return false;
	}

	return true;
}

//------------------------------------------------------------------------------
// CLI for Image management
//------------------------------------------------------------------------------

static bool cliExtFlashInit( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    bool rc_ok = true;
    uint32_t baudrate = IS25_TRANSFER_BAUDRATE;
    uint32_t baud;

    if (args==1)
    {
    	baudrate = argi[0];
    }

    rc_ok = image_initExternalFlashInterface(baudrate, &baud);
    printf("image_initExternalFlashInterface(%u) returned baud=%u status = %u\n", (unsigned int)baudrate, (unsigned int)baud, (unsigned int)rc_ok);

    return rc_ok;
}


static bool cliExtFlashDump( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    bool rc_ok = true;

	if (args == 0)
	{
		rc_ok = false;
	}
	else
	{
		unsigned int i;
		uint32_t strt, end;
		strt =  argi[0];
		end = strt+16;

		if (args>1)
		{
			if (argv[1][0] == '+')
			{
				end = strt + argi[1];
			} else
			{
				if (argi[1] > strt)
				{
					end = argi[1];
				}
			}
		}
		while (end > strt && rc_ok)
		{
			uint8_t buf[16];
			int count = (end - strt) > 16 ? 16 : (end - strt);

			printf(" %08x :",(unsigned int)strt);

			rc_ok = IS25_ReadBytes(strt, buf, count);
			if (rc_ok) {
				for(i=0; i<count; i++) {
					if (i==8) printf(" ");
					printf(" %02x",buf[i]);
				}
				printf("  ");
				for(i=0; i<count; i++) {
					if (i==8) printf(" ");
					printf("%c",buf[i] < 32 || buf[i]> 126 ? '.' : buf[i]);
				}

				put_s("\r\n");
				strt += count;
			}
		}
	}
    return rc_ok;
}


static bool cliExtFlashInfo( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    bool rc_ok = true;

    for(int i = APPLICATION_IMAGE; i <= EXT_IMAGE_2; i++)
    {
    	char *p;
    	bool current = false;
    	if(crcImage(i))
    	{
			if(i == APPLICATION_IMAGE)
			{
				p = (char*)__app_version;
			}
			else
			{
				uint32_t addr = getImageStartAddress(i);
				uint32_t length = (uint32_t)__app_version_size;
				p = (char*)m_aszCachedMetaData;
				rc_ok = IS25_ReadBytes(addr + 0x410, (uint8_t*) m_aszCachedMetaData, length);
				if(!rc_ok)
				{
					*p = 0;
				}
				else
				{
					current = (strcmp((char*)__app_version,(char*) m_aszCachedMetaData) == 0);
				}
			}
			printf("        version %s %s\n",
					getFirmwareFullSemVersionMeta(p),
					(current ? "(CURRENT)" : ""));
    	}
    }

    return rc_ok;
}


static bool cliCRC(uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	return crcImage(argi[0]);
}

static bool cliSaveApp(uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	return saveToImage(argi[0]);
}

static bool cliPrintConfig(uint32_t args, uint8_t* argv[], uint32_t* argi)
{
	print_bootconfig(&gBootCfg);
	return true;
}

static bool cliShowManifest(uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	printf("%s \r\n", image_createManifest(IMAGE_TYPE_APPLICATION, __app_version));
	return true;
}
//------------------------------------------------------------------------------
// CLI Help
//------------------------------------------------------------------------------

static const char imageHelp[] = {
        "image subcommands:\r\n"
        " info: show image and flash device information\r\n"
		" manifest: Show image manifest\r\n"
        " init <baud> <timeoutMs>: initialise external flash SPI interface\r\n"
        " dump <address> [end | +count]: dump external flash memory\r\n"
		" crc <image index>: check CRC (0 for App)\r\n"
		" saveapp <image index>: copy app to external image\r\n"
		" showconfig\r\n"
};


bool image_cliHelp(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
    printf("%s",imageHelp);
    return true;
}

struct cliSubCmd imageSubCmds[] =
{
	{"info" , 		cliExtFlashInfo},
	{"manifest" , 	cliShowManifest},
	{"init", 		cliExtFlashInit},
	{"dump", 		cliExtFlashDump},
	{"crc", 		cliCRC},
	{"saveapp", 	cliSaveApp},
	{"showconfig",	cliPrintConfig}
};


/*
 * Public interface
 */
bool image_cliImage( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    bool rc_ok = true;

    if (args)
    {
	  rc_ok = cliSubcommand(args,argv,argi, imageSubCmds, sizeof(imageSubCmds)/sizeof(*imageSubCmds));
	  if (rc_ok == false)
	  {
		  printf("Command failed!\n");
		  // but it was a valid syntax!
		  rc_ok = true;
	  }
    } else
    {
    	printf("%s",imageHelp);
    }
    return rc_ok;
}


#ifdef __cplusplus
}
#endif