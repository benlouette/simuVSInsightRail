#ifdef __cplusplus
extern "C" {
#endif

/*
 * NvmCalib.h
 *
 *  Created on: Nov 21, 2016
 *      Author: ka3112
 */

#ifndef SOURCES_DEVICE_NVMCALIB_H_
#define SOURCES_DEVICE_NVMCALIB_H_

#ifdef _MSC_VER
#define PACKED_STRUCT_START __pragma(pack(push, 1))
#define PACKED_STRUCT_END   __pragma(pack(pop))
#else
#define PACKED_STRUCT_START
#define PACKED_STRUCT_END   __attribute__((packed))
#endif

// next externals are defined in the linker script !
extern uint32_t  __device_calib[],
                 __device_calib_size;


PACKED_STRUCT_START
typedef struct  calibScalingStruct {
        float raw;
        float bearingEnv3;
        float wheelFlat;
} tCalibScaling;
PACKED_STRUCT_END

PACKED_STRUCT_START
// nvmCalib stuff
struct  nvmCalibStruct {
    uint32_t crc32; // 32 bit crc whole data struct

        PACKED_STRUCT_START
    struct  calibStruct {

        // some general  parameters
        tCalibScaling scaling;

        // etc.
    } dat;
        PACKED_STRUCT_END
};
PACKED_STRUCT_END


typedef struct nvmCalibStruct tNvmCalib;

extern tNvmCalib gNvmCalib;

bool NvmCalibRead( tNvmCalib * calib);
bool NvmCalibWrite( tNvmCalib * calib);
void NvmCalibDefaults( tNvmCalib * calib);

void NvmPrintCalib();


#endif /* SOURCES_DEVICE_NVMCALIB_H_ */


#ifdef __cplusplus
}
#endif