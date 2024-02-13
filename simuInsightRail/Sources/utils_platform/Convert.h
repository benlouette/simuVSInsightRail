#ifdef __cplusplus
extern "C" {
#endif

/*
 * Convert.h
 *
 *  Created on: 24 mrt. 2015
 *      Author: g100797
 */

#ifndef CONVERT_H_
#define CONVERT_H_

/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>

/*
 * Macros
 */

/*
 * Types
 */

union DataUnion32 {
    int32_t   val_int32;
    uint32_t  val_uint32;
    float     val_float;
    int16_t   val_int16[2];
    uint16_t  val_uint16[2];
    int8_t    val_int8[4];
    uint8_t   val_uint8[4];
};

/*
 * Data
 */

/*
 * Functions
 */

#endif /* CONVERT_H_ */


#ifdef __cplusplus
}
#endif