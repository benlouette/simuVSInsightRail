#ifdef __cplusplus
extern "C" {
#endif

/*
 * is25.h
 *
 *  Created on: June 17, 2016
 *      Author: Rex Taylor (BF1418)
 *
 */

#ifndef SOURCES_DRV_IS25_H_
#define SOURCES_DRV_IS25_H_

#define IS25_TRANSFER_BAUDRATE      (3072000U)     /*! Transfer clock rate, note any faster and we get read issues */

#define IS25_SECTOR_SIZE_BYTES 		(4096)
#define EXTFLASH_PAGE_SIZE_BYTES 	(256)
#define IS25_MEMORY_SIZE_BYTES 		(4096 * IS25_SECTOR_SIZE_BYTES )

#define  IS25_64_MBIT_PRODUCT_ID		(0x16)
#define  IS25_128_MBIT_PRODUCT_ID		(0x17)
#define  IS25_512_MBIT_PRODUCT_ID		(0x19)

// public function prototypes
bool IS25_Init(uint32_t baudrate, uint32_t * calculatedBaudrate_p);
bool IS25_Terminate();

bool IS25_ReadProductIdentity(uint8_t* id);
bool IS25_ReadManufacturerId(uint8_t* pManufacturerIDOut, uint8_t* pDeviceIDOut);
bool IS25_WriteBytes(uint32_t addr, uint8_t* data, uint32_t length);
bool IS25_ReadBytes(uint32_t addr, uint8_t* data, uint32_t length);
bool IS25_PerformSectorErase(uint32_t startAddress, uint32_t numBytes);
bool IS25_PerformChipErase(void);


#endif /* SOURCES_DRV_IS25_H_ */


#ifdef __cplusplus
}
#endif