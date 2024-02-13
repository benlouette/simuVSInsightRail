#ifdef __cplusplus
extern "C" {
#endif

/*
 * binaryCLI.h
 *
 *  Created on: 16 Apr 2020
 *      Author: RZ8556
 */

#ifndef SOURCES_CLI_PLATFORM_BINARYCLI_H_
#define SOURCES_CLI_PLATFORM_BINARYCLI_H_

#include <stdbool.h>

/*
Send Firmware notification to the sensor FWnotify (Utility->Sensor)
Receive packet request from the sensor RequestPacket (Sensor->Utility)
Respond to packet request Packet (Utility->Sensor)
Receive status from sensor (errors or success) Status (Sensor->Utility)
Exit from binary mode ExitBinary (Utility->Sensor)
*/

#define BINARY_CLI_MAXTXBUF	(280)
#define BINARY_CLI_MAXRXBUF	(BINARY_CLI_MAXTXBUF + 256)	//Add a bit of overhead for packets where data (of length BINARY_CLI_MAXBUF)
														//is appended to structure SKF_SvcFirmwareBlockReply
#define BINARY_CLI_MAX_ASCII (512)
#define CHECKSUM_TARGET (0xAA)

typedef enum
{
	E_BinaryCli_Exit = 1,
	E_BinaryCli_Ack,
	E_BinaryCli_Nack,
	E_BinaryCli_FW_notification,
	E_BinaryCli_PacketRequest,
	E_BinaryCli_PacketResponse,
	E_BinaryCli_Echo,
	E_Raw_ASCII,
	E_BinaryCli_OTAStart,
	E_BinaryCli_OTACompleted,
} tE_CLIcommand;

typedef enum
{
	E_CLI_MODE_BINARY,
	E_CLI_MODE_COMMAND
} tE_CLImode;

typedef enum
{
	E_BufferFree,
	E_BufferUsedByInterrupt,
	E_BufferUsedByTask
} tE_BufferOwner;

typedef struct
{
	uint8_t data[BINARY_CLI_MAXRXBUF];
	int cnt;
	tE_BufferOwner owner;
} RxBuffer_t;

tE_CLImode binaryCLI_getMode();
void binaryCLI_setMode(const tE_CLImode new_mode);
bool binaryCLI_verifyChecksum(const uint8_t* message, const size_t length);
void binaryCLI_generateChecksum(uint8_t* message, const size_t length);
void binaryCLI_processBytes();
bool binaryCLI_sendPacket(tE_CLIcommand eCommand, const void* pPacket, const size_t nPacketSize);
uint32_t binaryCLI_GetMaxPacketSize();
void binaryCLI_InitCLI();
void binaryCLI_addToASCIIbuffer(uint8_t c);
void binaryCLI_sendASCIIbuffer();

#endif /* SOURCES_CLI_PLATFORM_BINARYCLI_H_ */


#ifdef __cplusplus
}
#endif