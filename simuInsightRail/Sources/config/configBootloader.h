#ifdef __cplusplus
extern "C" {
#endif

/*
 * configBootloader.h
 *
 *  Created on: Jul 28, 2016
 *      Author: ka3112 (and that is the SKF code for George de Fockert)
 */

#ifndef SOURCES_CONFIG_CONFIGBOOTLOADER_H_
#define SOURCES_CONFIG_CONFIGBOOTLOADER_H_

#include <stdint.h>
#include <stdbool.h>

// Status codes for gBootCfg.cfg.ImageInfoFromLoader.newAppProgStatus. These
// codes are reported by Loader to the App.
#define LOADER_NEWPACKAGE_FLASH_FAIL_STATUS	(-3)
#define LOADER_NEWAPP_INCOMPATIBLE_STATUS	(-2)
#define LOADER_NEWAPP_FLASH_FAIL_STATUS		(-1)
#define LOADER_NEWPACKAGE_PROG_SUCCESS		(2)
#define LOADER_NEWAPP_PROG_SUCCESS			(1)
#define LOADER_NEWAPP_NONE					(0)

typedef enum {
	eBackUpUnknown = 0,
	eBackUpTrue = 1,
	eBackUpFalse = 2,
	eBackUpSkip = 3
} eBackUpStatus_t;

/******************************************************************************/
#ifdef _MSC_VER
#define PACKED_STRUCT_START __pragma(pack(push, 1))
#define PACKED_STRUCT_END   __pragma(pack(pop))
#else
#define PACKED_STRUCT_START
#define PACKED_STRUCT_END   __attribute__((packed))
#endif
struct bootConfig_s {
    uint32_t crc32;
    PACKED_STRUCT_START
    struct boot_cfg {

    	// Specific instructions from App
        struct options_s {
            uint32_t activeApp;					// Which app should be started (1 = loader)
            uint32_t NewAppAddress;				// If non-zero, program App from this image
            uint32_t fallback;					// >0, App suggests this in event of internal app CRC fail
        } FromApp;

        // used by App to program OTA - Loader sets this up
        struct image_info_s {
        	uint32_t OTAstartAddrForApp; 	// App should build OTA from this address in ext. flash
        	uint32_t OTAmaxImageSize;		// App should not exceed this size
        	int newAppProgStatus;			// 0-> Nothing to program, 1->APP successfully programmed,
        									// -1-> Indicates Flash error, -2-> Indicates Incompatible App, no prog. done.
        } ImageInfoFromLoader;

        eBackUpStatus_t appBackUpStatus;
    }  cfg;
    PACKED_STRUCT_END
};


typedef struct bootConfig_s BootCfg_t;

extern BootCfg_t gBootCfg;
/******************************************************************************/

bool blank_check(uint8_t *buf, uint32_t len);
bool Flash_Prog(uint8_t block, uint32_t flash_addr, uint8_t *data_addr, uint16_t nofDataBytes);
bool test_crc( uint8_t block );
bool bootConfigWriteFlash(BootCfg_t * cfg);
void print_bootconfig(BootCfg_t * cfg);
/*
 * configBootloader_GetAcceptableImageSize_Bytes
 *
 * @brief Provides the information about max image size acceptable for an OTA.
 *
 * @return  Maximum image size (Bytes) acceptable for an OTA.
 */
inline uint32_t configBootloader_GetAcceptableImageSize_Bytes()
{
	return gBootCfg.cfg.ImageInfoFromLoader.OTAmaxImageSize;
}

#endif /* SOURCES_CONFIG_CONFIGBOOTLOADER_H_ */


#ifdef __cplusplus
}
#endif