#ifdef __cplusplus
extern "C" {
#endif

/*
 * nfc.h
 *
 *  Created on: 16 Nov 2017
 *      Author: tk2319
 */

#ifndef SOURCES_APP_NFC_NFC_H_
#define SOURCES_APP_NFC_NFC_H_

typedef enum {
	NC_REG,
	LAST_NDEF_BLOCK,
	SRAM_MIRROR_BLOCK,
	WDT_LS,
	WDT_MS,
	I2C_CLOCK_STR,
	NS_REG
} NXP_Registers_t;


typedef enum {
	eNDEF_SELFTEST,
	//eNDEF_FACTORY,
	eNDEF_EMPTY,
	//eNDEF_TESTMODE,
	eNDEF_SHIPMODE,
	eNDEF_COMMISSIONED
} NDEF_Type_t;

#define BS_SIZE 256
typedef struct
{
	uint8_t contents[BS_SIZE];
	int count;
} NDEF;

#define NFC_BUS			I2C0_IDX

#define NDEF_TYPE_TEXT	0x54	// text record
#define NDEF_TYPE_URI	0x55	// uri record

#define SRAM_I2_READY 0x10
#define SRAM_RF_READY 0x08

#define PTHRU_DIR 1
#define PTHRU_ON_OFF 0x40

#define BYTES_PER_BLOCK (16)

#define MAX_NDEF (13*BYTES_PER_BLOCK)

#define MINIMUM_NFC_HARDWARE_LEVEL (HW_PASSRAIL_REV5)

bool CheckTagBufferEmpty(uint8_t buf[64]);
bool NFC_ReadRegister(NXP_Registers_t reg, uint8_t *val);
bool NFC_WriteRegister(NXP_Registers_t reg, uint8_t mask, uint8_t val);
bool NFC_SetDirection(bool i2c_is_master);
NDEF* NFC_GetNDEF();
bool NFC_getPMICtag();
bool NFC_EraseNDEF(void);
bool NFC_WriteNDEF(const int);
int NFC_BuildNDEF(NDEF_Type_t type);
bool NFC_ConfigSet(uint8_t fd);
bool NFC_SessionConfigSet(uint8_t new_val);
bool NFC_Config(void);
bool NFC_Session(void);
bool NFC_Fix(uint8_t addr);
bool NDEFAddText(uint8_t flags, NDEF *pNDEF, uint16_t NDEFsize, const char *pLabel, const char *pData);
const char* NFC_GetDevicePartNumber();
const char* NFC_GetManufacturingDate();

#endif /* SOURCES_APP_NFC_NFC_H_ */


#ifdef __cplusplus
}
#endif