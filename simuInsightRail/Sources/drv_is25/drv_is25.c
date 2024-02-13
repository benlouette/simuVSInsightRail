#ifdef __cplusplus
extern "C" {
#endif

/*
 * is25.c
 *
 *  Created on: June 17, 2016
 *      Author: Rex Taylor (BF1418)
 *
 */



#include "string.h"
#include "fsl_dspi_edma_master_driver.h"
#include "PinDefs.h"
#include "pinconfig.h"
#include "device.h"
#include "log.h"

#include "drv_is25.h"

#define IS25_512_MBIT_PRODUCT_ID_MAX_ADDR	(0x4000000)
#define IS25_128_MBIT_PRODUCT_ID_MAX_ADDR	(0x1000000)
#define IS25_64_MBIT_PRODUCT_ID_MAX_ADDR	(0x800000)
// private function declarations

static bool PerformWriteEnable();

static bool IS25_TakeMutex();
static bool IS25_GiveMutex();
static void FlashExtInitIOLines(void);

#define DSPI_MASTER_INSTANCE        (SPI0_IDX) 		/*! User change define to choose DSPI instance */

// longest running action is the chip erase, which can take 90 seconds !!!
#define IS25_DEFAULT_MUTEXWAIT_MS (90000)

// defines derived from the IS25LP128 and IS25LP512 datasheets

#define IS25_NORD					0x03		// Normal Read Mode
#define IS25_PP						0x02		// Input Page Program
#define IS25_SER					0xD7		// Sector Erase
#define IS25_BER32					0x52		// Block Erase 32KByte
#define IS25_BER64					0xD8		// Block Erase 64KByte
#define IS25_CER					0xC7		// Chip Erase
#define IS25_WREN					0x06		// Write Enable
#define IS25_RDSR					0x05		// Read Status Register
#define IS25_RDID					0xAB		// Read ID
#define IS25_RDMDID					0x90		// Read Manufacturer and Device ID
#define	IS25_WRBV					0x17		// Write bank address register (volatile) no WREN
#define IS25_RDBR					0x16		// Read bank address register

#define IS25_STATUS_WIP             0x01        // Write in progress
#define IS25_STATUS_WEL             0x02        // Write Enable Latch
#define IS25_STATUS_BP0             0x04        // block protect bits
#define IS25_STATUS_BP1             0x08
#define IS25_STATUS_BP2             0x10
#define IS25_STATUS_BP3             0x20
#define IS25_STATUS_QE              0x40        // quad enable
#define IS25_STATUS_SRWD            0x80        // status reg write prot

#define IS25_A24_A25_MASK (0x03000000)

// is25 command structures
// read and write go parallel when using spi, so output and input map the same
// if input and output bufferpointers in the SPI transfer are the same, input buffer is replaced by the output buffer contents.

// for all single byte commands
#ifdef _MSC_VER
#define PACKED_STRUCT_START __pragma(pack(push, 1))
#define PACKED_STRUCT_END   __pragma(pack(pop))
#else
#define PACKED_STRUCT_START
#define PACKED_STRUCT_END   __attribute__((packed))
#endif


PACKED_STRUCT_START
typedef struct  {
    uint8_t cmd;
} t_cmd;

PACKED_STRUCT_END

PACKED_STRUCT_START
typedef struct  {
    uint8_t cmd;
    uint8_t dummy[3];
    uint8_t deviceId;
} t_rdid;
PACKED_STRUCT_END

PACKED_STRUCT_START
typedef struct  {
    uint8_t cmd;
    uint8_t dummy[2];
    uint8_t address;
    uint8_t manufactureId;
    uint8_t deviceId;
} t_rdmdid;
PACKED_STRUCT_END

PACKED_STRUCT_START
// for all single byte commands
typedef struct  {
    uint8_t cmd;
    uint8_t status;
} t_rdsr;
PACKED_STRUCT_END

PACKED_STRUCT_START
typedef struct  {
    uint8_t cmd;
    uint8_t status;
} t_wrsr;
PACKED_STRUCT_END

PACKED_STRUCT_START
typedef struct  {
    uint8_t cmd;
    uint8_t address[3];
} t_ser;
PACKED_STRUCT_END

PACKED_STRUCT_START
typedef struct  {
    uint8_t cmd;
    uint8_t address[3];
    uint8_t data[256];
} t_cab;


// the transfer buffer (command and data)
static volatile t_cab cab; // volatile, because it is updated using DMA, this is outside the scope of the compiler, and should prevent optimized accesses

// collect all flash related data in this struct
static struct {
    // dma related stuff
#ifdef _MSC_VER
    edma_software_tcd_t __declspec(align(32))  stcdDspiMasterTest;
#else 
    edma_software_tcd_t stcdDspiMasterTest __attribute__((aligned(32)));
#endif // _MSC_VER
    

    dspi_edma_master_state_t edmaMasterState;
    dspi_edma_device_t edmaDevice;

    uint32_t calculatedBaudRate;// the real baudrate used

    bool initialized;
    mutex_t mutex;

} is25FlashStatus = {
        .initialized = false,
        .mutex = NULL
};

typedef enum
{
		eFLASH_OP_SECTOR_ERASE,
		eFLASH_OP_CHIP_ERASE,
		eFLASH_OP_PAGE_PROG,
} flashOp_t;

// Variable containing the product ID.
static uint8_t m_productId = 0;


uint32_t GetOpTimeout(flashOp_t op)
{
	uint32_t nTimeout_msec = 0;
	switch(m_productId)
	{
		case IS25_512_MBIT_PRODUCT_ID:
		{
			if(eFLASH_OP_SECTOR_ERASE == op)
			{
				nTimeout_msec = 300;	// 300msec
			}
			else if (eFLASH_OP_CHIP_ERASE == op)
			{
				nTimeout_msec = 270000;	// 270secs
			}
			else if(eFLASH_OP_PAGE_PROG == op)
			{
				nTimeout_msec = 2;	// 2msec
			}
			else
			{
				nTimeout_msec = IS25_DEFAULT_MUTEXWAIT_MS;
			}
		}
		break;

		case IS25_128_MBIT_PRODUCT_ID:
		{
			if(eFLASH_OP_SECTOR_ERASE == op)
			{
				nTimeout_msec = 300;	// 300msec
			}
			else if (eFLASH_OP_CHIP_ERASE == op)
			{
				nTimeout_msec = 90000;	// 90secs
			}
			else if(eFLASH_OP_PAGE_PROG == op)
			{
				nTimeout_msec = 1;	// 1msec
			}
			else
			{
				nTimeout_msec = IS25_DEFAULT_MUTEXWAIT_MS;
			}
		}
		break;

		default:
			nTimeout_msec = IS25_DEFAULT_MUTEXWAIT_MS;
		break;
	}
	return nTimeout_msec;
}

static bool IsAddrValid(uint32_t addr, const char* pCallingFunc)
{
	bool bValidAddr = true;

	switch(m_productId)
	{
		case IS25_512_MBIT_PRODUCT_ID:
			if(addr >= IS25_512_MBIT_PRODUCT_ID_MAX_ADDR)
			{
				bValidAddr = false;
			}
		break;
		case IS25_128_MBIT_PRODUCT_ID:
			if(addr >= IS25_128_MBIT_PRODUCT_ID_MAX_ADDR)
			{
				bValidAddr = false;
			}
		break;
		case IS25_64_MBIT_PRODUCT_ID:
			if(addr >= IS25_64_MBIT_PRODUCT_ID_MAX_ADDR)
			{
				bValidAddr = false;
			}
		break;
		default:
			bValidAddr = false;
		break;
	}

	if(false == bValidAddr)
	{
		LOG_DBG(LOG_LEVEL_I2C,"In %s() called from %s(): Addr:0x%x exceeds device size, DevId: 0x%x\n", __func__, pCallingFunc, addr, m_productId);
	}
	return bValidAddr;
}

/*!
 * DoTransfer
 *
 * @brief       Complete a DSPI edma data transfer
 *
 * @param       txBuf - pointer data to be transmitted
 *
 * @param       rxBuf - pointer data to be received, if any
 *
 * @param       length - number of bytes to transmit/receive
 *
 * @return     true or false - true = completed transfer ok
 *
 * buffer pointers declared volatile, because they are read/filled by the DMA controller, and that is out of the compiler scope
 */
static bool DoTransfer( void* volatile txBuf, void* volatile rxBuf, uint16_t length)
{
	bool bFlashOK = true;
	//uint32_t wordsTransfer = 0;
	dspi_status_t dspiResult;
	uint32_t timeout = 10;        // TODO - base timeout on baudrate


	dspiResult =  DSPI_DRV_EdmaMasterTransferBlocking(DSPI_MASTER_INSTANCE,
		                                                    NULL,
		                                                    txBuf,
		                                                    rxBuf,
		                                                    length,
		                                                   timeout);

	switch(dspiResult)
	{
	case kStatus_DSPI_Success:
		bFlashOK = true;
		break;
	case kStatus_DSPI_SlaveTxUnderrun:           /*!< DSPI Slave Tx Under run error*/
	case kStatus_DSPI_SlaveRxOverrun:            /*!< DSPI Slave Rx Overrun error*/
	case kStatus_DSPI_Timeout:                   /*!< DSPI transfer timed out*/
	case kStatus_DSPI_Busy:                      /*!< DSPI instance is already busy performing a transfer.*/
	case kStatus_DSPI_NoTransferInProgress:      /*!< Attempt to abort a transfer when no transfer was in progress*/
	case kStatus_DSPI_InvalidBitCount:           /*!< bits-per-frame value not valid*/
	case kStatus_DSPI_InvalidInstanceNumber:     /*!< DSPI instance number does not match current count*/
	case kStatus_DSPI_OutOfRange:                /*!< DSPI out-of-range error  */
	case kStatus_DSPI_InvalidParameter:          /*!< DSPI invalid parameter error */
	case kStatus_DSPI_NonInit:                   /*!< DSPI driver does not initialize: not ready */
	case kStatus_DSPI_Initialized:               /*!< DSPI driver has initialized: cannot re-initialize*/
	case kStatus_DSPI_DMAChannelInvalid:         /*!< DSPI driver could not request DMA channel(s) */
	case kStatus_DSPI_Error:                     /*!< DSPI driver error */
	case kStatus_DSPI_EdmaStcdUnaligned32Error:   /*!< DSPI Edma driver STCD unaligned to 32byte error */
	default:
		printf("ERROR - DoTransfer failed error code %d\r\n", dspiResult);
		vTaskDelay( 100 /  portTICK_PERIOD_MS );
		bFlashOK = false;
		break;
	}

	return bFlashOK;
}

// test for status bit condition (by the xor and and mask)
// return true when condition met
// return false when condition not met within specified time
// when pollwaitMs is zero, full speed polling is done, and the maxWaitMs is used together with the baudrate to calculate the max number of polling attempts
/*!
 * pollingWait
 *
 * @brief       test for status bit condition (by the xor and and mask)
 *              return true when condition met
 *
 * @param       statusBitsXor -
 * @param       statusBitsAnd -
 *                  together they make possible to test one or more status bits.
 *                  the result of operation (status xor statusBitsXor) and statusBitsAnd
 *                  must be non zero to exit the function.
 *
 * @param       initialWaitMs - this time is spend in a vTaskDelay(). Value may be zero
 *
 * @param       pollWaitMs - after initialWaitMs is expired, the status register is polled with pollWaitMs interval (value may be zero)
 *
 * @param       maxWaitMs  - maximum time it should take function is exit with false when expired)
 *                          when pollwaitMs is zero, full speed polling is done,
 *                           and the maxWaitMs is used together with the baudrate to calculate the max number of polling attempts.
 *
 * @return     true or false - and true means that the status bits are set/reset within the maximum timeout
 *
 * buffer pointers declared volatile, because they are read/filled by the DMA controller, and that is out of the compiler scope
 */

static bool pollingWait( uint8_t statusBitsXor, uint8_t statusBitsAnd, uint32_t initialWaitMs, uint32_t pollWaitMs, uint32_t maxWaitMs)
{
    uint32_t totalWait = 0;
    bool rc_ok=true;
    bool condition = false;

    if (initialWaitMs) {
        vTaskDelay(initialWaitMs/portTICK_PERIOD_MS);
        totalWait += initialWaitMs;
    }

    if (maxWaitMs == 0) maxWaitMs = 1;// sanity check

    if (pollWaitMs == 0 && initialWaitMs == 0) {
        // full speed polling
        // the baudrate and the readstatus command length gives us a minimum time for each poll loop,
        // setting up the transfer takes also time, so we stay conservative
        // transfer of one byte takes 8/IS25_TRANSFER_BAUDRATE
        //
        maxWaitMs  *= is25FlashStatus.calculatedBaudRate/ (sizeof(t_rdsr) * 8 ) / 1000;// and now maxwait is maximum number of status register read attempts
    }

    while (totalWait < maxWaitMs && !condition && rc_ok)
    {
        t_rdsr rdsr;

        rdsr.cmd = IS25_RDSR;
        rdsr.status = 0;

        rc_ok = DoTransfer( (void * volatile) &rdsr, (void * volatile) &rdsr, sizeof(rdsr));
        if (rc_ok) {
            condition = (rdsr.status ^ statusBitsXor) & statusBitsAnd;
            if (condition) break;// out of the while
        }

        if (pollWaitMs) {
            vTaskDelay(pollWaitMs/portTICK_PERIOD_MS);
            totalWait += pollWaitMs;
        } else {
            totalWait++;
        }
    }

    return condition;
}


/*!
 * PerformWriteEnable
 *
 * @brief       sets the Write Enable Latch (WEL) bit found in the status register.
 * 				In order to write data to the flash the Write ENable bit must be set.
 * 				This is confirmed by reading back the Write Enable Latch bit
 *
 * @param       None
 *
 * @returns     true or false - true = completed transfer ok
 */
static bool PerformWriteEnable()
{
    // try to set the WEL bit in the status register
    uint8_t cmd = IS25_WREN;
    bool rc_ok = DoTransfer( (void * volatile)&cmd, (void * volatile)&cmd, sizeof(cmd));
    if(rc_ok)
    {
        // now verify that WEL bit  [1] is set
        // try to read the Status Register 0x05 bit 1
        // operation takes typical 2ms, max is 15ms (take 5 extra) (datasheet: tW Write Status Register time 2 15 ms)
        rc_ok = pollingWait( 0x00 , IS25_STATUS_WEL, 2 , 1, 15+5);
    }

    return rc_ok;
}


/*!
 * PerformBankAddressWrite
 *
 * @brief       Sets the bank address bits in Bank Address Register
 *
 * @param       bank - bank to set (2 LS bits 00, 01, 10, 11)
 *
 * @returns     true or false - true = completed transfer ok
 */
static bool PerformBankAddressWrite(uint32_t bank)
{
	if(IS25_512_MBIT_PRODUCT_ID != m_productId)
	{
		return true;
	}

	t_wrsr wr_bank;

	wr_bank.cmd = IS25_WRBV;
	wr_bank.status = (uint8_t)(bank & 3);

	bool rc_ok = DoTransfer((void * volatile)&wr_bank, (void * volatile)&wr_bank, sizeof(wr_bank));
	if(rc_ok)
	{
		// now verify that WIP bit is clear
		rc_ok = pollingWait(IS25_STATUS_WIP, IS25_STATUS_WIP, 2, 1, 15+5);
	}

	wr_bank.cmd = IS25_RDBR;
	wr_bank.status = 0;

	rc_ok = DoTransfer((void * volatile)&wr_bank, (void * volatile)&wr_bank, sizeof(wr_bank));
	if(rc_ok)
	{
		rc_ok = ((wr_bank.status & 3) == (uint8_t)(bank & 3));
	}

	return rc_ok;
}


/*!
 * PerformChipErase
 *
 * @brief       Erase the entire chip
 *
 * @return      true or false - true = completed transfer ok
 */

bool IS25_PerformChipErase(void)
{
    LOG_DBG(LOG_LEVEL_I2C,"%s()\n", __func__);

    bool rc_ok = IS25_TakeMutex();
    if (rc_ok)
    {
		rc_ok = PerformWriteEnable();        // set the Write Enable Latch - you cannot do a write action without doing this first
		if (rc_ok)
		{
			uint8_t cmd = IS25_CER;
			rc_ok = DoTransfer((void * volatile) &cmd, (void * volatile) &cmd, sizeof(cmd));
		}

		if (rc_ok)
		{
			// wait for the operation to become ready WEL bit goes from 1 to 0
			// operation takes tbd
			rc_ok = pollingWait( IS25_STATUS_WEL , IS25_STATUS_WEL, 45 , 20, GetOpTimeout(eFLASH_OP_CHIP_ERASE));
		}
        IS25_GiveMutex();
    }

    return rc_ok;
}


/*!
 * PerformSectorErase
 *
 * @brief       Erase the sector(s) so the area from starting address upto start + length is erased
 *              Side effect is that when the addresses are not exactly on sector boundaries, also outside the given addresses will be erased in sector size blocks
 *
 * @param       startAddress - starting address in bytes of the sector to be erased
 *
 * @param       length - starting address of the area to be erased
 *
 * @return      true or false - true = completed transfer ok
 */

bool IS25_PerformSectorErase(uint32_t startAddress, uint32_t numBytes)
{
    if (numBytes == 0)
    {
        // that is an easy task !
        LOG_DBG(LOG_LEVEL_I2C,"%s(): warning, zero bytes to erase ???\n", __func__);
        return true;
    }

    if (!IsAddrValid(startAddress + numBytes - 1, __func__))
    {
        return false;
    }

    uint32_t startSector = startAddress/(IS25_SECTOR_SIZE_BYTES);
    uint32_t numSectors = ((startAddress + numBytes -1 )/IS25_SECTOR_SIZE_BYTES) - startSector + 1 ;

    LOG_DBG(LOG_LEVEL_I2C,"%s() from %08x to %08x\n", __func__, startSector * IS25_SECTOR_SIZE_BYTES, (startSector + numSectors)* IS25_SECTOR_SIZE_BYTES -1);

    bool rc_ok = IS25_TakeMutex();
    if (rc_ok)
    {
    	uint32_t maxTimeout_msec = GetOpTimeout(eFLASH_OP_SECTOR_ERASE);
    	uint32_t current_segment = 0;
    	PerformBankAddressWrite(0);

        while ((numSectors > 0 ) && rc_ok)
        {
        	uint32_t address = startSector * IS25_SECTOR_SIZE_BYTES;

        	if((address & IS25_A24_A25_MASK) != current_segment)
			{
				current_segment = address & IS25_A24_A25_MASK;
				rc_ok = PerformBankAddressWrite(current_segment >> 24);
			}

            if(rc_ok)
            {
            	rc_ok = PerformWriteEnable();        // set the Write Enable Latch - you cannot do a write action without doing this first
            }

            if (rc_ok)
            {
                t_ser ser;
                ser.cmd = IS25_SER;
                ser.address[0] = (address >>16) & 0xff;
                ser.address[1] = (address >> 8) & 0xff;
                ser.address[2] =  address       & 0xff;
                rc_ok = DoTransfer((void * volatile) &ser, (void * volatile) &ser, sizeof(ser));
            }

            if (rc_ok)
            {
                // wait for the operation to become ready WEL bit goes from 1 to 0
                // operation takes typical 45 ms max is 300ms.
                rc_ok = pollingWait(IS25_STATUS_WEL, IS25_STATUS_WEL, 45, 20, maxTimeout_msec);
                startSector++;
                numSectors--;
            }
        }
        IS25_GiveMutex();
    }

    return rc_ok;
}

/*!
 * IS25_WriteBytes
 *
 * @brief       Write data to the flash, can be more than a page,
 *              it will take care of address wrapping the 256 byte chunks
 *              if the start address is not at a 256 byte boundary
 *
 * @param       addr - starting address of the page to be programmed
 *
 * @param		data - pointer to the data to be programmed
 *
 * @param		length - the data length
 *
 * @returns     true or false - true = completed transfer ok
 */
bool IS25_WriteBytes(uint32_t addr, uint8_t* data, uint32_t length)
{
    if ( data == NULL )
    {
        // no silly values please
        return false;
    }

    if (!IsAddrValid((addr + length), __func__))
    {
        return false;
    }

    bool rc_ok = IS25_TakeMutex();
    if(rc_ok)
    {
    	uint32_t maxTimeout_msec = GetOpTimeout(eFLASH_OP_PAGE_PROG);
        uint32_t current_segment = 0;
        PerformBankAddressWrite(0);

    	// loop over all data until done
        while (rc_ok && length>0)
        {
        	if((addr & IS25_A24_A25_MASK) != current_segment)
			{
				current_segment = addr & IS25_A24_A25_MASK;
				rc_ok = PerformBankAddressWrite(current_segment >> 24);
			}

        	if(rc_ok)
        	{
				uint32_t transfer_count;
				// take care of the wrap around in the page buffer address, see device manual chapter 8.7 PAGE PROGRAM OPERATION
				transfer_count = 0x100 - (addr & 0xff); // is25 page size is 0x100 bytes
				if (transfer_count > length)
				{
					transfer_count = length;
				}

				rc_ok = PerformWriteEnable();        // set the Write Enable Latch - you cannot do a write action without doing this first
				if(rc_ok == true)
				{
					memset( (void *) &cab, 0xff, sizeof(cab));// 0xff is the erased state of the flash

					cab.cmd = IS25_PP;
					cab.address[0] = (addr>>16) & 0xff;
					cab.address[1] = (addr>> 8) & 0xff;
					cab.address[2] =  addr      & 0xff;

					// copy over the data
					memcpy( (void *) cab.data, data, transfer_count);
					rc_ok = DoTransfer((void * volatile) &cab, (void * volatile) &cab, transfer_count + sizeof(cab.cmd) + sizeof(cab.address));

					if (rc_ok)
					{
						// now wait until the  WEL bit  [1] is cleared again (happens after the erase is finished)
						// try to read the Status Register 0x05 bit 1
						// operation takes typical 0.2 ms, max is 1ms (take 1 milli seconds extra)
						rc_ok = pollingWait( IS25_STATUS_WEL , IS25_STATUS_WEL, 0, 0, maxTimeout_msec);

						addr += transfer_count;
						data += transfer_count;
						length -= transfer_count;
					}
				}
        	}
        }
        IS25_GiveMutex();
    }

    return rc_ok;
}



/*!
 * IS25_ReadBytes
 *
 * @brief       Read data from flash
 *
 * @param       addr - starting address of the page to be read
 *
 * @param		data - pointer to the data to be programmed
 *
 * @param		length - the data length
 *
 * @returns     true or false - true = completed transfer ok
 */

bool IS25_ReadBytes(uint32_t addr, uint8_t* data, uint32_t length)
{
    if ( data == NULL )
    {
        // sanity check
        return false;
    }

    if (!IsAddrValid((addr + length), __func__))
    {
        return false;
    }

    bool rc_ok = IS25_TakeMutex();
    if(rc_ok)
    {
    	uint32_t current_segment = 0;
    	PerformBankAddressWrite(0);

        while (rc_ok && length>0)
        {
        	if((addr & IS25_A24_A25_MASK) != current_segment)
			{
				current_segment = addr & IS25_A24_A25_MASK;
				rc_ok = PerformBankAddressWrite(current_segment >> 24);
			}

        	if(rc_ok)
        	{
				uint32_t transfer_count;
				memset( (void *) & cab, 0, sizeof(cab));
				cab.cmd = IS25_NORD;

				cab.address[0] = (addr>>16) & 0xff;
				cab.address[1] = (addr>> 8) & 0xff;
				cab.address[2] =  addr      & 0xff;

				transfer_count = sizeof(cab.data);
				if (length < transfer_count)
				{
					transfer_count = length;
				}
				rc_ok = DoTransfer((void * volatile) &cab, (void * volatile) &cab, transfer_count + sizeof(cab.cmd) + sizeof(cab.address));
				if(rc_ok )
				{
					memcpy(data, (void *) cab.data, transfer_count);
					addr += transfer_count;
					length -= transfer_count;
					data += transfer_count;
				}
        	}
        }
        IS25_GiveMutex();
    }

    return rc_ok;
}


/*!
 * IS25_ReadProductIdentity
 *
 * @brief       Read Read back the product identifier
 * 				**IMP_NOTE** This function should only be called after the
 * 				scheduler is started
 *
 * @param       id - pointer to the returned identifier
 *
 * @returns     true or false - true = completed transfer ok
 */
bool IS25_ReadProductIdentity(uint8_t* pDeviceIDOut)
{
	t_rdid id;
	bool rc_ok = false;
	if(m_productId != 0)
	{
		if(pDeviceIDOut != NULL)
		{
			*pDeviceIDOut =  m_productId;
		}
		rc_ok = true;
	}
	else
	{
		memset( (void *) &id, 0, sizeof(id));
		id.cmd = IS25_RDID;
		rc_ok = IS25_TakeMutex();
		if (rc_ok)
		{
			rc_ok = DoTransfer((void * volatile)&id, (void * volatile )&id, sizeof(id));
			IS25_GiveMutex();
			if (rc_ok)
			{
				m_productId = id.deviceId;
				if(pDeviceIDOut != NULL)
				{
					*pDeviceIDOut =  id.deviceId;
				}
			}
		}
	}
	return rc_ok;
}

#if 1
// not used at the moment
/*!
 * IS25_ReadManufacturerId
 *
 * @brief       Read Read back the manufacturer and device id
 *
 * @param       pManufacturerIDOut - pointer to the returned manufacturer id
 *
 * @param       pDeviceIDOut - pointer to the returned device id
 *
 * @returns     true or false - true = completed transfer ok
 */
bool IS25_ReadManufacturerId(uint8_t* pManufacturerIDOut, uint8_t* pDeviceIDOut)
{
	t_rdmdid rdmdid;

    memset((void *)&rdmdid, 0, sizeof(rdmdid));
    rdmdid.cmd = IS25_RDMDID;
    rdmdid.address = 0; // determines what comes first, mmanuf id, or device id

    bool rc_ok = IS25_TakeMutex();
    if (rc_ok) {
        rc_ok = DoTransfer((void * volatile)&rdmdid, (void * volatile )&rdmdid, sizeof(rdmdid));
        IS25_GiveMutex();

        if (rc_ok) {
            *pManufacturerIDOut = rdmdid.manufactureId;
            *pDeviceIDOut =  rdmdid.deviceId;
        }
    }

    return rc_ok;
}
#endif



/*
 * @function IS25_TakeMutex()
 *
 * @desc	Function to take control of the is25 mutex
 *
 * @param
 *
 * @returns Bool variable - Returns true if function completed correctly
 */
bool IS25_TakeMutex()
{
	bool rc_ok = false;

	if (is25FlashStatus.initialized)
	{
		rc_ok = kStatus_OSA_Success == OSA_MutexLock(&is25FlashStatus.mutex, IS25_DEFAULT_MUTEXWAIT_MS);
	}
	return rc_ok;
}

/*
 * @function IS25_GiveMutex()
 *
 * @desc	Function to release the is25 mutex
 *
 * @param
 *
 * @returns Bool variable - Returns true if function completed correctly
 */
bool IS25_GiveMutex()
{
    return (kStatus_OSA_Success == OSA_MutexUnlock(&is25FlashStatus.mutex));
}

// NOTE: EDMA_DRV_Init() must be called before the following function is called.
// Currently it is called in Resources.c
/*
 * @function IS25_Init()
 *
 * @desc	Initialises the is25 for data transfer via edma and dspi
 *
 * @param	baudrate  baudrate to use (0 for the default value)
 * @param   calculatedBaudrate_p  pointer to where the realized baudrate possible is returnd when not NULL

 * @returns Bool variable - Returns true if function completed correctly
 */
bool IS25_Init(uint32_t baudrate, uint32_t * calculatedBaudrate_p)
{
    bool rc_ok = true;
	dspi_status_t dspiResult;

    if (baudrate == 0)
	{
    	baudrate = IS25_TRANSFER_BAUDRATE;
	}
    is25FlashStatus.calculatedBaudRate = 0;

    // create the mutex if this is the very first time
    if (is25FlashStatus.mutex == NULL)
    {
        if ( kStatus_OSA_Success != OSA_MutexCreate(&is25FlashStatus.mutex))
        {
            rc_ok = false;
            is25FlashStatus.mutex = NULL;
        }
    }

	if (rc_ok)
	{
		FlashExtInitIOLines();
		// Configure FLASH_CSn for direct control from SPI peripheral
		pinConfigDigitalOut(FLASH_CSn, kPortMuxAlt2, 0, false);
		//  EDMA configuration goes here
		dspi_edma_master_user_config_t edmaMasterUserConfig =
		{
			.isChipSelectContinuous     = true,
			.isSckContinuous            = false,
			.pcsPolarity                = kDspiPcs_ActiveLow,
			.whichCtar                  = kDspiCtar0,
			.whichPcs                   = kDspiPcs0
		};

		// Setup the configuration.
		is25FlashStatus.edmaDevice.dataBusConfig.bitsPerFrame = 8;
		is25FlashStatus.edmaDevice.dataBusConfig.clkPhase     = kDspiClockPhase_FirstEdge;		// TODO - check this
		is25FlashStatus.edmaDevice.dataBusConfig.clkPolarity  = kDspiClockPolarity_ActiveHigh;
		is25FlashStatus.edmaDevice.dataBusConfig.direction    = kDspiMsbFirst;

		//Init the dspi module for DMA operation
		dspiResult = DSPI_DRV_EdmaMasterInit(DSPI_MASTER_INSTANCE,
											 &is25FlashStatus.edmaMasterState,
											 &edmaMasterUserConfig,
											 &is25FlashStatus.stcdDspiMasterTest);
		if (dspiResult != kStatus_DSPI_Success)
		{
			rc_ok = false;
		}
	}

	if (rc_ok)
	{
		// Configure baudrate.
		is25FlashStatus.edmaDevice.bitsPerSec = baudrate;

		dspiResult = DSPI_DRV_EdmaMasterConfigureBus(DSPI_MASTER_INSTANCE,
													 &is25FlashStatus.edmaDevice,
													 &is25FlashStatus.calculatedBaudRate);
		if (dspiResult != kStatus_DSPI_Success)
		{
			rc_ok = false;
		}
		// END OF EDMA CONFIG
	}
	if (calculatedBaudrate_p != NULL)
	{
		*calculatedBaudrate_p = is25FlashStatus.calculatedBaudRate;
	}
	
	is25FlashStatus.initialized  = rc_ok;
	return is25FlashStatus.initialized;
}

bool IS25_Terminate()
{
    is25FlashStatus.initialized = false;

    return kStatus_DSPI_Success == DSPI_DRV_EdmaMasterDeinit(DSPI_MASTER_INSTANCE);
}


/*
 * FlashExtInitIOLines
 *
 * @desc
 *
 * @param
 *
 * @returns -
 */
static void FlashExtInitIOLines(void)
{
    // FLASH_CSn out (PTA14): Do this first to prevent spurious SPI activity

    pinConfigDigitalOut(FLASH_CSn, kPortMuxAsGpio, 1, false);

    if (Device_GetHardwareVersion() == HW_PASSRAIL_REV1  && !Device_HasPMIC())
    {
    	// FLASH_WPn out (PTA29): Disable write-protect
    	pinConfigDigitalOut(FLASH_WPn, kPortMuxAsGpio, 1, false);
    }

    // SPI0_MOSI out (PTA16)
    pinConfigDigitalOut(SPI0_MOSI, kPortMuxAlt2, 0, false);

    // SPI0_SCK out (PTA15)
    pinConfigDigitalOut(SPI0_SCK, kPortMuxAlt2, 0, false);

    // SPI0_MISO in (PTA17): Needs pulling resistor to avoid floating when CS~
    // is high
    // TODO: Check whether need pullup or pulldown - what is the idle level
    // required for the SPI mode?
#if 0
    pinConfigDigitalIn(SPI0_MISO, kPortMuxAlt2, true, kPortPullDown,
                       kPortIntDisabled);
#else
    // john suggested pullup
    pinConfigDigitalIn(SPI0_MISO, kPortMuxAlt2, true, kPortPullUp,
                       kPortIntDisabled);

#endif
}





// test function to check that data is stored and retrieved without corruption problems
bool testFlashTransfers(uint32_t startByte, uint32_t numBytes, uint32_t seed)
{
    uint8_t flashtestpage[EXTFLASH_PAGE_SIZE_BYTES];// make sure the stack of the CLI can handle this !!

    bool rc_ok = true;
    uint32_t report_max = 40;
    uint32_t total_read_errors = 0;
    uint32_t startTick, endTick;
    uint32_t startPage, numPages;

    if (numBytes == 0)
    {
        // that is an easy task !
        LOG_DBG(LOG_LEVEL_I2C,"You do realize, that testing zero bytes is probably not what you want!\n");
        return true;
    }
    if (!IsAddrValid((startByte + numBytes), __func__))
    {
        return false;
    }

    startPage = startByte / EXTFLASH_PAGE_SIZE_BYTES;
    numPages = (startByte + numBytes -1 ) /EXTFLASH_PAGE_SIZE_BYTES - startPage + 1;

    // erase page(s)
    LOG_DBG(LOG_LEVEL_I2C,"%s() testing %d bytes external flash (byte address 0x%08x -> 0x%08x)\n", __func__, numBytes, startByte, startByte + numBytes);
    LOG_DBG(LOG_LEVEL_I2C,"%s() testbuffer at %08x\n", __func__, flashtestpage);
    {
        uint32_t startSector, numSectors;
        startSector = startByte/(IS25_SECTOR_SIZE_BYTES);
        numSectors = (startByte + numBytes -1 )/IS25_SECTOR_SIZE_BYTES - startSector + 1;
        LOG_DBG(LOG_LEVEL_I2C,"%s() Erase from %08x to %08x\n", __func__, startSector * IS25_SECTOR_SIZE_BYTES, (startSector+ numSectors)* IS25_SECTOR_SIZE_BYTES -1);

        // erasing goes in sector size blocks !
        startTick = xTaskGetTickCount();
        rc_ok = IS25_PerformSectorErase(startByte, numBytes );
        endTick = xTaskGetTickCount();
        if ( endTick == startTick)
		{
        	endTick++;
		}
        if (rc_ok)
        {
            float speed = IS25_SECTOR_SIZE_BYTES*numSectors/((float) (endTick - startTick) *  portTICK_PERIOD_MS/1000);
            LOG_DBG(LOG_LEVEL_I2C,"%d sectors of %d bytes erased in %d milli seconds -> %8.3f bytes/s\n", numSectors, IS25_SECTOR_SIZE_BYTES,  (endTick - startTick)*portTICK_PERIOD_MS, speed );
        }
    }

    // read and test erased
    if (rc_ok)
	{
    	LOG_DBG(LOG_LEVEL_I2C,"%s() Reading and Testing if erased\n", __func__);
	}

    startTick = xTaskGetTickCount();
    for (uint32_t page = startPage; page <  startPage + numPages && rc_ok; page++)
    {
    	uint16_t i;
    	memset(flashtestpage, 0, sizeof(flashtestpage));
    	if (page % 64 == 0)
		{
    		LOG_DBG(LOG_LEVEL_I2C,"Blank check page 0x%06x\r", page);
		}
    	rc_ok = IS25_ReadBytes(page * EXTFLASH_PAGE_SIZE_BYTES, flashtestpage, EXTFLASH_PAGE_SIZE_BYTES);
        if (rc_ok == false)
        {
            LOG_DBG(LOG_LEVEL_I2C,"%s(0x%06x) read page failed\n", __func__, page);
        }
        else
        {
        // abuse gRxBuf for this
        	for (i=0; i<EXTFLASH_PAGE_SIZE_BYTES; i++)
            {
                uint8_t testval =  0xff;
                if (flashtestpage[i] != testval)
                {
                    if (total_read_errors < report_max)
                    {
                        LOG_DBG(LOG_LEVEL_I2C,"[%06x][%02x] = %02x != %02x\n", page, i, flashtestpage[i], testval);
                    }
                    total_read_errors++;
                }
            }
        }
    }
    if (total_read_errors)
	{
    	rc_ok = false;
	}
    endTick = xTaskGetTickCount();
    if ( endTick == startTick)
	{
    	endTick++;
	}
    if (rc_ok)
    {
        float speed = EXTFLASH_PAGE_SIZE_BYTES*numPages/((float) (endTick - startTick) *  portTICK_PERIOD_MS/1000);
        LOG_DBG(LOG_LEVEL_I2C,"%d pages of %d bytes blank checked in %d milli seconds -> %8.3f bytes/s\n", numPages, EXTFLASH_PAGE_SIZE_BYTES,  (endTick - startTick)*portTICK_PERIOD_MS, speed );
    }

    // generate pattern and write page
    if (rc_ok)
	{
    	LOG_DBG(LOG_LEVEL_I2C,"%s() Writing\n", __func__);
	}
    startTick = xTaskGetTickCount();
    for (uint32_t byte = startByte; byte <  startByte + numBytes && rc_ok; byte += sizeof(flashtestpage))
    {
        uint16_t i;
        uint32_t count = startByte + numBytes - byte;
        if (count> sizeof(flashtestpage)) count = sizeof(flashtestpage);

        for (i=0; i<sizeof(flashtestpage); i++)
        {
            uint8_t testval = (i + byte + seed) & 0xff;
            flashtestpage[i] = testval;
        }
        if ((byte/256) % 64 == 0) LOG_DBG(LOG_LEVEL_I2C,"Writing byte 0x%06x\r", byte);
        rc_ok = IS25_WriteBytes(byte, flashtestpage, count);
        if (rc_ok == false)
        {
            LOG_DBG(LOG_LEVEL_I2C,"%s(0x%06x) write failed\n", __func__, byte);
        }
    }
    endTick = xTaskGetTickCount();
    if ( endTick == startTick) endTick++;
    if (rc_ok)
    {
        float speed = numBytes/((float) (endTick - startTick) *  portTICK_PERIOD_MS/1000);
        LOG_DBG(LOG_LEVEL_I2C,"%d bytes written in %d milli seconds -> %8.3f bytes/s\n", numBytes,  (endTick - startTick)*portTICK_PERIOD_MS, speed );
    }

    // read and test
    if (rc_ok)
	{
    	LOG_DBG(LOG_LEVEL_I2C,"%s() Reading and Testing\n", __func__);
	}
    startTick = xTaskGetTickCount();
    for (uint32_t byte = startByte; byte <  startByte + numBytes && rc_ok; byte += sizeof(flashtestpage))
    {
       uint16_t i;
       uint32_t count = startByte + numBytes - byte;
       if (count> sizeof(flashtestpage))
	   {
    	   count = sizeof(flashtestpage);
	   }
       memset(flashtestpage, 0x5a, sizeof(flashtestpage));
       if ((byte/256) % 64 == 0)
	   {
    	   LOG_DBG(LOG_LEVEL_I2C,"Testing byte 0x%06x\r", byte);
	   }
       rc_ok = IS25_ReadBytes(byte, flashtestpage, count);
        if (rc_ok == false)
        {
            LOG_DBG(LOG_LEVEL_I2C,"%s(0x%06x) read failed\n", __func__, byte);
        }
        else
        {
            for (i=0; i<count; i++)
            {
                uint8_t testval = (i + byte + seed) & 0xff;
                if (flashtestpage[i] != testval)
                {
                    if (total_read_errors < report_max)
                    {
                        LOG_DBG(LOG_LEVEL_I2C,"[%06x] = %02x != %02x\n", byte+ i, flashtestpage[i], testval);
                    }
                    total_read_errors++;
                }
            }
        }
    }
    endTick = xTaskGetTickCount();
    if ( endTick == startTick)
	{
    	endTick++;
	}
    if (rc_ok)
    {
        float speed = numBytes/((float) (endTick - startTick) *  portTICK_PERIOD_MS/1000);
        LOG_DBG(LOG_LEVEL_I2C,"%d bytes read/tested in %d milli seconds -> %8.3f bytes/s\n", numBytes,  (endTick - startTick)*portTICK_PERIOD_MS, speed );
    }

    LOG_DBG(LOG_LEVEL_I2C,"Flash read error byte count = %d\n",total_read_errors);
    if (total_read_errors)
	{
    	rc_ok = false;
	}
    return rc_ok;
}



#ifdef __cplusplus
}
#endif