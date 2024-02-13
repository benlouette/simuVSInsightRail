#ifdef __cplusplus
extern "C" {
#endif

/*
 * binaryCLI_Task.c
 *
 *  Created on: 22 Apr 2020
 *      Author: RZ8556
 */

#include "fsl_os_abstraction.h"
#include "fsl_os_abstraction_free_rtos.h"

#include "COBS.h"
#include "binaryCLI.h"
#include "binaryCLI_Task.h"
#include "xTaskDefs.h"
#include "SvcMqttFirmware.h"

extern int CLI_handle_command(char * cmdbuf, uint8_t dbg_put_ch);

QueueHandle_t BinaryCLIQueue = NULL;
uint8_t decoded_message[BINARY_CLI_MAXRXBUF];

void xTaskBinaryCLI(void *pvParameters)
{
	BinaryCLIQueue_t binaryCLIqueue;

	// create a queue
	BinaryCLIQueue = xQueueCreate(1, sizeof(BinaryCLIQueue_t));

	while(1)
	{
		if(pdTRUE == xQueueReceive(BinaryCLIQueue, &binaryCLIqueue, portMAX_DELAY))
		{
			size_t nMessageSize = COBS_decodeMessage(binaryCLIqueue->data, decoded_message, binaryCLIqueue->cnt);
			if(binaryCLI_verifyChecksum(decoded_message, nMessageSize))
			{
				//Extract command and remove header and checksum
				tE_CLIcommand eCommand = decoded_message[0];
				uint8_t* pbyMessage = &decoded_message[1];
				nMessageSize -= 2;

				switch(eCommand)
				{
				case E_BinaryCli_Ack:
				case E_BinaryCli_Nack:
				case E_BinaryCli_OTACompleted:
					break;

				case E_BinaryCli_PacketRequest:
					break;

				case E_BinaryCli_PacketResponse:
					SvcFirmware_MessageHandler_BlockReply(pbyMessage, nMessageSize, eTRANSPORT_PIPE_CLI);
					break;

				case E_BinaryCli_Exit:
					// other housekeeping?
					binaryCLI_setMode(E_CLI_MODE_COMMAND);
					break;

				case E_BinaryCli_FW_notification:
					SvcFirmware_MessageHandler_UpdateNotification(pbyMessage, nMessageSize, eTRANSPORT_PIPE_CLI);
					break;

				case E_BinaryCli_Echo:
					binaryCLI_sendPacket(E_BinaryCli_Echo, pbyMessage, nMessageSize);
					break;

				case E_Raw_ASCII:
					pbyMessage[nMessageSize - 1] = 0; //Make sure it's zero terminated
					CLI_handle_command((char*)pbyMessage, 0);
					break;

				case E_BinaryCli_OTAStart:
					xTaskApp_binaryCliOta();
					break;

				default:
					break;
				}
			}

			// release the buffer
			binaryCLIqueue->owner = E_BufferUsedByInterrupt;
		}
	}
}


TaskHandle_t TaskHandle_BinaryCLI = NULL;// not static because of easy print of task info in the CLI

/*
 * Start the binary CLI task
 */
bool binaryCLItask_StartBinaryCLItask()
{
	if (pdPASS != xTaskCreate(
			xTaskBinaryCLI,				/* pointer to the task */
            (char const*)"BinCLI",		/* task name for kernel awareness debugging */
			STACKSIZE_XTASK_BINCLI, 	/* task stack size */
            NULL,						/* optional task startup argument */
			PRIORITY_XTASK_BINCLI,		/* initial priority */
            &TaskHandle_BinaryCLI		/* optional task handle to create */
        ))
    {
        return false; /* error! probably out of memory */
    }
    return true;
}



#ifdef __cplusplus
}
#endif