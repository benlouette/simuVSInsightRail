#ifdef __cplusplus
extern "C" {
#endif

/*
 * UT_Temp.c
 *
 *  Created on: 16 May 2019
 *      Author: tk2319
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "FreeRTOS.h"

#include "ExtFlash.h"
#include "UnitTest.h"
#include "linker.h"
#include "configLog.h"
#include "temperature.h"

// Unit tests to check the memory management for Temperature records.

static uint16_t iGetTemperatureRecordCount()
{
	uint16_t records;
	Temperature_GetRecordCount(&records, NULL);
	return records;
}

static uint16_t iGetTemperatureWrIndx()
{
	uint16_t offset;
	Temperature_GetWrIndx(&offset);
	return offset;
}

static bool bTemperatureWriteRecord(int totalRecords)
{
	uint32_t secs = 1000;
	bool retVal = true;
	for (int i = 1; (i <= totalRecords) && retVal; i++)
	{
		retVal = Temperature_WriteRecord(i + 0.5f, secs + i);
	}
	return retVal;
}

static bool bIsSectorErased(int sector)
{
	if(sector < 1 || sector > TEMPERATURE_MEAS_SECTORS)
	{
		return false;
	}
	register uint32_t *p = &((uint32_t*)__sample_buffer)[(IS25_SECTOR_SIZE_BYTES / sizeof(uint32_t)) * (sector - 1)];
	for(register int i = 0; i < (IS25_SECTOR_SIZE_BYTES / sizeof(uint32_t)); i++)
	{
		if(p[i] != MEMORY_ERASE_STATE)
		{
			return false;
		}
	}
	return true;
}

static bool bIsSectorLastWordErased(int sector)
{
	if(sector < 1 || sector > TEMPERATURE_MEAS_SECTORS)
	{
		return false;
	}
	register uint32_t *p = (uint32_t*)(((char*)__sample_buffer) + (IS25_SECTOR_SIZE_BYTES * sector) - sizeof(uint32_t));
	if(*p != MEMORY_ERASE_STATE)
	{
		return false;
	}
	return true;
}

static bool bIsTemperatureBlockErased()
{
	register uint32_t *p = (uint32_t*)__sample_buffer;
	for(register int i = 0; i < (TEMPERATURE_MEAS_SIZE / sizeof(uint32_t)); i++)
	{
		if(p[i] != MEMORY_ERASE_STATE)
		{
			return false;
		}
	}
	return true;
}

extern uint32_t dbg_logging;
static uint32_t dbg_logging_save;

static int init_suite1()
{
	dbg_logging_save = dbg_logging;
	dbg_logging &= ~LOG_LEVEL_I2C;	// stop extFlash debug messages during UT
	return 0;
}

static int clean_suite1()
{
	dbg_logging = dbg_logging_save;
	return 0;
}

/*
 * test1
 *
 * @desc	erase all records
 *
 * @param	none
 *
 * @returns	none
 */
static void test1()
{
	//1. erase all records
	CU_ASSERT(true == Temperature_EraseAllRecords());
	// check there are no records and that flash is fully erased
	CU_ASSERT(0 == iGetTemperatureRecordCount());
	CU_ASSERT(true == bIsTemperatureBlockErased());
}

/*
 * test2
 *
 * @desc	write 200 records
 *
 * @param	none
 *
 * @returns	none
 */
static void test2()
{
#define TOTAL_RECORDS	200

	// perform test 1 to erase the temperature measurements
	test1();

	CU_ASSERT(true == bTemperatureWriteRecord(TOTAL_RECORDS));
	CU_ASSERT(TOTAL_RECORDS == iGetTemperatureRecordCount());
	CU_ASSERT((TOTAL_RECORDS * TEMPERATURE_RECORD_SIZE_BYTES) == iGetTemperatureWrIndx());
	CU_ASSERT(false == bIsSectorErased(1));
	CU_ASSERT(true == bIsSectorErased(2));
}

/*
 * test3
 *
 * @desc	write 339 + 2 +1 & erase 2nd sector
 *
 * @param	none
 *
 * @returns	none
 */
static void test3()
{
#define TOTAL_RECORDS3	(TEMPERATURE_RECORDS_IN_A_SECTOR - 2)

	// perform test 1 to erase the temperature measurements
	test1();

	// write a number of records, check the count and write offset
	CU_ASSERT(true == bTemperatureWriteRecord(TOTAL_RECORDS3));
	CU_ASSERT(TOTAL_RECORDS3 == iGetTemperatureRecordCount());
	CU_ASSERT((TOTAL_RECORDS3 * TEMPERATURE_RECORD_SIZE_BYTES) == iGetTemperatureWrIndx());
	CU_ASSERT(false == bIsSectorErased(1));
	CU_ASSERT(true == bIsSectorErased(2));

	// now write a further 2 records
	CU_ASSERT(true == bTemperatureWriteRecord(2));
	CU_ASSERT((TOTAL_RECORDS3 + 2) == iGetTemperatureRecordCount());

	// 2nd sector should still be erased
	CU_ASSERT(false == bIsSectorErased(1));
	CU_ASSERT(true == bIsSectorErased(2));

	// should now be pointing to 2nd sector
	CU_ASSERT(IS25_SECTOR_SIZE_BYTES == iGetTemperatureWrIndx());

	// now write a further record which means we are in the second sector
	CU_ASSERT(true == bTemperatureWriteRecord(1));
	CU_ASSERT((TOTAL_RECORDS3 + 3) == iGetTemperatureRecordCount());

	// 2nd sector should still be blank
	CU_ASSERT(false == bIsSectorErased(1));
	CU_ASSERT(false == bIsSectorErased(2));
}

/*
 * test4
 *
 * @desc	write 1363 + 1 + 1 which will fail
 *
 * @param	none
 *
 * @returns	none
 */
static void test4()
{
#define TOTAL_RECORDS4	(TEMPERATURE_MAX_NO_RECORDS - 1)

	// perform test 1 to erase the temperature measurements
	test1();

	// fill all sectors - one record
	CU_ASSERT(true == bTemperatureWriteRecord(TOTAL_RECORDS4));
	CU_ASSERT(TOTAL_RECORDS4 == iGetTemperatureRecordCount());

	// all sectors should have info
	for(int i = 1; i <= TEMPERATURE_MEAS_SECTORS; i++)
	{
		CU_ASSERT(false == bIsSectorErased(i));
		CU_ASSERT(true == bIsSectorLastWordErased(i));
	}

	// one more for luck
	CU_ASSERT(true == bTemperatureWriteRecord(1));
	CU_ASSERT(true == bIsSectorLastWordErased(TEMPERATURE_MEAS_SECTORS));

	// last write should fail
	CU_ASSERT(false == bTemperatureWriteRecord(1));
}

/*
 * test5
 *
 * @desc	clean up by calling test1 again
 *
 * @param	none
 *
 * @returns	none
 */
static void test5()
{
	// perform test 1 to erase the temperature measurements
	test1();
}

#if 0
/*
 * test5
 *
 * @desc	perform a counter test
 *
 * @param	none
 *
 * @returns	none
 */
static void test5()
{
	//5. 1st sector FULL & add some records in the 2nd sector.
	uint16_t totalRecords = TEMPERATURE_RECORDS_IN_A_SECTOR + 10;
	uint16_t records = iGetTemperatureRecordCount();
		//printf("\nInitial Record Count:%d\n", records);
		records = totalRecords - records;
		//printf("\nTest %d: 1st sector FULL & %d records added in 2nd sector \n", argi[0], records);
		uint32_t secs = 1000;
		bool retVal = true;
		for (int i = 1; (i <= records) && retVal; i++)
		{
			retVal = Temperature_WriteRecord( i + 0.5f, secs + i);
		}
		records = iGetTemperatureRecordCount();
		uint16_t offset, expectedOffset;
		Temperature_GetWrIndx(&offset);
		if(records > TEMPERATURE_RECORDS_IN_A_SECTOR)
		{
			expectedOffset = (totalRecords * TEMPERATURE_RECORD_SIZE_BYTES) +
							 (IS25_SECTOR_SIZE_BYTES - (TEMPERATURE_RECORDS_IN_A_SECTOR * TEMPERATURE_RECORD_SIZE_BYTES));
		}
		else
		{
			expectedOffset = (totalRecords * TEMPERATURE_RECORD_SIZE_BYTES);
		}
		// Check for test result.
		CU_ASSERT((records == totalRecords) && (offset == expectedOffset));
#if 0
		{
			printf("\nTest%d Passed !!\n", argi[0]);
		}
		else
		{
			printf("\n****Test%d FAILED****, Record Count:%d, offset:%d, expectedOffset:%d\n",
					argi[0], records, offset, expectedOffset);
		}
#endif
}

/*
 * test6
 *
 * @desc	perform a counter test
 *
 * @param	none
 *
 * @returns	none
 */
static void test6()
{
	//6. 1st sector FULL & Leave space for 1 record in 2nd sector.
	uint16_t totalRecords = (TEMPERATURE_RECORDS_IN_A_SECTOR * 2) - 1;
	uint16_t records = iGetTemperatureRecordCount();
		//printf("\nInitial Record Count:%d\n", records);
		records = totalRecords - records;
		//printf("\nTest %d: 1st sector FULL & Leave space for 1 record in 2nd sector,add :%d records\n", argi[0], records);
		uint32_t secs = 1000;
		bool retVal = true;
		for (int i = 1; (i <= records) && retVal; i++)
		{
			retVal = Temperature_WriteRecord(i + 0.5f, secs + i);
		}
		records = iGetTemperatureRecordCount();
		CU_ASSERT(records == totalRecords);
#if 0
		{
			printf("\nTest%d Passed !!\n", argi[0]);
		}
		else
		{
			printf("\n****Test%d FAILED****, Record Count:%d, offset:%d, expectedOffset:%d\n",
					argi[0], records, offset);
		}
#endif
}

/*
 * test7
 *
 * @desc	perform a counter test
 *
 * @param	none
 *
 * @returns	none
 */
static void test7()
{
	//7. 1st sector FULL & Fill up the 2nd Sector, Expect 1st sector to get erased.
	uint16_t totalRecords = (TEMPERATURE_RECORDS_IN_A_SECTOR * 2);
	uint16_t records = iGetTemperatureRecordCount();
		//printf("\nInitial Record Count:%d\n", records);
		records = totalRecords - records;
		//printf("\nTest %d: 1st sector FULL & Fill up the 2nd Sector,add :%d records, Expect 1st sector to be erased\n", argi[0], records);
		uint32_t secs = 1000;
		bool retVal = true;
		for (int i = 1; (i <= records) && retVal; i++)
		{
			retVal = Temperature_WriteRecord( i + 0.5f, secs + i);
		}
		records = iGetTemperatureRecordCount();
		CU_ASSERT(records == TEMPERATURE_RECORDS_IN_A_SECTOR);
#if 0
		{
			printf("\nTest%d Passed !!\n", argi[0]);
		}
		else
		{
			printf("\n****Test%d FAILED****, Record Count:%d, Offset:%d\n",
					argi[0], records, offset);
		}
#endif
}

/*
 * test8
 *
 * @desc	perform a counter test
 *
 * @param	none
 *
 * @returns	none
 */
static void test8()
{
	//8. Leave space for 1 record in 1st sector, after the 2nd sector is FULL.
	uint16_t totalRecords = (TEMPERATURE_RECORDS_IN_A_SECTOR * 2) - 1;
	uint16_t records = iGetTemperatureRecordCount();
		////printf("\nInitial Record Count:%d\n", records);
		records = totalRecords - records;
		//printf("\nTest %d: Leave space for 1 record in 1st sector, 2nd sector is FULL, add :%d records\n", argi[0], records);
		uint32_t secs = 1000;
		bool retVal = true;
		for (int i = 1; (i <= records) && retVal; i++)
		{
			retVal = Temperature_WriteRecord(i + 0.5f, secs + i);
		}
		records = iGetTemperatureRecordCount();
		CU_ASSERT(records == totalRecords);
#if 0
		{
			printf("\nTest%d Passed !!\n", argi[0]);
		}
		else
		{
			printf("\n****Test%d FAILED****, Record Count:%d, offset:%d\n",
					argi[0], records, offset);
		}
#endif
}

/*
 * test9
 *
 * @desc	perform a counter test
 *
 * @param	none
 *
 * @returns	none
 */
static void test9()
{
	//9.  Last record written in 1st sector, after the 2nd sector is FULL, expect 2nd sector to get erased.
	uint16_t totalRecords = (TEMPERATURE_RECORDS_IN_A_SECTOR * 2);
	uint16_t records = iGetTemperatureRecordCount();
		//printf("\nInitial Record Count:%d\n", records);
		records = totalRecords - records;
		//printf("\nTest %d: Last record written in 1st sector, 2nd sector was FULL,add :%d records, Expect 2nd sector get erased\n", argi[0], records);
		uint32_t secs = 1000;
		bool retVal = true;
		for (int i = 1; (i <= records) && retVal; i++)
		{
			retVal = Temperature_WriteRecord(i + 0.5f, secs + i);
		}
		records = iGetTemperatureRecordCount();
		CU_ASSERT(records == TEMPERATURE_RECORDS_IN_A_SECTOR);
#if 0
		{
			printf("\nTest%d Passed !!\n", argi[0]);
		}
		else
		{
			printf("\n****Test%d FAILED****, Record Count:%d, Offset:%d\n",
					argi[0], records, offset);
		}
#endif
}
#endif

CUnit_suite_t UTtemp = {
	{ "temp", init_suite1, clean_suite1, CU_TRUE, "test Temperature Measurement functions" },
	{
		{"erase all records", test1},
		{"write 200 records", test2},
		{"write 339 + 2 + 1",   test3},
		{"write 1363 + 1 + failing to write 1",   test4},
		{"clean out records", test5},
#if 0
		{"write to 1st sector", test5},
		{"alarm test",   test6},
		{"write to 1st sector", test7},
		{"alarm test",   test8},
		{"alarm test",   test9},
#endif
		{ NULL, NULL }
	}
};


#ifdef __cplusplus
}
#endif