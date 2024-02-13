#ifdef __cplusplus
extern "C" {
#endif

/*
 * DrvTmp431.h
 *
 *  Created on: 17 Dec 2015
 *      Author: Rex Taylor BF1418
 */

#ifndef DRVTMP431_H_
#define DRVTMP431_H_


// Register definitions
#define MSB_LOCAL_TEMP_READ					0x00
#define MSB_REMOTE_TEMP_READ				0x01
#define STATUS_REGISTER_TEMP				0x02
#define CONFIG_REG_1_READ					0x03
#define CONVERSION_RATE_READ				0x04
#define MSB_LOCAL_TEMP_HIGH_LIMIT_READ		0x05
#define MSB_LOCAL_TEMP_LOW_LIMIT_READ		0x06
#define MSB_REMOTE_TEMP_HIGH_LIMIT_READ		0x07
#define MSB_REMOTE_TEMP_LOW_LIMIT_READ		0x08
#define CONFIG_REG_1_WRITE					0x09
#define CONVERSION_RATE_WRITE				0x0A
#define MSB_LOCAL_TEMP_HIGH_LIMIT_WRITE		0x0B
#define MSB_LOCAL_TEMP_LOW_LIMIT_WRITE		0x0C
#define MSB_REMOTE_TEMP_HIGH_LIMIT_WRITE	0x0D
#define MSB_REMOTE_TEMP_LOW_LIMIT_WRITE		0x0E
#define ONE_SHOT_WRITE						0x0F
#define LSB_REMOTE_TEMP_READ				0x10
#define LSB_REMOTE_TEMP_HIGH_LIMIT_RW		0x13
#define LSB_REMOTE_TEMP_LOW_LIMIT_RW		0x14
#define LSB_LOCAL_TEMP_READ					0x15
#define LSB_LOCAL_TEMP_HIGH_LIMIT_RW		0x16
#define LSB_LOCAL_TEMP_LOW_LIMIT_RW			0x17
#define N_FACTOR_CORRECTION_RW				0x18
#define REMOTE_THERM_LIMIT_RW				0x19
#define CONFIG_REG_2_RW						0x1A
#define CHANNEL_MASK_RW						0x1F
#define LOCAL_THERM_LIMIT_RW				0x20
#define THERM_HYSTERESIS_RW					0x21
#define CONSECUTIVE_ALERT_RW				0x22
#define BETA_RANGE_RW						0x25
#define SOFTWARE_RESET_WRITE				0xFC
#define TMP431_DEVICE_ID_READ				0xFD
#define MANUFACTURER_ID_READ				0xFE

#define TMP431_LOCAL	0
#define TMP431_REMOTE	1

#define TMP431_ERROR_TEMP	-100.0
#define FIX_TEMP(t)		(__isnanf(t) ? TMP431_ERROR_TEMP : t)

#ifdef TMP431_LIMITS
// TODO with a comment stating that "Jayant needs to delete this if not required for the Temp. check" Is that OK?

typedef struct tempConfig
{
    int8_t msbLocalHighLimit;
    int8_t msbLocalLowLimit;
    int8_t lsbLocalHighLimit;
    int8_t lsbLocalLowLimit;

    int8_t msbRemoteHighLimit;
    int8_t msbRemoteLowLimit;
    int8_t lsbRemoteHighLimit;
    int8_t lsbRemoteLowLimit;

    uint8_t localThermLimit;
    //uint8_t remoteThermLimit;
    uint8_t tripThermLimit;

    uint8_t thermHysteresis;
} TMP431_config_t;
bool ConfigTmp431(TMP431_config_t tempConfig);
bool CheckTempAlert();
#endif


// public function prototypes
bool tmp431_init();
bool ReadTmp431Temperatures(float* pLocalTemp, float* pRemoteTemp);
bool ReadTmp431Register(uint32_t register, uint8_t* value);
bool tmp431Valid(int localOrRemote, float *pTemperature);

#endif /* DRVTMP431_H_ */


#ifdef __cplusplus
}
#endif