#ifdef __cplusplus
extern "C" {
#endif

/*
 * DataStore.c
 *
 *  Created on: 19 dec. 2015
 *      Author: Daniel van der Velde
 *
 */

/*
 * Includes
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#if 1

#include "CLIcmd.h"
#include "printgdf.h"

// framework files
#include "DataStore.h"
#include "DataDef.h"

// project supplied file
#include "configData.h"

/*
 * Macros
 */

/*
 * Types
 */
/*
 * Data
 */

extern int strcasecmp(const char *, const char *);		// Mutes the compiler warning.

/*
 * Functions
 */
#if 0
// next two functions should move to a config file

/*
 * copyBytesRamToStore
 *
 * @desc    glorified memcpy, but extendable to copy to internal and external flash etc.
 *
 * @param   dst_info   pointer to the structure which describes the datastore metadata about the object
 * @param   byteOffset offset in bytes relative to the start address of the object
 * @param   ram_p   source address in internal processor ram.
 * @param   count   number of bytes to copy from ram to destination object
 *
 * @returns false  on failure
 *
 */

bool copyBytesRamToStore(const DataDef_t * dst_info, uint8_t byteOffset, uint8_t *ram_p, uint32_t count)
{
    bool rc_ok = true;

    if (dst_info->rw != DD_RW) return false;// readonly item, so not a good idea to write

    switch (dst_info->memId) {

    case INT_RAM:
    case CONFIG_RAM:
        memcpy(&dst_info->address[byteOffset], ram_p, count);
        break;

    case PROGRAM_FLASH:
    case CONFIG_FLASH:
    case EXT_FLASH:
        // not implemented (yet)
        rc_ok = false;
    default:
        //not supported memory type
        rc_ok = false;
        break;
    }
    return rc_ok;
}


/*
 * copyBytesStoreToRam
 *
 * @desc    glorified memcpy, but extendable to copy to internal and external flash etc.
 *
 * @param   ram_p      destination address in internal processor ram.
 * @param   src_info   pointer to the structure which describes the datastore metadata about the object
 * @param   byteOffset offset in bytes relative to the start address of the object

 * @param   count   number of bytes to copy from source object to ram
 *
 * @returns false  on failure
 *
 */

bool copyBytesStoreToRam(uint8_t *ram_p, const DataDef_t * src_info, uint32_t byteOffset,  uint32_t count)
{
    bool rc_ok = true;

    switch (src_info->memId) {

    case INT_RAM:
    case CONFIG_RAM:
        memcpy(ram_p, &src_info->address[byteOffset], count);
        break;

    case PROGRAM_FLASH:
    case CONFIG_FLASH:
    case EXT_FLASH:

    default:
        //not supported memory type
        rc_ok = false;
        break;
    }
    return rc_ok;
}
#endif


// for the moment, a very stupid lookup

const DataDef_t * getDataDefElementByIndex(uint32_t idx)
{
	uint32_t maxidx = getNrDataDefElements();
	if (idx < maxidx) {
		return &sDataDef[idx];
	} else {
		return NULL;
	}
}

// to speed up the lookup process when accessing arrays in parts, a simple 'cache' of the last value requested
static uint32_t last_objId = DATASTORE_RESERVED;
static const DataDef_t * last_result = &sDataDef[0];// the wrong one, but a valid pointer
//static uint32_t cachehit=0;
//static uint32_t cachemiss=0;

const DataDef_t * getDataDefElementById(uint32_t objectId)
{
	uint32_t idx;
	uint32_t maxidx = getNrDataDefElements();
	const DataDef_t *result = NULL;

	if (objectId == last_objId) {
	    //cachehit++;
	    return last_result;
	}
	for (idx=0; idx < maxidx; idx++) {
		if (sDataDef[idx].objectId == objectId) {
			result = &sDataDef[idx];
			break;
		}
	}

	last_objId = objectId;
	last_result = result;
	//cachemiss++;
	return result;
}

const DataDef_t * getDataDefElementByCliName(char * cliName)
{
	uint32_t idx;
	uint32_t maxidx = getNrDataDefElements();
	const DataDef_t *result = NULL;

	for (idx=0; idx < maxidx; idx++) {
	    if (sDataDef[idx].cliName) {
	        if (0 == strcasecmp(sDataDef[idx].cliName, cliName) ) {
	            result = &sDataDef[idx];
	            break;
	        }
	    }
	}

	return result;
}


/*
 * byte transfer(s)
 */
bool DataStore_BlockGetUint8( uint32_t objectId, uint32_t index, uint32_t count, uint8_t * dest_p )
{
    const DataDef_t * dataDef;
    bool rc_ok = true;

    dataDef = getDataDefElementById(objectId);

    if (dataDef) {
        uint8_t* address;
        address = (uint8_t *) dataDef->address;

        if (!(dataDef->type == DD_TYPE_BOOL || dataDef->type == DD_TYPE_BYTE || dataDef->type == DD_TYPE_SBYTE)) return false;
        if (dataDef->length <= index) return false;
        if (dataDef->length < (index+count)) return false;

        rc_ok =  copyBytesStoreToRam(dest_p, dataDef, index * sizeof(*address) ,  count * sizeof(*address) );
    } else {
        // TODO PANIC
        rc_ok = false;
    }
    return rc_ok;
}

uint8_t DataStore_GetUint8( uint32_t objectId, uint32_t index )
{
    uint8_t value;

    DataStore_BlockGetUint8(objectId, index, 1, &value);

    return value;
}


bool DataStore_BlockSetUint8( uint32_t objectId, uint32_t index, uint32_t count, uint8_t * value_p, bool su)
{
    bool rc_ok = true;
    const DataDef_t * dataDef;
    uint8_t* address;

    dataDef = getDataDefElementById(objectId);
    if (dataDef) {

        if (!(dataDef->type == DD_TYPE_BOOL || dataDef->type == DD_TYPE_BYTE || dataDef->type == DD_TYPE_SBYTE)) rc_ok = false;
        if (dataDef->length <= index) rc_ok = false;
        if (dataDef->length < (index+count)) rc_ok = false;

        if (dataDef->paramCheck_p && rc_ok) {
            if (dataDef->paramCheck_p->rangeCheck) {
                rc_ok = dataDef->paramCheck_p->rangeCheck( value_p);
            } else {
                switch  (dataDef->type) {
                case DD_TYPE_BOOL:
                    break;
                case  DD_TYPE_BYTE:
                    rc_ok = (* (uint8_t *) value_p >= dataDef->paramCheck_p->lo.u8) && (*(uint8_t *)value_p <= dataDef->paramCheck_p->hi.u8);
                    break;
                case DD_TYPE_SBYTE:
                    rc_ok = (*(int8_t *)value_p >= dataDef->paramCheck_p->lo.u8) && (*(int8_t *)value_p <= dataDef->paramCheck_p->hi.u8);
                    break;
                default:
                    rc_ok = false;
                    break;
                }
            }
        }
        if (rc_ok) {
            address = (uint8_t *) dataDef->address;
            rc_ok = copyBytesRamToStore( dataDef, index * sizeof(*address), value_p, count * sizeof(*address), su);
        }

    } else {
        // TODO PANIC
        rc_ok = false;
    }
    return rc_ok;
}

bool DataStore_SetUint8( uint32_t objectId, uint32_t index, uint8_t value, bool su)
{
    return DataStore_BlockSetUint8(objectId, index, 1, &value, su );
}


/*
 * 16bit transfer(s)
 */


bool DataStore_BlockGetUint16( uint32_t objectId, uint32_t index, uint32_t count, uint16_t * dest_p )
{
    const DataDef_t * dataDef;
    bool rc_ok = true;

    dataDef = getDataDefElementById(objectId);

    if (dataDef) {
        uint16_t* address;
        address = (uint16_t *) dataDef->address;

        if (!(dataDef->type == DD_TYPE_UINT16 || dataDef->type == DD_TYPE_INT16)) return false;
        if (dataDef->length <= index) return false;
        if (dataDef->length < (index+count)) return false;

        rc_ok =  copyBytesStoreToRam( (uint8_t *)dest_p, dataDef, index * sizeof(*address) ,  count * sizeof(*address) );
    } else {
        // TODO PANIC
        rc_ok = false;
    }
    return rc_ok;
}

uint16_t DataStore_GetUint16( uint32_t objectId, uint32_t index )
{
    uint16_t value;

    DataStore_BlockGetUint16(objectId, index, 1, &value);

    return value;
}



bool DataStore_BlockSetUint16( uint32_t objectId, uint32_t index, uint32_t count, uint16_t * value_p, bool su)
{
    bool rc_ok = true;
    const DataDef_t * dataDef;
    uint16_t* address;

    dataDef = getDataDefElementById(objectId);
    if (dataDef) {

        if (!(dataDef->type == DD_TYPE_UINT16 || dataDef->type == DD_TYPE_INT16)) rc_ok = false;
        if (dataDef->length <= index) rc_ok = false;
        if (dataDef->length < (index+count)) rc_ok = false;

        if (dataDef->paramCheck_p && rc_ok) {
             if (dataDef->paramCheck_p->rangeCheck) {
                 rc_ok = dataDef->paramCheck_p->rangeCheck( value_p);
             } else {
                 switch  (dataDef->type) {

                 case  DD_TYPE_UINT16:
                     rc_ok = (* (uint16_t *) value_p >= dataDef->paramCheck_p->lo.u16) && (*(uint16_t *)value_p <= dataDef->paramCheck_p->hi.u16);
                     break;
                 case DD_TYPE_INT16:
                     rc_ok = (*(int16_t *)value_p >= dataDef->paramCheck_p->lo.u16) && (*(int16_t *)value_p <= dataDef->paramCheck_p->hi.u16);
                     break;
                 default:
                     rc_ok = false;
                     break;
                 }
             }
         }

        if (rc_ok) {
            address = (uint16_t *) dataDef->address;
            rc_ok = copyBytesRamToStore( dataDef, index * sizeof(*address), (uint8_t *)value_p, count * sizeof(*address), su);
        }

    } else {
        // TODO PANIC
        rc_ok = false;
    }
    return rc_ok;
}

bool DataStore_SetUint16( uint32_t objectId, uint32_t index, uint16_t value, bool su)
{
    return DataStore_BlockSetUint16(objectId, index, 1, &value , su);
}
//#define DataStore_SetInt16(objectId, index, value , su) ( DataStore_SetUint16(objectId, index, (uint16_t) value, su))


/*
 * 32bit transfer(s)
 */

bool DataStore_BlockGetUint32( uint32_t objectId, uint32_t index, uint32_t count, uint32_t * dest_p )
{
    const DataDef_t * dataDef;
    bool rc_ok = true;

    dataDef = getDataDefElementById(objectId);

    if (dataDef) {
        uint32_t* address;
        address = (uint32_t *) dataDef->address;

        if (!(dataDef->type == DD_TYPE_UINT32 || dataDef->type == DD_TYPE_INT32)) return false;
        if (dataDef->length <= index) return false;
        if (dataDef->length < (index+count)) return false;

        rc_ok =  copyBytesStoreToRam( (uint8_t *)dest_p, dataDef, index * sizeof(*address) ,  count * sizeof(*address) );
    } else {
        // TODO PANIC
        rc_ok = false;
    }
    return rc_ok;
}

uint32_t DataStore_GetUint32( uint32_t objectId, uint32_t index )
{
    uint32_t value;

    DataStore_BlockGetUint32(objectId, index, 1, &value);

    return value;
}


//#define DataStore_GetInt32(objectId, index ) ((int32_t) DataStore_GetUint32(objectId, index))


bool DataStore_BlockSetUint32( uint32_t objectId, uint32_t index, uint32_t count, uint32_t * value_p, bool su)
{
    bool rc_ok = true;
    const DataDef_t * dataDef;
    uint32_t* address;

    dataDef = getDataDefElementById(objectId);
    if (dataDef) {

        if (!(dataDef->type == DD_TYPE_UINT32 || dataDef->type == DD_TYPE_INT32)) rc_ok = false;
        if (dataDef->length <= index) rc_ok = false;
        if (dataDef->length < (index+count)) rc_ok = false;

        if (dataDef->paramCheck_p && rc_ok) {
           if (dataDef->paramCheck_p->rangeCheck) {
               rc_ok = dataDef->paramCheck_p->rangeCheck( value_p);
           } else {
               switch  (dataDef->type) {

               case  DD_TYPE_UINT32:
                   rc_ok = (* (uint32_t *) value_p >= dataDef->paramCheck_p->lo.u32) && (*(uint32_t *)value_p <= dataDef->paramCheck_p->hi.u32);
                   break;
               case DD_TYPE_INT32:
                   rc_ok = (*(int32_t *)value_p >= dataDef->paramCheck_p->lo.u32) && (*(int32_t *)value_p <= dataDef->paramCheck_p->hi.u32);
                   break;
               default:
                   rc_ok = false;
                   break;
               }
           }
       }

        if (rc_ok) {
            address = (uint32_t *) dataDef->address;
            rc_ok = copyBytesRamToStore( dataDef, index * sizeof(*address), (uint8_t *) value_p, count * sizeof(*address), su);
        }

    } else {
        // TODO PANIC
        rc_ok = false;
    }
    return rc_ok;
}

bool DataStore_SetUint32( uint32_t objectId, uint32_t index, uint32_t value, bool su)
{
    return DataStore_BlockSetUint32(objectId, index, 1, &value, su );
}


//#define DataStore_SetInt32(objectId, index, value ) ( DataStore_SetUint32(objectId, index, (uint32_t) value))


/*
 * 64bit transfer(s)
 */

bool DataStore_BlockGetUint64( uint32_t objectId, uint32_t index, uint32_t count, uint64_t * dest_p )
{
    const DataDef_t * dataDef;
    bool rc_ok = true;

    dataDef = getDataDefElementById(objectId);

    if (dataDef) {
        uint64_t* address;
        address = (uint64_t *) dataDef->address;

        if (!(dataDef->type == DD_TYPE_UINT64 || dataDef->type == DD_TYPE_INT64 || dataDef->type == DD_TYPE_DATETIME)) return false;
        if (dataDef->length <= index) return false;
        if (dataDef->length < (index+count)) return false;

        rc_ok =  copyBytesStoreToRam( (uint8_t *)dest_p, dataDef, index * sizeof(*address) ,  count * sizeof(*address) );
    } else {
        // TODO PANIC
        rc_ok = false;
    }
    return rc_ok;
}

uint64_t DataStore_GetUint64( uint32_t objectId, uint32_t index )
{
    uint64_t value;

    DataStore_BlockGetUint64(objectId, index, 1, &value);

    return value;
}


//#define DataStore_GetInt64(objectId, index ) ((int64_t) DataStore_GetUint64(objectId, index))

bool DataStore_BlockSetUint64( uint32_t objectId, uint32_t index, uint32_t count, uint64_t * value_p, bool su)
{
    bool rc_ok = true;
    const DataDef_t * dataDef;
    uint64_t* address;

    dataDef = getDataDefElementById(objectId);
    if (dataDef) {

        if (!(dataDef->type == DD_TYPE_UINT64 || dataDef->type == DD_TYPE_INT64 || dataDef->type == DD_TYPE_DATETIME)) rc_ok = false;
        if (dataDef->length <= index) rc_ok = false;
        if (dataDef->length < (index+count)) rc_ok = false;


        if (dataDef->paramCheck_p && rc_ok) {
           if (dataDef->paramCheck_p->rangeCheck) {
               rc_ok = dataDef->paramCheck_p->rangeCheck( value_p);
           } else {
               switch  (dataDef->type) {
               case DD_TYPE_DATETIME:
               case  DD_TYPE_UINT64:
                   rc_ok = (* (uint64_t *) value_p >= dataDef->paramCheck_p->lo.u64) && (*(uint64_t *)value_p <= dataDef->paramCheck_p->hi.u64);
                   break;
               case DD_TYPE_INT64:
                   rc_ok = (*(int64_t *)value_p >= dataDef->paramCheck_p->lo.u64) && (*(int64_t *)value_p <= dataDef->paramCheck_p->hi.u64);
                   break;
               default:
                   rc_ok = false;
                   break;
               }
           }
       }

        if (rc_ok) {
            address = (uint64_t *) dataDef->address;
            rc_ok = copyBytesRamToStore( dataDef, index * sizeof(*address), (uint8_t *) value_p, count * sizeof(*address), su);
        }

    } else {
        // TODO PANIC
        rc_ok = false;
    }
    return rc_ok;
}

bool DataStore_SetUint64( uint32_t objectId, uint32_t index, uint64_t value, bool su)
{
    return DataStore_BlockSetUint64(objectId, index, 1, &value, su );
}



//#define DataStore_SetInt64(objectId, index, value ) ( DataStore_SetUint64(objectId, index, (uint64_t) value))







// some chips have no floating point hardware, and library functions can be space and time consuming, make float support optional
#ifdef DATASTORE_ENABLE_FLOAT


/*
 * float transfer(s)
 */

bool DataStore_BlockGetSingle( uint32_t objectId, uint32_t index, uint32_t count, float * dest_p )
{
    const DataDef_t * dataDef;
    bool rc_ok = true;

    dataDef = getDataDefElementById(objectId);

    if (dataDef) {
        float* address;
        address = (float *) dataDef->address;

        if (!(dataDef->type == DD_TYPE_SINGLE)) return false;
        if (dataDef->length <= index) return false;
        if (dataDef->length < (index+count)) return false;

        rc_ok =  copyBytesStoreToRam( (uint8_t *)dest_p, dataDef, index * sizeof(*address) ,  count * sizeof(*address) );
    } else {
        // TODO PANIC
        rc_ok = false;
    }
    return rc_ok;
}

float DataStore_GetSingle( uint32_t objectId, uint32_t index )
{
    float value;

    DataStore_BlockGetSingle(objectId, index, 1, &value);

    return value;
}


bool DataStore_BlockSetSingle( uint32_t objectId, uint32_t index, uint32_t count, float * value_p, bool su)
{
    bool rc_ok = true;
    const DataDef_t * dataDef;
    float* address;

    dataDef = getDataDefElementById(objectId);
    if (dataDef) {

        if (!(dataDef->type == DD_TYPE_SINGLE)) rc_ok = false;
        if (dataDef->length <= index) rc_ok = false;
        if (dataDef->length < (index+count)) rc_ok = false;

        if (dataDef->paramCheck_p && rc_ok) {
            if (dataDef->paramCheck_p->rangeCheck) {
                rc_ok = dataDef->paramCheck_p->rangeCheck( value_p);
            } else {

                rc_ok = (* value_p >= dataDef->paramCheck_p->lo.f) && (*value_p <= dataDef->paramCheck_p->hi.f);
            }
        }

        if (rc_ok) {
            address = (float *) dataDef->address;
            rc_ok = copyBytesRamToStore( dataDef, index * sizeof(*address), (uint8_t *) value_p, count * sizeof(*address), su);
        }

    } else {
        // TODO PANIC
        rc_ok = false;
    }
    return rc_ok;
}

bool DataStore_SetSingle( uint32_t objectId, uint32_t index, float value, bool su)
{
    return DataStore_BlockSetSingle(objectId, index, 1, &value, su );
}



/*
 * double transfer(s)
 */

bool DataStore_BlockGetDouble( uint32_t objectId, uint32_t index, uint32_t count, double * dest_p )
{
    const DataDef_t * dataDef;
    bool rc_ok = true;

    dataDef = getDataDefElementById(objectId);

    if (dataDef) {
        double* address;
        address = (double *) dataDef->address;

        if (!(dataDef->type == DD_TYPE_DOUBLE)) return false;
        if (dataDef->length <= index) return false;
        if (dataDef->length < (index+count)) return false;

        rc_ok =  copyBytesStoreToRam( (uint8_t *)dest_p, dataDef, index * sizeof(*address) ,  count * sizeof(*address) );
    } else {
        // TODO PANIC
        rc_ok = false;
    }
    return rc_ok;
}

double DataStore_GetDouble( uint32_t objectId, uint32_t index )
{
    double value;

    DataStore_BlockGetDouble(objectId, index, 1, &value);

    return value;
}


bool DataStore_BlockSetDouble( uint32_t objectId, uint32_t index, uint32_t count, double * value_p, bool su)
{
    bool rc_ok = true;
    const DataDef_t * dataDef;
    double* address;

    dataDef = getDataDefElementById(objectId);
    if (dataDef) {

        if (!(dataDef->type == DD_TYPE_DOUBLE)) rc_ok = false;
        if (dataDef->length <= index) rc_ok = false;
        if (dataDef->length < (index+count)) rc_ok = false;

        if (dataDef->paramCheck_p && rc_ok) {
             if (dataDef->paramCheck_p->rangeCheck) {
                 rc_ok = dataDef->paramCheck_p->rangeCheck( value_p);
             } else {

                 rc_ok = (* value_p >= dataDef->paramCheck_p->lo.d) && (*value_p <= dataDef->paramCheck_p->hi.d);
             }
        }

        if (rc_ok) {
            address = (double *) dataDef->address;
            rc_ok = copyBytesRamToStore( dataDef, index * sizeof(*address), (uint8_t *) value_p, count * sizeof(*address), su);
        }

    } else {
        // TODO PANIC
        rc_ok = false;
    }
    return rc_ok;
}

bool DataStore_SetDouble( uint32_t objectId, uint32_t index, double value, bool su)
{
    return DataStore_BlockSetDouble(objectId, index, 1, &value, su );
}

#endif

/*
 * string transfer
 */

bool DataStore_GetString( uint32_t objectId, uint32_t index, uint32_t count, uint8_t * dest_p )
{
    const DataDef_t * dataDef;
    bool rc_ok = true;

    dataDef = getDataDefElementById(objectId);

    if (dataDef) {
        uint8_t * address;
        address = (uint8_t *) dataDef->address;

        if (!(dataDef->type == DD_TYPE_STRING)) return false;
        if (dataDef->length <= index) return false;
        if (( index + count ) > dataDef->length ) count = dataDef->length -index;// do not read beyond the end of the object

        rc_ok =  copyBytesStoreToRam( (uint8_t *)dest_p, dataDef, index * sizeof(*address) ,  count * sizeof(*address) );
    } else {
        // TODO PANIC
        rc_ok = false;
    }
    return rc_ok;
}


// this one only valid for internal(flash) memory
uint8_t * DataStore_GetStringAddress( uint32_t objectId, uint32_t index )
{
	const DataDef_t * dataDef;
	uint8_t* address = NULL;
	dataDef = getDataDefElementById(objectId);

	if (dataDef) {
        if (!(dataDef->type == DD_TYPE_STRING )) return NULL;
        if (dataDef->length <= index) return NULL;

        address = (uint8_t *) (dataDef->address /* index not yet supported (yet) + index * dataDef->length */ );
	}
	return ( address);
}


bool DataStore_SetString( uint32_t objectId, uint32_t index, uint8_t * value_p, bool su)
{
	bool rc_ok = true;
	const DataDef_t * dataDef;
	uint8_t * address;


	dataDef = getDataDefElementById(objectId);
	if (dataDef) {
	    uint32_t count;

		if (!(dataDef->type == DD_TYPE_STRING )) rc_ok = false;
		count = strlen((char *)value_p)+1;// strlen does not include the terminating null, so add it
		if (dataDef->length <= index) rc_ok = false;
		if (dataDef->length < (index+count)) rc_ok = false;

		if (rc_ok) {
			address = (uint8_t *) (dataDef->address /* index not yet supported (yet) + index * dataDef->length */);

	        rc_ok = copyBytesRamToStore( dataDef, index * sizeof(*address), (uint8_t *) value_p, count * sizeof(*address), su);

			//strncpy((char *) address, (char *) value, dataDef->length);
		}
	} else {
		// TODO PANIC
		rc_ok = false;
	}
	return rc_ok;
}

//
// function general for all datatypes, but less type checking
//
bool DataStore_BlockSet( uint32_t objectId, uint32_t index, uint32_t count, void * value_p, bool su)
{
    bool rc_ok = true;
    const DataDef_t * dataDef;

    dataDef = getDataDefElementById(objectId);

    if (dataDef) {
        // furst some basic bounds checking
        if (dataDef->length <= index) rc_ok = false;
        if (dataDef->length < (index+count)) rc_ok = false;

        if (rc_ok) {// it is in bounds
            switch (dataDef->type) {
            case DD_TYPE_BOOL:
            case DD_TYPE_BYTE:
            case DD_TYPE_SBYTE:
                {
                    uint8_t *address = (uint8_t *) dataDef->address;
                    rc_ok = copyBytesRamToStore( dataDef, index * sizeof(*address), (uint8_t *) value_p, count * sizeof(*address), su);
                }
                break;

            case DD_TYPE_STRING:
                {
                    uint8_t *address = (uint8_t *) dataDef->address;
                    rc_ok = copyBytesRamToStore( dataDef, index * sizeof(*address), (uint8_t *) value_p, count * sizeof(*address), su);
                    if ( (index+count) < dataDef->length) {
                        // there is room, so add the C string '\0' !!!
                        uint8_t zero = '\0';
                        rc_ok = copyBytesRamToStore( dataDef, index + count , (uint8_t *) &zero, 1 , su);// this can lead to a second 'copyBytesRamToStore : not writing read only item' message on read only items.
                    }
                }
                break;
            case DD_TYPE_INT16:
            case DD_TYPE_UINT16:
                {
                    uint16_t *address = (uint16_t *) dataDef->address;
                    rc_ok = copyBytesRamToStore( dataDef, index * sizeof(*address), (uint8_t *) value_p, count * sizeof(*address), su);
                }
                break;
            case DD_TYPE_INT32:
            case DD_TYPE_UINT32:
            case DD_TYPE_SINGLE:
                {
                	// OK, Upload_repeat is special, if we get an invalid value we stand the chance of stopping the node
                	// from ever connecting again so let's validate it
                	if((SR_Upload_repeat == dataDef->objectId) && dataDef->paramCheck_p->rangeCheck)
                	{
                		dataDef->paramCheck_p->rangeCheck(value_p);
                	}
                	uint32_t *address = (uint32_t *) dataDef->address;
                    rc_ok = copyBytesRamToStore( dataDef, index * sizeof(*address), (uint8_t *) value_p, count * sizeof(*address), su);
                }
                break;

            case DD_TYPE_INT64:
            case DD_TYPE_UINT64:
            case DD_TYPE_DOUBLE:
            case DD_TYPE_DATETIME:
                {
                    uint64_t *address = (uint64_t *) dataDef->address;
                    rc_ok = copyBytesRamToStore( dataDef, index * sizeof(*address), (uint8_t *) value_p, count * sizeof(*address), su);
                }
                break;

            default: // catch unknown types, and return eror
                rc_ok = false;
                break;

            }
        }

    } else {
        // TODO PANIC
        rc_ok = false;
    }
    return rc_ok;
}


/*
 * CLI support functions
 */




static void printDataStoreValue(const DataDef_t * dataDef, bool byId)
{
	if (byId) {
		printf("%5u ",dataDef->objectId);
	} else {
		printf("%s ",dataDef->cliName);
	}

	switch (dataDef->type) {
	case 	DD_TYPE_BOOL:
	case 	DD_TYPE_BYTE:
		printf(" %d", DataStore_GetUint8(dataDef->objectId, 0));
		break;
	case 	DD_TYPE_SBYTE:
		printf(" %d", DataStore_GetInt8(dataDef->objectId, 0));
		break;
	case 	DD_TYPE_UINT16:
		printf(" %d", DataStore_GetUint16(dataDef->objectId, 0));
		break;
	case 	DD_TYPE_INT16:
		printf(" %d", DataStore_GetInt16(dataDef->objectId, 0));
		break;
	case 	DD_TYPE_UINT32:
		printf(" %d", DataStore_GetUint32(dataDef->objectId, 0));
		break;
	case 	DD_TYPE_INT32:
		printf(" %d", DataStore_GetInt32(dataDef->objectId, 0));
		break;
	case 	DD_TYPE_STRING:
#if 1
	    {
            uint8_t buf[80];
            memset(buf,0,sizeof(buf));
            DataStore_GetString( dataDef->objectId, 0, sizeof(buf)-1, buf );
            printf(" \"%s\"", buf);
	    }

#else
		printf(" \"%s\"", DataStore_GetStringAddress(dataDef->objectId, 0));
#endif
		break;
	case 	DD_TYPE_INT64:
        printf(" %lld", DataStore_GetUint64(dataDef->objectId, 0));
        break;
    case    DD_TYPE_UINT64:
    case    DD_TYPE_DATETIME:
        printf(" %llu", DataStore_GetUint64(dataDef->objectId, 0));
        break;

#ifdef DATASTORE_ENABLE_FLOAT
	case 	DD_TYPE_SINGLE:
        printf(" %g", DataStore_GetSingle(dataDef->objectId, 0));
        break;
	case 	DD_TYPE_DOUBLE:
        printf(" %g", DataStore_GetDouble(dataDef->objectId, 0));
        break;

#else
    case    DD_TYPE_SINGLE:
    case    DD_TYPE_DOUBLE:
        printf("Error, float/double not configured\n");
                break;

#endif


	case 	DD_TYPE_STRARRAY:

	default:
		printf("Error, not supported (yet) datatype\n");
		break;
	}
	printf("\n");
}

static void dumpList()
{
	const DataDef_t * dataDef;

	uint32_t idx=0;
	while (NULL != (dataDef = getDataDefElementByIndex(idx)))  {
		/* index objectId memid configObject length type r/rw nickname address */
		printf("[%3u] %3u %s %c %5u %s %s 0x%08x %s\n", idx,
				dataDef->objectId, MEMID2STRING(dataDef->memId), dataDef->configObject ? 'Y':'N', dataDef->length,   DATADEFTYPE2STRING(dataDef->type),
				DATADEFRW2STRING(dataDef->rw),
				dataDef->address,dataDef->cliName);
		idx++;
	}
}

static bool setDataStoreValue(const DataDef_t * dataDef, char * str)
{
	bool rc_ok = false;

	switch (dataDef->type) {
	case 	DD_TYPE_BOOL:
	case 	DD_TYPE_BYTE:
		rc_ok = DataStore_SetUint8( dataDef->objectId, 0, strtoul(str, NULL, 10), true);
		break;
	case 	DD_TYPE_SBYTE:
		rc_ok = DataStore_SetInt8( dataDef->objectId, 0, strtol(str, NULL, 10), true);
		break;
	case 	DD_TYPE_UINT16:
		rc_ok = DataStore_SetUint16( dataDef->objectId, 0, strtoul(str, NULL, 10), true);
		break;
	case 	DD_TYPE_INT16:
		rc_ok = DataStore_SetInt16( dataDef->objectId, 0, strtol(str, NULL, 10), true);
		break;
	case 	DD_TYPE_UINT32:
		rc_ok = DataStore_SetUint32( dataDef->objectId, 0, strtoul(str, NULL, 10), true);
		break;
	case 	DD_TYPE_INT32:
		rc_ok = DataStore_SetInt32( dataDef->objectId, 0, strtol(str, NULL, 10), true);
		break;
	case 	DD_TYPE_STRING:
		rc_ok = DataStore_SetString( dataDef->objectId, 0, (uint8_t *)str, true);
		break;
	case 	DD_TYPE_INT64:
        rc_ok = DataStore_SetUint64( dataDef->objectId, 0, strtoll(str, NULL, 10), true);
        break;
	case 	DD_TYPE_UINT64:
	case    DD_TYPE_DATETIME:
        rc_ok = DataStore_SetUint64( dataDef->objectId, 0, strtoull(str, NULL, 10), true);
        break;

#ifdef DATASTORE_ENABLE_FLOAT
    case    DD_TYPE_SINGLE:
        rc_ok = DataStore_SetSingle( dataDef->objectId, 0, strtof(str, NULL), true);
        break;
    case    DD_TYPE_DOUBLE:
        rc_ok = DataStore_SetDouble( dataDef->objectId, 0, strtod(str, NULL), true);
         break;

#else
    case    DD_TYPE_SINGLE:
    case    DD_TYPE_DOUBLE:
        printf("Error, float/double not configured\n");
                break;

#endif


	case 	DD_TYPE_STRARRAY:

	default:
		printf("Error, not supported (yet) datatype\n");
		break;
	}
	return rc_ok;
}


static const char datastoreHelp[] = {
		"datastore\t\tLists all id's and nickname's\r\n"
		"datastore ?\t\tExtended list (development aid)\r\n"
		"datastore *<i>\t\tComplete data dump <by id>\r\n"
		"datastore [<id>|<nickname>]\t\tRead value by id or nickname\r\n"
		"datastore [<id>|<nickname>] value\tWrite value\r\n"
};

bool cliExtHelpDataStore(uint32_t argc, uint8_t * argv[], uint32_t * argi)
{
	printf("%s",datastoreHelp);
	return true;
}

bool cliDataStore( uint32_t args, uint8_t * argv[], uint32_t * argi)
{
	bool rc_ok = true;
	bool byId = false;
	bool dumpAll = false;
	const DataDef_t * dataDef;

	if (args==1) {
		if (argv[0][0]=='*') {
			dumpAll = true;
			byId = false;
			if (argv[0][1]=='i') byId = true;
		}
		if (argv[0][0]=='?') {
			dumpList();
			return true;
		}
	}

	if ( args == 0 || dumpAll) {
		// list id/cliName value
		uint32_t idx=0;
		while (NULL != (dataDef = getDataDefElementByIndex(idx++)))  {
			if (dataDef->cliName || byId) {
				if (dumpAll) {
					printf("datastore ");
					printDataStoreValue(dataDef, byId);
				} else {
					// only list the id and nickname
					printf(" %5u %s\n",dataDef->objectId, dataDef->cliName);
				}
			}
		};
	} else {
		if (argi[0]==0) {
			// did someone used an ascii name ?
			dataDef = getDataDefElementByCliName( (char *) argv[0]);
			byId = false;
		} else {
			dataDef = getDataDefElementById( argi[0]);
			byId = true;
		}
		if (dataDef == NULL) {
			// not found
			printf("Error: DataStore item not found\n");
		} else {
			if (args==1) {
				// print value of item
				printDataStoreValue(dataDef, byId);
			} else {
				// 2 or more arguments, store value
				if (false == setDataStoreValue(dataDef, (char *) argv[1])) {
					printf("Error: storage failed\n");;
				}
			}
		}
	}

	return rc_ok;
}

#if 0
static const struct cliCmd dataStoreCommands[] = {
		{"datastore", "\t<id>\tData dictionary read id=1..n, or name", cliDataStore, cliExtHelpDataStore },
};
#endif

void DataStore_Init()
{
	//
	//cliRegisterCommands(dataStoreCommands , sizeof(dataStoreCommands)/sizeof(*dataStoreCommands));
}
void DataStore_Term()
{
	//
}


void DataStore_dumpDatastore()
{
#ifndef LIMIT_CLI_SPAMMING
	uint8_t arg[2];
	arg[0] = '*';
	uint8_t *parg = arg;
	cliDataStore(1 , &parg, 0);
#endif
}

#endif



#ifdef __cplusplus
}
#endif