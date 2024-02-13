#ifdef __cplusplus
extern "C" {
#endif


#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include "drv_is25.h"
#include "PowerControl.h"
#include "printgdf.h"
#include "Resources.h"
#include "AdcApiDefs.h"
#include "FccTest.h"
#include "PinConfig.h"
#include "PinDefs.h"		// For LED
#include "Modem.h"		// EHS53init

#ifdef FCC_TEST_BUILD
static uint32_t g_SampleBufReadStartIndx = 0;
static uint32_t g_nTotalSamplesCollected = 0;
static bool g_bPrintSamples = false;
#if defined(DEBUG_FCC_TEST)
uint32_t testSampleVal = 0;
#include "AD7766_DMA.h"			// for AD7766_FinishSampling();
#endif

static void FccTest_WriteSamplesToExtFlash(uint32_t startAddress, uint8_t *pSrcBuf, uint32_t numBytes)
{
	if(IS25_PerformSectorErase(startAddress, numBytes) == false)
	{
		printf("\nEXT FLASH ERASE FAILED !!!!\n");
		return;
	}
	if(IS25_WriteBytes(startAddress,  pSrcBuf, numBytes) == false)
	{
		printf("\nEXT FLASH WRITE FAILED !!!!\n");
	}
}

#if defined (DEBUG_FCC_TEST)
static void PrintStoredSamples(uint32_t nStartAddr, uint32_t nsectors)
{
	unsigned int i;
	uint32_t strt, end;
	strt =  nStartAddr;
	end = strt+4096 * nsectors;
	bool rc_ok = true;
	while (end > strt && rc_ok)
	{
		uint8_t buf[16];
		printf(" %08x :",strt);

		rc_ok = IS25_ReadBytes(strt, buf, sizeof(buf));
		if (rc_ok)
		{
			for(i=0; i<16; i++)
			{
				if (i==8) printf(" ");
				printf(" %02x",buf[i]);
			}
			printf("  ");
			for(i=0; i<16; i++)
			{
				if (i==8) printf(" ");
				printf("%c",buf[i] < 32 || buf[i]> 126 ? '.' : buf[i]);
			}

			put_s("\r\n");
			strt += sizeof(buf);
		}
	}
}
#endif

/*
 * FCCTest_Init
 *
 * @desc    Initialises the external flash , powers up the GNSS module,
 * 			turns off the modem and configures the LED port pin.
 *
 * @return	void
 */
void FCCTest_Init()
{
	// Initialise some modules
	// used to call IS25_Init but removed as it's already done elsewhere
	// and doing it twice was causing a problem
	Modem_powerup(20000);
	powerGNSSOn();
	powerAnalogOn();
	pinConfigDigitalOut(SYSTEM_STATUS_LED1, kPortMuxAsGpio, 0, false);
}

/*
 * FccTest_StoreSamplesToSampleBuf
 *
 * @desc    Stores the samples to the sample buffer and write them to external flash
 * 			once a sector (4096 bytes) full is available.
 *
 * @param	pAdcBlock - pointer to the samples stored in the ping-pong buffer.
 *
 * @return	void
 */
void FccTest_StoreSamplesToSampleBuf(uint32_t *pAdcBlock)
{
	const uint8_t sampleSize_Bytes = 4;

	if ((g_nTotalSamplesCollected + ADC_SAMPLES_PER_BLOCK) <= SAMPLE_BUFFER_SIZE_WORDS)  // Buffer overrun protection
	{
		// Transfer DSP sample buffer into output sample buffer
		for (int i = 0; i < ADC_SAMPLES_PER_BLOCK; i++)
		{
#if defined (DEBUG_FCC_TEST)
			g_pSampleBuffer[g_nTotalSamplesCollected + i] = testSampleVal;
#else
			g_pSampleBuffer[g_nTotalSamplesCollected + i] = pAdcBlock[i];
#endif
		}
	}
	else
	{
		// ERROR: Sample buffer overrun
		printf("\n*** BUFFER OVERRUN ***\n");
		return;
	}

	g_nTotalSamplesCollected += ADC_SAMPLES_PER_BLOCK;
	// If a sector (4096 bytes) worth of data is ready, then write to ext flash.
	if (((g_nTotalSamplesCollected * sampleSize_Bytes) % IS25_SECTOR_SIZE_BYTES) == 0)
	{
		g_SampleBufReadStartIndx = g_nTotalSamplesCollected - (IS25_SECTOR_SIZE_BYTES / sampleSize_Bytes);
		FccTest_WriteSamplesToExtFlash((g_SampleBufReadStartIndx * sampleSize_Bytes),
									   (uint8_t *)(g_pSampleBuffer + g_SampleBufReadStartIndx),
									   IS25_SECTOR_SIZE_BYTES);
		// toggle the led to indicate FCC test mode is active.
		GPIO_DRV_TogglePinOutput(SYSTEM_STATUS_LED1);
#if defined (DEBUG_FCC_TEST)
		// Print the data written to the flash.
		if(g_bPrintSamples)
		{
			PrintStoredSamples((uint32_t)(g_SampleBufReadStartIndx * sampleSize_Bytes), (uint32_t)1);
		}
		if((g_nTotalSamplesCollected % 1024) == 0)
		{
			testSampleVal++;
		}
#endif
		// Reset the count to the beginning of the sample buf array, when entire SAMPLE BUF is FULL(32 sectors are written).
		if(g_nTotalSamplesCollected >= SAMPLE_BUFFER_SIZE_WORDS)
		{
			g_nTotalSamplesCollected = 0;
#ifdef DEBUG_FCC_TEST
			// This is to see the samples stored into the ext flash once 32 sectors
			// are written.
			AD7766_FinishSampling();
			g_SampleBufReadStartIndx = 0;
			for(int i = 1; i <= 32; i++)
			{
				PrintStoredSamples((uint32_t)g_SampleBufReadStartIndx, (uint32_t)1);
				vTaskDelay(1000);
				g_SampleBufReadStartIndx += 4096;
			}
#endif
		}
	}
}

/*
 * FccTest_PrintSamples
 *
 * @desc    Enables printing of the samples on the CLI.
 *
 * @param	bOutputEn - True enables the samples to be printed onto CLI
 *
 * @return	void
 */
void FccTest_PrintSamples(bool bOutputEn)
{
	g_bPrintSamples = bOutputEn;
}
#endif	// FCC_TEST_BUILD


#ifdef __cplusplus
}
#endif