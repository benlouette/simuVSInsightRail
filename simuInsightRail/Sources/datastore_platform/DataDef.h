#ifdef __cplusplus
extern "C" {
#endif

/*
 * DataDef.h
 *
 *  Created on: 19 dec. 2015
 *      Author: Daniel van der Velde
 */

#ifndef SOURCES_DATASTORE_DATADEF_H_
#define SOURCES_DATASTORE_DATADEF_H_

/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>


/*
 * Macros
 */

#define DD_READ_BOOL_VALUE

/*
 * Types
 */

// Mapping IDEF types onto DataDef types
//typedef IDEF_String_t     DataDef_String_t;
//typedef IDEF_DateTime_t   DataDef_DateTime_t;


/*
 * Data definition value types
 */
typedef enum {
	DD_TYPE_BOOL      = 0,       // Unsigned 8 bit
	DD_TYPE_BYTE      = 1,       // Unsigned 8 bit
	DD_TYPE_SBYTE     = 2,       // Signed 8 bit
	DD_TYPE_INT16     = 3,
	DD_TYPE_UINT16    = 4,
	DD_TYPE_INT32     = 5,
	DD_TYPE_UINT32    = 6,
	DD_TYPE_INT64     = 7,
	DD_TYPE_UINT64    = 8,
	DD_TYPE_SINGLE    = 9,       // "float" IEEE 754 Single Precision (4 bytes)
	DD_TYPE_DOUBLE    = 10,      // "double" IEEE 754 Double Precision (8 bytes)
	DD_TYPE_STRING    = 11,      // Special string type with length field (idef_string_t) // internal it is null terminated
	DD_TYPE_DATETIME  = 12,
	DD_TYPE_STRARRAY  = 13,
} DD_Type_enum;


#define DATADEFTYPE2STRING(dd) \
  (dd == DD_TYPE_BOOL)     ? "BOOL    " : \
  (dd == DD_TYPE_BYTE)     ? "BYTE    " : \
  (dd == DD_TYPE_SBYTE)    ? "SBYTE   " : \
  (dd == DD_TYPE_INT16)    ? "INT16   " : \
  (dd == DD_TYPE_UINT16)   ? "UINT16  " : \
  (dd == DD_TYPE_INT32)    ? "INT32   " : \
  (dd == DD_TYPE_UINT32)   ? "UINT32  " : \
  (dd == DD_TYPE_INT64)    ? "INT64   " : \
  (dd == DD_TYPE_UINT64)   ? "UINT64  " : \
  (dd == DD_TYPE_SINGLE)   ? "SINGLE  " : \
  (dd == DD_TYPE_DOUBLE)   ? "DOUBLE  " : \
  (dd == DD_TYPE_STRING)   ? "STRING  " : \
  (dd == DD_TYPE_DATETIME) ? "DATETIME" : \
  (dd == DD_TYPE_STRARRAY) ? "STRARRAY" : \
		  "Unknown"

#define DATADEFTYPE2SIZE(dd)\
        (dd == DD_TYPE_BOOL)     ? sizeof(bool) : \
         (dd == DD_TYPE_BYTE)     ? sizeof(uint8_t) : \
         (dd == DD_TYPE_SBYTE)    ? sizeof(int8_t): \
         (dd == DD_TYPE_INT16)    ? sizeof(int16_t) : \
         (dd == DD_TYPE_UINT16)   ? sizeof(uint16_t): \
         (dd == DD_TYPE_INT32)    ? sizeof(int32_t) : \
         (dd == DD_TYPE_UINT32)   ? sizeof(uint32_t) : \
         (dd == DD_TYPE_INT64)    ? sizeof(int64_t) : \
         (dd == DD_TYPE_UINT64)   ? sizeof(uint64_t) : \
         (dd == DD_TYPE_SINGLE)   ? sizeof(float) : \
         (dd == DD_TYPE_DOUBLE)   ? sizeof(double) : \
         (dd == DD_TYPE_STRING)   ? sizeof(char) : \
         (dd == DD_TYPE_DATETIME) ? sizeof(uint64_t): 0\


typedef enum {
    DD_R  = 0,
    DD_RW = 1
} DD_rw_t;

#define DATADEFRW2STRING(dd) \
		(dd == DD_R)  ? "R " : \
		(dd == DD_RW) ? "RW" : \
				  "Unknown"


typedef struct {
	uint16_t len;
	uint8_t *buf;
} DataDef_String_t;

#if 1
typedef union {
    uint8_t u8;
    int8_t  i8;
    uint16_t u16;
    int16_t  i16;
    uint32_t u32;
    int32_t  i32;
    uint64_t u64;
    int64_t  i64;
    float   f;
    double   d;
} DataDef_TypesUnion_t;

typedef struct {
   bool (*rangeCheck)( void * value);// pointer to function which does the range check, when it is more complex than hi/lo
                           // when rangecheck pointer NULL, use these hi/lo values
   DataDef_TypesUnion_t hi;//  hi: highest allowed value
   DataDef_TypesUnion_t lo;//  lo: lowest allowed value
} DataDef_RangeCheck_t;

/*
 * not really needed for this simple check, but it is just an example
 * bool rangeCheckParameterSamples( uint32_t * value)
 *  {
 *  return (*value == 256) || (*value == 512));
 *  }
 *
 *  example initialisation : DataDef_RangeCheck_t numSamplesRangecheck = {rangecheckParameterX, 0, 0};
 *
 *  Simple form : DataDef_RangeCheck_t numMaxTimeout = {NULL, .hi.u32 = 100, .lo.u32 = 0};
 */
#endif

/*
 * Property
 */
#if 0
typedef struct {
	DataDef_String_t            name;
};
#endif

/*
 * Data Object definition structure
 */
typedef struct {
    uint32_t        objectId;      // Unique ID
    uint8_t         memId;         // Memory region ID
    bool            configObject;  // Configuration object (= store in NV memory)
    uint32_t        length;        // Number of elements (or max number for variable length values)
    DD_Type_enum    type;          // Base types or complex types
    DD_rw_t         rw;            // r or rw
    const char *	cliName;		// nickname used in the CLI
    DataDef_RangeCheck_t * paramCheck_p; // NULL for no param check
    uint8_t*        address;       // Location of the object in memory (identified by memId)


//    DataDef_ValueType_enum      valueType;
//    DataDef_Property_t          property;
} DataDef_t;

/*
 * Data
 */



#endif /* SOURCES_DATASTORE_DATADEF_H_ */


#ifdef __cplusplus
}
#endif