#ifdef __cplusplus
extern "C" {
#endif

/*
 * UT_NvmConfig.c
 *
 *  Created on: 21 Oct 2019
 *      Author: XG8241
 */

#include <stdio.h>
#include "UnitTest.h"
#include "NvmConfig.h"
#include "flash.h"
#include "Resources.h"

extern bool deviceFirstConfigInitialisation();


static void testNvmCfgWriteInvalid_IMEI_ICCID_params();
static void testNvmCfgRead_IMEI_ICCID_params_from_Modem();
static int testNvmConfigInit();
static int testNvmConfigCleanUp();
/*
 * UTNVMConfig unit test suite definition.
 */
CUnit_suite_t UTNVMConfig =
{
	{ "NVMConfig", testNvmConfigInit, testNvmConfigCleanUp, CU_TRUE, ""},
	// Array of test functions.
	{
		{ "test NVMConfig CRC check", testNvmCfgWriteInvalid_IMEI_ICCID_params},
		{ "Populate IMEI + ICCID in NVMConfig", testNvmCfgRead_IMEI_ICCID_params_from_Modem},
		{ NULL, NULL }
	}
};

static tNvmCfg cfgBackup;
static int testNvmConfigInit()
{
	// take a backup of the current Nvm Config.
	memcpy((uint8_t*)&cfgBackup, (uint8_t*)&gNvmCfg, sizeof(gNvmCfg));
	return 0;
}

static int testNvmConfigCleanUp()
{
	int error = 1;
	// Restore the original NVM Config.
	memcpy((uint8_t*)&gNvmCfg, (uint8_t*)&cfgBackup, sizeof(gNvmCfg));
	if(NvmConfigWrite(&gNvmCfg))
	{
		error = 0;
	}
	return error;
}

// Objective of this test is to corrupt the NVM Config (by modifying its data members)
// and confirm that on subsequent power cycle the sensor detects the corruption
// and loads the default values.
static void testNvmCfgWriteInvalid_IMEI_ICCID_params()
{
	tNvmCfg tempCfg;
	strcpy((char*)gNvmCfg.dev.modem.iccid, "987653421");
	strcpy((char*)gNvmCfg.dev.modem.imei, "12345678910");

	// program the modified params without updating the CRC.
	CU_ASSERT(true == DrvFlashEraseProgramSector((uint32_t*)__app_cfg, (uint32_t*)&gNvmCfg, sizeof(gNvmCfg)));
	NvmConfigRead(&tempCfg);
	CU_ASSERT_STRING_EQUAL(tempCfg.dev.modem.iccid, gNvmCfg.dev.modem.iccid);
	CU_ASSERT_STRING_EQUAL(tempCfg.dev.modem.imei, gNvmCfg.dev.modem.imei);

	// Simulate as if node power cycled and Resources_InitLowLevel() gets invoked.
	// A call to Resources_InitLowLevel(), should now have detected the
	// NVM config corruption and loaded the "default" values. The IMEI &
	// ICCID default values are null strings.
	Resources_InitLowLevel();
	NvmConfigDefaults(&tempCfg);
	CU_ASSERT_STRING_EQUAL(gNvmCfg.dev.modem.iccid, tempCfg.dev.modem.iccid);
	CU_ASSERT_STRING_EQUAL(gNvmCfg.dev.modem.imei, tempCfg.dev.modem.imei);
}

// Objective of this test is to confirm that post loading the default config
// values, an attempt to perform a COMMS populates the IMEI & ICCID values after
// been read successfully from the modem.
static void testNvmCfgRead_IMEI_ICCID_params_from_Modem()
{
	testNvmCfgWriteInvalid_IMEI_ICCID_params();
	CU_ASSERT_STRING_EQUAL(gNvmCfg.dev.modem.iccid, "\0");
	CU_ASSERT_STRING_EQUAL(gNvmCfg.dev.modem.imei, "\0");

	deviceFirstConfigInitialisation();

	CU_ASSERT_EQUAL(strlen((char *)gNvmCfg.dev.modem.iccid), (MODEM_ICCID_LEN - 1));
	CU_ASSERT_EQUAL(strlen((char*)gNvmCfg.dev.modem.imei), (MODEM_IMEI_LEN - 1));
}


#ifdef __cplusplus
}
#endif