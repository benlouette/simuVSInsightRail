#ifdef __cplusplus
extern "C" {
#endif

/*
 * ErrLog.c
 *
 *  Created on: Jun 21, 2016
 *      Author: Sandeep K
 *      This is a circular buffer Eventor log. The message are dynamic in nature( multiple of 8 bytes for flash block write)
 *      Frame_Length  Entire Frame Length should be within 7 bit  128 byte (limit it if exceeds)
 *      Time stamp  8 byte Unix stamp 
 *      SevLevel  1 byte  (Fatal, major, minor, warn, debug, info)
 *      Component_code  2 byte (Each function will be having unique code, may be written in .h files)
 *      ErrCode  2 byte ( Error code number, defined in .h files)
 *      RepCounter  Keep a track on same kind of event. Threshold will be decided later (If Repcounter exceeds 5, then 6th log
 *                    will not be recorded in the flash)
 *      Log message- char *  -- dynamic (ends with null)
 *      Log record size:-  15 byte (+1 byte for extra padding) + log message(dynamic. But multiple of 8 bytes)
 *      Note: Block write is 8 bytes in Kinetis devices So Internally rounding off to 8 bytes.
 *
 *      Note: Special care has been taken for writing /reading operation during the transition from one sector to another
 *      Functions used in Application program:-
 *      EventLog_In--Event> writes an entry into flash. returns false/true depending upon the status
 *                            Format:- EventLog_In(uint16_t eventcode, uint16_t compNum, uint8_t sevlevel, char logmsg[])
 *                            Example: EventLog_In(1111, 222, ERRLOGWARN, "Vibration level(0.6) above threshold");
 *
 *      getlog-----> This function will be called in application to read each Event log
 *                   Call the EventLog_InitFlashData() function only for the first time before calling the getlog() function.
 *                   This will help us to identify the last read sector ["init" Will identify-> If you have already read, or if you haven't read at all]
 *                   getlog() function will update the last read sector every time it enters and thus will help us if the communication is lost in between
 *                   Once you have read all the logs and if communication is successful, then write a log in flash indicating this
 *                   EventLog_In(9999, 9999, ERRLOINFO, "\0");
 *
 *      Use CLI for further help. See EventLogHelp [ Note: Enable DEBUGPRINTKEY in ConfigErrLog.h]
 *
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "freeRTOS.h"
#include "semphr.h"

#include "EventLog.h"
#include "printgdf.h"
#include "flash.h"
#include "SSD_FTFx.h"// to access the macro PGM_SIZE_BYTE
#include "configSvcData.h"
#include "log.h"
#include "fsl_rtc_driver.h"
#include "fsl_rtc_hal.h"
#include "UnitTest.h"
#include "configBootloader.h"

#ifdef CONFIG_PLATFORM_EVENT_LOG

// Uncomment this to use mutex protection - recommended!
#define USE_EVENTLOG_MUTEX

#define EMPTY_8		0xFF
#define EMPTY_64	0xFFFFFFFFFFFFFFFFLLU
#define ZERO_64		0x0000000000000000LLU
#define HASH_TAG 	0xA5
#define MSG_SIZE	0x38

#define SYS_FLASH_ERRLOG_SIZE (uint32_t)__eventlog_size
#define SYS_FLASH_ERRLOG_ADDRESS (uint32_t)__event_log
#define SYS_FLASH_SECTOR_SIZE FSL_FEATURE_FLASH_PFLASH_BLOCK_SECTOR_SIZE

#define ERRLOG_SECTORS (SYS_FLASH_ERRLOG_SIZE/SYS_FLASH_SECTOR_SIZE)

#define SYS_FLASH_ERRLOG_ADDR(sector) (SYS_FLASH_ERRLOG_ADDRESS + (sector * SYS_FLASH_SECTOR_SIZE))

typedef uint64_t fill_t;

#if defined(USE_EVENTLOG_MUTEX)
#define EVENT_LOG_MAX_WAIT_MS (100)
static SemaphoreHandle_t xEventlogMutex = NULL;
#endif

static bool writeToFlash(uint32_t *dest, uint32_t *src, uint32_t len);

/*
 * This is a shared resource, access must be protected by a semaphore
 */
static struct sEventLogFlashData {
    tEventLog_inFlash *start, *write;
    uint32_t lastId;
} recEventLogFlashData;

static const char EventLogHelp[] = {
        "eventlog 1: Read un-sent log entry\r\n"
        "eventlog 2: Erasing the entire log buffer\r\n"
        "eventlog 3: From Linker script, Starting address and Length of EventLog Buffer\r\n"
        "eventlog 4: Write sample 1 to log\r\n"
        "eventlog 5: Write sample 2 to log\r\n"
        "eventlog 7: Write sample 3 (more than 128 bytes) to log\r\n"
        "eventlog 8 <eventCode> <compNumber> <severity> <string>: Write user defined sample to log\r\n"
		"eventlog 9: Display all un-sent log entries\r\n"
		"eventlog 10 <no entries>: Write a log sample <no entries> times\r\n"
		"eventlog 11 <sector>: Erase sector <sector>\r\n"
		"eventlog 12: Initialise eventlog\r\n"
};

static const char msg4[] = "Vibration level(0.%d) above threshold";
static const char msg10[] = "Node shutdown by exceeding max on time";
static bool quiet = false;

static bool validChecksum(uint8_t *addr)
{
    uint8_t crc = 0xFF;
    for(int i = 1; i < sizeof(tEventLog_inFlash); i++)
    {
        crc ^= addr[i];
    }
    return (crc == *addr);
}

/*
 * UTCToString
 *
 * @desc    format UTC seconds into a string
 *
 * @param   - UTC time in seconds
 *
 * @return - string in the format "YYYY/MM/DD HH:MM:SS"
 */
static char *UTCToString(uint32_t seconds)
{
	static char string[64];
	rtc_datetime_t datetime;

	RTC_HAL_ConvertSecsToDatetime(&seconds, &datetime);
	sprintf(string, "%04d/%02d/%02d %02d:%02d:%02d",
			datetime.year, datetime.month, datetime.day,
			datetime.hour, datetime.minute, datetime.second);
	return string;
}

/*
 * EventLog_printLogEntry:- display log entry to the user
 * Return value:- none
 */
void EventLog_printLogEntry(tEventLog_inFlash * addrEventLog)
{
	printf("\naddrEventLog = %08x\n", addrEventLog);
	printf("idefTimestamp = 0x%016llx %s.%03d\n",
			addrEventLog->logHeader.unixTimestamp,
			UTCToString((uint32_t)(addrEventLog->logHeader.unixTimestamp / 10000000ULL)),
			((uint32_t)(addrEventLog->logHeader.unixTimestamp % 10000000ULL)/10000));
	printf("compNumber = %d, eventCode = %d, sevLvl = %d\n",
			addrEventLog->logHeader.compNumber, addrEventLog->logHeader.eventCode,
			addrEventLog->logHeader.sevLvl);
	printf("Message = %s\n", addrEventLog->logMsg);
}

static bool irqDisabledDrvFlashEraseSector(uint32_t sector)
{
	bool rc_ok = true;

	// if it's already blank don't bother (hopefully saves wear and tear)
    if(!blank_check((uint8_t*)SYS_FLASH_ERRLOG_ADDR(sector), SYS_FLASH_ERRLOG_SIZE))
    {
		__disable_irq();// we may run in the same flash bank as the one we are erasing/programming, then interrupts may not execute code inside this bank !
		rc_ok = DrvFlashEraseSector(((uint32_t *)SYS_FLASH_ERRLOG_ADDR(sector)), SYS_FLASH_SECTOR_SIZE);
		__enable_irq();
#ifdef DEBUGPRINTKEY
		if (rc_ok == false)
		{
			//TODO Think about alternative solution. If erasing is not possible then writing( event logging) is also not possible
			printf("%s() erase sector at %08x failed\n", __func__, SYS_FLASH_ERRLOG_ADDR(sector));
		}
#endif
    }
    return rc_ok;

}

/*
 * EventLog_Clear:- to Clear the record from the Memory (only to be called from the CLI !!)
 * Return value:- None
 */
void EventLog_Clear(void)
{
    bool rc_ok=true;

    //Record the time stamp after clearing the buffer- This will be considered as an event
    for (int sector = 0; sector < ERRLOG_SECTORS && rc_ok; sector++)
    {
        rc_ok = irqDisabledDrvFlashEraseSector(sector);
        if (rc_ok == false)
        {
            // We cannot report an error in the event logger with the event logger !! EventLog_In(5555, 5555, ERRLOGMAJOR, "Erase was not successful");// TODO define a proper message
            printf("%s(): erase sector at %08x failed\n", __func__, SYS_FLASH_ERRLOG_ADDR(sector));
        }
        else
        {
#ifdef DEBUGPRINTKEY
            printf("%s() erase sector %d success \n", __func__, sector);
#endif
        }
    }

    // Update the static field
    if (rc_ok == true)
    {
        recEventLogFlashData.start = recEventLogFlashData.write = (tEventLog_inFlash *)SYS_FLASH_ERRLOG_ADDRESS;
        recEventLogFlashData.lastId = 0;
    }
}

///*
// * PrintMem:- To print the address and size of the allocated flash memory for erelog (From Linker script)
// * Return value:- None
// *
// */
static void PrintMem(void)
{
    printf("\n"
    	   "            memory address (size)\n"
           "m_eventlog   = 0x%08x (0x%08x)\n"
    	   "Total sectors: %d\n",
		   SYS_FLASH_ERRLOG_ADDRESS, SYS_FLASH_ERRLOG_SIZE,
           ERRLOG_SECTORS);
    printf("start:   0x%X\n"
    	   "write:   0x%X\n"
    	   "Last ID: %d\n",
			recEventLogFlashData.start,
    		recEventLogFlashData.write,
    		recEventLogFlashData.lastId);
}

//
/* Global call
 * EventLog_In:- to record the Eventor in Flash Memory.
 * After recording the time stamp, call writeEventLogToFlash`
 * Return value:- None
 *
 */
bool EventLog_In(uint16_t eventcode, uint16_t compNum, uint8_t sevlevel,  const char *fmt, ...)
{
    tEventLog_inFlash dtEventLog;
    bool rc_ok = true;
    va_list ap;

#if defined(USE_EVENTLOG_MUTEX)
    xSemaphoreTake(xEventlogMutex, EVENT_LOG_MAX_WAIT_MS/portTICK_PERIOD_MS);
#endif

    dtEventLog.logHeader.id = ++recEventLogFlashData.lastId;
    dtEventLog.logHeader.unixTimestamp = ConfigSvcData_GetIDEFTime(); // unix timestamp using idef
    dtEventLog.logHeader.eventCode = eventcode;
    dtEventLog.logHeader.sevLvl = sevlevel;
    dtEventLog.logHeader.compNumber = compNum;
    dtEventLog.logHeader.tag = HASH_TAG; // Default value

    memset(dtEventLog.logMsg, EMPTY_8, MAXLOGSTRINGLENGTH);
    va_start(ap, fmt);
    if((compNum == LOG_LEVEL_PMIC) && (eventcode >= PMIC_EVENTLOG_BAND))
    {
    	dtEventLog.logHeader.unixTimestamp = (uint64_t)va_arg(ap, uint32_t) * 10000000ULL;
    	strcpy(dtEventLog.logMsg, fmt);
    }
    else
    {
        int count = vsnprintf(dtEventLog.logMsg, sizeof(dtEventLog.logMsg), fmt, ap);
        if(count >= (MAXLOGSTRINGLENGTH - 1))
        {
             dtEventLog.logMsg[sizeof(dtEventLog.logMsg)-1] = '\0';
        }
    }
    va_end(ap);
     uint8_t *p = (uint8_t *)&dtEventLog.logHeader.tag;
    dtEventLog.logHeader.crc = 0xFF;
    for(int i = 0; i < sizeof(dtEventLog) - 1; i++)
    {
        dtEventLog.logHeader.crc ^= p[i];
    }

#ifdef DEBUG
    if (!quiet && dbg_logging)
    {
        //there is an event and some logging is on, we better also print it out here
        printf("\nEvent: ");
        ConfigSvcData_PrintIDEFTime(dtEventLog.logHeader.unixTimestamp);
        printf(" code:%d comp:%d level:%d,\n       \"%s\"\n",
                dtEventLog.logHeader.eventCode,
                dtEventLog.logHeader.compNumber,
                dtEventLog.logHeader.sevLvl,
                dtEventLog.logMsg);
    }
#endif
    uint32_t dest = (uint32_t)(recEventLogFlashData.write);
    rc_ok = writeToFlash((uint32_t *)dest, (uint32_t*)&dtEventLog, sizeof(dtEventLog));
    dest = (uint32_t)++recEventLogFlashData.write;
    if(dest >= (SYS_FLASH_ERRLOG_ADDRESS + SYS_FLASH_ERRLOG_SIZE))
    {
        recEventLogFlashData.write = (tEventLog_inFlash *)SYS_FLASH_ERRLOG_ADDRESS;
        dest = (uint32_t)recEventLogFlashData.write;
    }
    if((dest % SYS_FLASH_SECTOR_SIZE) == 0)
    {
        int sector = ((dest - SYS_FLASH_ERRLOG_ADDRESS) / SYS_FLASH_SECTOR_SIZE);
        rc_ok = irqDisabledDrvFlashEraseSector(sector);
        if((SYS_FLASH_ERRLOG_ADDR(sector) + SYS_FLASH_SECTOR_SIZE) >= (SYS_FLASH_ERRLOG_ADDRESS + SYS_FLASH_ERRLOG_SIZE))
        {
            recEventLogFlashData.start = (tEventLog_inFlash *)SYS_FLASH_ERRLOG_ADDRESS;
        }
        else
        {
        	recEventLogFlashData.start = (tEventLog_inFlash *)SYS_FLASH_ERRLOG_ADDR(++sector);
        }
    }
#if defined(USE_EVENTLOG_MUTEX)
    xSemaphoreGive(xEventlogMutex);
#endif

    return rc_ok;
}

/*
 * writeToFlash:- to write the record into the Flash
 * parameters: refcopy -> source address
 *             sector -> Current sector info
 *             totsum  -> Count of address in the sector which will be starting address of the sector to be written
 *             len ->  Length of the record to be written
 * Return value:- pass/fail (bool)
 */

static bool writeToFlash(uint32_t *dest, uint32_t *src, uint32_t len)
{
    // we may run in the same flash bank as the one we are erasing/programming,
    // then interrupts may not execute code inside this bank !
    __disable_irq();
    bool rc_ok = DrvFlashProgram((uint32_t *)dest, (uint32_t *)src, (uint32_t)len);
    __enable_irq();
    if (rc_ok)
    {
        // lets verify the data
        __disable_irq();
        rc_ok = DrvFlashVerify((uint32_t *)dest, (uint32_t *)src, len, 0);
   	    __enable_irq();
#ifdef DEBUGPRINTKEY
   		if(rc_ok)
        {
          //Do not report in event log
            printf("%s() flash verify at %08x %s\n", __func__, dest, (rc_ok) ? "passed" : "failed");
        }
#endif
    }
    else
    {
        //Do not report in event log
#ifdef DEBUGPRINTKEY
        printf("%s() flash write at %08x failed\n", __func__, dest);
#endif
        /*recommended to erase the flash. If Write is not successful, the SDK Flash driver will corrupt the memory address which will make an hard fault during reading
         * To avoid that, i have gone with erasing optionof the sector*/
        irqDisabledDrvFlashEraseSector(0);
        rc_ok =false;
    }
    return rc_ok;

}

/*
 * EventLog_InitFlashData:- Point to the last record. Get the last written sector and address(tosum)
 *                       Point to the last read location [If you have already read, then it points to last read time stamp recorded
 *                                                       If you have not read at all, then it will point to the last written time stamp]
 * Return value:- None
 */
void EventLog_InitFlashData(void)
{
#if defined(USE_EVENTLOG_MUTEX)
    if(xEventlogMutex == NULL)
    {
    	xEventlogMutex = xSemaphoreCreateMutex();
    }

    xSemaphoreTake(xEventlogMutex, EVENT_LOG_MAX_WAIT_MS/portTICK_PERIOD_MS);
#endif
    // set up the structure
    recEventLogFlashData.start = recEventLogFlashData.write = (tEventLog_inFlash *)SYS_FLASH_ERRLOG_ADDR(0);
    recEventLogFlashData.lastId = 0;

    // take the easy way out if eventlog is erased
    if(blank_check((uint8_t*)SYS_FLASH_ERRLOG_ADDRESS, SYS_FLASH_ERRLOG_SIZE))
    {
		#if defined(USE_EVENTLOG_MUTEX)
		xSemaphoreGive(xEventlogMutex);
		#endif
    	return;
    }

    uint32_t loId = 0xFFFF, hiId = 0;
    for (int sector = 0; sector < ERRLOG_SECTORS; sector++)
    {
        // check the starting address of the sector
        // if found empty then sector is empty else find the starting address
        tEventLog_inFlash * pEventLog = (tEventLog_inFlash *)(SYS_FLASH_ERRLOG_ADDR(sector));
        for(int i = 0; i < (SYS_FLASH_SECTOR_SIZE/sizeof(tEventLog_inFlash)); i++)
        {
        	if(*(fill_t*)&pEventLog[i] == EMPTY_64)
        	{
        		break;
        	}
            if((pEventLog[i].logHeader.tag != HASH_TAG) || !validChecksum((uint8_t*)&pEventLog[i]))
            {
            	EventLog_Clear();
            	return;
            }
            if(pEventLog[i].logHeader.id < loId)
            {
                loId = pEventLog[i].logHeader.id;
                recEventLogFlashData.start = &pEventLog[i];
            }
            if(pEventLog[i].logHeader.id > hiId)
            {
                hiId = pEventLog[i].logHeader.id;
                if((uint32_t)&pEventLog[i+1] >= (SYS_FLASH_ERRLOG_ADDRESS + SYS_FLASH_ERRLOG_SIZE))
                {
                    recEventLogFlashData.write = (tEventLog_inFlash*)SYS_FLASH_ERRLOG_ADDRESS;
                }
                else
                {
                    recEventLogFlashData.write = &pEventLog[i+1];
                }
            }
        }
    }
    recEventLogFlashData.lastId = hiId;
#if defined(USE_EVENTLOG_MUTEX)
    xSemaphoreGive(xEventlogMutex);
#endif
}

/* Global call
 * EventLog_getLog:- to write the record into the Flash[ This function also updates the starting address]
 * parameters: addrEventLog -> Gives the source address
 *
 * Return value:- bool   rc_ok = 1 expect data [ frame length contains length of an event log including null]
 *                       rc_ok = 0 pointing to null (no more data to read)[ frame length  will also be Null]
 */
bool EventLog_getLog(tEventLog_inFlash ** addrEventLog)
{
    // when the EventLog_getLog function is first called, this pointer should be NULL
    if(NULL == *addrEventLog)
    {
    	*addrEventLog = recEventLogFlashData.start;
    }
    else if((uint32_t)++*addrEventLog >= (SYS_FLASH_ERRLOG_ADDRESS + SYS_FLASH_ERRLOG_SIZE))
    {
        *addrEventLog = (tEventLog_inFlash *)SYS_FLASH_ERRLOG_ADDRESS;
    }
    return (*addrEventLog != recEventLogFlashData.write);
}

bool cliExtHelpEventLog(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{

    printf((char*)EventLogHelp);

    return true;
}

/* The suite initialisation function.
  * Returns zero on success, non-zero otherwise.
 */
static int init_suite1(void)
{
	quiet = true;
	return 0;
}

/* The suite cleanup function.
 * Returns zero on success, non-zero otherwise.
 */
static int clean_suite1(void)
{
	quiet = false;
	EventLog_Clear();
	return 0;
}

/*
 * testLog1
 *
 * @desc	Test for a valid log entry
 *
 * @param	none
 *
 * @returns	none
 */
static void testLogEntry(uint32_t addr)
{
	// validate a log entry by checking the size, tag, string and filler
	CU_ASSERT_TRUE(validChecksum((uint8_t*)addr));
	CU_ASSERT_EQUAL(HASH_TAG, *(uint8_t*)(addr+1));
	CU_ASSERT_EQUAL(0, strcmp((char*)(addr+sizeof(struct errLog_header)), msg10));
	CU_ASSERT_EQUAL(EMPTY_8, *(uint8_t*)(addr+0x37));
}

/*
 * testLogWriteMultiple
 *
 * @desc	Write <n> log entries to the flash
 *
 * @param	no - number of log entries to write
 *
 * @returns	none
 */
static void testLogWriteMultiple(int no, bool increment)
{
	uint8_t s[8];
	uint32_t args = 3;
	uint8_t *argv[3] = { (uint8_t*)"10", s, (increment ? (uint8_t*)"1" : (uint8_t*)"0" )};
	uint32_t argi[3] = { 10, no, increment };

	sprintf((char*)s, "%d", no);
	CU_ASSERT_TRUE(cliEventLog(args, argv, argi));
}

/*
 * testLogPadded
 *
 * @desc	Write a log entry of <pad> length +1
 *
 * @param	no - number of log entries to write
 *
 * @returns	none
 */
static void testLogPadded(int pad)
{
	uint32_t args;
	uint8_t *argv[5];
	uint32_t argi[5];
	int i;
	char msg[128];

	if(pad < (sizeof(msg) - 1))
	{
		for(i = 0; i < pad; i++)
		{
			msg[i] = '0' + (i % 10);
		}
		msg[i] = 0;
		args = 5;
		argv[0] = (uint8_t*)"8";
		argv[1] = argv[2] = argv[3] = (uint8_t*)"1";
		argv[4] = (uint8_t*)msg;
		argi[0] = 8;
		argi[1] = argi[2] = argi[3] = 1;
		argi[4] = 0;
		CU_ASSERT_TRUE(cliEventLog(args, argv, argi));
	}
}

/*
 * testLogEntryCount
 *
 * @desc	Count the number of log entries
 *
 * @param	reinit - set to initialise eventlog
 *
 * @param	count - number of log entries expected
 *
 * @returns	none
 */
static void testLogEntryCount(bool reinit, int count)
{
	tEventLog_inFlash * addrEventLog = NULL;
	int i = 0;

	if(reinit)
	{
		// re-init the buffer details
		EventLog_InitFlashData();
	}

	while (EventLog_getLog(&addrEventLog))
	{
		i++;
	}
	CU_ASSERT_EQUAL(count, i);
}

/*
 * testLog1
 *
 * @desc	erase the event log flash and validate the erase
 *
 * @param	none
 *
 * @returns	none
 */
static void testLog1()
{
	// erase the event log flash and validate the erase
	EventLog_Clear();
	CU_ASSERT_TRUE(blank_check((uint8_t*)SYS_FLASH_ERRLOG_ADDRESS, SYS_FLASH_ERRLOG_SIZE));
}

/*
 * testLog2
 *
 * @desc	write a single log entry and validate
 *
 * @param	none
 *
 * @returns	none
 */
static void testLog2()
{
	// write one log entry and validate it
	testLogWriteMultiple(1, true);
	testLogEntry(SYS_FLASH_ERRLOG_ADDRESS);
	CU_ASSERT_EQUAL((SYS_FLASH_ERRLOG_ADDRESS + sizeof(tEventLog_inFlash)), (uint32_t)recEventLogFlashData.write);
	CU_ASSERT_EQUAL(1, recEventLogFlashData.lastId);
}

/*
 * testLog3
 *
 * @desc	performs the following:
 *          1) clears event log flash
 *          2) write padding entries
 *          3) writes enough records to fill the first sector - 48 bytes
 *          4) reinitialise the flash structures
 *          5) write a single log entry at the sector crossover and validate
 *
 * @param	none
 *
 * @returns	none
 */
static void testLog3()
{
	// erase the event log flash
	testLog1();

	// write some padding event logs
	testLogPadded(53);

	// check that the EMPTY padding is in
	CU_ASSERT_EQUAL(0xffff, *(uint16_t*)(SYS_FLASH_ERRLOG_ADDRESS+0x46));

	// write multiple records to the first sector including 8 bytes of the last entry in the second
	testLogWriteMultiple(32, true);

	// check last entry bridges the sectors
	testLogEntry(SYS_FLASH_ERRLOG_ADDR(1));

	// check that the EMPTY padding is in
	CU_ASSERT_EQUAL(0xff, *(uint8_t*)(SYS_FLASH_ERRLOG_ADDR(1) + 0x37));

	// clear the start of the second sector
	CU_ASSERT_TRUE(irqDisabledDrvFlashEraseSector(1));

	// re-initialise the event log structures
	EventLog_InitFlashData();

	// now write another entry
	testLogWriteMultiple(1, true);

	// check log entry is at sector 1 + 8
	testLogEntry(SYS_FLASH_ERRLOG_ADDR(1));
}

/*
 * testLog4
 *
 * @desc	performs the following:
 *          1) clears event log flash
 *          2) writes enough records to roll over the flash
 *          3) check for the filler at the end of the flash
 *          4) reinitialise the flash structures
 *          5) write a single log entry and validate
 *
 * @param	none
 *
 * @returns	none
 */
static void testLog4()
{
	// erase the event log flash
	testLog1();

	// write sufficient log entries to cause a roll over
	testLogWriteMultiple((SYS_FLASH_ERRLOG_SIZE/sizeof(tEventLog_inFlash))+1, true);

	// check that the first entry is valid
	testLogEntry(SYS_FLASH_ERRLOG_ADDRESS);

	// check that the rest of the flash is erased
	CU_ASSERT_TRUE(blank_check((uint8_t*)SYS_FLASH_ERRLOG_ADDRESS+0x38, SYS_FLASH_SECTOR_SIZE-0x38));

	// reinitialise the event log
	EventLog_InitFlashData();

	// add another log event
	testLogWriteMultiple(1, true);

	// check we add a new entry correctly
	testLogEntry(SYS_FLASH_ERRLOG_ADDRESS+sizeof(tEventLog_inFlash));
}

/*
 * testLog5
 *
 * @desc	performs the following:
 *          1) clears event log flash
 *          2) writes 4 entries
 *          3) read back 4 entries
 *          4) write EVENT_LASTLOGREAD
 *          5) read back and check for 1 entry, the EVENT_LASTLOGREAD
 *
 * @param	none
 *
 * @returns	none
 */
static void testLog5()
{
	// erase the event log flash
	testLog1();

	// write 4 entries
	testLogWriteMultiple(4, true);

	// check we have 4 entries
	testLogEntryCount(true, 4);

    // write 10 entries
    testLogWriteMultiple(10, true);

	// check we have 4 + 10 = 14 entries
	testLogEntryCount(true, 14);
}

/*
 * testLog6
 *
 * @desc	performs the following:
 *          1) clears event log flash
 *          2) write padding entry msg size 24 bytes
 *          3) write 72 entries
 *          4) write 112 character log
 *          5) write single log entry
 *          6) erase sector 0
 *          7) initialise eventlog
 *          8) write single log entry
 *          9) validate single log entry
 *
 * @param	none
 *
 * @returns	none
 */
static void testLog6()
{
	// erase the event log flash
	testLog1();

	// fill the first sector plus one entry in the second
	testLogWriteMultiple((SYS_FLASH_SECTOR_SIZE/sizeof(tEventLog_inFlash))+1, true);

	// write 112 character log
	uint32_t args = 1;
	uint8_t *argv = (uint8_t*)"7";
	uint32_t argi = 7;
	CU_ASSERT_TRUE(cliEventLog(args, &argv, &argi));

	// now write another entry
	testLogWriteMultiple(1, true);

	// clear the first sector
	CU_ASSERT_TRUE(irqDisabledDrvFlashEraseSector(0));

	// re-initialise the event log structures
	EventLog_InitFlashData();

	// now write another entry
	testLogWriteMultiple(1, true);

    // check we have only 4 entries now
    testLogEntryCount(true, 4);
}

/*
 * testLog7
 *
 * @desc	performs the following:
 *          1) clears event log flash
 *          2) write (146*3)+1 log entries to wrap round twice
 *          3) validate single log entry at start of log
 *          4) initialise & check for 73 entries in the log
 *
 * @param	none
 *
 * @returns	none
 */
static void testLog7()
{
	// erase the event log flash
	testLog1();

	// write multiple records to the first sector including 8 bytes of the last entry in the second
	int noRecs = ((SYS_FLASH_SECTOR_SIZE/sizeof(tEventLog_inFlash)) * ERRLOG_SECTORS) + 1;
	testLogWriteMultiple(noRecs, true);

	// check log entry is at sector 1 + 8
	testLogEntry(SYS_FLASH_ERRLOG_ADDR(0));

	// check for the correct eventCode
	CU_ASSERT_EQUAL((900 + noRecs - 1), *(uint16_t*)(SYS_FLASH_ERRLOG_ADDRESS+0x4));

	// check we have only 74 entries now
	testLogEntryCount(true, ((SYS_FLASH_SECTOR_SIZE/sizeof(tEventLog_inFlash)) * (ERRLOG_SECTORS - 1)) + 1);
}

/*
 * testLog8
 *
 * @desc	performs the following:
 *          1) clears event log flash
 *          2) loop 4 times, writing 36 log entries & EVENT_LASTLOGREAD
 *          3) write a single log entry
 *          4) validate single log entry at start of log
 *          5) initialise & check for 2 entries in the log
 *
 * @param	none
 *
 * @returns	none
 */
static void testLog8()
{
	// erase the event log flash
	testLog1();

	// write multiple records to wrap around the available space
	testLogWriteMultiple(SYS_FLASH_ERRLOG_SIZE/sizeof(tEventLog_inFlash), true);

	// check the pointers
	CU_ASSERT_EQUAL(SYS_FLASH_ERRLOG_ADDRESS, (uint32_t)recEventLogFlashData.write);
	CU_ASSERT_EQUAL((SYS_FLASH_ERRLOG_ADDRESS + SYS_FLASH_SECTOR_SIZE), (uint32_t)recEventLogFlashData.start);

	// re-initialise the pointers
	EventLog_InitFlashData();

	// check the pointers
	CU_ASSERT_EQUAL(SYS_FLASH_ERRLOG_ADDRESS, (uint32_t)recEventLogFlashData.write);
	CU_ASSERT_EQUAL((SYS_FLASH_ERRLOG_ADDRESS + SYS_FLASH_SECTOR_SIZE), (uint32_t)recEventLogFlashData.start);
}

#if 0
/*
 * testLog9
 *
 * @desc    performs the following:
 *          1) writes random length log messages
 *          2) rebuilds index
 *          3) test index is the same
 *
 * @param   none
 *
 * @returns none
 */
static struct sEventLogFlashData save;
int result;
static void testLog9()
{
    char str[129];

    for(int j = 0; j < 128; j++)
    {
        for(int i = 0; i < 256; i++)
        {
            int size = rand() % 128;
            memset(str, 'A', size);
            str[size] = 0;
            EventLog_In(size, LOG_NUM_APP, ERRLOGINFO, str);
        }
        memcpy(&save, &recEventLogFlashData, sizeof(recEventLogFlashData));
        EventLog_InitFlashData();
        CU_ASSERT_EQUAL(save.start, recEventLogFlashData.start);
        CU_ASSERT_EQUAL(save.write, recEventLogFlashData.write);
        CU_ASSERT_EQUAL(save.lastId, recEventLogFlashData.lastId);
   }
}
#endif

#if defined(TEST_TASK_LOGGING)

/*
 * If these tests are to be used, make sure
 * #define INCLUDE_vTaskDelete is set to 1 in FreeRTOS config file.
 */

/*
 * testLog_Tasking
 *
 * @desc	performs the following:
 *          1) clears event log flash
 *          2) Create 2 tasks
 *          Each task creates the number of log entries passed as a parameter
 * @param	none
 *
 * @returns	none
 */
void TestTask1(void* pvParameters)
{
	uint32_t loop_count = (uint32_t)pvParameters;

	for(int i=0; i<loop_count; i++)
	{
		LOG_EVENT(900+i, LOG_NUM_APP, ERRLOGFATAL, msg10);
		vTaskDelay(0);
	}
	vTaskDelete(NULL);
}

void TestTask2(void* pvParameters)
{
	uint32_t loop_count = (uint32_t)pvParameters;

	for(int i=0; i<loop_count; i++)
	{
		LOG_EVENT(1900+i, LOG_NUM_APP, ERRLOGFATAL, msg4, i);
		vTaskDelay(0);
	}
	vTaskDelete(NULL);
}

static void testLog_Tasking(uint32_t loop_count)
{
	TaskHandle_t xHandle1 = NULL, xHandle2 = NULL;

	// erase the event log flash
	EventLog_Clear();

	xTaskCreate(TestTask1, "", 1200, loop_count, 1, &xHandle1);
	if(xHandle1 != NULL)
	{
		xTaskCreate(TestTask2, "", 1200, loop_count, 1, &xHandle2);
	}
}
#endif

CUnit_suite_t UTeventlog = {
		{ "eventlog", init_suite1, clean_suite1, "test event log functions" },
		{
				{"test 1", testLog1},
				{"test 2", testLog2},
				{"test 3", testLog3},
				{"test 4", testLog4},
				{"test 5", testLog5},
				{"test 6", testLog6},
				{"test 7", testLog7},
				{"test 8", testLog8},
				//{"test 9", testLog9},
				{ NULL, NULL }
		}
};

bool cliEventLog( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
    bool rc_ok = true;
    if (args > 0)
    {
        switch(argi[0])
        {
#if defined(TEST_TASK_LOGGING)
        case 999:
        	testLog_Tasking(args == 2 ? (argi[1] <= 200 ? argi[1]: 200) : 10);
        	break;
#endif

        case 1:
			{
				tEventLog_inFlash * addrEventLog = NULL;
				// In the application program, call for the init and then use getlog
				if(EventLog_getLog(&addrEventLog))
				{
					EventLog_printLogEntry(addrEventLog);
				}
			}
            break;

        case 2:
        	EventLog_Clear();
            break;

        case 3:
            PrintMem();
            break;

        case 4:
        	LOG_EVENT(1111, 222, ERRLOGWARN, msg4, 6); //36--  padding 40
        	break;

        case 5:
        	LOG_EVENT(6666, 777, ERRLOGFATAL, "Watch dog reset @0x%08X. External power supply voltage drop", 0x12345678); //51--padding 56
        	break;

         case 7:
            //char test[]="GPS Modem activation failure. State received and processed is not the same. Please check the modem connection1234";
            LOG_EVENT(7654, 456, ERRLOGMINOR, "GPS Modem activation failure. State received and processed is not the same. Please check the modem connection1234");//113
            break;

        case 8:
            if (args==5)
            {
            	LOG_EVENT(argi[1], argi[2], argi[3], (char*)argv[4]);
            }
            break;

        case 9:
			{
				tEventLog_inFlash * addrEventLog = NULL;

				// re-init the buffer details
				EventLog_InitFlashData();
				while (EventLog_getLog(&addrEventLog))
				{
					EventLog_printLogEntry(addrEventLog);
				}
				// re-init the buffer details
				EventLog_InitFlashData();
			}
			break;

        case 10: // write <n> logs to the eventlog
        	if(args >= 2)
        	{
        		for(int i = 0; i < argi[1]; i++)
        		{
       				LOG_EVENT( (((args > 2) && argi[2]) ? 900 + i : 1), LOG_NUM_APP, ERRLOGDEBUG, msg10);
        		}
        	}
        	break;

        case 11: // erase sector <n>
        	if((args == 2) && (argi[1] < ERRLOG_SECTORS))
        	{
        		rc_ok = irqDisabledDrvFlashEraseSector(argi[1]);
        	}
        	break;

        case 12: // initialise the eventlog data
        	EventLog_InitFlashData();
        	break;

        case 13:
			{
				// perform a blank check using the user margin level 0-normal, 1-user, 2-factory
				int margin = argi[1];
				uint32_t dest = (args < 3) ? SYS_FLASH_ERRLOG_ADDRESS : argi[2];
				uint32_t len = (args < 4) ? SYS_FLASH_ERRLOG_SIZE : argi[3];
			    __disable_irq();
				bool rc_ok = DrvFlashCheckErased((int32_t *)dest, len, margin);
			    __enable_irq();
			    if(rc_ok)
					printf("blank check passed\n");
				else
					printf("blank check failed\n");
			}
        	break;
        default:
       	    printf((char*)EventLogHelp);
        	break;
        }
    }
    return rc_ok;
}

#endif



#ifdef __cplusplus
}
#endif