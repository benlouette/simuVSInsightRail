#ifdef __cplusplus
extern "C" {
#endif

#include "UnitTest.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "nfc.h"
#include "pmic.h"
#include "Device.h"

static int storeNDEF(void);
static void testCheckTagBuffer(void);
static void testEraseNFC(void);
static void testWriteNFC(void);
static int restoreNDEF(void);

static NDEF m_NDEF;
static int m_len=0;

CUnit_suite_t UTnfc = {
    { "nfc", storeNDEF, restoreNDEF, CU_TRUE, "test NFC functions"},
    {
    	{ "testCheckTagBuffer", testCheckTagBuffer },
		{ "testEraseNFC", testEraseNFC },
		{ "testWriteNFC", testWriteNFC },
		{ NULL, NULL }
	}
};

static void testCheckTagBuffer(void)
{
	//Check all empty cases report true
	uint8_t buf[64] = {0};
	buf[16] = 0x03;
	buf[17] = 0x00;
	buf[18] = 0xFE;
	CU_ASSERT(CheckTagBufferEmpty(buf) == true);

	buf[16] = 0x03;
    buf[17] = 0x03;
    buf[18] = 0xD0;
    buf[19] = 0x00;
    buf[20] = 0x00;
    buf[21] = 0xFE;
    CU_ASSERT(CheckTagBufferEmpty(buf) == true);

	buf[16] = 0x00;
    buf[17] = 0x00;
    buf[21] = 0x00;
	//Check some non empty cases report false
	CU_ASSERT(CheckTagBufferEmpty(buf) == false);
}

static void testEraseNFC(void)
{
	CU_ASSERT(NFC_EraseNDEF());
	vTaskDelay(1000/portTICK_PERIOD_MS);
	CU_ASSERT(NFC_getPMICtag());

	//Read the NDEF record back and check it is empty
	NDEF* pNDEF = NFC_GetNDEF();

	CU_ASSERT(NULL != pNDEF);

	CU_ASSERT(CheckTagBufferEmpty(pNDEF->contents) == true);
}

static void testWriteNFC(void)
{
	CU_ASSERT(NFC_EraseNDEF());
	vTaskDelay(1000/portTICK_PERIOD_MS);

	//Access the NDEF buffer
	NDEF* pndef = NFC_GetNDEF();

	//Set message begin 0x80, always set 0x10, NFC Forum well-known type [RFC RTD] 0x01
	const char *const pstrLabel ="This is a test label:";
	const char *const pstrData = "Data test this is";
    uint8_t nBlockStart = 0x03;
    uint8_t nNDEFStart = 0x91;

    // clear the buffer and set the initial count
    memset(pndef->contents, 0, sizeof(pndef->contents));
    // we don't do anything to first block
    pndef->count = BYTES_PER_BLOCK;
	(pndef->contents)[pndef->count++] = nBlockStart;
	(pndef->contents)[pndef->count++] = 0;
	//Populate the ndef buffer with some test data
	CU_ASSERT(NDEFAddText(nNDEFStart, pndef, BS_SIZE, pstrLabel, pstrData));

	pndef->contents[pndef->count++] = 0xFE;
	pndef->count -= BYTES_PER_BLOCK;		// remove the first block offset
	pndef->contents[BYTES_PER_BLOCK+1] = pndef->count - 3;	// remove block start, size and terminator from the count

	//Write the ndef buffer to the device
	int len = pndef->count;
    CU_ASSERT(NFC_WriteNDEF(len));
    vTaskDelay(1000/portTICK_PERIOD_MS);
    CU_ASSERT(NFC_getPMICtag());
    CU_ASSERT( strncmp((char*)&pndef->contents[25], pstrLabel, sizeof(pstrLabel)-1) == 0);
    CU_ASSERT( strncmp((char*)&pndef->contents[46], pstrData, sizeof(pstrData)-1) == 0);
}

static int storeNDEF(void)
{
	NFC_getPMICtag();

	//Access the NDEF buffer
	NDEF* pndef = NFC_GetNDEF();
	m_len = pndef->count + BYTES_PER_BLOCK;
	if (m_len<=MK24_PMIC_MAX_MESSAGE_PAYLOAD)
	{
	    //Copy the NDEF buffer to a global variable
		memcpy(m_NDEF.contents, pndef->contents, m_len);
		m_NDEF.count = m_len;
	}
	return 0;
}

static int restoreNDEF(void)
{
	NDEF* pndef = NFC_GetNDEF();
	if (m_len<=MK24_PMIC_MAX_MESSAGE_PAYLOAD)
	{
	    //Copy the stored copy of the NDEF back to the NDEF buffer
		memcpy(pndef->contents, m_NDEF.contents, m_len);
	}
	NFC_WriteNDEF(m_len);
	vTaskDelay(1000/portTICK_PERIOD_MS);
	return 0;
}


#ifdef __cplusplus
}
#endif