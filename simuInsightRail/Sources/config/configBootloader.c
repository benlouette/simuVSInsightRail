#ifdef __cplusplus
extern "C" {
#endif

/*
 * configBootloader.c
 *
 *  Created on: Jul 28, 2016
 *      Author: ka3112 (and that is the SKF code for George de Fockert)
 */

#include "linker.h"
#include "configBootloader.h"

BootCfg_t gBootCfg;

/*
 * TODO: next routines should be removed from this config file, and go to their own bootloader subdirectory, but not today
 */
#include <string.h> // memcpy etc.

#include "freeRTOS.h"
#include "flash.h"
#include "CLIcmd.h"
#include "CLIio.h"
#include "log.h"
#include "printgdf.h"
#include "fsl_interrupt_manager.h"
#include "CRC.h"

extern int strcasecmp(const char *, const char *);		// Mutes the compiler warning.

/*!
 * bootpConfigWriteFlash
 *
 * @brief      writes the startup config
 *
 * @param      pointer to the source buffer
 *
 * @returns true when successful, false otherwise
 */
bool bootConfigWriteFlash(BootCfg_t * cfg)
{
    bool stat = true;

    if (cfg != NULL)
    {
        // calculate crc
        cfg->crc32 = crc32_hardware((uint8_t*)&cfg->cfg, sizeof(cfg->cfg));

        __disable_irq();// we may run in the same flash bank as the one we are erasing/programming, then interrupts may not execute code inside this bank !
        stat = DrvFlashEraseProgramSector((uint32_t*) __bootcfg, (uint32_t*) cfg, sizeof(*cfg));
        __enable_irq();

    }
    else
    {
        stat = false;
    }

    return stat;
}

/*
 * blank_check
 *
 * @desc    check if the flash area is all 0xff (=erased)
 *
 * @param   buf pointer to start of area
 *
 * @param   len number of bytes to check
 *
 * @returns true when blank
 */
bool blank_check(uint8_t *buf, uint32_t len)
{
    if ((((uint32_t) buf & 0x3) == 0)  && ((len & 0x3)==0))
    {
        // we can speedup the process by testing 32 bits at once
        uint32_t *buf32 = (uint32_t *) buf;
        len>>=2;
        while(len--)
        {
            if (*buf32++ != (uint32_t) 0xffffffff)
            {
            	return false;
            }
        }
    }
    else
    {
        // bytewise, slower test
        while ( len--)
        {
            if (*buf++ != 0xff)
            {
            	return false;
            }
        }
    }
    return true;
}


void print_bootconfig(BootCfg_t * cfg)
{
    printf("\n\tCRC\t\t\t= 0x%x\n", cfg->crc32);
	printf("From App\n");
    printf("\tactive_app\t\t= %d\n", cfg->cfg.FromApp.activeApp);
    printf("\tnew app address\t\t= %d\n", cfg->cfg.FromApp.NewAppAddress);
    printf("\tcrc_fail_app\t\t= 0x%x\n", cfg->cfg.FromApp.fallback);

    printf("ImageInfo\n");
    printf("\tOTA start address\t= 0x%x\n", cfg->cfg.ImageInfoFromLoader.OTAstartAddrForApp);
    printf("\tOTA max image size\t= 0x%x\n", cfg->cfg.ImageInfoFromLoader.OTAmaxImageSize);
    printf("\tNEW APP program status\t= %d\n", cfg->cfg.ImageInfoFromLoader.newAppProgStatus);

    printf("\tAPP backed up\t\t= ");

        switch(cfg->cfg.appBackUpStatus)
        {
        case eBackUpUnknown:
        	printf("unknown\n");
        	break;

        case eBackUpSkip:
        	printf("skipped");
        	break;

        case eBackUpTrue:
        	printf("true\n");
        	break;

        case eBackUpFalse:
        	printf("false\n");
        	break;
        }
}







#ifdef __cplusplus
}
#endif