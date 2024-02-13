#ifdef __cplusplus
extern "C" {
#endif

/*
 * nfc.c
 *
 *  Created on: 19 Sep 2017
 *      Author: John McRobert tk2319 (Livingston)
 */
#include "configFeatures.h"
#ifdef CONFIG_PLATFORM_NFC

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "fsl_rtc_hal.h"
#include "fsl_rtc_driver.h"

#include "i2c.h"
#include "device.h"
#include "json.h"
#include "linker.h"
#include "NvmConfig.h"
#include "pmic.h"

#include "nfc.h"
#include "Modem.h"
#define _CRT_SECURE_NO_WARNINGS
#define MUTEX_MAXWAIT_MS (300)

#define BLOCK_START		0x03	// block start

#define NDEF_MB			0x80	// message begin
#define NDEF_MM 		0x00	// message middle
#define NDEF_ME 		0x40	// message end
#define NDEF_CF			0x20	// chunk flag
#define NDEF_SET		0x10	// always set
#define NDEF_IL			0x08	// ID_LENGTH field is present

#define NDEF_TNF_0      0x00    // Empty
#define NDEF_TNF_1		0x01	// NFC Forum well-known type [RFC RTD]
#define NDEF_TNF_2      0x02    // Media-type as defined in RFC 2046 [RFC 2046]
#define NDEF_TNF_3      0x03    // Absolute URI as defined in RFC 3986 [RFC 3986]
#define NDEF_TNF_4      0x04    // NFC Forum external type [NFC RTD]
#define NDEF_TNF_5      0x05    // Unknown
#define NDEF_TNF_6      0x06    // Unchanged
#define NDEF_TNF_7      0x07    // Reserved

#define NDEF_START		(NDEF_MB + NDEF_SET + NDEF_TNF_1)
#define NDEF_MIDDLE		(NDEF_SET + NDEF_TNF_1)
#define NDEF_END		(NDEF_ME + NDEF_SET + NDEF_TNF_1)

i2c_device_t nfc =
{
    .address = 0x55,
    .baudRate_kbps = 100
};

static NDEF ndef;

static int NFC_BuildNDEFHelper(const char* const);

#ifdef USE_NFC_MUTEX

// local variables
static mutex_t NFCMutex = NULL;


/*
 * NFCInit()
 *
 * @desc    Allocates OS resources
 *
 * @param
 *
 * @return Bool variable - Returns true if function completed correctly
 */
bool NFCInit()
{
    bool rc_ok = true;
    if(NFCMutex == NULL)
    {
        rc_ok =  (kStatus_OSA_Success == OSA_MutexCreate(&NFCMutex));
    }
    return rc_ok;
}

/*
 * NFCTakeMutex()
 *
 * @desc    Function to take control of the NFC mutex
 *
 * @param
 *
 * @return Bool variable - Returns true if function completed correctly
 */
static bool NFCTakeMutex()
{
    bool rc_ok = false;

    if (NFCMutex) {
        rc_ok = (kStatus_OSA_Success == OSA_MutexLock(&NFCMutex, MUTEX_MAXWAIT_MS));
    }

	return rc_ok;
}

/*
 * NFCGiveMutex()
 *
 * @desc    Function to release the NFC mutex
 *
 * @param
 *
 * @return Bool variable - Returns true if function completed correctly
 */
static bool NFCGiveMutex()
{
    return  kStatus_OSA_Success == OSA_MutexUnlock(&NFCMutex);
}
#endif


const char* NFC_GetManufacturingDate()
{
	static char date[7];
	rtc_datetime_t datetime;

	RTC_DRV_GetDatetime(RTC_IDX, &datetime);
	sprintf(date, "%02d%02d%02d", datetime.day, datetime.month, datetime.year % 100);

	return date;
}

/*
 *NFC_getPMICtag()
 *
 * @desc    Read NFC tag contents from PMIC
 *
 * @param	none
  *
 * @return - true if OK, otherwise false
 */
bool NFC_getPMICtag()
{
	bool rc_ok = false;
	if(Device_HasPMIC())
	{
	  uint8_t cmd[MK24_PMIC_PROTOCOL_OVERHEAD + MK24_PMIC_MESSAGE_OVERHEAD];

	  // construct 'MK24_READ_NDEF_RECORDS_ID' command
	  int len = PMIC_formatCommand(MK24_READ_NDEF_RECORDS_ID, cmd, sizeof(cmd), 0, 0);

	  // send it and wait for reply
	  PMIC_SendCommand(cmd, len);
	  if(pdTRUE != xQueueReceive(PMIC_GetNDEF_QueueHandle(), &rc_ok, 8000/portTICK_PERIOD_MS))
	  {
		  return false;
	  }
	}
	else
	{
		rc_ok = true; //ToDo battery variant code
	}
	return rc_ok;
}

/*
 * TODO - interrogate modem for region
 *
 */
const char* NFC_GetDevicePartNumber()
{
	static char partNumber[8] = {0};

	const char* generation = Device_IsHarvester() ? "3" : "2";
	// TODO enquire from Modem
	char* modem = "1";

	if(partNumber[0] == 0)
	{
		if((Device_GetHardwareVersion() == HW_PASSRAIL_REV10) ||
		   (Device_GetHardwareVersion() >= HW_PASSRAIL_REV12))
		{
			modem = "3";
		}
		sprintf(partNumber, "CMWR %s%s", generation, modem);
	}

	return partNumber;
}


/*
 * NFC_FIx
 *
 * @desc    Restores the NFC Chip Address back to 0xAA
 *
 * @param   addr - address the chip is currently set to
 *
 * @return Bool variable - Returns true if function completed correctly
 */

bool NFC_Fix(uint8_t addr)
{
    bool rc_ok = false;

    if(Device_HasPMIC())
	{
		printf("Command not supported!\n");
		return false;
	}
#ifdef ENA_LEGACY_SUPPORT_SINGLE_MCU
    uint8_t bufRecv[64] = {0};

#ifdef USE_NFC_MUTEX
    if (NFCTakeMutex())
#endif
    {
    	nfc.address = addr;
		rc_ok = I2C_ReadRegister(NFC_BUS, nfc, 0, 16, &bufRecv[0]);
		rc_ok = I2C_ReadRegister(NFC_BUS, nfc, 1, 16, &bufRecv[16]);
		bufRecv[0] = 0xAA;		// write back I2C address
		bufRecv[12] = 0xE1;
		bufRecv[13] = 0x10;
		bufRecv[14] = 0x6D;
		bufRecv[15] = 0x00;
		rc_ok = I2C_WriteRegisters(NFC_BUS, nfc, 0, 16, &bufRecv[0]);
#ifdef USE_NFC_MUTEX
        // always give back when we took it
        if (NFCGiveMutex()==false) {
            rc_ok = false;
        }
#endif
    }

#endif  // ENA_LEGACY_SUPPORT_SINGLE_MCU
    return rc_ok;
}

/*
 * NDEFAddText
 *
 * @desc    Add an NDEF entry to the buffer for text records only
 *
 * @param   - flags - NDEF flags for this record.
 * @param   - pNDEF - pointer to buffer to fill in
 * @param   - NDEFsize - overall NDEF space available
 * @param   - pLabel - pointer to the label for the record
 * @param   - pData - pointer to data string
 *
 * @return - true if ok, otherwise false
 */

bool NDEFAddText(uint8_t flags, NDEF *pNDEF, uint16_t NDEFsize, const char *pLabel, const char *pData)
{
	uint8_t nLabelLen = strlen(pLabel);
	uint8_t nDataLen = strlen(pData);
	uint8_t size = nLabelLen + nDataLen + 3; // + no of bytes in header

	if((pNDEF->count + size + 4) > NDEFsize)
	{
		return false;
	}
	pNDEF->contents[pNDEF->count++] = flags;			// NDEF flags
	pNDEF->contents[pNDEF->count++] = 1;				// type length always 1
	pNDEF->contents[pNDEF->count++] = size;				// payload length, variable
	pNDEF->contents[pNDEF->count++] = NDEF_TYPE_TEXT;	// text type
	pNDEF->contents[pNDEF->count++] = 2;
	pNDEF->contents[pNDEF->count++] = 'e';				// "en" for english
	pNDEF->contents[pNDEF->count++] = 'n';
	strcpy((char*)&(pNDEF->contents[pNDEF->count]), pLabel);
	pNDEF->count += nLabelLen;
	strcpy((char*)&(pNDEF->contents[pNDEF->count]), pData);
	pNDEF->count += nDataLen;

	return true;
}

/*
 * CheckTagBufferEmpty
 *
 * @desc    Read passed in tag buffer and check if empty
 *
 * @param   - buf[64] - the passed in tag bugger
 *
 * @return - true if empty, otherwise false
 */
bool CheckTagBufferEmpty(uint8_t buf[64])
{
    if(((buf[16] == 0x03) && (buf[17] == 0x00) && (buf[18] == 0xFE)) ||
       ((buf[16] == 0x03) && (buf[17] == 0x03) && (buf[18] == 0xD0) && (buf[19] == 0x00) && (buf[20] == 0x00) && (buf[21] == 0xFE)))
    {
        return true;
    }
    else{
        return false;
    }
}


/*
 * NFC_BuildEmptyNDEF
 *
 * @desc    Build blank NDEF data for the tag
  *
 * @return - nothing
 */

static int BuildEmptyNDEF()
{
	// ignore the first block
	ndef.count = BYTES_PER_BLOCK;

	ndef.contents[ndef.count++] = BLOCK_START;
	ndef.contents[ndef.count++] = 3;
	ndef.contents[ndef.count++] = 0xD0;	// come back to this later
	ndef.contents[ndef.count++] = 0x00;	// come back to this later
	ndef.contents[ndef.count++] = 0x00;	// come back to this later
	ndef.contents[ndef.count++] = 0xFE;	// terminator

	ndef.count -= BYTES_PER_BLOCK;

	return ndef.count;
}


/*
 * BuildTestModeNDEF
 *
 * @desc    Build NDEF 'test mode' NDEF for  the tag
 * 			Valid filed value is 'activated' - this acts a as 1-shot flag,
 * 			when self-test is performed NDEF gets reset to original contents
 *
 * @param   - none
 *
 * @return - number of bytes in created NDEF
 */
static int BuildTestModeNDEF()
{
	ndef.contents[ndef.count++] = BLOCK_START;
	ndef.contents[ndef.count++] = 0;		// come back to this later
	// the NDEF record
	if(!NDEFAddText(NDEF_START, &ndef, BS_SIZE, "selftest:", "activated"))
	{
		return 0;
	}

	ndef.contents[ndef.count++] = 0xFE;
	ndef.count -= BYTES_PER_BLOCK;		// remove the first block offset
	ndef.contents[BYTES_PER_BLOCK + 1] = ndef.count - 3;	// remove block start, size and terminator from the count

	return ndef.count;
}


/*
 * NFC_BuildNDEFHelper
 *
 * @desc    Build NDEF data for the tag - call with relevant params set
 *
 * @param   - imei - pointer to the IMEI string to be written to the tag
 * @param   - iccid - pointer to the ICCIDs string to be written to the tag
 * @param   - mode - sets the 'command' record contents
 *
 * @return - number of bytes in created NDEF
 */

static int NFC_BuildNDEFHelper(const char* const mode)
{
	char buf[16];
	bool rc_ok = false;

	do
	{
		ndef.contents[ndef.count++] = BLOCK_START;
		ndef.contents[ndef.count++] = 0;		// come back to this later
		// the NDEF records
		if(!NDEFAddText(NDEF_START, &ndef, BS_SIZE, "imei:", (char*)gNvmCfg.dev.modem.imei))
		{
			break;
		}
		if(!NDEFAddText(NDEF_MIDDLE, &ndef, BS_SIZE, "iccid:", (char*)gNvmCfg.dev.modem.iccid))
		{
			break;
		}
		if(!NDEFAddText(NDEF_MIDDLE, &ndef, BS_SIZE, "model:", NFC_GetDevicePartNumber()))
		{
			break;
		}
		if(!NDEFAddText(NDEF_MIDDLE, &ndef, BS_SIZE, "date:", NFC_GetManufacturingDate()))
		{
			break;
		}

		HwVerEnum eHwVer = Device_GetHardwareVersion();
		sprintf(buf, "%08d", eHwVer);
		if(!NDEFAddText(NDEF_MIDDLE, &ndef, BS_SIZE, "hardware:", buf))
		{
			break;
		}

		uint32_t FirmwareVersion = getFirmwareVersionMeta(__loader_version);
		sprintf(buf,"%d.%d.%d",
				(int)((FirmwareVersion >> 24) & 0xFF),
				(int)((FirmwareVersion >> 16) & 0xFF),
				(int)((FirmwareVersion >> 0) & 0xFFFF));
		if(!NDEFAddText(NDEF_MIDDLE, &ndef, BS_SIZE, "boot:", buf))
		{
			break;
		}

		FirmwareVersion = getFirmwareVersionMeta(__app_version);
		sprintf(buf,"%d.%d.%d",
				(int)((FirmwareVersion >> 24) & 0xFF),
				(int)((FirmwareVersion >> 16) & 0xFF),
				(int)((FirmwareVersion >> 0) & 0xFFFF));
		if(!NDEFAddText(NDEF_MIDDLE, &ndef, BS_SIZE, "appl:", buf))
		{
			break;
		}

		// TODO command to set the angle at installation
		if(!NDEFAddText(NDEF_MIDDLE, &ndef, BS_SIZE, "angle:", "?"))
		{
			break;
		}

		if(!NDEFAddText(NDEF_END, &ndef, BS_SIZE, "cmd:", mode))
		{
			break;
		}

		ndef.contents[ndef.count++] = 0xFE;
		ndef.count -= BYTES_PER_BLOCK;		// remove the first block offset
		ndef.contents[BYTES_PER_BLOCK + 1] = ndef.count - 3;	// remove block start, size and terminator from the count

		rc_ok = true;

	} while(0);

    if(rc_ok)
    {
    	return ndef.count;
    }
    else
    {
    	return 0;
    }
}



/*
 * NFC_BuildNDEF
 *
 * @desc    Build NDEF data for the tag - gets IMEI numbers etc.
  *
 * @return - NDEF length
 */
extern tNvmCfg gNvmCfg;
extern bool Modem_readCellularIds(uint8_t rat,char * simPin,  uint8_t * imei, uint8_t * iccid, uint32_t maxWaitMs);

int NFC_BuildNDEF(NDEF_Type_t type)
{
	const char* pModeText = NULL;

	// clear the buffer and set the initial count
	memset(ndef.contents, 0, sizeof(ndef.contents));
	// we don't do anything to first block
	ndef.count = BYTES_PER_BLOCK;

	switch(type)
	{
	case eNDEF_COMMISSIONED:
		pModeText = "cmsd";
		break;

	case eNDEF_SHIPMODE:
#if defined(USE_NFC_TESTMODE)
	case eNDEF_TESTMODE:
		pModeText = "test";
		break;
#endif
		pModeText = "ship";
		break;

	case eNDEF_SELFTEST:
		return BuildTestModeNDEF();

	case eNDEF_EMPTY:
		return BuildEmptyNDEF();

	default:
		LOG_DBG(LOG_LEVEL_I2C,"%s(): Unknown NDEF type %d\n", __func__, type);
		return 0;
	}

	return 	NFC_BuildNDEFHelper(pModeText);
}


/*
 * NFC_GetNDEF
 *
 * @desc    Get access to NDEF data
 *
 * @return - pointer to NDEF object
 */
NDEF* NFC_GetNDEF()
{
	return &ndef;
}


/*
 * NFC_WriteNDEF
 *
 * @desc    Write NDEF data to the tag (either directly or via PMIC)
 *
 * @param   - l: length of data to write
 *
 * @return - true if ok, otherwise false
 */

bool NFC_WriteNDEF(const int l)
{
	bool rc_ok = false;

	if(!Device_HasPMIC())
	{
		rc_ok = true;
		uint8_t block = 1;
		for(int i = 0; rc_ok && (i < l); i += BYTES_PER_BLOCK)
		{
			rc_ok = I2C_WriteRegisters(NFC_BUS, nfc, block, BYTES_PER_BLOCK, &(ndef.contents[i+BYTES_PER_BLOCK]));
			vTaskDelay(100);
			block++;
		}
	}
	else
	{
		uint8_t cmd[MK24_PMIC_MAX_MESSAGE_PAYLOAD];
		int len = PMIC_formatCommand(MK24_NDEF_RECORDS_ID, cmd, sizeof(cmd), ndef.contents + BYTES_PER_BLOCK, l);
		vTaskDelay(3000);
		rc_ok = (PMIC_SendCommand(cmd, len)==kStatus_UART_Success);
	}

    return rc_ok;
}


/*
 * NFC_EraseNDEF
 *
 * @desc    Erase NDEF data from tag
 *
 * @param   - void
 *
 * @return - true if ok, otherwise false
 */

bool NFC_EraseNDEF(void)
{
	bool rc_ok = false;

	NFC_BuildNDEF(eNDEF_EMPTY);

	if(!Device_HasPMIC())
	{
		rc_ok = I2C_WriteRegisters(NFC_BUS, nfc, 1, BYTES_PER_BLOCK, ndef.contents + BYTES_PER_BLOCK);
	}
	else
	{
		LOG_DBG(LOG_LEVEL_I2C,"Erase NDEF..\n");
		rc_ok = NFC_WriteNDEF(MAX_NDEF);
	}

    return rc_ok;
}


#if 0
/*
 * Data sheet recommends switching PASSTHROUGH OFF before altering direction
 * Functions leaves Pass-Through enabled
 */
bool NFC_SetDirection(bool i2c_is_master)
{
	uint8_t nc_session_register;

	if(!NFC_ReadRegister(NC_REG, &nc_session_register))
	{
		return false;
	}

	nc_session_register &= ~PTHRU_ON_OFF;
	if(!NFC_WriteRegister(NC_REG, 0xFF, nc_session_register))
	{
		return false;
	}

	if(i2c_is_master)
	{
		nc_session_register &= ~PTHRU_DIR;
	}
	else
	{
		nc_session_register |= PTHRU_DIR;
	}

	if(!NFC_WriteRegister(NC_REG, 0xFF, nc_session_register))
	{
		return false;
	}

	nc_session_register |= PTHRU_ON_OFF;

	if(!NFC_WriteRegister(NC_REG, 0xFF, nc_session_register))
	{
		return false;
	}

	return true;
}
#endif


/*
 * NFC_ReadRegister(NXP_Registers_t reg, uint8_t *pVal)
 *
 * @desc	Function to read session register from the device
 *
 * @param	reg			- register to read
 * 			val	    	- pointer to variable to store returned data
 *
 * @returns Bool variable - Returns true if function completed correctly
 */
bool NFC_ReadRegister(NXP_Registers_t reg, uint8_t *pVal)
{
	bool rc_ok = false;
	if(Device_HasPMIC())
	{
		printf("Command not supported!\n");
		return false;
	}
#ifdef ENA_LEGACY_SUPPORT_SINGLE_MCU
	uint8_t cmdBuff[2];

	/* See section 9.8
	 * (WRITE and READ register operation) in data sheet
	 */
	cmdBuff[0] = 0xFE;
	cmdBuff[1] = (uint8_t)reg;

	// estimate the maximum transfer time
	uint32_t maxTimeMs;

	maxTimeMs = (1 + 1 + 1)*8 / nfc.baudRate_kbps; // 1000*(address + command + length data) / (kilobauds * 1000), the 1000 for kilo and milli cancel out
	// take some margin of 1.5
	maxTimeMs += maxTimeMs/2;
	// some sanity checking
	if (maxTimeMs < 30)
	{
		maxTimeMs = 30;
	}

	rc_ok = i2cTakeMutex(NFC_BUS, MUTEX_MAXWAIT_MS );		// take control of the i2c mutex
	if(true == rc_ok)
	{
		i2c_status_t i2c_ret = I2C_DRV_MasterReceiveDataBlocking(NFC_BUS,		// instance	The I2C peripheral instance number.
										&nfc,						// device	The pointer to the I2C device information structure.
										cmdBuff,					// cmdBuff	The pointer to the commands to be transferred, could be NULL.
										2,							// cmdSize	The length in bytes of the commands to be transferred, could be 0.
										pVal,						// rxBuff	The pointer to the data to be transferred, cannot be NULL.
										1,							// rxSize	The length in bytes of the data to be transferred, cannot be 0.
										maxTimeMs);					// timeout_ms	A timeout for the transfer in milliseconds.
		if (i2c_ret != kStatus_I2C_Success)
		{
			LOG_DBG(LOG_LEVEL_I2C,"I2C_ReadRegister() failed, error = %d\n", i2c_ret );
			rc_ok = false;
		}

		/* return control of the i2c mutex */
		if(false == i2cGiveMutex(NFC_BUS))
		{
			rc_ok = false;
		}
	}
#endif // ENA_LEGACY_SUPPORT_SINGLE_MCU
	return rc_ok;
}


/*
 * NFC_WriteRegister(NXP_Registers_t reg, uint8_t mask, uint8_t val)
 *
 * @desc	Function to write session register to the device
 *
 * @param	reg			- register to read
 * 			mask    	- bitwise mask - only bits set to 1 are affected
 * 			val			- value to write
 *
 * @returns Bool variable - Returns true if function completed correctly
 */
bool NFC_WriteRegister(NXP_Registers_t reg, uint8_t mask, uint8_t val)
{
	bool rc_ok = false;
	if(Device_HasPMIC())
	{
		printf("Command not supported!\n");
		return false;
	}
#ifdef ENA_LEGACY_SUPPORT_SINGLE_MCU
	uint8_t cmdBuff[2];
	uint8_t txBuff[3];


	/* See section 9.8
	 * (WRITE and READ register operation) in data sheet
	 */
	cmdBuff[0] = 0xFE;

	txBuff[0] = (uint8_t)reg;
	txBuff[1] = mask;
	txBuff[2] = val;

	// estimate the maximum transfer time
	uint32_t maxTimeMs;

	maxTimeMs = (1 + 1 + 2)*8 / nfc.baudRate_kbps; // 1000*(address + command + length data) / (kilobauds * 1000), the 1000 for kilo and milli cancel out
	// take some margin of 1.5
	maxTimeMs += maxTimeMs/2;
	// some sanity checking
	if (maxTimeMs < 30)
	{
		maxTimeMs = 30;
	}

	rc_ok = i2cTakeMutex(NFC_BUS, MUTEX_MAXWAIT_MS );		// take control of the i2c mutex
	if(true == rc_ok)
	{
		i2c_status_t i2c_ret = I2C_DRV_MasterSendDataBlocking(NFC_BUS,		// instance	The I2C peripheral instance number.
										&nfc,					// device	The pointer to the I2C device information structure.
		                                cmdBuff,				// cmdBuff	The pointer to the commands to be transferred, could be NULL.
		                                1,						// cmdSize	The length in bytes of the commands to be transferred, could be 0.
		                                txBuff,					// txBuff	The pointer to the data to be transferred, cannot be NULL.
		                                3,						// txSize	The length in bytes of the data to be transferred, cannot be 0.
										maxTimeMs);				// timeout_ms	A timeout for the transfer in milliseconds.

		if (i2c_ret != kStatus_I2C_Success)
		{
			LOG_DBG(LOG_LEVEL_I2C,"I2C_ReadRegister() failed, error = %d\n", i2c_ret );
			rc_ok = false;
		}

		/* return control of the i2c mutex */
		if(false == i2cGiveMutex(NFC_BUS))
		{
			rc_ok = false;
		}
	}

#endif // ENA_LEGACY_SUPPORT_SINGLE_MCU
	return rc_ok;
}


/*
 * NFC_ConfigSet
 *
 * @desc    Modify the NFC tag configuration register
 *
 * @param   - none
 *
 * @return - true if ok, otherwise false
 */

#define CONFIG_REGISTER 0x3A

bool NFC_ConfigSet(uint8_t fd)
{
    bool rc_ok = false;

    if(Device_HasPMIC())
	{
		printf("Command not supported!\n");
		return false;
	}
#ifdef ENA_LEGACY_SUPPORT_SINGLE_MCU
    uint8_t buf[16] = {0};

#ifdef USE_NFC_MUTEX
    if (NFCTakeMutex())
#endif
    {
    	rc_ok = I2C_ReadRegister(NFC_BUS, nfc, CONFIG_REGISTER, BYTES_PER_BLOCK, buf);
    	if(rc_ok)
    	{
    		// only change condition for FD ON
    		fd &= 0x03;
    		buf[0] &= 0xF3;
    		buf[0] |= (fd << 2);

        	rc_ok = I2C_WriteRegisters(NFC_BUS, nfc, CONFIG_REGISTER, BYTES_PER_BLOCK, buf);
        	if(rc_ok)
        	{
#ifdef USE_NFC_MUTEX
				// always give back when we took it
				if (NFCGiveMutex()==false) {
					rc_ok = false;
				}
#endif
        		vTaskDelay(100);
        		return NFC_Config();
        	}
        	else
        	{
        		printf("error writing NFC configuration\n");
        	}
    	}
    	else
    	{
    		printf("error reading NFC configuration\n");
    	}
#ifdef USE_NFC_MUTEX
        // always give back when we took it
        if (NFCGiveMutex()==false) {
            rc_ok = false;
        }
#endif
    }

#endif  // ENA_LEGACY_SUPPORT_SINGLE_MCU
    return rc_ok;
}



/*
 * NFC_SessionConfigSet
 *
 * @desc    Modify the NFC tag session configuration register (NC_REG)
 *
 * @param   - none
 *
 * @return - true if ok, otherwise false
 */

bool NFC_SessionConfigSet(uint8_t new_val)
{
    bool rc_ok = false;

    if(Device_HasPMIC())
	{
		printf("Command not supported!\n");
		return false;
	}
#ifdef ENA_LEGACY_SUPPORT_SINGLE_MCU
    uint8_t val = 0;

#ifdef USE_NFC_MUTEX
if (NFCTakeMutex())
{
#endif
	if(new_val == 1)
	{
		val = 0x40; // Pass-through enable
    	//val = 2;  // SRAM mirror enable
	}
	else
	{
		val = 0;
	}

	// sets pass-through and set direction I2C -> RF
	rc_ok = NFC_WriteRegister(NC_REG, 0x41, val);

   	//rc_ok = NFCWriteRegister(SRAM_MIRROR_BLOCK, 0xFF, 50);

   	if(rc_ok)
   	{
		vTaskDelay(100);

#ifdef USE_NFC_MUTEX
        // always give back when we took it
        if (NFCGiveMutex()==false) {
            rc_ok = false;
        }
#endif

		return NFC_Session();
   	}

	else
	{
		printf("error writing NFC session configuration\n");
	}

#ifdef USE_NFC_MUTEX
}
// always give back when we took it
if (NFCGiveMutex()==false) {
	rc_ok = false;
}
#endif

#endif  // ENA_LEGACY_SUPPORT_SINGLE_MCU
   	return rc_ok;
}


/*
 * NFC_Config
 *
 * @desc    Read and display Config Registers
 *
 * @param   - void
 *
 * @return - true if ok, otherwise false
 */
bool NFC_Config(void)
{
    bool rc_ok = false;

    if(Device_HasPMIC())
	{
		printf("Command not supported!\n");
		return false;
	}
#ifdef ENA_LEGACY_SUPPORT_SINGLE_MCU
    uint8_t buf[16] = {0};

#ifdef USE_NFC_MUTEX
    if (NFCTakeMutex())
#endif
    {
    	rc_ok = I2C_ReadRegister(NFC_BUS, nfc, 58, 7, buf);
    	if(rc_ok)
    	{
			printf("Configuration Registers\n"
					"  NC_REG            - %02X\n"
					"  LAST_NDEF_BLOCK   - %02X\n"
					"  SRAM_MIRROR_BLOCK - %02X\n"
					"  WDT_LS            - %02X\n"
					"  WDT_MS            - %02X\n"
					"  I2C_CLOCK_STR     - %02X\n"
					"  REG_LOCK          - %02X\n",
					buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6] );
    	}
    	else
    	{
    		printf("error reading NFC configuration\n");
    	}
#ifdef USE_NFC_MUTEX
        // always give back when we took it
        if (NFCGiveMutex()==false) {
            rc_ok = false;
        }
#endif
    }

#endif  // ENA_LEGACY_SUPPORT_SINGLE_MCU
    return rc_ok;
}


/*
 * NFC_Session
 *
 * @desc    Read and display the NFC session registers
 *
 * @param   - none
 *
 * @return - true if ok, otherwise false
 */

bool NFC_Session(void)
{
    bool rc_ok = true;

    if(Device_HasPMIC())
	{
		printf("Command not supported!\n");
		return false;
	}
#ifdef ENA_LEGACY_SUPPORT_SINGLE_MCU
    NXP_Registers_t reg;
    uint8_t val;
    uint8_t buf[8];
    reg = NC_REG;
    for(int i=0; i<7; i++)
    {
    	if(NFC_ReadRegister(reg++, &val))
    	{
    		buf[i] = val;
    	}
    	else
    	{
    		rc_ok = false;
    		break;
    	}
    }

    if(rc_ok)
    {
		printf("Session Registers\n"
				"  NC_REG            - %02X\n"
				"  LAST_NDEF_BLOCK   - %02X\n"
				"  SRAM_MIRROR_BLOCK - %02X\n"
				"  WDT_LS            - %02X\n"
				"  WDT_MS            - %02X\n"
				"  I2C_CLOCK_STR     - %02X\n"
				"  NS_REG            - %02X\n",
				buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6]);
    }

#endif  // ENA_LEGACY_SUPPORT_SINGLE_MCU
    return rc_ok;
}

#endif




#ifdef __cplusplus
}
#endif