#ifdef __cplusplus
extern "C" {
#endif

/*
 * NvmCalib.c
 *
 *  Created on: Nov 21, 2016
 *      Author: ka3112
 */


/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "printgdf.h"

// TODO: next include for the enable/disable irq function, it draws in way too much, find the include file which is really needed
#include "fsl_interrupt_manager.h"
#include "flash.h"
#include "NvmCalib.h"
#include "CRC.h"

tNvmCalib gNvmCalib;

static const tCalibScaling scalingDefaults = {
        .raw = 1.0,
        .bearingEnv3 = 1.0,
        .wheelFlat = 1.0
};



/*!
 * NvmCalibRead
 *
 * @brief      reads the Nvm calibration data
 *
 * @param      pointer to the destination buffer
 *
 * @returns true when successful, false otherwise
 */

bool NvmCalibRead( tNvmCalib * calib)
{
    bool stat = false;

    if (calib != NULL)
    {
        memcpy( (uint8_t *) calib,(uint8_t*) __device_calib,  sizeof(*calib)) ;

        // check crc
		stat = (calib->crc32 == crc32_hardware((void *)&calib->dat, sizeof(calib->dat)));
    }
    return stat;
}

/*!
 * NvmCalibWrite
 *
 * @brief      writes the Nvm calibration
 *
 * @param      pointer to the source buffer
 *
 * @returns true when successful, false otherwise
 */
bool NvmCalibWrite( tNvmCalib * calib)
{
    bool stat = false;

    if (calib != NULL)
    {
        // calculate crc
        calib->crc32 = crc32_hardware((void *)&calib->dat, sizeof(calib->dat));

        __disable_irq();// we may run in the same flash bank as the one we are erasing/programming, then interrupts may not execute code inside this bank !
        stat = DrvFlashEraseProgramSector((uint32_t*) __device_calib, (uint32_t*) calib, sizeof(*calib));
        __enable_irq();
    }

    return stat;
}


/*!
 * NvmCalibDefaults
 *
 * @brief      initialises and writes the Nvm calibration data
 *
 * @param      pointer to the source buffer
 *
 * @returns
 */

void NvmCalibDefaults(tNvmCalib * calib)
{
    memset( (uint8_t *) calib, 0xff, sizeof(*calib));// all to default flash empty state

    memcpy(&calib->dat.scaling, &scalingDefaults, sizeof(scalingDefaults));

}

static void printCalibScaling(tCalibScaling * scaling)
{
    printf("\nBEGIN SCALING:\n");
    printf("Raw         = %f\n", scaling->raw);
    printf("bearingEnv3 = %f\n", scaling->bearingEnv3);
    printf("wheelFlat   = %f\n", scaling->wheelFlat);
    printf("END SCALING\n\n");
}

void NvmPrintCalib()
{

    printf("\nNvm Calibration data dump:\n\n");
    printCalibScaling(&gNvmCalib.dat.scaling);
}


#ifdef __cplusplus
}
#endif