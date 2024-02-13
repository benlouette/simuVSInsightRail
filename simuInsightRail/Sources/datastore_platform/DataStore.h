#ifdef __cplusplus
extern "C" {
#endif

/*
 * DataStore.h
 *
 *  Created on: 19 dec. 2015
 *      Author: Daniel van der Velde
 */

#ifndef SOURCES_DATASTORE_DATASTORE_H_
#define SOURCES_DATASTORE_DATASTORE_H_

/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>

#include "DataDef.h"
/*
 * Macros
 */
#if 0
#define DATASTORE_READOBJECT(m_objectid)
#define DATASTORE_WRITEOBJECT(m_objectid, m_objectvalue)
#define DATASTORE_GETOBJECTDEF(m_objectid)
#endif
/*
 * Types
 */

/*
 * Data
 */

/*
 * Functions
 */

void DataStore_Init( void );
void DataStore_Term( void );

// NV (Non-Volatile) functions operate on configuration parameters only
//void DataStore_NVWriteConfig( void );
//void DataStore_NVReadConfig( void );

const DataDef_t * getDataDefElementById(uint32_t objectId);

// Parameter access functions
bool DataStore_BlockGetUint8( uint32_t objectId, uint32_t index, uint32_t count, uint8_t * dest_p);
bool DataStore_BlockSetUint8( uint32_t objectId, uint32_t index, uint32_t count, uint8_t * value_p, bool su);
bool DataStore_BlockGetUint16( uint32_t objectId, uint32_t index, uint32_t count, uint16_t * dest_p);
bool DataStore_BlockSetUint16( uint32_t objectId, uint32_t index, uint32_t count, uint16_t * value_p, bool su);
bool DataStore_BlockGetUint32( uint32_t objectId, uint32_t index, uint32_t count, uint32_t * dest_p);
bool DataStore_BlockSetUint32( uint32_t objectId, uint32_t index, uint32_t count, uint32_t * value_p, bool su);
bool DataStore_BlockGetUint64( uint32_t objectId, uint32_t index, uint32_t count, uint64_t * dest_p);
bool DataStore_BlockSetUint64( uint32_t objectId, uint32_t index, uint32_t count, uint64_t * value_p, bool su);
bool DataStore_BlockGetSingle( uint32_t objectId, uint32_t index, uint32_t count, float * dest_p);
bool DataStore_BlockSetSingle( uint32_t objectId, uint32_t index, uint32_t count, float * value_p, bool su);
bool DataStore_BlockGetDouble( uint32_t objectId, uint32_t index, uint32_t count, double * dest_p);
bool DataStore_BlockSetDouble( uint32_t objectId, uint32_t index, uint32_t count, double * value_p, bool su);

bool DataStore_BlockSet( uint32_t objectId, uint32_t index, uint32_t count, void * value_p, bool su);

uint8_t DataStore_GetUint8( uint32_t objectId, uint32_t index );
#define DataStore_GetInt8(objectId, index ) ((int8_t) DataStore_GetUint8(objectId, index))
bool DataStore_SetUint8( uint32_t objectId, uint32_t index, uint8_t value, bool su);
#define DataStore_SetInt8(objectId, index, value , su) ( DataStore_SetUint8(objectId, index, (uint8_t) value, su))
uint16_t DataStore_GetUint16( uint32_t objectId, uint32_t index );
#define DataStore_GetInt16(objectId, index ) ((int16_t) DataStore_GetUint16(objectId, index))
bool DataStore_SetUint16( uint32_t objectId, uint32_t index, uint16_t value, bool su);
#define DataStore_SetInt16(objectId, index, value , su) ( DataStore_SetUint16(objectId, index, (uint16_t) value, su))
uint32_t DataStore_GetUint32( uint32_t objectId, uint32_t index );
#define DataStore_GetInt32(objectId, index ) ((int32_t) DataStore_GetUint32(objectId, index))
bool DataStore_SetUint32( uint32_t objectId, uint32_t index, uint32_t value, bool su);
#define DataStore_SetInt32(objectId, index, value, su ) ( DataStore_SetUint32(objectId, index, (uint32_t) value, su))
uint64_t DataStore_GetUint64( uint32_t objectId, uint32_t index );
bool DataStore_SetUint64( uint32_t objectId, uint32_t index, uint64_t value, bool su);
float DataStore_GetSingle( uint32_t objectId, uint32_t index );
bool DataStore_SetSingle( uint32_t objectId, uint32_t index, float value, bool su);
double DataStore_GetDouble( uint32_t objectId, uint32_t index );
bool DataStore_SetDouble( uint32_t objectId, uint32_t index, double value, bool su);

uint8_t * DataStore_GetStringAddress( uint32_t objectId, uint32_t index );// internal memory only !
bool DataStore_GetString( uint32_t objectId, uint32_t index, uint32_t count, uint8_t * dest_p );
bool DataStore_SetString( uint32_t objectId, uint32_t index, uint8_t * value, bool su);

// etc. for all used types, also the set functions
bool cliExtHelpDataStore(uint32_t argc, uint8_t * argv[], uint32_t * argi);
bool cliDataStore( uint32_t args, uint8_t * argv[], uint32_t * argi);

void DataStore_dumpDatastore();

#endif /* SOURCES_DATASTORE_DATASTORE_H_ */


#ifdef __cplusplus
}
#endif