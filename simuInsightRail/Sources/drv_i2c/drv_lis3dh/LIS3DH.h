#ifdef __cplusplus
extern "C" {
#endif

/*
 * LIS3DH.c
 *
 *  Created on: 19 Jan 2016
 *      Author: Rex Taylor BF1418 (Livingston)
 */

#ifndef LIS3DH_H_
#define LIS3DH_H_

// sample freq in Hz
#define LIS3DH_SAMPLEFREQ (100)

typedef struct  {
    int16_t x;
    int16_t y;
    int16_t z;
} tLis3dAcc;

typedef struct  {
    float x;
    float y;
    float z;
} tLis3dAccf;

// public function prototypes

bool lis3dh_read(uint32_t samples,  tLis3dAcc *acc3buf, tLis3dAccf * acc3dmeanp, tLis3dAccf * acc3dStd);
bool lis3dh_readAveraged(float* gX, float* gY, float* gZ, uint8_t durationSecs, uint8_t samplesPerSecond);
bool lis3dh_movementMonitoring(float threshold_g, uint32_t duration_sec,  bool * movementDetectedp);
bool lis3dh_selfTest(uint32_t samples);

#endif	// #ifndef LIS3DH_H_


#ifdef __cplusplus
}
#endif