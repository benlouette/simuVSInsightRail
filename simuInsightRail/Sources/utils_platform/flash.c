#ifdef __cplusplus
extern "C" {
#endif

/*
 * flash.c
 *
 *  Created on: Nov 17, 2014
 *      Author: George de Fockert
 */

/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "SSD_FTFx.h"
#include "Resources.h"
#include "flash.h"

/*
 * little shell around the PE flash read/write routines
 */

/*
 * Functions
 */



// most is stolen from sdk 'flash' example for frdmk22f

#if  (FSL_FEATURE_FLASH_IS_FTFE == 1)
#define FTFx_REG_BASE             (FTFE_BASE)
#endif

#if  (FSL_FEATURE_FLASH_IS_FTFL == 1)
#define FTFx_REG_BASE             (FTFL_BASE)
#endif

#define P_FLASH_BASE              0x00000000

// Program Flash block information
#define P_FLASH_SIZE            (FSL_FEATURE_FLASH_PFLASH_BLOCK_SIZE * FSL_FEATURE_FLASH_PFLASH_BLOCK_COUNT)
#define P_BLOCK_NUM             FSL_FEATURE_FLASH_PFLASH_BLOCK_COUNT
#define P_SECTOR_SIZE           FSL_FEATURE_FLASH_PFLASH_BLOCK_SECTOR_SIZE
// Data Flash block information
#define FLEXNVM_BASE            FSL_FEATURE_FLASH_FLEX_NVM_START_ADDRESS
#define FLEXNVM_SECTOR_SIZE     FSL_FEATURE_FLASH_FLEX_NVM_BLOCK_SECTOR_SIZE
#define FLEXNVM_BLOCK_SIZE      FSL_FEATURE_FLASH_FLEX_NVM_BLOCK_SIZE
#define FLEXNVM_BLOCK_NUM       FSL_FEATURE_FLASH_FLEX_NVM_BLOCK_COUNT

// Flex Ram block information
#define EERAM_BASE              FSL_FEATURE_FLASH_FLEX_RAM_START_ADDRESS
#define EERAM_SIZE              FSL_FEATURE_FLASH_FLEX_RAM_SIZE


// size of array to copy__Launch_Command function to
// It should be at least equal to actual size of __Launch_Command func
// User can change this value based on RAM size availability and actual size of __Launch_Command function
//
// TODO : SDK: how to secure this will always be big enough !
//  .text.FlashCommandSequence
//                0x00013fbc       0x2c ./SDK/platform/drivers/src/flash/C90TFS/drvsrc/source/FlashCommandSequence.o
//                0x00013fbc                FlashCommandSequence
// after linking it appears 0x2c in size, so reserve 0x40 is safe ??
// TODO
#define LAUNCH_CMD_SIZE           0x40



#define DEBUGENABLE               0x00

///////////////////////////////////////////////////////////////////////////////
// Prototypes
///////////////////////////////////////////////////////////////////////////////
extern uint32_t RelocateFunction(uint32_t dest, uint32_t size, uint32_t src);


static pFLASHCOMMANDSEQUENCE g_FlashLaunchCommand = (pFLASHCOMMANDSEQUENCE)0xFFFFFFFF;

// Array to copy __Launch_Command func to RAM.
uint32_t ramFunc[LAUNCH_CMD_SIZE/sizeof(uint32_t)];// on a 32 bit architecture, I prefer 32 bit boundaries

// Flash Standard Software Driver Structure.
static FLASH_SSD_CONFIG flashSSDConfig =
{
    FTFx_REG_BASE,          /*! FTFx control register base */
    P_FLASH_BASE,           /*! Base address of PFlash block */
    P_FLASH_SIZE,           /*! Size of PFlash block */
    FLEXNVM_BASE,           /*! Base address of DFlash block */
    0,                      /*! Size of DFlash block */
    EERAM_BASE,             /*! Base address of EERAM block */
    0,                      /*! Size of EEE block */
    DEBUGENABLE,            /*! Background debug mode enable bit */
    NULL_CALLBACK           /*! Pointer to callback function */
};



static bool flashInit()
{
    uint32_t result;               /*! Return code from each SSD function */

    // Setup flash SSD structure for device and initialize variables.
    result = FlashInit(&flashSSDConfig);// probably does not hurt when doing it more than  once
	// Set command to RAM.
	if (result==FTFx_OK ) {
		g_FlashLaunchCommand = (pFLASHCOMMANDSEQUENCE)RelocateFunction((uint32_t)ramFunc , LAUNCH_CMD_SIZE ,(uint32_t)FlashCommandSequence);
	}

    return result==FTFx_OK;
}

/*
 * DrvFlashEraseSector
 *
 * @brief           erase flash memory, always complete sectors, assumes buffer starts at a flash sector boundary,
 * 					smallest erased amount is one sector
 *
 * @param dst       destination address (in flash)
 * @param len       length in bytes
 * @returns true when successful, false otherwise
 */

bool DrvFlashEraseSector(uint32_t *dst, uint32_t len)
{
	bool rc_ok = true;
    uint32_t result;               /*! Return code from each SSD function */

    if (len < FTFx_PSECTOR_SIZE) len = FTFx_PSECTOR_SIZE;
    rc_ok = flashInit();

    if (rc_ok) {

    	// Erase a sector from destAdrss.
    	result = FlashEraseSector(&flashSSDConfig, (uint32_t) dst, len, g_FlashLaunchCommand);
    	if (FTFx_OK != result) {
    		rc_ok = false;
    	}
    }
    return rc_ok;
}

/*
 * DrvFlashCheckErased (TODO : test this functions)
 *
 * @brief           test if erased properly
 *
 * @param dst       destination address (in flash)
 * @param len       length in bytes
 * @param margin	0=normal, 1=user, 2=factory - margin read for reading see processor manual for details about margin
 * @returns true when successful, false otherwise
 */

/*! 0=normal, 1=user, 2=factory - margin read for reading */
bool DrvFlashCheckErased(int32_t *dst, uint32_t len, uint32_t margin)
{
	bool rc_ok = true;
    uint32_t result;               /*! Return code from each SSD function */

    rc_ok = flashInit();

    if (rc_ok) {

		result = FlashVerifySection(&flashSSDConfig, (uint32_t) dst, len, margin, g_FlashLaunchCommand);

		if (FTFx_OK != result) {
			rc_ok = false;
		}
    }
	return rc_ok;
}

/*
 * DrvFlashProgram
 *
 * @brief           write buffer to flash, assumes memory is 'blank'
 *
 * @param src       source address (in ram)
 * @param dst       destination address (in flash)
 * @param len       length in bytes (rounded upwards to multiple of 4 bytes
 * @returns true when successful, false otherwise
 */
bool DrvFlashProgram(uint32_t * dst, uint32_t *src, uint32_t len)
{
	bool rc_ok = true;
    uint32_t result;               /*! Return code from each SSD function */

    rc_ok = flashInit();

    len += (PGM_SIZE_BYTE - 0x01U);
    len &= ~(PGM_SIZE_BYTE - 0x01U);
    if (rc_ok) {
#if 0
        if ((len & (PGM_SIZE_BYTE - 0x01U)) || ((uint32_t) dst % PGM_SIZE_BYTE))
        {
           printf("address = %08x size = %d\n", dst,len);
        }

#else
        result = FlashProgram(&flashSSDConfig, (uint32_t) dst, len, (uint8_t*) src, g_FlashLaunchCommand);

    	if (FTFx_OK != result) {
    		rc_ok = false;
    	}
#endif
    }
    return rc_ok;
}

/*
 * DrvFlashVerify (TODO : test this functions)
 *
 * @brief           verify programmed flash against memory buffer
 *
 * @param src       source address (in ram)
 * @param dst       destination address (in flash)
 * @param len       length in bytes
 * @param margin	0=normal, 1=user, 2=factory - margin read for reading see processor manual for details about margin
 * @returns true when successful, false otherwise
 */

bool DrvFlashVerify(uint32_t * dst, uint32_t *src, uint32_t len, uint32_t margin)
{
	bool rc_ok = true;
    uint32_t result;               /*! Return code from each SSD function */
    uint32_t failAddr;

    rc_ok = flashInit();

    if (rc_ok) {

        result = FlashProgramCheck(&flashSSDConfig, (uint32_t) dst, len, (uint8_t *)src, &failAddr, margin, g_FlashLaunchCommand);

        if (FTFx_OK != result) {
        	rc_ok = false;
        }
    }
	return rc_ok;
}


/*
 * DrvFlashEraseProgramSector
 *
 * @brief           write buffer to flash, assumes buffer starts at a flash sector boundary
 *
 * @param src       source address (in ram)
 * @param dst       destination address (in flash)
 * @param len       length in bytes
 * @returns true when successful, false otherwise
 */
bool DrvFlashEraseProgramSector(uint32_t * dst, uint32_t *src, uint32_t len)
{
    bool rc_ok = true;
    //unsigned int i;

    // TODO: announce all interrupts will be disabled


    rc_ok = DrvFlashEraseSector(dst, len);
    if (rc_ok) {
    	rc_ok = DrvFlashProgram(dst, src, len);
    }

    // TODO: announce interrupts will be enabled again
    return rc_ok;
}



#ifdef __cplusplus
}
#endif