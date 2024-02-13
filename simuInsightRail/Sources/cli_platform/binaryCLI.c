#ifdef __cplusplus
extern "C" {
#endif

/*
 * binaryCLI.c
 *
 *  Created on: 16 Apr 2020
 *      Author: RZ8556
 */

#include <stdbool.h>
#include <string.h>

#include "fsl_device_registers.h"
#include "fsl_uart_hal.h"
#include "fsl_os_abstraction.h"
#include "fsl_os_abstraction_free_rtos.h"
#include "fsl_misc_utilities.h"

#include "printgdf.h"
#include "configLog.h"
#include "CLIio.h"
#include "CLIcmd.h"
#include "COBS.h"
#include "binaryCLI.h"
#include "binaryCLI_Task.h"

extern UART_Type* CLI_uartBase;
extern QueueHandle_t BinaryCLIQueue;

static tE_CLImode m_eCLImode = E_CLI_MODE_COMMAND;
static RxBuffer_t binaryRxBuf[2] = {{.owner = E_BufferUsedByInterrupt}, {.owner = E_BufferUsedByInterrupt}};
static uint8_t g_abyCobs[BINARY_CLI_MAXTXBUF];
//COBS worst-case overhead is n/254 rounded up. So for n≈256, an overhead of 10 is more than enough
static uint8_t g_abyTxBuf[BINARY_CLI_MAXTXBUF - 10];
static uint8_t m_ascii_buffer[BINARY_CLI_MAX_ASCII];
static int m_ascii_pointer = 0;

/*
 * Get the current CLI mode
 */
tE_CLImode binaryCLI_getMode()
{
	return m_eCLImode;
}


/*
 * Set CLI operating mode
 */
void binaryCLI_setMode(const tE_CLImode new_mode)
{
	m_eCLImode = new_mode;
}


/*
 * Verify that checksum is 'target checksum'
 */
bool binaryCLI_verifyChecksum(const uint8_t* message, const size_t length)
{
	uint8_t checksum = 0;

	for(int i=0; i<length; i++)
	{
		checksum ^= *message++;
	}
	return checksum == CHECKSUM_TARGET;
}


/*
 * Set last byte to ensure 'target' checksum
 */
void binaryCLI_generateChecksum(uint8_t* message, const size_t length)
{
	uint8_t csum = 0;
	for(int i=0; i<length; i++)
	{
		csum ^= *message++;
	}
	*message = csum ^ CHECKSUM_TARGET;
}


/*
 * When in 'binary CLI' mode, output of print statements end up here
 */
void binaryCLI_addToASCIIbuffer(uint8_t c)
{
	if(m_ascii_pointer < BINARY_CLI_MAX_ASCII)
	{
		m_ascii_buffer[m_ascii_pointer++] = c;
	}
}


/*
 * Called when a 'print' call finalises
 */
void binaryCLI_sendASCIIbuffer()
{
	binaryCLI_sendPacket(E_Raw_ASCII, m_ascii_buffer, m_ascii_pointer);
	m_ascii_pointer = 0;
}


/*
 * Called from UART ISR when in binary mode
 */
void binaryCLI_processBytes()
{
	static int active_buffer = 0;

	/* Read 8-bit characters from the receiver */
	RxBuffer_t* pRxBuf = &(binaryRxBuf[active_buffer]);
	while ((pRxBuf->cnt < BINARY_CLI_MAXRXBUF) && (UART_RD_S1(CLI_uartBase) & UART_S1_RDRF_MASK))
	{
		uint8_t c = (uint8_t)UART_RD_D(CLI_uartBase);

		if(COBS_IsDelimiter(c))
		{
			if(pRxBuf->cnt > 0)
			{
				if(pRxBuf->owner != E_BufferUsedByInterrupt)
				{
					// TODO, log this!
				}
				pRxBuf->owner = E_BufferUsedByTask;
				BinaryCLIQueue_t queue = pRxBuf;
				BaseType_t xHigherPriorityTaskWoken;
				xQueueSendFromISR(BinaryCLIQueue, &queue, &xHigherPriorityTaskWoken);

				active_buffer = (active_buffer + 1) & 1;
				pRxBuf = &(binaryRxBuf[active_buffer]);
				pRxBuf->cnt = 0;
			}
		}
		else
		{
			pRxBuf->data[pRxBuf->cnt] = c;
			pRxBuf->cnt++;
		}
	}
}

//------------------------------------------------------------------------------
/// Send a packet over binary CLI.
///
/// @param  eCommand (tE_CLIcommand) -- The type of command to be sent.
/// @param  pPacket (const void*) -- Pointer to packet to be sent.
/// @param  nPacketSize (const size_t) -- Size of packet to be sent.
///
/// @return bool true = Success, false = Failure
//------------------------------------------------------------------------------
bool binaryCLI_sendPacket(tE_CLIcommand eCommand, const void* pPacket, const size_t nPacketSize)
{
	bool bSuccess = false;

	if (nPacketSize > ARRAY_SIZE(g_abyTxBuf) - 2) //Allow space for header and checksum
	{
		LOG_EVENT(0, LOG_NUM_APP, ERRLOGMAJOR, "%s - packet size too large (%d > %d)", __func__, nPacketSize, ARRAY_SIZE(g_abyTxBuf) - 2);
	}
	else
	{
		g_abyTxBuf[0] = eCommand;
		memcpy(&g_abyTxBuf[1], pPacket, nPacketSize);

		binaryCLI_generateChecksum(g_abyTxBuf, nPacketSize + 1); //Include header

		const size_t buf_cobs_len = COBS_encodeMessage(g_abyTxBuf, g_abyCobs, nPacketSize + 2); //Include header and checksum
		g_abyCobs[buf_cobs_len] = COBS_DELIMITER;

		for(int i=0; i < buf_cobs_len + 1; i++)
		{
			CLI_put_ch((char)g_abyCobs[i]);
		}

		bSuccess = true;
	}

	return bSuccess;
}

//------------------------------------------------------------------------------
/// Get the maximum packet size for sending over binary CLI.
///
/// @return uint32_t The maximum packet size for sending over binary CLI.
//------------------------------------------------------------------------------
uint32_t binaryCLI_GetMaxPacketSize()
{
	return ARRAY_SIZE(g_abyTxBuf) - 2; //Allow space for header and checksum
}

/*
 * binaryCLI_CLI
 */

#include <string.h>
static bool cliBinaryCLI(uint32_t args, uint8_t * argv[], uint32_t * argi);

static const struct cliCmd binaryCLISpecificCommands[] =
{
	{"binaryCLI", "", cliBinaryCLI, 0}
};

/*
 * Call this if we have a command console
 */
void binaryCLI_InitCLI()
{
	(void)cliRegisterCommands(binaryCLISpecificCommands,
			sizeof(binaryCLISpecificCommands)/sizeof(*binaryCLISpecificCommands));
}


/*
 * Use this command to switch to Binary CLI mode
 */
static bool cliBinaryCLI(uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	if(strcmp((const char*)argv[0], "enable") == 0)
	{
		if(binaryCLI_getMode() == E_CLI_MODE_BINARY)
		{
			//printf("Already in Binary CLI mode\n");
		}
		else
		{
			if(!binaryCLItask_StartBinaryCLItask())
			{
				LOG_EVENT(0, LOG_NUM_APP, ERRLOGMAJOR, "binaryCLItask_StartBinaryCLItask() did not start");
				return true;
			}
			binaryCLI_setMode(E_CLI_MODE_BINARY);
			//printf("Binary CLI mode!\n");
		}
	}
	return true;
}








#ifdef __cplusplus
}
#endif